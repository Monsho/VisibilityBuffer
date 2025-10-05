#include "cbuffer.hlsli"
#include "mesh_shader.hlsli"
#include "visibility_buffer.hlsli"

#ifndef MATERIAL_TYPE
#	define MATERIAL_TYPE 0 // 0: Opacity, 1: Masked
#endif

struct Meshlet
{
	float3		aabbMin;
	uint		primitiveOffset;
	float3		aabbMax;
	uint		primitiveCount;
	float3		coneApex;
	uint		vertexIndexOffset;
	float3		coneAxis;
	uint		vertexIndexCount;
	float		coneCutoff;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<FrustumCB>		cbFrustum		: register(b1);
ConstantBuffer<MeshCB>			cbMesh			: register(b2);
ConstantBuffer<MeshletCullCB>	cbMeshletCull	: register(b3);

ByteAddressBuffer				rVertexBuffer	: register(t0);
ByteAddressBuffer				rIndexBuffer	: register(t1);
StructuredBuffer<SubmeshData>	rSubmeshData	: register(t2);
StructuredBuffer<MeshletData>	rMeshletData	: register(t3);
StructuredBuffer<DrawCallData>	rDrawCallData	: register(t4);

bool BackFaceCull(float3 ClipPos0, float3 ClipPos1, float3 ClipPos2)
{
	return determinant(float3x3(ClipPos0, ClipPos1, ClipPos2)) < 0;
}

#if TRIANGLE_BACKFACE_CULLING
groupshared float3 ClipPos[256];
#endif

#if MATERIAL_TYPE == 0
#	define VSOutput VSOpacityOutput
#elif MATERIAL_TYPE == 1
#	define VSOutput VSMaskedOutput
#else
#	error "Invalid material type"
#endif

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
	uint gtid : SV_GroupThreadID,
	uint gid : SV_GroupID,
	in payload Payload payload,
	out indices uint3 tris[256],
	out vertices VSOutput verts[255],
	out primitives PrimAttr prims[256]
)
{
	uint globalMeshletIndex = payload.MeshletIndices[gid];
	DrawCallData dcData = rDrawCallData[globalMeshletIndex];
	MeshletData mlData = rMeshletData[dcData.meshletIndex];
	SubmeshData smData = rSubmeshData[mlData.submeshIndex];
	uint vcount = mlData.meshletVertexIndexCount;
	uint pcount = mlData.meshletPackedPrimCount;

	SetMeshOutputCounts(vcount, pcount);

	uint v1 = gtid;
	uint v2 = v1 + 128;
	if (v1 < vcount)
	{
		VSOutput Out;

		float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

		uint index = rIndexBuffer.Load(mlData.meshletVertexIndexOffset + v1 * 4);
		float3 InPos = GetVertexPosition(rVertexBuffer, smData, index);
		float4 cp = mul(mtxLocalToProj, float4(InPos, 1));
		Out.position = cp;
#if MATERIAL_TYPE == 1
		float2 InUV = GetVertexTexcoord(rVertexBuffer, smData, index);
		Out.texcoord = InUV;
#endif
		Out.meshletIndex = globalMeshletIndex;

#if TRIANGLE_BACKFACE_CULLING
		ClipPos[v1] = cp.xyw;
#endif
		verts[v1] = Out;
	}
	if (v2 < vcount)
	{
		VSOutput Out;

		float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

		uint index = rIndexBuffer.Load(mlData.meshletVertexIndexOffset + v2 * 4);
		float3 InPos = GetVertexPosition(rVertexBuffer, smData, index);
		float4 cp = mul(mtxLocalToProj, float4(InPos, 1));
		Out.position = cp;
#if MATERIAL_TYPE == 1
		float2 InUV = GetVertexTexcoord(rVertexBuffer, smData, index);
		Out.texcoord = InUV;
#endif
		Out.meshletIndex = globalMeshletIndex;

#if TRIANGLE_BACKFACE_CULLING
		ClipPos[v2] = cp.xyw;
#endif
		verts[v2] = Out;
	}

	GroupMemoryBarrierWithGroupSync();
	
	uint p1 = gtid;
	uint p2 = p1 + 128;
	if (p1 < pcount)
	{
		uint3 tri = UnpackPrimitiveIndex(rIndexBuffer.Load(mlData.meshletPackedPrimOffset + p1 * 4));
		tris[p1] = tri;
		prims[p1].primitiveID = p1;
#if TRIANGLE_BACKFACE_CULLING
		prims[p1].isCull = BackFaceCull(ClipPos[tri.x], ClipPos[tri.y], ClipPos[tri.z]);
#endif
	}
	if (p2 < pcount)
	{
		uint3 tri = UnpackPrimitiveIndex(rIndexBuffer.Load(mlData.meshletPackedPrimOffset + p2 * 4));
		tris[p2] = tri;
		prims[p2].primitiveID = p2;
#if TRIANGLE_BACKFACE_CULLING
		prims[p2].isCull = BackFaceCull(ClipPos[tri.x], ClipPos[tri.y], ClipPos[tri.z]);
#endif
	}
}

//	EOF
