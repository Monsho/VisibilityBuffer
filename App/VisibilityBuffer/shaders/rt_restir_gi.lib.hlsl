#include "common.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"
#include "payload.hlsli"
#include "cbuffer.hlsli"

#define RayTMax			10000.0
#define TEMPORAL_DEPTH_EPS	0.01

struct Reservoir
{
	float3	sampleRadiance;
	float	weightSum;
	float3	samplePosition;
	float	targetPdf;
	float3	sampleNormal;
	float	ucw;
	uint	M;
	uint	isValid;
	uint	pad0;
	uint	pad1;
};

// global
ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<LightCB>				cbLight			: REG(b1);

RaytracingAccelerationStructure		TLAS			: REG(t0);
Texture2D<float4>					texGBufferC		: REG(t1);
Texture2D<float>					texDepth		: REG(t2);
Texture2D							texIrradiance	: REG(t3);
Texture2D<float2>					texMotion		: REG(t4);
Texture2D<float>					texPrevDepth	: REG(t5);
StructuredBuffer<Reservoir>			prevReservoirs	: REG(t6);

RWStructuredBuffer<Reservoir>		rwReservoirs	: REG(u0);

SamplerState	samLinear			: REG(s0);

float Hash(uint v)
{
	v ^= 2747636419u;
	v *= 2654435769u;
	v ^= v >> 16;
	v *= 2654435769u;
	v ^= v >> 16;
	v *= 2654435769u;
	return (float)(v & 0x00ffffffu) * (1.0 / 16777216.0);
}

static const float kEpsPdf = 1e-6;

void ReservoirUpdateCandidate(
	inout Reservoir r,
	float3 sampleRadiance,
	float3 samplePosition,
	float3 sampleNormal,
	float targetPdf,
	float w,
	uint  m,
	float rnd)
{
	[branch]
	if (w <= 0.0 || m == 0 || targetPdf <= 0.0)
		return;

	// Weighted reservoir update.
	r.weightSum += w;
	r.M += m;
	[branch]
	if (rnd < (w / max(r.weightSum, kEpsPdf)))
	{
		r.sampleRadiance = sampleRadiance;
		r.samplePosition = samplePosition;
		r.sampleNormal   = sampleNormal;
		r.targetPdf      = targetPdf;
		r.isValid        = 1;
	}
}

void ReservoirFinalize(inout Reservoir r)
{
	[branch]
	if (r.isValid == 0 || r.M == 0 || r.targetPdf <= 0.0)
	{
		r.ucw = 0.0;
		return;
	}

	// Unbiased contribution weight (ucw) used later for spatial/temporal reuse.
	r.ucw = r.weightSum / ((float)r.M * max(r.targetPdf, kEpsPdf));
}

