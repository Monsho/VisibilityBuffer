#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"
#include "restir.hlsli"

ConstantBuffer<SceneCB>         cbScene         : REG(b0);
StructuredBuffer<Reservoir>     reservoirs      : REG(t0);
RWTexture2D<float3>             rwGi            : REG(u0);

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;
	uint2 dim = (uint2)cbScene.screenSize;
	if (any(pixelPos >= dim))
	{
		return;
	}

	uint pixelIndex = pixelPos.x + pixelPos.y * dim.x;
	Reservoir reservoir = reservoirs[pixelIndex];
	if (!IsReservoirValid(reservoir))
	{
		rwGi[pixelPos] = 0.0;
		return;
	}

	float3 radiance = reservoir.sampleRadiance * reservoir.weightSum;
	rwGi[pixelPos] = radiance * (1.0 / PI);
}

// EOF
