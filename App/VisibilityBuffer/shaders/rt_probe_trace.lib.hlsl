#include "common.hlsli"
#include "rtxgi.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"
#include "payload.hlsli"
#include "cbuffer.hlsli"

#define RayTMax			10000.0

// global
ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<LightCB>				cbLight			: REG(b1);

RaytracingAccelerationStructure				TLAS			: REG(t0);
StructuredBuffer<DDGIVolumeDescGPUPacked>	DDGIVolumes		: REG(t1);
Texture2DArray<float4>						ProbeIrradiance	: REG(t2);
Texture2DArray<float4>						ProbeDistance	: REG(t3);
Texture2DArray<float4>						ProbeData		: REG(t4);
Texture2D									texIrradiance	: REG(t5);

RWTexture2DArray<float4>					rwRayData		: REG(u0);

SamplerState	samLinear			: REG(s0);


[shader("raygeneration")]
void ProbeTraceRGS()
{
    // Compute the probe index for this thread
	DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGIVolumes[0]);
	int rayIndex = DispatchRaysIndex().x;                    // index of the ray to trace for this probe
	int probePlaneIndex = DispatchRaysIndex().y;             // index of this probe within the plane of probes
	int planeIndex = DispatchRaysIndex().z;                  // index of the plane this probe is part of
	int probesPerPlane = DDGIGetProbesPerPlane(volume.probeCounts);
    int probeIndex = (planeIndex * probesPerPlane) + probePlaneIndex;

	// Get the probe's grid coordinates
	float3 probeCoords = DDGIGetProbeCoords(probeIndex, volume);

	// Adjust the probe index for the scroll offsets
	probeIndex = DDGIGetScrollingProbeIndex(probeCoords, volume);

	// Get the probe's state
	float probeState = DDGILoadProbeState(probeIndex, ProbeData, volume);

	// Early out: do not shoot rays when the probe is inactive *unless* it is one of the "fixed" rays used by probe classification
	if (probeState == RTXGI_DDGI_PROBE_STATE_INACTIVE && rayIndex >= RTXGI_DDGI_NUM_FIXED_RAYS) return;

	// Get the probe's world position
	// Note: world positions are computed from probe coordinates *not* adjusted for infinite scrolling
	float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, ProbeData);

	// Get a random normalized ray direction to use for a probe ray
	float3 probeRayDirection = DDGIGetProbeRayDirection(rayIndex, volume);

	// Get the coordinates for the probe ray in the RayData texture array
	// Note: probe index is the scroll adjusted index (if scrolling is enabled)
	uint3 outputCoords = DDGIGetRayDataTexelCoords(rayIndex, probeIndex, volume);

	// probe ray.
	RayDesc ray;
	ray.Origin = probeWorldPosition;
	ray.Direction = probeRayDirection;
	ray.TMin = 0.0;
	ray.TMax = volume.probeMaxRayDistance;
	MaterialPayload payload = (MaterialPayload)0;

	TraceRay(TLAS, RAY_FLAG_FORCE_OPAQUE, ~0, 0, 1, 0, ray, payload);

	if (payload.hitT < 0.0)
	{
		// miss hit.
		float3 skyIrradiance = texIrradiance.SampleLevel(samLinear, CartesianToLatLong(probeRayDirection), 0).rgb * cbLight.ambientIntensity;
        DDGIStoreProbeRayMiss(rwRayData, outputCoords, volume, skyIrradiance);
		return;
	}

	MaterialParam matParam;
	DecodeMaterialPayload(payload, matParam);

	if (matParam.flag & kFlagBackFaceHit)
	{
		// back face hit.
		DDGIStoreProbeRayBackfaceHit(rwRayData, outputCoords, volume, payload.hitT);
		return;
	}

	// fixed ray early out.
	if((volume.probeRelocationEnabled || volume.probeClassificationEnabled) && rayIndex < RTXGI_DDGI_NUM_FIXED_RAYS)
	{
		DDGIStoreProbeRayFrontfaceHit(rwRayData, outputCoords, volume, payload.hitT);
		return;
	}

	// hit material direct diffuse lighting.
	float NoL = saturate(dot(matParam.normal, cbLight.directionalVec));
	float ShadowFactor = 1.0;
	[branch]
	if (NoL > 0.0)
	{
		RayDesc shadowRay;
		shadowRay.Origin = probeWorldPosition + probeRayDirection * payload.hitT + matParam.normal * 1.0;
		shadowRay.Direction = cbLight.directionalVec;
		shadowRay.TMin = 0.0;
		shadowRay.TMax = volume.probeMaxRayDistance;

		MaterialPayload shadowPayload = (MaterialPayload)0;
		TraceRay(TLAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 1, 0, shadowRay, shadowPayload);
		ShadowFactor = shadowPayload.hitT < 0.0 ? 1.0 : 0.0;
	}
	float3 DiffuseColor = matParam.baseColor.rgb * (1 - matParam.metallic) * (matParam.baseColor.a < 0.33 ? 0 : 1);
	float3 DiffuseResult = DiffuseLambert(DiffuseColor);
	float3 LightResult = DiffuseResult * NoL * cbLight.directionalColor * ShadowFactor + matParam.emissive;

	// indirect lighting (recursive).
	float3 Irradiance = 0.0;
    float3 surfaceBias = DDGIGetSurfaceBias(matParam.normal, ray.Direction, volume);

	DDGIVolumeResources resources;
	resources.probeIrradiance = ProbeIrradiance;
	resources.probeDistance = ProbeDistance;
	resources.probeData = ProbeData;
	resources.bilinearSampler = samLinear;

	// Compute volume blending weight
	float3 hitPosition = ray.Origin + ray.Direction * payload.hitT;
	float volumeBlendWeight = DDGIGetVolumeBlendWeight(hitPosition, volume);

	// Don't evaluate irradiance when the surface is outside the volume
	if (volumeBlendWeight > 0)
	{
		// Get irradiance from the DDGIVolume
		Irradiance = DDGIGetVolumeIrradiance(
			hitPosition,
			surfaceBias,
			matParam.normal,
			volume,
			resources);

		// Attenuate irradiance by the blend weight
		Irradiance *= volumeBlendWeight;
	}

	// Store the final ray radiance and hit distance
	float3 radiance = LightResult + (DiffuseColor / PI) * Irradiance;
	DDGIStoreProbeRayFrontfaceHit(rwRayData, outputCoords, volume, radiance, payload.hitT);
}

[shader("miss")]
void ProbeTraceMS(inout MaterialPayload payload : SV_RayPayload)
{
	payload.hitT = -1.0;
}

// EOF
