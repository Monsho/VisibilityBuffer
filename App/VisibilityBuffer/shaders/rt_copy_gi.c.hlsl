#include "common.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB> cbScene : REG(b0);
Texture2D<float3>       texInputGI : REG(t0);
RWTexture2D<float3>     rwOutputGI : REG(u0);

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;
	if (any(pixelPos >= (uint2)cbScene.screenSize))
	{
		return;
	}

	rwOutputGI[pixelPos] = texInputGI[pixelPos];
}

// EOF
