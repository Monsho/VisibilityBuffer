#ifndef MESH_SHADER_HLSLI
#define MESH_SHADER_HLSLI

#define LANE_COUNT_IN_WAVE 32

struct Payload
{
	uint MeshletIndices[LANE_COUNT_IN_WAVE];
};

struct VSOutput
{
	float4	position	: SV_POSITION;
	nointerpolation uint meshletIndex	: MESHLET_INDEX;
};

struct PrimAttr
{
	uint	primitiveID : SV_PrimitiveID;
};

uint3 UnpackPrimitiveIndex(uint index)
{
	return uint3(
		(index >> 0) & 0x3ff,
		(index >> 10) & 0x3ff,
		(index >> 20) & 0x3ff);
}

#endif // MESH_SHADER_HLSLI