[shader("raygeneration")]
void InitialSampleRGS()
{
	uint2 pixelPos = DispatchRaysIndex().xy;
	uint2 dim = DispatchRaysDimensions().xy;
	uint pixelIndex = pixelPos.x + pixelPos.y * dim.x;

	Reservoir reservoir = (Reservoir)0;

	float depth = texDepth[pixelPos];
	if (depth <= 0.0)
	{
		rwReservoirs[pixelIndex] = reservoir;
		return;
	}

	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);

	// reconstruct world position.
	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	// generate ray direction in world hemisphere.
	float2 rndVec2 = float2(Hash(pixelIndex * 2 + 0), Hash(pixelIndex * 2 + 1));
	float3 up = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
	float3 tangent = normalize(cross(up, normal));
	float3 bitangent = cross(normal, tangent);
	float phi = 2.0 * PI * rndVec2.x;
	float cosTheta = sqrt(saturate(1.0 - rndVec2.y));
	float sinTheta = sqrt(saturate(rndVec2.y));
	float3 localDir = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	float3 rayDir = normalize(localDir.x * tangent + localDir.y * bitangent + localDir.z * normal);

	// ray tracing.
	RayDesc ray;
	ray.Origin = worldPos.xyz + normal * 0.01;
	ray.Direction = rayDir;
	ray.TMin = 0.0;
	ray.TMax = RayTMax;

	MaterialPayload payload = (MaterialPayload)0;
	TraceRay(TLAS, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);

	[branch]
	if (payload.hitT >= 0.0)
	{
		// hit material.
		MaterialParam matParam;
		DecodeMaterialPayload(payload, matParam);
		float NoL = saturate(dot(matParam.normal, cbLight.directionalVec));
		float ShadowFactor = 1.0;

		[branch]
		if (NoL > 0.0)
		{
			RayDesc shadowRay;
			shadowRay.Origin = ray.Origin + ray.Direction * payload.hitT + matParam.normal * 0.01;
			shadowRay.Direction = cbLight.directionalVec;
			shadowRay.TMin = 0.0;
			shadowRay.TMax = RayTMax;

			MaterialPayload shadowPayload = (MaterialPayload)0;
			TraceRay(TLAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 1, 0, shadowRay, shadowPayload);
			ShadowFactor = shadowPayload.hitT < 0.0 ? 1.0 : 0.0;
		}
		float3 DiffuseColor = matParam.baseColor.rgb * (1 - matParam.metallic) * (matParam.baseColor.a < 0.33 ? 0 : 1);
		float3 DiffuseResult = DiffuseLambert(DiffuseColor);
		float3 LightResult = DiffuseResult * NoL * cbLight.directionalColor * ShadowFactor + matParam.emissive;

		reservoir.sampleRadiance = matParam.emissive + LightResult;
		reservoir.samplePosition = ray.Origin + ray.Direction * payload.hitT;
		reservoir.sampleNormal = matParam.normal;
		reservoir.targetPdf = max(dot(normal, rayDir), 0.0) * (1.0 / PI);
		reservoir.weightSum = reservoir.targetPdf > 0.0 ? rcp(reservoir.targetPdf) : 0.0;
		reservoir.ucw = reservoir.weightSum;
		reservoir.M = 1;
		reservoir.isValid = 1;
	}
	else
	{
		// miss, and compute skylight.
		float3 skyIrradiance = texIrradiance.SampleLevel(samLinear, CartesianToLatLong(rayDir), 0).rgb * cbLight.ambientIntensity;

		reservoir.sampleRadiance = skyIrradiance;
		reservoir.samplePosition = ray.Direction * RayTMax;
		reservoir.sampleNormal = ray.Direction;
		reservoir.targetPdf = max(dot(normal, rayDir), 0.0) * (1.0 / PI);
		reservoir.weightSum = reservoir.targetPdf > 0.0 ? rcp(reservoir.targetPdf) : 0.0;
		reservoir.ucw = reservoir.weightSum;
		reservoir.M = 1;
		reservoir.isValid = 1;
	}

	// -------------------------------------------------------------------------
	// Temporal reuse (reservoir update in time)
	// -------------------------------------------------------------------------
	// Reproject current pixel to previous frame and merge with previous reservoir.
	// This is intentionally minimal (good enough for a first temporal ReSTIR GI
	// sample). You can improve robustness by using normals/roughness/material-id
	// checks and by clamping history length.
	{
		float2 motionUV = texMotion[pixelPos];
		float2 currUV = (float2(pixelPos) + 0.5) / (float2)dim;
		float2 prevUV = currUV + motionUV;
		float2 prevPixF = prevUV * (float2)dim - 0.5;
		uint2 prevPixelPos = (uint2)round(prevPixF);

		[branch]
		if (all(prevPixelPos >= 0) && all(prevPixelPos < dim))
		{
			float prevDepth = texPrevDepth[prevPixelPos];
			// Simple disocclusion rejection using depth.
			[branch]
			if (abs(prevDepth - depth) <= TEMPORAL_DEPTH_EPS)
			{
				uint prevIndex = prevPixelPos.x + prevPixelPos.y * dim.x;
				Reservoir prevRes = prevReservoirs[prevIndex];

				[branch]
				if (prevRes.isValid != 0)
				{
					// Evaluate the reused sample under the *current* shading point.
					float3 dirPrev = normalize(prevRes.samplePosition - worldPos.xyz);
					float targetPdfPrev = max(dot(normal, dirPrev), 0.0) * (1.0 / PI);

					// Convert previous reservoir's accumulated weight to current target.
					// (Very common ReSTIR trick: scale by p_hat_current / p_hat_previous).
					float wPrev = prevRes.weightSum * (targetPdfPrev / max(prevRes.targetPdf, kEpsPdf));

					Reservoir merged = (Reservoir)0;
					// Use a single random number for sequential reservoir updates.
					float rndScalar = Hash(pixelIndex * 4 + 0);

					// Candidate 0: current frame initial sample.
					ReservoirUpdateCandidate(
						merged,
						reservoir.sampleRadiance,
						reservoir.samplePosition,
						reservoir.sampleNormal,
						reservoir.targetPdf,
						reservoir.weightSum,
						reservoir.M,
						rndScalar);

					// Candidate 1: reprojected previous-frame reservoir.
					ReservoirUpdateCandidate(
						merged,
						prevRes.sampleRadiance,
						prevRes.samplePosition,
						prevRes.sampleNormal,
						targetPdfPrev,
						wPrev,
						prevRes.M,
						rndScalar);

					ReservoirFinalize(merged);

					[branch]
					if (merged.isValid != 0)
					{
						reservoir = merged;
					}
				}
			}
		}
	}

	rwReservoirs[pixelIndex] = reservoir;
}

[shader("miss")]
void InitialSampleMS(inout MaterialPayload payload : SV_RayPayload)
{
	payload.hitT = -1.0;
}

// EOF
