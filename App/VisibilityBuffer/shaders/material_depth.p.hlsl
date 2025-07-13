#include "constant_defs.h"
#include "cbuffer.hlsli"
#include "math.hlsli"
#include "visibility_buffer.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD;
};

struct PSOutput
{
	float	depth	: SV_DEPTH;
};

Texture2D<uint>					texVis				: register(t0);
StructuredBuffer<SubmeshData>	rSubmeshData		: register(t1);
StructuredBuffer<MeshletData>	rMeshletData		: register(t2);
StructuredBuffer<DrawCallData>	rDrawCallData		: register(t3);
Texture2D<float>				texDepth			: register(t4);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	uint2 pixelPos = uint2(In.position.xy);
	float depth = texDepth[pixelPos];
	[branch]
	if (depth <= 0.0)
	{
		Out.depth = 1.0;
	}
	else
	{
		uint drawCallIndex, primID;
		DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
		DrawCallData dc = rDrawCallData[drawCallIndex];
		MeshletData ml = rMeshletData[dc.meshletIndex];
		SubmeshData sm = rSubmeshData[ml.submeshIndex];
		Out.depth = (float)sm.materialIndex / (float)CLASSIFY_DEPTH_RANGE;
	}

	return Out;
}
