#include "constant_defs.h"
#include "cbuffer.hlsli"
#include "visibility_buffer.hlsli"

ConstantBuffer<SceneCB>			cbScene				: register(b0);
ConstantBuffer<TileCB>			cbTile				: register(b1);

Texture2D<uint>					texVis				: register(t0);
StructuredBuffer<SubmeshData>	rSubmeshData		: register(t1);
StructuredBuffer<MeshletData>	rMeshletData		: register(t2);
StructuredBuffer<DrawCallData>	rDrawCallData		: register(t3);
Texture2D<float>				texDepth			: register(t4);

RWByteAddressBuffer				rwDrawArg			: register(u0);
RWByteAddressBuffer				rwTileIndex			: register(u1);

groupshared uint shMaterialFlag[CLASSIFY_MATERIAL_CHUNK_MAX];

void ClassifyPixel(uint2 pos)
{
	if (all(pos < (uint2)cbScene.screenSize))
	{
		float depth = texDepth[pos];
		[branch]
		if (depth < 1.0)
		{
			uint drawCallIndex, primID;
			DecodeVisibility(texVis[pos], drawCallIndex, primID);
			DrawCallData dc = rDrawCallData[drawCallIndex];
			MeshletData ml = rMeshletData[dc.meshletIndex];
			SubmeshData sm = rSubmeshData[ml.submeshIndex];
			uint index = sm.materialIndex / 32;
			uint bit = sm.materialIndex % 32;
			uint orig;
			InterlockedOr(shMaterialFlag[index], 0x1 << bit, orig);
		}
	}
}

[numthreads(CLASSIFY_THREAD_WIDTH, CLASSIFY_THREAD_WIDTH, 1)]
void main(
	uint3 gid : SV_GroupID,
	uint3 gtid : SV_GroupThreadID,
	uint3 did : SV_DispatchThreadID)
{
	const uint kMatChunkIndex = gtid.y * CLASSIFY_THREAD_WIDTH + gtid.x;
	shMaterialFlag[kMatChunkIndex] = 0x0;
	
	// sync
	GroupMemoryBarrierWithGroupSync();

	// classify material.
	uint2 basePos = gid.xy * CLASSIFY_TILE_WIDTH + gtid.xy;
	for (uint x = 0; x < 4; x++)
	{
		for (uint y = 0; y < 4; y++)
		{
			ClassifyPixel(basePos + uint2(x, y) * CLASSIFY_THREAD_WIDTH);
		}
	}

	// sync
	GroupMemoryBarrierWithGroupSync();

	// compute tile draw args.
	if (shMaterialFlag[kMatChunkIndex] != 0)
	{
		const uint kMatBaseIndex = kMatChunkIndex * 32;
		uint bits = shMaterialFlag[kMatChunkIndex];
		while (bits != 0)
		{
			uint firstBit = firstbitlow(bits);
			uint matIndex = kMatBaseIndex + firstBit;
			bits &= ~(0x1 << firstBit);

			uint arg_addr = matIndex * 16;
			uint store_addr = 0;
			rwDrawArg.InterlockedAdd(arg_addr + 4, 1, store_addr);

			uint tile_no = gid.y * cbTile.numX + gid.x;
			store_addr = ((matIndex * cbTile.tileMax) + store_addr) * 4;
			rwTileIndex.Store(store_addr, tile_no);
		}
	}
}