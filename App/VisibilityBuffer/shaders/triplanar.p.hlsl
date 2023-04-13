#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float3	posInWS		: POS_IN_WS;
};

struct PSOutput
{
	float4	color	: SV_TARGET0;
	float4	orm		: SV_TARGET1;
	float4	normal	: SV_TARGET2;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<DetailCB>	cbDetail		: register(b1);

Texture2D			texNormal		: register(t0);
SamplerState		samLinearWrap	: register(s0);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float4 baseColor = float4(0.5, 0.5, 0.5, 1.0);
	float3 orm = float3(1.0, 0.5, 0.0);

	float3 N = In.normal.xyz;

	// triplanar weights.
	const float k = 3.0;
	float3 weights = abs(N) - 0.2;
	weights = pow(max(0, weights), k);
	weights /= dot(weights, (1.0).xxx);

	// sample normal.
	const float kTile = cbDetail.triplanarTile;
	float3 normalX = texNormal.Sample(samLinearWrap, In.posInWS.yz * kTile).xyz * 2 - 1;
	float3 normalY = texNormal.Sample(samLinearWrap, In.posInWS.xz * kTile).xyz * 2 - 1;
	float3 normalZ = texNormal.Sample(samLinearWrap, In.posInWS.xy * kTile).xyz * 2 - 1;

	float3 normalInWS;
	if (cbDetail.triplanarType == 1)
	{
		// surface gradient triplanar.
		float2 deriv_x = NormalInTS2SurfaceGradientDeriv(normalX);
		float2 deriv_y = NormalInTS2SurfaceGradientDeriv(normalY);
		float2 deriv_z = NormalInTS2SurfaceGradientDeriv(normalZ);
		float3 surfGrad = SurfaceGradientFromTriplanar(N, weights, deriv_x, deriv_y, deriv_z);
		normalInWS = ResolveNormalFromSurfaceGradient(N, surfGrad);
	}
	else
	{
		// standard blend triplanar.
		float3 nSign = sign(N);
		normalX.z *= nSign.x;
		normalY.z *= nSign.y;
		normalZ.z *= nSign.z;
		normalInWS = normalize(normalX.zyx * weights.x + normalY.xzy * weights.y + normalZ.xyz * weights.z);
	}

	Out.color = baseColor;
	Out.orm.rgb = orm;
	Out.normal.xyz = normalInWS * 0.5 + 0.5;

	return Out;
}
