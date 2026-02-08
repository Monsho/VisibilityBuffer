#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "math.hlsli"
#include "common.hlsli"
#include "rtxgi.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	worldPos	: WORLDPOS;
	float3	normal		: NORMAL;
};

struct PSOutput
{
	float4	accum		: SV_TARGET0;
};

ConstantBuffer<SceneCB>		cbScene			: REG(b0);

StructuredBuffer<DDGIVolumeDescGPUPacked>	DDGIVolumes		: REG(t0);
Texture2DArray<float4>						ProbeIrradiance	: REG(t1);
Texture2DArray<float4>						ProbeDistance	: REG(t2);
Texture2DArray<float4>						ProbeData		: REG(t3);

SamplerState		samLinear	: REG(s0);

[earlydepthstencil]
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	// indirect lighting (recursive).
	DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGIVolumes[0]);
	float3 camDirInWS = normalize(In.worldPos.xyz - cbScene.eyePosition.xyz);
	float3 surfaceBias = DDGIGetSurfaceBias(In.normal, camDirInWS, volume);

	DDGIVolumeResources resources;
	resources.probeIrradiance = ProbeIrradiance;
	resources.probeDistance = ProbeDistance;
	resources.probeData = ProbeData;
	resources.bilinearSampler = samLinear;

	float3 irradiance = 0;
	float blendWeight = DDGIGetVolumeBlendWeight(In.worldPos.xyz, volume);
	if (blendWeight > 0)
	{
		irradiance += DDGIGetVolumeIrradiance(
			In.worldPos.xyz,
			surfaceBias,
			In.normal,
			volume,
			resources);

		irradiance *= blendWeight;
	}

	Out.accum = float4(irradiance, 1);
	return Out;
}
