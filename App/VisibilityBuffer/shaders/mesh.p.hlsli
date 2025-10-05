#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "math.hlsli"

#ifndef ENABLE_MASKED
#	define ENABLE_MASKED 0
#endif

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD0;
};

struct PSOutput
{
	float4	accum		: SV_TARGET0;
	float4	color		: SV_TARGET1;
	float4	orm			: SV_TARGET2;
	float4	normal		: SV_TARGET3;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<DetailCB>	cbDetail		: register(b1);
ConstantBuffer<MaterialTileCB>	cbMaterialTile	: register(b0, space1);

Texture2D			texColor		: register(t0);
Texture2D			texNormal		: register(t1);
Texture2D			texORM			: register(t2);
Texture2D			texEmissive		: register(t3);
Texture2D			texDetail		: register(t4);
SamplerState		samLinearWrap	: register(s0);
RWTexture2D<uint2>	rwFeedback		: register(u0);

#if !ENABLE_MASKED
[earlydepthstencil]
#endif
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float4 baseColor = texColor.Sample(samLinearWrap, In.uv);
#if ENABLE_MASKED
	if (baseColor.a < 0.333)
	{
		discard;
	}
#endif
	float3 orm = texORM.Sample(samLinearWrap, In.uv);
	float3 emissive = texEmissive.Sample(samLinearWrap, In.uv);

	uint2 PixPos = uint2(In.position.xy);
	uint2 TileIndex = PixPos / 4;
	uint2 TilePos = PixPos % 4;
	uint neededMiplevel = uint(ComputeMiplevelPS(In.uv, 4096));
	if (all(TilePos == cbScene.feedbackIndex))
	{
		rwFeedback[TileIndex] = uint2(cbMaterialTile.materialIndex, neededMiplevel);
	}
	
	float3 T, B, N;
	GetTangentSpace(In.normal, In.tangent, T, B, N);
	float3 normalInTS = texNormal.Sample(samLinearWrap, In.uv).xyz * 2 - 1;
	normalInTS *= float3(1, -sign(In.tangent.w), 1);
	float3 normalInWS;
	if (cbDetail.detailType >= 2)
	{
		normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);
		float2 detailDeriv;
		if (cbDetail.detailType == 2)
		{
			detailDeriv = NormalInTS2SurfaceGradientDeriv(texDetail.SampleLevel(samLinearWrap, In.uv * cbDetail.detailTile, 0).xyz * 2 - 1);
		}
		else
		{
			detailDeriv = DecodeSurfaceGradientDeriv(texDetail.SampleLevel(samLinearWrap, In.uv * cbDetail.detailTile, 0).xy);
		}
		float3 surfGrad = SurfaceGradientFromTBN(detailDeriv, T, B);
		normalInWS = ResolveNormalFromSurfaceGradient(normalInWS, surfGrad * cbDetail.detailIntensity);
	}
	else
	{
		if (cbDetail.detailType == 1)
		{
			float3 detailInTS = texDetail.SampleLevel(samLinearWrap, In.uv * cbDetail.detailTile, 0).xyz * 2 - 1;
			detailInTS *= float3(1, -sign(In.tangent.w), 1);
			detailInTS.xy *= cbDetail.detailIntensity;
			detailInTS = normalize(detailInTS);
			normalInTS = normalize(float3(normalInTS.xy + detailInTS.xy, normalInTS.z));
		}
		normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);
	}

	Out.accum = float4(emissive, 0);
	Out.color = baseColor;
	Out.orm.rgb = orm;
	Out.normal.xyz = normalInWS * 0.5 + 0.5;

	return Out;
}
