#include "cbuffer.hlsli"
#include "math.hlsli"

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
StructuredBuffer<DrawCallData>	rDrawCallData		: register(t2);
Texture2D<float>				texDepth			: register(t3);

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
		uint drawCallIndex = (texVis[pixelPos] >> 16) & 0xffff;
		DrawCallData dc = rDrawCallData[drawCallIndex];
		SubmeshData sm = rSubmeshData[dc.submeshIndex];
		Out.depth = (float)sm.materialIndex / (float)CLASSIFY_DEPTH_RANGE;
	}

	return Out;
}
