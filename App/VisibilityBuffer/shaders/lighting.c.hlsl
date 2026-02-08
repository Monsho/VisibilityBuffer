#include "cbuffer.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"

ConstantBuffer<SceneCB>				cbScene				: register(b0);
ConstantBuffer<LightCB>				cbLight				: register(b1);
ConstantBuffer<ShadowCB>			cbShadow			: register(b2);

Texture2D							texGBufferA			: register(t0);
Texture2D							texGBufferB			: register(t1);
Texture2D							texGBufferC			: register(t2);
Texture2D<float>					texDepth			: register(t3);
#if SHADOW_TYPE == 0
Texture2D<float>					texShadowDepth		: register(t4);
#else
Texture2D							texShadowExp		: register(t4);
#endif

#if SHADOW_TYPE == 0
SamplerComparisonState				samShadow			: register(s0);
#else
SamplerState						samLinearClamp		: register(s0);
#endif

RWTexture2D<float4>					rwOutput			: register(u0);

#if SHADOW_TYPE == 0
float Shadow(float4 shadowClipPos)
{
	float3 shadowProjPos = shadowClipPos.xyz / shadowClipPos.w;
	float2 shadowUV = shadowProjPos.xy * float2(0.5, -0.5) + 0.5;
	static const int kKernelLevel = 2;
	static const int kKernelWidth = kKernelLevel * 2 + 1;
	float shadow = 0;
	[unroll]
	for (int i = -kKernelLevel; i <= kKernelLevel; i++)
	{
		[unroll]
		for (int j = -kKernelLevel; j <= kKernelLevel; j++)
		{
			shadow += texShadowDepth.SampleCmpLevelZero(samShadow, shadowUV, shadowProjPos.z + cbShadow.constBias, int2(i, j)).r;
		}
	}
	return shadow / (float)(kKernelWidth * kKernelWidth);
}
#else
float Chebyshev(float2 moments, float depth)
{
	const float kVarianceMin = 0.0;
	const float kLightBleedCoeff = 0.0;
	
	if (depth <= moments.x)
		return 1.0;

	float variance = moments.y - (moments.x * moments.x);
	variance = max(variance, kVarianceMin / 1000.0);

	float d = depth - moments.x;
	float p_max = variance / (variance + d * d);

	return saturate((p_max - kLightBleedCoeff) / (1.0 - kLightBleedCoeff));
}

float Shadow(float4 shadowClipPos)
{
	float3 shadowProjPos = shadowClipPos.xyz / shadowClipPos.w;
	float2 shadowUV = shadowProjPos.xy * float2(0.5, -0.5) + 0.5;
	float depth = 1.0 - shadowProjPos.z;

	depth -= cbShadow.constBias;

	float4 moments = texShadowExp.SampleLevel(samLinearClamp, shadowUV, 0);

	float p = exp(cbShadow.exponent.x * depth);
	float n = -exp(-cbShadow.exponent.y * depth);

	float posShadow = Chebyshev(moments.xy, p);
	float negShadow = Chebyshev(moments.zw, n);
	return min(posShadow, negShadow);
}
#endif

float3 Lighting(uint2 pixelPos, float depth)
{
	// get gbuffer.
	float4 color = texGBufferA[pixelPos];
	float3 orm = texGBufferB[pixelPos].xyz;
	float3 normal = texGBufferC[pixelPos].xyz * 2.0 - 1.0;
	float roughness = max(orm.g, 1e-4);

	// get world position.
	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	// get shadow.
	float4 shadowClipPos = mul(cbShadow.mtxWorldToProj, float4(worldPos.xyz, 1));
	float shadow = Shadow(shadowClipPos);

	// apply light.
	float3 viewDirInWS = normalize(cbScene.eyePosition.xyz - worldPos.xyz);
	float3 diffuseColor = color.rgb * (1 - orm.b);
	float3 specularColor = 0.04 * (1 - orm.b) + color.rgb * orm.b;
	float3 directColor = BrdfGGX(diffuseColor, specularColor, roughness, normal, cbLight.directionalVec, viewDirInWS) * cbLight.directionalColor;

	return directColor * shadow;
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
			rwOutput[pixelPos] = float4(0, 0, 1, 1);
		}
		else
		{
			rwOutput[pixelPos] += float4(Lighting(pixelPos, depth), 1);
		}
	}
}