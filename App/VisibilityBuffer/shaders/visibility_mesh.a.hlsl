#include "cbuffer.hlsli"
#include "culling.hlsli"
#include "mesh_shader.hlsli"

#ifndef OCC_PASS_INDEX
#define OCC_PASS_INDEX 1
#endif

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<FrustumCB>		cbFrustum		: register(b1);
ConstantBuffer<MeshCB>			cbMesh			: register(b2);
ConstantBuffer<MeshletCullCB>	cbMeshletCull	: register(b3);

StructuredBuffer<MeshletBound>	rMeshletBounds	: register(t0);
Texture2D<float>				rHiZ			: register(t1);
ByteAddressBuffer				rDrawFlags		: register(t2);

RWByteAddressBuffer				rwDrawFlags		: register(u0);

groupshared Payload sPayload;

[NumThreads(LANE_COUNT_IN_WAVE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, uint dtid : SV_DispatchThreadID)
{
	bool visible = false;
	bool nonOccCull = false;
	uint meshletIndex = dtid;
	if (meshletIndex < cbMeshletCull.meshletCount)
	{
		MeshletBound bound = rMeshletBounds[meshletIndex];
		
#if  OCC_PASS_INDEX == 1
		uint globalMeshletIndex = meshletIndex + cbMeshletCull.meshletStartIndex;
		if (!IsFrustumCull(bound, cbFrustum.frustumPlanes, cbMesh.mtxLocalToWorld)
			&& !IsBackfaceCull(bound, cbScene.eyePosition.xyz, cbMesh.mtxLocalToWorld))
		{
			float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, cbMesh.mtxLocalToWorld);
			float3 aabbMin, aabbMax;
			if (!ToScreenAABB(bound, mtxLocalToProj, cbScene.nearFar.x, cbScene.nearFar.y, aabbMin, aabbMax))
			{
				if (!IsOcclusionCull(aabbMin, aabbMax, cbScene.screenSize * 0.5, rHiZ, 5))
				{
					visible = true;
				}
			}
			else
			{
				visible = true;
			}
		}
		else
		{
			nonOccCull = true;
		}

		rwDrawFlags.Store(globalMeshletIndex * 4, (visible || nonOccCull) ? 1 : 0);
#else
		uint globalMeshletIndex = meshletIndex + cbMeshletCull.meshletStartIndex;
		if (rDrawFlags.Load(globalMeshletIndex * 4) == 0)
		{
			float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, cbMesh.mtxLocalToWorld);
			float3 aabbMin, aabbMax;
			if (!ToScreenAABB(bound, mtxLocalToProj, cbScene.nearFar.x, cbScene.nearFar.y, aabbMin, aabbMax))
			{
				if (!IsOcclusionCull(aabbMin, aabbMax, cbScene.screenSize * 0.5, rHiZ, 5))
				{
					visible = true;
				}
			}
		}
#endif
	}

	if (visible)
	{
		uint index = WavePrefixCountBits(visible);
		sPayload.MeshletIndices[index] = meshletIndex;
	}

	uint visible_count = WaveActiveCountBits(visible);
	DispatchMesh(visible_count, 1, 1, sPayload);
}

//	EOF
