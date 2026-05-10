#include "cbuffer.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	worldPos	: WORLDPOS;
};

struct PSOutput
{
	float4	accum	: SV_TARGET0;
};

ConstantBuffer<SceneCB>	cbScene	: register(b0);
ConstantBuffer<WaterCB>	cbWater	: register(b1);

Texture2D			rColor : register(t0);
Texture2D<float>	rDepth : register(t1);

SamplerState samLinear : register(s0);


float3 Refract(float3 v, float3 n, float eta)
{
	float cosTheta = -dot(v, n);
	float k = 1.0 - eta * eta * (1.0f - cosTheta * cosTheta);
	if (k < 0.0)
	{
		return float3(0.0f, 0.0f, 0.0f);
	}
	return normalize(eta * v - (eta * cosTheta + sqrt(k)) * n);
}

[earlydepthstencil]
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float3 WaterNormal = float3(0.0, 1.0, 0.0);
	float3 ViewVec = normalize(In.worldPos - cbScene.eyePosition);
	float3 RefractedVec = Refract(ViewVec, WaterNormal, cbWater.eta);

	float3 RefractedVecVS = mul((float3x3)cbScene.mtxWorldToView, RefractedVec);
	float2 PixelPos = In.position.xy;
	float2 ScreenUV = (PixelPos + 0.5) / cbScene.screenSize.xy;
	float2 RefractedUV = ScreenUV + RefractedVecVS.xy * float2(1.0, -1.0) * cbWater.intensity * cbScene.invScreenSize.xy;

	float4 ScreenColor = rColor.SampleLevel(samLinear, RefractedUV, 0.0);

	Out.accum.rgb = lerp(ScreenColor.rgb, cbWater.color.rgb, cbWater.color.a);
	Out.accum.a = 1.0;

	return Out;
}

//	EOF
