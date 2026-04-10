#include "common.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB> cbScene : REG(b0);

Texture2D<float2>       texMoments : REG(t0);
RWTexture2D<float>      rwVariance : REG(u0);

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
    uint2 pixPos = did.xy;
    uint2 dim = (uint2)cbScene.screenSize;
    if (any(pixPos >= dim))
    {
        return;
    }

    float2 moments = texMoments[pixPos];
    rwVariance[pixPos] = max(0.0, moments.y - moments.x * moments.x);
}

// EOF
