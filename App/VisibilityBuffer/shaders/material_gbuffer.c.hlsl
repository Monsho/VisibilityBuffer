#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "visibility_buffer.hlsli"
#include "vrs.hlsli"
#include "math.hlsli"

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<DetailCB>		cbDetail		: register(b1);
ConstantBuffer<MaterialIndexCB>	cbMaterialIndex	: register(b0, space1);

Texture2D<uint>					texVis			: register(t0);
ByteAddressBuffer				rVertexBuffer	: register(t1);
ByteAddressBuffer				rIndexBuffer	: register(t2);
StructuredBuffer<InstanceData>	rInstanceData	: register(t3);
StructuredBuffer<SubmeshData>	rSubmeshData	: register(t4);
StructuredBuffer<MeshletData>	rMeshletData	: register(t5);
StructuredBuffer<DrawCallData>	rDrawCallData	: register(t6);
Texture2D<float>				texDepth		: register(t7);
StructuredBuffer<uint>			rCount			: register(t8);
StructuredBuffer<uint>			rOffset			: register(t9);
StructuredBuffer<uint>			rPixelPos		: register(t10);
Texture2D						texColor		: register(t11);
Texture2D						texNormal		: register(t12);
Texture2D						texORM			: register(t13);
Texture2D						texEmissive		: register(t14);
Texture2D						texDetail		: register(t15);

SamplerState		samLinearWrap	: register(s0);

RWTexture2D<uint2>	rwFeedback		: register(u0);
RWTexture2D<float4>	rwAccum			: register(u1);
RWTexture2D<float4>	rwColor			: register(u2);
RWTexture2D<float4>	rwORM			: register(u3);
RWTexture2D<float4>	rwNormal		: register(u4);


VertexAttr ComputeVertexAttribute(in uint2 pos, out InstanceData OutInstance)
{
	// get visibility.
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
			rVertexBuffer, inData, smData, vertexIndices, (float2)pos,
			cbScene.mtxWorldToProj, cbScene.screenSize, cbScene.eyePosition);
	}
	else if(kBaryCalcType == 1)
	{
		attr = GetVertexAttrPerspectiveCorrect(
			rVertexBuffer, inData, smData, vertexIndices, (float2)pos + 0.5,
			cbScene.mtxWorldToProj, cbScene.screenSize);
	}

	OutInstance = inData;
	return attr;
}

void FeedbackMiplevel(in uint2 pos, in VertexAttr attr, in uint matIndex)
{
	// miplevel feedback.
	uint2 TileIndex = pos / 4;
	uint2 TilePos = pos % 4;
	uint neededMiplevel = uint(ComputeMiplevelCS(attr.texcoord, attr.texcoordDDX, attr.texcoordDDY, 4096));
	if (all(TilePos == cbScene.feedbackIndex))
	{
		rwFeedback[TileIndex] = uint2(matIndex, neededMiplevel);
	}
}

void WriteGBuffer(uint2 pixelPos, uint vrsType, float3 emissive, float3 bc, float3 orm, float3 normalInWS)
{
	// write gbuffer.
	float4 normal = float4(normalInWS * 0.5 + 0.5, 1);
	rwAccum[pixelPos] = float4(emissive, 0);
	rwColor[pixelPos] = float4(bc, 1);
	rwORM[pixelPos] = float4(orm, 1);
	rwNormal[pixelPos] = normal;
	if (vrsType & VRS_2x1)
	{
		rwAccum[pixelPos + uint2(1, 0)] = float4(emissive, 0);
		rwColor[pixelPos + uint2(1, 0)] = float4(bc, 1);
		rwORM[pixelPos + uint2(1, 0)] = float4(orm, 1);
		rwNormal[pixelPos + uint2(1, 0)] = normal;
	}
	if (vrsType & VRS_1x2)
	{
		rwAccum[pixelPos + uint2(0, 1)] = float4(emissive, 0);
		rwColor[pixelPos + uint2(0, 1)] = float4(bc, 1);
		rwORM[pixelPos + uint2(0, 1)] = float4(orm, 1);
		rwNormal[pixelPos + uint2(0, 1)] = normal;
	}
	if (vrsType == VRS_2x2)
	{
		rwAccum[pixelPos + uint2(1, 1)] = float4(emissive, 0);
		rwColor[pixelPos + uint2(1, 1)] = float4(bc, 1);
		rwORM[pixelPos + uint2(1, 1)] = float4(orm, 1);
		rwNormal[pixelPos + uint2(1, 1)] = normal;
	}
}

[numthreads(32, 1, 1)]
void StandardCS(uint dtid : SV_DispatchThreadID)
{
	uint matIndex = cbMaterialIndex.materialIndex;
	if (dtid >= rCount[matIndex])
		return;
	
	uint pixIndex = rOffset[matIndex] + dtid;
	uint2 pixelPos;
	uint vrsType;
	DecodePixelPos(rPixelPos[pixIndex], pixelPos, vrsType);

	// compute vertex attributes.
	InstanceData inData;
	VertexAttr attr = ComputeVertexAttribute(pixelPos, inData);

	// feedback maplevel.
	FeedbackMiplevel(pixelPos, attr, matIndex);

	// sample texture.
	float3 bc = texColor.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 orm = texORM.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 emissive = texEmissive.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
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

	WriteGBuffer(pixelPos, vrsType, emissive, bc, orm, normalInWS);
}

[numthreads(32, 1, 1)]
void TriplanarCS(uint dtid : SV_DispatchThreadID)
{
	uint matIndex = cbMaterialIndex.materialIndex;
	if (dtid >= rCount[matIndex])
		return;
	
	uint pixIndex = rOffset[matIndex] + dtid;
	uint2 pixelPos;
	uint vrsType;
	DecodePixelPos(rPixelPos[pixIndex], pixelPos, vrsType);

	// compute vertex attributes.
	InstanceData inData;
	VertexAttr attr = ComputeVertexAttribute(pixelPos, inData);

	float3 bc = float3(0.5, 0.5, 0.5);
	float3 orm = float3(1.0, 0.5, 0.0);
	float3 emissive = 0;
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

	WriteGBuffer(pixelPos, vrsType, emissive, bc, orm, normalInWS);
}
