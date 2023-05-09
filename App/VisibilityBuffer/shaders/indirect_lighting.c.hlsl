#include "cbuffer.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"

ConstantBuffer<SceneCB>				cbScene				: register(b0);
ConstantBuffer<LightCB>				cbLight				: register(b1);
ConstantBuffer<DebugCB>				cbDebug				: register(b2);

Texture2D							texGBufferA			: register(t0);
Texture2D							texGBufferB			: register(t1);
Texture2D							texGBufferC			: register(t2);
Texture2D<float>					texDepth			: register(t3);
Texture2D<float>					texAO				: register(t4);
Texture2D<float3>					texGI				: register(t5);

RWTexture2D<float4>					rwOutput			: register(u0);

float3 IndirectLighting(uint2 pixelPos)
{
	// get gbuffer.
	float4 color = texGBufferA[pixelPos];
	float3 orm = texGBufferB[pixelPos].xyz;
	float3 normal = texGBufferC[pixelPos].xyz * 2.0 - 1.0;

	// ao and gi.
	float ao = texAO[pixelPos];
	float3 gi = texGI[pixelPos];

	// apply light.
	float ambientT = normal.y * 0.5 + 0.5;
	float3 ambient = lerp(cbLight.ambientGround, cbLight.ambientSky, ambientT) * cbLight.ambientIntensity;

	return (ambient * ao + gi) * color.rgb;
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
		if (depth > 0.0)
		{
			rwOutput[pixelPos] = rwOutput[pixelPos] + float4(IndirectLighting(pixelPos), 0);
		}
		[branch]
		if (cbDebug.displayMode > 0)
		{
			switch (cbDebug.displayMode)
			{
			case 1: // BaseColor
				rwOutput[pixelPos] = float4(texGBufferA[pixelPos].rgb, 1); break;
			case 2: // Roughness
				rwOutput[pixelPos] = float4(texGBufferB[pixelPos].ggg, 1); break;
			case 3: // Metallic
				rwOutput[pixelPos] = float4(texGBufferB[pixelPos].bbb, 1); break;
			case 4: // World Normal
				rwOutput[pixelPos] = float4(texGBufferC[pixelPos].rgb, 1); break;
			case 5: // AO
				rwOutput[pixelPos] = float4(texAO[pixelPos].xxx, 1); break;
			case 6: // GI
				rwOutput[pixelPos] = float4(texGI[pixelPos].rgb, 1); break;
			}
		}
	}
}