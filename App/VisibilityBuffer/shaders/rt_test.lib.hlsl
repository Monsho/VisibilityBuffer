#include "common.hlsli"
#include "payload.hlsli"
#include "cbuffer.hlsli"

#define RayTMax			10000.0

// global
ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<LightCB>				cbLight			: REG(b1);

RaytracingAccelerationStructure		TLAS			: REG(t0);

RWTexture2D<float4>					rtAlbedo		: REG(u0);


[shader("raygeneration")]
void TestRGS()
{
	uint2 PixelPos = DispatchRaysIndex().xy;
	float2 xy = (float2)PixelPos + 0.5;
	float2 clipSpacePos = xy / float2(DispatchRaysDimensions().xy) * float2(2, -2) + float2(-1, 1);

	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, 1, 1));
	worldPos.xyz /= worldPos.w;

	float3 origin = cbScene.eyePosition.xyz;
	float3 direction = normalize(worldPos.xyz - origin);

	float3 color = 0;
	float3 albedo = 0;
	float3 normal = float3(0, 0, 1);
	{
		// primary ray.
		RayDesc ray = { origin, 0.0, direction, RayTMax };
		MaterialPayload payload = (MaterialPayload)0;

		TraceRay(TLAS, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);
		if (payload.hitT >= 0.0)
		{
			MaterialParam matParam;
			DecodeMaterialPayload(payload, matParam);
			albedo = matParam.baseColor.rgb;

			RayDesc shadowRay;
			shadowRay.Origin = origin + direction * payload.hitT + matParam.normal * 1.0;
			shadowRay.Direction = cbLight.directionalVec;
			shadowRay.TMin = 0.0;
			shadowRay.TMax = RayTMax;

			MaterialPayload shadowPayload = (MaterialPayload)0;
			shadowPayload.hitT = 100.0;
			TraceRay(TLAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 1, 0, shadowRay, shadowPayload);
			albedo *= shadowPayload.hitT < 0.0 ? 1.0 : 0.0;
		}
	}

	rtAlbedo[PixelPos] = float4(albedo, 1);
}

[shader("miss")]
void TestMS(inout MaterialPayload payload : SV_RayPayload)
{
	payload.hitT = -1.0;
}

// EOF
