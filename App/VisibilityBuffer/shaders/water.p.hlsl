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

Texture2D			rColor		: register(t0);
Texture2D			rGBufferC	: register(t1);
Texture2D<float>	rDepth		: register(t2);

SamplerState samLinear : register(s0);


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

float3 ReconstructWorldPosition(float2 screenPos, float depth)
{
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
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

[earlydepthstencil]
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float3 WaterNormal = float3(0.0, 1.0, 0.0);
	float3 eyePos = cbScene.eyePosition;
	float3 ViewVec = normalize(In.worldPos - eyePos);
	float3 RefractedVec = Refract(ViewVec, WaterNormal, cbWater.eta);

	float4 ScreenColor = 0;

	[branch]
	if (!cbWater.bNewtonMethod)
	{
		// simple refraction.
		float3 RefractedVecVS = mul((float3x3)cbScene.mtxWorldToView, RefractedVec);
		float2 PixelPos = In.position.xy;
		float2 ScreenUV = (PixelPos + 0.5) / cbScene.screenSize.xy;
		float2 RefractedUV = ScreenUV + RefractedVecVS.xy * float2(1.0, -1.0) * cbWater.intensity * cbScene.invScreenSize.xy;

		ScreenColor = rColor.SampleLevel(samLinear, RefractedUV, 0.0);
	}
	else
	{
		// refraction via newton method
		uint2 PixelPos = (uint2)In.position.xy;
		float depth = rDepth[PixelPos];
		if (depth > 0.0)
		{
			float3 planeP = ReconstructWorldPosition(((float2)PixelPos + 0.5) / cbScene.screenSize, depth);
			float3 planeN = rGBufferC[PixelPos].xyz * 2.0 - 1.0;;
			float2 screenUV;
			bool bHit = true;
			for (int i = 0; i < cbWater.loopCount; i++)
			{
				float3 hitP;
				if (!IntersectPlaneRay(planeP, planeN, In.worldPos, RefractedVec, hitP))
				{
					bHit = false;
					break;
				}
				float4 clipPos = mul(cbScene.mtxWorldToProj, float4(hitP, 1.0));
				screenUV = saturate((clipPos.xy / clipPos.w) * float2(0.5, -0.5) + 0.5);
				depth = rDepth.SampleLevel(samLinear, screenUV, 0.0);
				planeP = ReconstructWorldPosition(screenUV, depth);
				planeN = rGBufferC.SampleLevel(samLinear, screenUV, 0.0);
				if (distance(hitP, planeP) < 1.0)
				{
					break;
				}
			}
			if (bHit)
			{
				ScreenColor = rColor.SampleLevel(samLinear, screenUV, 0.0);
			}
			else
			{
				// fallback: ray march
				const float StepLength = cbWater.stepLength;
				const float DepthThreshold = 10.0;
				float3 step = RefractedVec * StepLength;
				float3 wp = In.worldPos;
				for (int i = 0; i < cbWater.loopCount; i++)
				{
					wp += step;
					float4 clipPos = mul(cbScene.mtxWorldToProj, float4(wp, 1.0));
					screenUV = saturate((clipPos.xy / clipPos.w) * float2(0.5, -0.5) + 0.5);
					if (any(screenUV < 0.0) || any(screenUV > 1.0))
					{
						screenUV = saturate(screenUV);
						break;
					}
					depth = rDepth.SampleLevel(samLinear, screenUV, 0.0);
					planeP = ReconstructWorldPosition(screenUV, depth);
					float l1 = length(planeP - eyePos);
					float l2 = length(wp - eyePos);
					if (l1 < l2 && (l2 - l1) < DepthThreshold)
					{
						break;
					}
				}
				ScreenColor = rColor.SampleLevel(samLinear, screenUV, 0.0);
			}
		}
		else
		{
			ScreenColor = rColor[PixelPos];
		}
	}

	Out.accum.rgb = lerp(ScreenColor.rgb, cbWater.color.rgb, cbWater.color.a);
	Out.accum.a = 1.0;

	return Out;
}

//	EOF
