#include "cbuffer.hlsli"
#include "mesh_shader.hlsli"
#include "visibility_buffer.hlsli"

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
	uint meshletIndex = payload.MeshletIndices[gid];
	uint globalMeshletIndex = meshletIndex + cbMeshletCull.meshletStartIndex;
	DrawCallData dcData = rDrawCallData[globalMeshletIndex];
	MeshletData mlData = rMeshletData[dcData.meshletIndex];
	SubmeshData smData = rSubmeshData[mlData.submeshIndex];
	uint vcount = mlData.meshletVertexIndexCount;
	uint pcount = mlData.meshletPackedPrimCount;

	SetMeshOutputCounts(vcount, pcount);

	uint p1 = gtid;
	uint p2 = p1 + 128;
	if (p1 < pcount)
	{
		prims[p1].primitiveID = p1;
		tris[p1] = UnpackPrimitiveIndex(rIndexBuffer.Load(mlData.meshletPackedPrimOffset + p1 * 4));
	}
	if (p2 < pcount)
	{
		prims[p2].primitiveID = p2;
		tris[p2] = UnpackPrimitiveIndex(rIndexBuffer.Load(mlData.meshletPackedPrimOffset + p2 * 4));
	}

	uint v1 = gtid;
	uint v2 = v1 + 128;
	if (v1 < vcount)
	{
		VSOutput Out;

		float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

		uint index = rIndexBuffer.Load(mlData.meshletVertexIndexOffset + v1 * 4);
		float3 InPos = GetVertexPosition(rVertexBuffer, smData, index);
		Out.position = mul(mtxLocalToProj, float4(InPos, 1));
		Out.meshletIndex = globalMeshletIndex;

		verts[v1] = Out;
	}
	if (v2 < vcount)
	{
		VSOutput Out;

		float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

		uint index = rIndexBuffer.Load(mlData.meshletVertexIndexOffset + v2 * 4);
		float3 InPos = GetVertexPosition(rVertexBuffer, smData, index);
		Out.position = mul(mtxLocalToProj, float4(InPos, 1));
		Out.meshletIndex = globalMeshletIndex;

		verts[v2] = Out;
	}
}

//	EOF
