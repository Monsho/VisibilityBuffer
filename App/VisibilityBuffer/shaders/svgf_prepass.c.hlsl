#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB> cbScene : REG(b0);
ConstantBuffer<SvgfCB>  cbSvgf  : REG(b1);

Texture2D<float>        texDepth : REG(t0);
Texture2D<float4>       texNormal : REG(t1);
Texture2D<float3>       texInputGI : REG(t2);

RWTexture2D<float3>     rwPrepassGI : REG(u0);

float Luma(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
    uint2 pixPos = did.xy;
    uint2 dim = (uint2)cbScene.screenSize;
    if (any(pixPos >= dim))
    {
        return;
    }

    float centerDepth = texDepth[pixPos];
    if (centerDepth <= 0.0)
    {
        rwPrepassGI[pixPos] = texInputGI[pixPos];
        return;
    }

    float3 centerNormal = normalize(texNormal[pixPos].xyz * 2.0 - 1.0);
    float centerVD = ClipDepthToViewDepthRH(centerDepth, cbScene.mtxViewToProj);

    int kernelRadius = cbSvgf.prepassKernelRadius;
    int kernelWidth = 2 * kernelRadius + 1;
    int loopCount = kernelWidth * kernelWidth;

    float meanL = 0.0;
    float meanL2 = 0.0;
    float statW = 0.0;

    [loop]
    for (int i = 0; i < loopCount; ++i)
    {
        int x = (i % kernelWidth) - kernelRadius;
        int y = (i / kernelWidth) - kernelRadius;
        // [loop]
        // for (int x = -kernelRadius; x <= kernelRadius; ++x)
        {
            int2 p = clamp(int2(pixPos) + int2(x, y), int2(0, 0), int2(dim) - 1);
            float depth = texDepth[p];
            if (depth <= 0.0)
            {
                continue;
            }

            // 輝度クランプする際に深度・法線のウェイトを考慮する場合は有効にする
#if 0
            float3 normal = normalize(texNormal[p].xyz * 2.0 - 1.0);
            float vd = ClipDepthToViewDepthRH(depth, cbScene.mtxViewToProj);
            float depthW = exp(-abs(vd - centerVD) * cbSvgf.phiDepth * cbSvgf.prepassDepthPhiScale);
            float normalW = pow(saturate(dot(normal, centerNormal)), cbSvgf.phiNormal);
            float w = depthW * normalW;
#else
            float w = 1.0;
#endif

            float lum = Luma(texInputGI[p]);
            meanL += lum * w;
            meanL2 += lum * lum * w;
            statW += w;
        }
    }

    float invStatW = rcp(max(statW, 1e-4));
    float lumaMean = meanL * invStatW;
    float lumaVar = max(0.0, meanL2 * invStatW - lumaMean * lumaMean + cbSvgf.prepassVarianceBias);
    float lumaSigma = sqrt(lumaVar);
    float lumaMin = lumaMean - cbSvgf.prepassClampSigma * lumaSigma;
    float lumaMax = lumaMean + cbSvgf.prepassClampSigma * lumaSigma;

    float3 sumGI = 0.0;
    float sumW = 0.0;

    [loop]
    for (int i = 0; i < loopCount; ++i)
    // for (int y = -kernelRadius; y <= kernelRadius; ++y)
    {
        int x = (i % kernelWidth) - kernelRadius;
        int y = (i / kernelWidth) - kernelRadius;
        // [loop]
        // for (int x = -kernelRadius; x <= kernelRadius; ++x)
        {
            int2 p = clamp(int2(pixPos) + int2(x, y), int2(0, 0), int2(dim) - 1);
            float depth = texDepth[p];
            if (depth <= 0.0)
            {
                continue;
            }

            float3 gi = texInputGI[p];
            float lum = Luma(gi);
            float clampedLum = clamp(lum, lumaMin, lumaMax);
            float lumScale = (lum > 1e-4) ? (clampedLum / lum) : 0.0;
            float3 clampedGi = gi * lumScale;

            float3 normal = normalize(texNormal[p].xyz * 2.0 - 1.0);
            float vd = ClipDepthToViewDepthRH(depth, cbScene.mtxViewToProj);
            float depthW = exp(-abs(vd - centerVD) * cbSvgf.phiDepth * cbSvgf.prepassDepthPhiScale);
            float normalW = pow(saturate(dot(normal, centerNormal)), cbSvgf.phiNormal);
            float w = depthW * normalW;

            sumGI += clampedGi * w;
            sumW += w;
        }
    }

    rwPrepassGI[pixPos] = sumGI / max(sumW, 1e-4);
}

// EOF
