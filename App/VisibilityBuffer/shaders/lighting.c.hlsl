#include "cbuffer.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"

ConstantBuffer<SceneCB>				cbScene				: register(b0);
ConstantBuffer<LightCB>				cbLight				: register(b1);

Texture2D							texGBufferA			: register(t0);
Texture2D							texGBufferB			: register(t1);
Texture2D							texGBufferC			: register(t2);
Texture2D<float>					texDepth			: register(t3);

RWTexture2D<float4>					rwOutput			: register(u0);

float3 Lighting(uint2 pixelPos, float depth)
{
	// get gbuffer.
	float4 color = texGBufferA[pixelPos];
	float3 orm = texGBufferB[pixelPos].xyz;
	float3 normal = texGBufferC[pixelPos].xyz * 2.0 - 1.0;

	// get world position.
	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	// apply light.
	float3 viewDirInWS = cbScene.eyePosition.xyz - worldPos.xyz;
	float3 diffuseColor = color.rgb * (1 - orm.b);
	float3 specularColor = 0.04 * (1 - orm.b) + color.rgb * orm.b;
	float3 directColor = BrdfGGX(diffuseColor, specularColor, orm.g, normal, cbLight.directionalVec, viewDirInWS) * cbLight.directionalColor;
	float ambientT = normal.y * 0.5 + 0.5;
	float3 ambient = lerp(cbLight.ambientGround, cbLight.ambientSky, ambientT) * cbLight.ambientIntensity;
	return directColor + ambient * diffuseColor;
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
		if (depth >= 1.0)
		{
			rwOutput[pixelPos] = float4(0, 0, 1, 1);
		}
		else
		{
			rwOutput[pixelPos] = float4(Lighting(pixelPos, depth), 1);
		}
	}
}