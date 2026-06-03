#include "cbuffer.hlsli"

#ifndef WATER_METHOD
#	define WATER_METHOD 1 // 0: Uniform, 1: Newton + GBuffer, 2: Newton + Face, 3: Ray March
#endif

#define NORMAL_FROM_GBUFFER (WATER_METHOD == 1)

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

Texture2D			rColor		: register(t0);
Texture2D			rGBufferC	: register(t1);
Texture2D<float>	rDepth		: register(t2);
Texture2D			rNormal		: register(t3);

SamplerState samLinear		: register(s0);
SamplerState samLinearWrap	: register(s1);


float3 Refract(float3 v, float3 n, float eta)
{
	float cosTheta = -dot(v, n);
	float k = 1.0 - eta * eta * (1.0 - cosTheta * cosTheta);
	if (k < 0.0)
	{
		return float3(0.0f, 0.0f, 0.0f);
	}
	return normalize(eta * v + (eta * cosTheta - sqrt(k)) * n);
}

float3 ReconstructWorldPosition(float2 screenUV, float depth)
{
	float2 clipSpacePos = screenUV * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	return worldPos.xyz /= worldPos.w;
}

bool IntersectPlaneRay(float3 planeP, float3 planeN, float3 rayO, float3 rayD, out float3 hitP)
{
	float denom = dot(rayD, planeN);
	if (denom >= 1e-6)
	{
		return false;
	}
	float t = dot(planeP - rayO, planeN) / denom;
	if (t < 0.0f)
	{
		return false;
	}
	hitP = rayO + rayD * t;
	return true;
}

float3 ReconstructFaceNormal(float2 UV, float3 BaseWorldPos)
{
	float2 uvr = UV + float2(cbScene.invScreenSize.x, 0.0);
	float2 uvb = UV + float2(0.0, cbScene.invScreenSize.y);
	float dr = rDepth.SampleLevel(samLinear, uvr, 0.0);
	float db = rDepth.SampleLevel(samLinear, uvb, 0.0);
	float3 pr = ReconstructWorldPosition(uvr, dr);
	float3 pb = ReconstructWorldPosition(uvb, db);
	return normalize(cross(pb - BaseWorldPos, pr - BaseWorldPos));
}

float ComputeMipLevel(float3 waterPos, float3 planePos, float3 refractedDir)
{
	float3 currDir = normalize(planePos - waterPos);
	float cs = dot(currDir, refractedDir);
	return smoothstep(0.9, 0.2, cs) * 4.0;
}

void LoadNormalAndDepth(float2 UV, float mipLevel, out float3 normal, out float depth)
{
	float3 n = rGBufferC.SampleLevel(samLinear, UV, mipLevel).xyz * 2.0 - 1.0;
	float lenSq = dot(n, n);
	normal = (lenSq > 1e-6) ? n * rsqrt(lenSq) : float3(0.0, 1.0, 0.0);
	depth = rDepth.SampleLevel(samLinear, UV, mipLevel);
}

float2 RaymarchRefractedUV(float3 rayOrigin, float3 rayD, float3 eyePos, float2 initialScreenUV)
{
	const float StepLength = cbWater.stepLength;
	const float DepthThreshold = cbWater.depthThreshold;
	float3 step = rayD * StepLength;
	float3 wp = rayOrigin;
	float2 screenUV = initialScreenUV;
	[loop]
	for (int i = 0; i < cbWater.loopCount; i++)
	{
		wp += step;
		float4 clipPos = mul(cbScene.mtxWorldToProj, float4(wp, 1.0));
		screenUV = saturate((clipPos.xy / clipPos.w) * float2(0.5, -0.5) + 0.5);
		[branch]
		if (any(screenUV < 0.0) || any(screenUV > 1.0))
		{
			screenUV = saturate(screenUV);
			break;
		}
		float depth = rDepth.SampleLevel(samLinear, screenUV, 0.0);
		float3 planeP = ReconstructWorldPosition(screenUV, depth);
		float l1 = length(planeP - eyePos);
		float l2 = length(wp - eyePos);
		[branch]
		if (l1 < l2 && (l2 - l1) < DepthThreshold)
		{
			break;
		}
	}
	return screenUV;
}

