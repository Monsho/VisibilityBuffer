#include "cbuffer.hlsli"

struct VSOutput
{
	float4	position	: SV_POSITION;
};

ConstantBuffer<SceneCB>			cbScene		: register(b0);
ConstantBuffer<TileCB>			cbTile		: register(b1);
ConstantBuffer<MaterialTileCB>	cbMatTile	: register(b2);

ByteAddressBuffer				rTileIndex	: register(t0);

VSOutput main(uint instanceID : SV_InstanceID, uint vertexID : SV_VertexID)
{
	VSOutput Out = (VSOutput)0;

	uint addr = cbMatTile.materialIndex * cbTile.tileMax + instanceID;
	uint tileIndex = rTileIndex.Load(addr * 4);
	uint tileX = tileIndex % cbTile.numX;
	uint tileY = tileIndex / cbTile.numX;
	tileX += (vertexID == 2 || vertexID == 3 || vertexID == 5) ? 1 : 0;
	tileY += (vertexID == 1 || vertexID == 4 || vertexID == 5) ? 1 : 0;
	float2 pos = float2(tileX * CLASSIFY_TILE_WIDTH, tileY * CLASSIFY_TILE_WIDTH);
	float2 screenPos = pos / cbScene.screenSize;
	float2 clipPos = screenPos * float2(2, -2) - float2(1, -1);

	float depth = (float)cbMatTile.materialIndex / (float)CLASSIFY_DEPTH_RANGE;

	Out.position = float4(clipPos, depth, 1);

	return Out;
}

//	EOF
