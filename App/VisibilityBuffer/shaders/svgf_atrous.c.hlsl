#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB> cbScene : REG(b0);
ConstantBuffer<SvgfCB>  cbSvgf  : REG(b1);
ConstantBuffer<SvgfAtrousRootCB> cbAtrous : REG_SPACE(b0, 1);

Texture2D<float3>       texInputGI : REG(t0);
Texture2D<float2>       texMoments : REG(t1);
Texture2D<float>        texDepth   : REG(t2);
Texture2D<float4>       texNormal  : REG(t3);

SamplerState            samLinearClamp : REG(s0);

RWTexture2D<float3>     rwOutputGI : REG(u0);

float ClipDepthToViewDepth(float D, float4x4 mtxViewToClip)
{
    return (D * mtxViewToClip[3][3] - mtxViewToClip[2][3]) / (mtxViewToClip[2][2] - D * mtxViewToClip[3][2]);
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
        rwOutputGI[pixPos] = texInputGI[pixPos];
        return;
    }
    float centerVD = ClipDepthToViewDepth(centerDepth, cbScene.mtxViewToProj);
    float3 centerNormal = normalize(texNormal[pixPos].xyz * 2.0 - 1.0);
    float3 centerGI = texInputGI[pixPos];

    float2 moments = texMoments[pixPos];
    float variance = max(0.0, moments.y - moments.x * moments.x);
    float colorSigma = cbSvgf.phiColor * sqrt(variance + 1e-4);

    float3 sumGI = 0.0;
    float sumW = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 p = int2(pixPos) + int2(x, y) * int(cbAtrous.filterRadius);
            p = clamp(p, int2(0, 0), int2(dim) - 1);

            float3 gi = texInputGI[p];
            float depth = texDepth[p];
            if (depth <= 0.0)
            {
                continue;
            }
            float vd = ClipDepthToViewDepth(depth, cbScene.mtxViewToProj);
            float3 normal = normalize(texNormal[p].xyz * 2.0 - 1.0);

            float depthW = exp(-abs(vd - centerVD) * cbSvgf.phiDepth);
            float normalW = pow(saturate(dot(normal, centerNormal)), cbSvgf.phiNormal);
            float colorW = exp(-length(gi - centerGI) / (colorSigma + 1e-4));

            float w = depthW * normalW * colorW;
            sumGI += gi * w;
            sumW += w;
        }
    }

    rwOutputGI[pixPos] = sumGI / max(sumW, 1e-4);
}

// EOF
