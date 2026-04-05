#include "common.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"
#include "payload.hlsli"
#include "cbuffer.hlsli"
#include "restir.hlsli"

#define RayTMax			10000.0

// global
ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<LightCB>				cbLight			: REG(b1);

RaytracingAccelerationStructure		TLAS			: REG(t0);
Texture2D<float4>					texGBufferC		: REG(t1);
Texture2D<float>					texDepth		: REG(t2);
Texture2D							texIrradiance	: REG(t3);

RWTexture2D<float3>					rwGI			: REG(u0);

SamplerState	samLinear			: REG(s0);

[shader("raygeneration")]
void ModteCarloGIRGS()
{
	uint2 pixelPos = DispatchRaysIndex().xy;
	uint2 dim = DispatchRaysDimensions().xy;
	uint pixelIndex = pixelPos.x + pixelPos.y * dim.x;

	float depth = texDepth[pixelPos];
	if (depth <= 0.0)
	{
		rwGI[pixelPos] = 0.0;
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

	float3 radiance = 0.0;

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
		radiance = LightResult;
	}
	else
	{
		// miss, and compute skylight.
		float3 skyIrradiance = texIrradiance.SampleLevel(samLinear, CartesianToLatLong(rayDir), 0).rgb * cbLight.ambientIntensity;
		radiance = skyIrradiance;
	}
	rwGI[pixelPos] = radiance * (1.0 / PI);
}

[shader("miss")]
void MonteCarloGIMS(inout MaterialPayload payload : SV_RayPayload)
{
	payload.hitT = -1.0;
}

// EOF
