#include "common.hlsli"
#include "math.hlsli"
#include "../cbuffer.hlsli"

ConstantBuffer<SceneCB> cbScene : REG(b0);
ConstantBuffer<SvgfCB>  cbSvgf  : REG(b1);
ConstantBuffer<SvgfAtrousRootCB> cbAtrous : REG_SPACE(b0, 1);

Texture2D<float3>       texInputGI  : REG(t0);
Texture2D<float>        texVariance : REG(t1);
Texture2D<float>        texDepth    : REG(t2);
Texture2D<float4>       texNormal   : REG(t3);

SamplerState            samLinearClamp : REG(s0);

RWTexture2D<float3>     rwOutputGI : REG(u0);
RWTexture2D<float>      rwOutputVariance : REG(u1);

float Luma(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
    uint2 pixPos = did.xy;
    uint2 dim;
    rwOutputGI.GetDimensions(dim.x, dim.y);
    if (any(pixPos >= dim))
    {
        return;
    }

    float centerDepth = texDepth[pixPos];
    if (centerDepth <= 0.0)
    {
        rwOutputGI[pixPos] = texInputGI[pixPos];
        rwOutputVariance[pixPos] = texVariance[pixPos];
        return;
    }
    float centerVD = ClipDepthToViewDepthRH(centerDepth, cbScene.mtxViewToProj);
    float3 centerNormal = normalize(texNormal[pixPos].xyz * 2.0 - 1.0);
    float3 centerGI = texInputGI[pixPos];

    // Includes the spatial estimate for short or rejected histories.
    float variance = max(texVariance[pixPos], 0.0);
    float colorSigma = cbSvgf.phiColor * sqrt(variance + 1e-4);

    float3 sumGI = 0.0;
    float sumW = 0.0;
    float sumVariance = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 p = int2(pixPos) + int2(x, y) * int(cbAtrous.filterRadius);
            // Skip out-of-bounds taps so the same edge pixel is not counted repeatedly.
            if (any(p < 0) || any(p >= int2(dim))) continue;

            float3 gi = texInputGI[p];
            float depth = texDepth[p];
            if (depth <= 0.0)
            {
                continue;
            }
            float vd = ClipDepthToViewDepthRH(depth, cbScene.mtxViewToProj);
            float3 normal = normalize(texNormal[p].xyz * 2.0 - 1.0);

            float depthW = exp(-abs(vd - centerVD) * cbSvgf.phiDepth);
            float normalW = pow(saturate(dot(normal, centerNormal)), cbSvgf.phiNormal);
            float colorW = exp(-abs(Luma(gi) - Luma(centerGI)) / (colorSigma + 1e-4));

            float w = depthW * normalW * colorW;
            sumGI += gi * w;
            sumVariance += w * w * max(texVariance[p], 0.0);
            sumW += w;
        }
    }

    float invSumW = rcp(max(sumW, 1e-4));
    rwOutputGI[pixPos] = sumGI * invSumW;
    // Propagate variance using squared normalized filter weights.
    rwOutputVariance[pixPos] = sumVariance * invSumW * invSumW;
}

// EOF
