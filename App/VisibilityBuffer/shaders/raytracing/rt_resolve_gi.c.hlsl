#include "common.hlsli"
#include "math.hlsli"
#include "../cbuffer.hlsli"
#include "restir.hlsli"

ConstantBuffer<SceneCB>         cbScene         : REG(b0);
StructuredBuffer<Reservoir>     reservoirs      : REG(t0);
Texture2D<float>                texDepth        : REG(t1);
Texture2D<float4>               texGBufferC     : REG(t2);
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
	float depth = texDepth[pixelPos];
	if (!IsReservoirValid(reservoir) || depth <= 0.0)
	{
		rwGi[pixelPos] = 0.0;
		return;
	}

	float3 worldPos = GetWorldPos(pixelPos, depth, cbScene.screenSize, cbScene.mtxProjToWorld);
	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);
	float3 wi = normalize(reservoir.samplePosition - worldPos);
	float NoL = saturate(dot(normal, wi));
	// The reservoir weight includes the inverse PDF, not the receiver cosine.
	// Receiver albedo is applied later by the indirect lighting pass.
	rwGi[pixelPos] = reservoir.sampleRadiance * reservoir.weightSum * (NoL / PI);
}

// EOF
