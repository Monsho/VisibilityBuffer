#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "visibility_buffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<DetailCB>		cbDetail		: register(b1);

Texture2D<uint>					texVis			: register(t0);
ByteAddressBuffer				rVertexBuffer	: register(t1);
ByteAddressBuffer				rIndexBuffer	: register(t2);
StructuredBuffer<InstanceData>	rInstanceData	: register(t3);
StructuredBuffer<SubmeshData>	rSubmeshData	: register(t4);
StructuredBuffer<MeshletData>	rMeshletData	: register(t5);
StructuredBuffer<DrawCallData>	rDrawCallData	: register(t6);
Texture2D<float>				texDepth		: register(t7);
Texture2D						texNormal		: register(t8);

SamplerState		samLinearWrap	: register(s0);

struct PSOutput
{
	float4	color	: SV_TARGET0;
	float4	orm		: SV_TARGET1;
	float4	normal	: SV_TARGET2;
};

[earlydepthstencil]
PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	// get visibility.
	uint2 pos = (uint2)In.position.xy;
	uint vis = texVis[pos];
	uint drawCallIndex, triIndex;
	DecodeVisibility(vis, drawCallIndex, triIndex);

	// get triangle indices.
	DrawCallData dcData = rDrawCallData[drawCallIndex];
	InstanceData inData = rInstanceData[dcData.instanceIndex];
	MeshletData mlData = rMeshletData[dcData.meshletIndex];
	SubmeshData smData = rSubmeshData[mlData.submeshIndex];
	uint3 vertexIndices = GetVertexIndices(rIndexBuffer, mlData, triIndex);

	// barycentric type.
	const uint kBaryCalcType = 1;

	VertexAttr attr;
	if (kBaryCalcType == 0)
	{
		attr = GetVertexAttrFromRay(
			rVertexBuffer, inData, smData, vertexIndices, In.position.xy,
			cbScene.mtxWorldToProj, cbScene.screenSize, cbScene.eyePosition);
	}
	else if(kBaryCalcType == 1)
	{
		attr = GetVertexAttrPerspectiveCorrect(
			rVertexBuffer, inData, smData, vertexIndices, In.position.xy,
			cbScene.mtxWorldToProj, cbScene.screenSize);
	}

	float4 bc = float4(0.5, 0.5, 0.5, 1.0);
	float3 orm = float3(1.0, 0.5, 0.0);
	float3 N = normalize(mul((float3x3)inData.mtxLocalToWorld, attr.normal));

	// triplanar weights.
	const float k = 3.0;
	float3 weights = abs(N) - 0.2;
	weights = pow(max(0, weights), k);
	weights /= dot(weights, (1.0).xxx);

	// sample normal.
	const float kTile = cbDetail.triplanarTile;
	float3 P = attr.position * kTile;
	float3 dPdX = attr.positionDDX * kTile;
	float3 dPdY = attr.positionDDY * kTile;
	float3 normalX = texNormal.SampleGrad(samLinearWrap, P.yz, dPdX.yz, dPdY.yz).xyz * 2 - 1;
	float3 normalY = texNormal.SampleGrad(samLinearWrap, P.xz, dPdX.xz, dPdY.xz).xyz * 2 - 1;
	float3 normalZ = texNormal.SampleGrad(samLinearWrap, P.xy, dPdX.xy, dPdY.xy).xyz * 2 - 1;

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

	Out.color = bc;
	Out.orm.rgb = orm;
	Out.normal.xyz = normalInWS * 0.5 + 0.5;

	return Out;
}
