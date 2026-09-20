#ifndef MESH_SHADER_HLSLI
#define MESH_SHADER_HLSLI

#define LANE_COUNT_IN_WAVE 32

#define TRIANGLE_BACKFACE_CULLING 0

struct Payload
{
	uint MeshletIndices[LANE_COUNT_IN_WAVE];
};

struct VSOpacityOutput
{
	float4	position	: SV_POSITION;
	nointerpolation uint meshletIndex	: MESHLET_INDEX;
};

struct VSMaskedOutput
{
	float4	position	: SV_POSITION;
	float2	texcoord	: TEXCOORD0;
	nointerpolation uint meshletIndex	: MESHLET_INDEX;
};

struct PrimAttr
{
	uint	primitiveID : SV_PrimitiveID;
#if TRIANGLE_BACKFACE_CULLING
	bool	isCull : SV_CullPrimitive;
#endif
};

uint3 UnpackPrimitiveIndex(uint index)
{
	return uint3(
		(index >> 0) & 0x3ff,
		(index >> 10) & 0x3ff,
		(index >> 20) & 0x3ff);
}

#endif // MESH_SHADER_HLSLI
