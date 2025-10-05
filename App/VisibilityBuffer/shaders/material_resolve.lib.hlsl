#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"
#include "visibility_buffer.hlsli"
#include "math.hlsli"

static const int kTileSize = 8;
static const int kMaxScreenWidth = 3840;
static const int kMaxScreenHeight = 2160;
static const int kMaxDispatchX = (kMaxScreenWidth + kTileSize - 1) / kTileSize;
static const int kMaxDispatchY = (kMaxScreenHeight + kTileSize - 1) / kTileSize;

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
StructuredBuffer<MaterialData>	rMaterialData	: register(t8);
Texture2D						texDetail		: register(t9);

Texture2D						texMaterial[]	: register(t0, space32);

SamplerState		samLinearWrap	: register(s0);

RWTexture2D<uint2>	rwFeedback		: register(u0);
RWTexture2D<float4>	rwAccum			: register(u1);
RWTexture2D<float4>	rwColor			: register(u2);
RWTexture2D<float4>	rwORM			: register(u3);
RWTexture2D<float4>	rwNormal		: register(u4);

struct DistributeNodeRecord
{
	uint3	GridSize	: SV_DispatchGrid;
};

struct MaterialNodeRecord
{
	uint2	PixelPos;
	uint	MaterialIndex;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(kMaxDispatchX, kMaxDispatchY, 1)]
[NumThreads(kTileSize, kTileSize, 1)]
void DistributeMaterialNode(
	uint2 dtid : SV_DispatchThreadID,
	DispatchNodeInputRecord<DistributeNodeRecord> inputRecord,
	[MaxRecords(kTileSize * kTileSize)] [NodeArraySize(2)] NodeOutputArray<MaterialNodeRecord> MaterialNode)
{
	uint2 pixelPos = dtid;
	float depth = texDepth[pixelPos];
	[branch]
	if (depth <= 0.0)
	{
		return;
	}
	uint drawCallIndex, primID;
	DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
	DrawCallData dc = rDrawCallData[drawCallIndex];
	MeshletData ml = rMeshletData[dc.meshletIndex];
	SubmeshData sm = rSubmeshData[ml.submeshIndex];
	MaterialData mat = rMaterialData[sm.materialIndex];

	uint shaderIndex = mat.shaderIndex;
	ThreadNodeOutputRecords<MaterialNodeRecord> outMaterial =
		MaterialNode[shaderIndex].GetThreadNodeOutputRecords(1);
	outMaterial.Get().PixelPos = pixelPos;
	outMaterial.Get().MaterialIndex = sm.materialIndex;

	outMaterial.OutputComplete();
}

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

[Shader("node")]
[NodeID("MaterialNode", 0)]
[NodeLaunch("thread")]
void MaterialStandardNode(ThreadNodeInputRecord<MaterialNodeRecord> inputRecord)
{
	MaterialNodeRecord record = inputRecord.Get();
	uint2 pixelPos = record.PixelPos;
	uint matIndex = record.MaterialIndex;

	// compute vertex attributes.
	InstanceData inData;
	VertexAttr attr = ComputeVertexAttribute(pixelPos, inData);

	// feedback maplevel.
	FeedbackMiplevel(pixelPos, attr, matIndex);

	// sample texture.
	MaterialData mat = rMaterialData[matIndex];
	float3 bc = texMaterial[NonUniformResourceIndex(mat.colorTexIndex)].SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 orm = texMaterial[NonUniformResourceIndex(mat.ormTexIndex)].SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 emissive = texMaterial[NonUniformResourceIndex(mat.emissiveTexIndex)].SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 normalInTS = texMaterial[NonUniformResourceIndex(mat.normalTexIndex)].SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).xyz * 2 - 1;

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
	
	rwAccum[pixelPos] = float4(emissive, 0);
	rwColor[pixelPos] = float4(bc, 1);
	rwORM[pixelPos] = float4(orm, 1);
	rwNormal[pixelPos] = float4(normalInWS * 0.5 + 0.5, 1);
}

[Shader("node")]
[NodeID("MaterialNode", 1)]
[NodeLaunch("thread")]
void MaterialTriplanarNode(ThreadNodeInputRecord<MaterialNodeRecord> inputRecord)
{
	MaterialNodeRecord record = inputRecord.Get();
	uint2 pixelPos = record.PixelPos;
	uint matIndex = record.MaterialIndex;

	// compute vertex attributes.
	InstanceData inData;
	VertexAttr attr = ComputeVertexAttribute(pixelPos, inData);

	float4 bc = float4(0.5, 0.5, 0.5, 1.0);
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
	MaterialData mat = rMaterialData[matIndex];
	Texture2D texNormal = texMaterial[NonUniformResourceIndex(mat.normalTexIndex)];
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

	rwAccum[pixelPos] = float4(emissive, 0);
	rwColor[pixelPos] = bc;
	rwORM[pixelPos] = float4(orm, 1);
	rwNormal[pixelPos] = float4(normalInWS * 0.5 + 0.5, 1);
}
