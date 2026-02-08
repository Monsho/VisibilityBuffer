#include "common.hlsli"
#include "rtxgi.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB>				cbScene				: REG(b0);
ConstantBuffer<LightCB>				cbLight				: REG(b1);
ConstantBuffer<AmbOccCB>			cbAO				: REG(b2);

Texture2D									texGBufferC		: REG(t0);
Texture2D<float>							texDepth		: REG(t1);
StructuredBuffer<DDGIVolumeDescGPUPacked>	DDGIVolumes		: REG(t2);
Texture2DArray<float4>						ProbeIrradiance	: REG(t3);
Texture2DArray<float4>						ProbeDistance	: REG(t4);
Texture2DArray<float4>						ProbeData		: REG(t5);

RWTexture2D<float4>					rwOutput			: REG(u0);

SamplerState						samLinear			: REG(s0);

float3 ComputeDDGI(uint2 pixelPos, float depth)
{
	float3 normal = texGBufferC[pixelPos].xyz * 2.0 - 1.0;

	// get world position.
	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGIVolumes[0]);
	float3 camDirInWS = normalize(worldPos.xyz - cbScene.eyePosition.xyz);
	float3 surfaceBias = DDGIGetSurfaceBias(normal, camDirInWS, volume);

	DDGIVolumeResources resources;
	resources.probeIrradiance = ProbeIrradiance;
	resources.probeDistance = ProbeDistance;
	resources.probeData = ProbeData;
	resources.bilinearSampler = samLinear;

	float3 irradiance = 0;
	float blendWeight = DDGIGetVolumeBlendWeight(worldPos.xyz, volume);
	if (blendWeight > 0)
	{
		irradiance += DDGIGetVolumeIrradiance(
			worldPos.xyz,
			surfaceBias,
			normal,
			volume,
			resources) * blendWeight;
	}
	return irradiance * cbAO.giIntensity;
}

[numthreads(8, 8, 1)]
void main(
	uint3 gid : SV_GroupID,
	uint3 gtid : SV_GroupThreadID,
	uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;

	if (all(pixelPos < (uint2)cbScene.screenSize))
	{
		float depth = texDepth[pixelPos];
		[branch]
		if (depth <= 0.0)
		{
			rwOutput[pixelPos] = float4(0, 0, 0, 1);
			return;
		}

		rwOutput[pixelPos] = float4(ComputeDDGI(pixelPos, depth) / PI, 1);
	}
}