#include "cbuffer.hlsli"

ConstantBuffer<SceneCB>		cbScene		: register(b0);
Texture2D<float>			texDepth	: register(t0);
RWTexture2D<float2>			rwMotion	: register(u0);

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
    uint2 pixPos = did.xy;
    if (any(pixPos >= cbScene.screenSize))
    {
        return;
    }

    float2 uv = (float2(pixPos) + 0.5) * cbScene.invScreenSize;
    float depth = texDepth[pixPos];
    float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);

    float4 prevClipPos = mul(cbScene.mtxProjToPrevProj, clipPos);
    prevClipPos.xyz *= rcp(prevClipPos.w);
    float2 prevUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;

    rwMotion[pixPos] = prevUV - uv;
}