[earlydepthstencil]
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float3 WaterNormal = float3(0.0, 1.0, 0.0);
	[branch]
	if (cbWater.bUseNormalTex)
	{
		float2 WaterUV = In.worldPos.xz * 0.01;
		WaterNormal = normalize(lerp(WaterNormal, rNormal.Sample(samLinearWrap, WaterUV).xzy * 2.0 - 1.0, cbWater.normalIntensity));
	}
	float3 eyePos = cbScene.eyePosition.xyz;
	float3 ViewVec = normalize(In.worldPos - eyePos);
	float3 RefractedVec = Refract(ViewVec, WaterNormal, cbWater.eta);

	float4 ScreenColor = 0;

#if WATER_METHOD == 0
	{
		// uniform refraction.
		float3 NormalVecVS = mul((float3x3)cbScene.mtxWorldToView, WaterNormal);
		float2 PixelPos = In.position.xy;
		float2 ScreenUV = (PixelPos + 0.5) / cbScene.screenSize.xy;
		float2 RefractedUV = ScreenUV - NormalVecVS.xy * float2(1.0, -1.0) * cbWater.intensity * cbScene.invScreenSize.xy;

		ScreenColor = rColor.SampleLevel(samLinear, RefractedUV, 0.0);
	}
#elif (WATER_METHOD == 1) || (WATER_METHOD == 2)
	{
		// newton method
		uint2 PixelPos = (uint2)In.position.xy;
		float depth = rDepth[PixelPos];
		[branch]
		if (depth > 0.0)
		{
			float2 BaseUV = ((float2)PixelPos + 0.5) * cbScene.invScreenSize;
			float3 planeP = ReconstructWorldPosition(BaseUV, depth);
#if NORMAL_FROM_GBUFFER
			float mipLevel = ComputeMipLevel(In.worldPos, planeP, RefractedVec);
			float3 planeN;
			LoadNormalAndDepth(BaseUV, mipLevel, planeN, depth);
			planeP = ReconstructWorldPosition(BaseUV, depth);
#else
			float3 planeN = ReconstructFaceNormal(BaseUV, planeP);
#endif
			float2 screenUV;
			bool bHit = false;
			[loop]
			for (int i = 0; i < 4; i++)
			{
				float3 hitP;
				[branch]
				if (!IntersectPlaneRay(planeP, planeN, In.worldPos, RefractedVec, hitP))
				{
					break;
				}

				float4 clipPos = mul(cbScene.mtxWorldToProj, float4(hitP, 1.0));
				screenUV = saturate((clipPos.xy / clipPos.w) * float2(0.5, -0.5) + 0.5);
				[branch]
				if (any(screenUV < 0.0) || any(screenUV > 1.0))
				{
					screenUV = saturate(screenUV);
					bHit = true;
					break;
				}

#if NORMAL_FROM_GBUFFER
				mipLevel = ComputeMipLevel(In.worldPos, hitP, RefractedVec);
				LoadNormalAndDepth(screenUV, mipLevel, planeN, depth);
				planeP = ReconstructWorldPosition(screenUV, depth);
#else
				depth = rDepth.SampleLevel(samLinear, screenUV, 0.0);
				planeP = ReconstructWorldPosition(screenUV, depth);
				planeN = ReconstructFaceNormal(BaseUV, planeP);
#endif
				[branch]
				if (planeP.y > In.worldPos.y)
				{
					break;
				}
				[branch]
				if (distance(hitP, planeP) < 1.0)
				{
					bHit = true;
					break;
				}
			}
			ScreenColor = rColor.SampleLevel(samLinear, screenUV, 0.0);
			[branch]
			if (!bHit)
			{
				// fallback: ray march
				screenUV = RaymarchRefractedUV(In.worldPos, RefractedVec, eyePos, screenUV);
				ScreenColor = !cbWater.debugFallback ? rColor.SampleLevel(samLinear, screenUV, 0.0) : float4(1, 0, 0, 1);
			}
		}
		else
		{
			ScreenColor = rColor[PixelPos];
		}
	}
#else
	{
		// ray march only.
		uint2 PixelPos = (uint2)In.position.xy;
		float depth = rDepth[PixelPos];
		[branch]
		if (depth > 0.0)
		{
			float2 BaseUV = ((float2)PixelPos + 0.5) * cbScene.invScreenSize;
			float2 screenUV = RaymarchRefractedUV(In.worldPos, RefractedVec, eyePos, BaseUV);
			ScreenColor = rColor.SampleLevel(samLinear, screenUV, 0.0);
		}
		else
		{
			ScreenColor = rColor[PixelPos];
		}
	}
#endif

	Out.accum.rgb = lerp(ScreenColor.rgb, cbWater.color.rgb, cbWater.color.a);
	Out.accum.a = 1.0;

	return Out;
}

//	EOF
