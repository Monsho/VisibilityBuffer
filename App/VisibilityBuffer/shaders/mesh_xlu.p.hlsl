#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD0;
	float3	worldPos	: WORLDPOS;
};

struct PSOutput
{
	float4	accum		: SV_TARGET0;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<LightCB>		cbLight			: register(b1);

Texture2D			texColor		: register(t0);
Texture2D			texNormal		: register(t1);
Texture2D			texORM			: register(t2);
Texture2D			texEmissive		: register(t3);
Texture2D			texIrradiance	: register(t4);
SamplerState		samLinearWrap	: register(s0);
SamplerState		samLinearEnv	: register(s1);

[earlydepthstencil]
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float4 baseColor = texColor.Sample(samLinearWrap, In.uv);
	float3 orm = texORM.Sample(samLinearWrap, In.uv);
	float3 emissive = texEmissive.Sample(samLinearWrap, In.uv);

	float3 T, B, N;
	GetTangentSpace(In.normal, In.tangent, T, B, N);
	float3 normalInTS = texNormal.Sample(samLinearWrap, In.uv).xyz * 2 - 1;
	normalInTS *= float3(1, -sign(In.tangent.w), 1);
	float3 normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);

	// apply light.
	float3 viewDirInWS = cbScene.eyePosition.xyz - In.worldPos.xyz;
	float3 diffuseColor = baseColor.rgb * (1 - orm.b);
	float3 specularColor = 0.04 * (1 - orm.b) + baseColor.rgb * orm.b;
	float3 directColor = BrdfGGX(diffuseColor, specularColor, orm.g, normalInWS, cbLight.directionalVec, viewDirInWS) * cbLight.directionalColor;

	// apply irradiance.
	float3 ambient = texIrradiance.SampleLevel(samLinearEnv, CartesianToLatLong(normalInWS), 0).rgb * cbLight.ambientIntensity;

	// total result.
	float3 color = emissive + directColor + ambient;

	Out.accum = float4(color, baseColor.a);

	return Out;
}
