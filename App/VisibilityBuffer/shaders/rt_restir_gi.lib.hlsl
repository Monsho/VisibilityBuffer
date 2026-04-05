#include "common.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"
#include "payload.hlsli"
#include "cbuffer.hlsli"
#include "restir.hlsli"

#define RayTMax			10000.0
#define TEMPORAL_DEPTH_EPS	0.01

// global
ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<LightCB>				cbLight			: REG(b1);
ConstantBuffer<RestirCB>			cbRestir		: REG(b2);

RaytracingAccelerationStructure		TLAS			: REG(t0);
Texture2D<float4>					texGBufferC		: REG(t1);
Texture2D<float>					texDepth		: REG(t2);
Texture2D							texIrradiance	: REG(t3);
Texture2D<float2>					texMotion		: REG(t4);
Texture2D<float>					texPrevDepth	: REG(t5);
StructuredBuffer<Reservoir>			prevReservoirs	: REG(t6);

RWStructuredBuffer<Reservoir>		rwReservoirs	: REG(u0);

SamplerState	samLinear			: REG(s0);

[shader("raygeneration")]
void InitialSampleRGS()
{
	uint2 pixelPos = DispatchRaysIndex().xy;
	uint2 dim = DispatchRaysDimensions().xy;
	uint pixelIndex = pixelPos.x + pixelPos.y * dim.x;

	Reservoir reservoir = ReservoirEmpty();

	float depth = texDepth[pixelPos];
	if (depth <= 0.0)
	{
		rwReservoirs[pixelIndex] = reservoir;
		return;
	}

	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);

	// reconstruct world position.
	float3 worldPos = GetWorldPos(pixelPos, depth, cbScene.screenSize, cbScene.mtxProjToWorld);

	// generate ray direction in world hemisphere.
	uint temporalSeed = cbScene.frameIndex * 0x9e3779b9u;
	float2 rndVec2 = float2(
		Hash(pixelIndex * 2 + 0 + temporalSeed),
		Hash(pixelIndex * 2 + 1 + temporalSeed));
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

	float primaryNoL = saturate(dot(normal, rayDir));
	float samplePDF = primaryNoL / PI;

	[branch]
	if (payload.hitT >= 0.0)
	{
		// hit material.
		MaterialParam matParam;
		DecodeMaterialPayload(payload, matParam);
		float sampledNoL = saturate(dot(matParam.normal, cbLight.directionalVec));
		float ShadowFactor = 1.0;

		[branch]
		if (sampledNoL > 0.0)
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
		float3 LightResult = DiffuseResult * sampledNoL * cbLight.directionalColor * ShadowFactor + matParam.emissive;

		ReservoirMake(reservoir,
			LightResult,
			ray.Origin + ray.Direction * payload.hitT,
			matParam.normal,
			samplePDF);
	}
	else
	{
		// miss, and compute skylight.
		float3 skyIrradiance = texIrradiance.SampleLevel(samLinear, CartesianToLatLong(rayDir), 0).rgb * cbLight.ambientIntensity;

		ReservoirMake(reservoir,
			skyIrradiance,
			rayDir * RayTMax,
			rayDir,
			samplePDF);
	}
	float selectedPdf = ReservoirGetGIPdf(reservoir.sampleRadiance, primaryNoL);

	if (MATH_VERIFY_MODE)
	{
		rwReservoirs[pixelIndex] = reservoir;
		return;
	}

	// Select initial sample.
	Reservoir merged = ReservoirEmpty();
	ReservoirCombine(merged, reservoir, selectedPdf, 0.5);

	// Temporal reuse.
	bool IsPreviousFounded = false;
	Reservoir prevRes = ReservoirEmpty();
	float3 prevWorldPos = 0;

	[branch]
	if (!cbRestir.initialFrame)
	{
		float2 motionUV = texMotion[pixelPos];
		float2 currUV = (float2(pixelPos) + 0.5) / (float2)dim;
		float2 prevUV = currUV + motionUV;
		float2 prevPixF = prevUV * (float2)dim - 0.5;
		uint2 prevPixelPos = (uint2)round(prevPixF);

		if (all(prevPixelPos >= 0) && all(prevPixelPos < dim))
		{
			float prevDepth = texPrevDepth[prevPixelPos];
			// Simple disocclusion rejection using depth.
			if (abs(prevDepth - depth) <= TEMPORAL_DEPTH_EPS)
			{
				uint prevIndex = prevPixelPos.x + prevPixelPos.y * dim.x;
				prevRes = prevReservoirs[prevIndex];
				IsPreviousFounded = IsReservoirValid(prevRes);
				prevWorldPos = GetWorldPos(prevPixelPos, prevDepth, cbScene.screenSize, mul(cbScene.mtxProjToWorld, cbScene.mtxPrevProjToProj));
			}
		}
	}

	[branch]
	if (IsPreviousFounded)
	{
		float Jacobian = ComputeJacobian(worldPos, prevWorldPos, prevRes.samplePosition, prevRes.sampleNormal);
		if (!IsValidateJacobian(Jacobian))
			IsPreviousFounded = false;

		prevRes.weightSum *= Jacobian;

		// Increment history age.
		prevRes.M = min(prevRes.M, kMaxReservoirM);
		prevRes.age++;

		if (prevRes.age < kMaxReservoirAge)
			IsPreviousFounded = false;
	}

	[branch]
	if (IsPreviousFounded)
	{
		// Evaluate the reused sample under the *current* shading point.
		float3 dirPrev = normalize(prevRes.samplePosition - worldPos.xyz);
		float targetPdfPrev = ReservoirGetGIPdf(prevRes.sampleRadiance, max(dot(normal, dirPrev), 0.0));

		// Use a single random number for sequential reservoir updates.
		float rndScalar = Hash(pixelIndex * 4 + cbScene.frameIndex * 31u + 17u);

		// Candidate 1: reprojected previous-frame reservoir.
		bool IsPreviousSelection = ReservoirCombine(merged, prevRes, targetPdfPrev, rndScalar);
		if (IsPreviousSelection)
		{
			selectedPdf = targetPdfPrev;
		}
	}

	// normalize weightSum.
	float normalizeN = 1.0;
	float normalizeD = selectedPdf * merged.M;
	ReservoirFinalizeResampling(merged, normalizeN, normalizeD);

	rwReservoirs[pixelIndex] = merged;
}

[shader("miss")]
void InitialSampleMS(inout MaterialPayload payload : SV_RayPayload)
{
	payload.hitT = -1.0;
}

// EOF
