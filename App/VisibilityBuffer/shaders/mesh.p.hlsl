#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD0;
};

struct PSOutput
{
	float4	color	: SV_TARGET0;
	float4	orm		: SV_TARGET1;
	float4	normal	: SV_TARGET2;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<DetailCB>	cbDetail		: register(b1);

Texture2D			texColor		: register(t0);
Texture2D			texNormal		: register(t1);
Texture2D			texORM			: register(t2);
Texture2D			texDetail		: register(t3);
SamplerState		samLinearWrap	: register(s0);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float4 baseColor = texColor.Sample(samLinearWrap, In.uv);
	float3 orm = texORM.Sample(samLinearWrap, In.uv);

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

	Out.color = baseColor;
	Out.orm.rgb = orm;
	Out.normal.xyz = normalInWS * 0.5 + 0.5;

	return Out;
}
