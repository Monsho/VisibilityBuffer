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
ConstantBuffer<MaterialTileCB>	cbMaterialTile	: register(b0, space1);

Texture2D<uint>					texVis			: register(t0);
ByteAddressBuffer				rVertexBuffer	: register(t1);
ByteAddressBuffer				rIndexBuffer	: register(t2);
StructuredBuffer<InstanceData>	rInstanceData	: register(t3);
StructuredBuffer<SubmeshData>	rSubmeshData	: register(t4);
StructuredBuffer<MeshletData>	rMeshletData	: register(t5);
StructuredBuffer<DrawCallData>	rDrawCallData	: register(t6);
Texture2D<float>				texDepth		: register(t7);
Texture2D						texColor		: register(t8);
Texture2D						texNormal		: register(t9);
Texture2D						texORM			: register(t10);
Texture2D						texDetail		: register(t11);

SamplerState		samLinearWrap	: register(s0);

RWTexture2D<uint2>	rwFeedback		: register(u0);

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

	// miplevel feedback.
	uint2 TileIndex = pos / 4;
	uint2 TilePos = pos % 4;
	uint neededMiplevel = uint(ComputeMiplevelCS(attr.texcoord, attr.texcoordDDX, attr.texcoordDDY, 4096));
	if (all(TilePos == cbScene.feedbackIndex))
	{
		rwFeedback[TileIndex] = uint2(cbMaterialTile.materialIndex, neededMiplevel);
	}

	// sample texture.
	float3 bc = texColor.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 orm = texORM.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 normalInTS = texNormal.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).xyz * 2 - 1;

	float3 normalV = normalize(mul((float3x3)inData.mtxLocalToWorld, attr.normal));
	float4 tangentV = float4(normalize(mul((float3x3)inData.mtxLocalToWorld, attr.tangent.xyz)), attr.tangent.w);
	
	float3 T, B, N;
	GetTangentSpace(normalV, tangentV, T, B, N);
	normalInTS *= float3(1, -sign(tangentV.w), 1);
	float3 normalInWS;
	if (cbDetail.detailType >= 2)
	{
		normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);
		float2 detailDeriv;
		if (cbDetail.detailType == 2)
		{
			detailDeriv = NormalInTS2SurfaceGradientDeriv(texDetail.SampleLevel(samLinearWrap, attr.texcoord * cbDetail.detailTile, 0).xyz * 2 - 1);
		}
		else
		{
			detailDeriv = DecodeSurfaceGradientDeriv(texDetail.SampleLevel(samLinearWrap, attr.texcoord * cbDetail.detailTile, 0).xy);
		}
		float3 surfGrad = SurfaceGradientFromTBN(detailDeriv, T, B);
		normalInWS = ResolveNormalFromSurfaceGradient(normalInWS, surfGrad * cbDetail.detailIntensity);
	}
	else
	{
		if (cbDetail.detailType == 1)
		{
			float3 detailInTS = texDetail.SampleLevel(samLinearWrap, attr.texcoord * cbDetail.detailTile, 0).xyz * 2 - 1;
			detailInTS *= float3(1, -sign(tangentV.w), 1);
			detailInTS.xy *= cbDetail.detailIntensity;
			detailInTS = normalize(detailInTS);
			normalInTS = normalize(float3(normalInTS.xy + detailInTS.xy, normalInTS.z));
		}
		normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);
	}

	Out.color = float4(bc, 1);
	Out.orm.rgb = orm;
	Out.normal.xyz = normalInWS * 0.5 + 0.5;

	return Out;
}
