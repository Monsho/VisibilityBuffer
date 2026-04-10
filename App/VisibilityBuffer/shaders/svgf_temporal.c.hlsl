#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB> cbScene : REG(b0);
ConstantBuffer<SvgfCB>  cbSvgf  : REG(b1);

Texture2D<float>        texDepth        : REG(t0);
Texture2D<float>        texPrevDepth    : REG(t1);
Texture2D<float4>       texGBufferC     : REG(t2);
Texture2D<float3>       texGI           : REG(t3);
Texture2D<float3>       texPrevGI       : REG(t4);
Texture2D<float2>       texPrevMoments  : REG(t5);

SamplerState            samLinearClamp  : REG(s0);

RWTexture2D<float3>     rwTemporalGI    : REG(u0);
RWTexture2D<float2>     rwMoments       : REG(u1);

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

    float depth = texDepth[pixPos];
    float3 normal = normalize(texGBufferC[pixPos].xyz * 2.0 - 1.0);
    float3 currGI = texGI[pixPos];

    float luma = dot(currGI, float3(0.299, 0.587, 0.114));
    float2 currMoments = float2(luma, luma * luma);

    float2 uv = (float2(pixPos) + 0.5) * cbScene.invScreenSize;
    float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
    float4 prevClipPos = mul(cbScene.mtxProjToPrevProj, clipPos);
    prevClipPos.xyz *= (1.0 / prevClipPos.w);
    float2 prevUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;

    [branch]
    if (any(prevUV < 0) || any(prevUV > 1) || depth <= 0.0)
    {
        rwTemporalGI[pixPos] = currGI;
        rwMoments[pixPos] = currMoments;
        return;
    }

    float prevDepth = texPrevDepth.SampleLevel(samLinearClamp, prevUV, 0);
    float prevVD = ClipDepthToViewDepth(prevDepth, cbScene.mtxPrevViewToProj);
    float currVD = ClipDepthToViewDepth(prevClipPos.z, cbScene.mtxPrevViewToProj);
    float depthDiff = abs(prevVD - currVD);

    uint2 prevPix = min((uint2)(prevUV * cbScene.screenSize), dim - 1);
    float3 prevNormal = normalize(texGBufferC[prevPix].xyz * 2.0 - 1.0);
    float normalCos = dot(normal, prevNormal);

    bool validHistory = depthDiff < cbSvgf.disocclusionDepth && normalCos > cbSvgf.disocclusionNormal;

    float3 prevGI = texPrevGI.SampleLevel(samLinearClamp, prevUV, 0);
    float2 prevMoments = texPrevMoments.SampleLevel(samLinearClamp, prevUV, 0);

    float alpha = validHistory ? cbSvgf.temporalResponse : 1.0;
    float momentAlpha = validHistory ? cbSvgf.momentAlpha : 1.0;

    rwTemporalGI[pixPos] = lerp(prevGI, currGI, saturate(alpha));
    rwMoments[pixPos] = lerp(prevMoments, currMoments, saturate(momentAlpha));
}

// EOF
