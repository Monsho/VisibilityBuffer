#include "cbuffer.hlsli"
#include "culling.hlsli"

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<FrustumCB>		cbFrustum		: register(b1);
ConstantBuffer<MeshCB>			cbMesh			: register(b2);
ConstantBuffer<MeshletCullCB>	cbMeshletCull	: register(b3);

StructuredBuffer<MeshletBound>	rMeshletBounds	: register(t0);

RWByteAddressBuffer				rwIndirectArgs	: register(u0);

[numthreads(32, 1, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	const uint kDrawIndexedInstancedByteSize = 20;
	const uint kRootConstByteSize = 4;
	const uint kIndirectArgsByteSize = kRootConstByteSize + kDrawIndexedInstancedByteSize;
	
	if (did.x < cbMeshletCull.meshletCount)
	{
		uint meshletIndex = did.x + cbMeshletCull.meshletStartIndex;
		uint argAddress = meshletIndex * kIndirectArgsByteSize;
		uint rootConstAddress = argAddress;
		argAddress += kRootConstByteSize;

		if (!IsFrustumCull(rMeshletBounds[did.x], cbFrustum.frustumPlanes, cbMesh.mtxLocalToWorld)
			&& !IsBackfaceCull(rMeshletBounds[did.x], cbScene.eyePosition.xyz, cbMesh.mtxLocalToWorld))
		{
			// root constant.
			rwIndirectArgs.Store(rootConstAddress, meshletIndex);
		}
		else
		{
			// disable meshlet.
			rwIndirectArgs.Store(argAddress, 0);
		}
	}
}