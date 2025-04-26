#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD;
};

Texture2D				texDepth		: register(t0);
RWTexture2D<float>		rwDepth0		: register(u0);
RWTexture2D<float>		rwDepth1		: register(u1);

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 outXY1 = did.xy;
	uint2 outXY0 = outXY1 * 2;
	uint2 inXY = outXY0 * 2;
	float d0 = texDepth[inXY];
	d0 = min(d0, texDepth[inXY + uint2(1, 0)]);
	d0 = min(d0, texDepth[inXY + uint2(0, 1)]);
	d0 = min(d0, texDepth[inXY + uint2(1, 1)]);
	rwDepth0[outXY0] = d0;

	float d1 = texDepth[inXY + uint2(2, 0)];
	d1 = min(d1, texDepth[inXY + uint2(3, 0)]);
	d1 = min(d1, texDepth[inXY + uint2(0, 1)]);
	d1 = min(d1, texDepth[inXY + uint2(3, 1)]);
	rwDepth0[outXY0 + uint2(1, 0)] = d1;

	float d2 = texDepth[inXY + uint2(0, 2)];
	d2 = min(d2, texDepth[inXY + uint2(1, 0)]);
	d2 = min(d2, texDepth[inXY + uint2(0, 3)]);
	d2 = min(d2, texDepth[inXY + uint2(1, 3)]);
	rwDepth0[outXY0 + uint2(0, 1)] = d2;

	float d3 = texDepth[inXY + uint2(2, 2)];
	d3 = min(d3, texDepth[inXY + uint2(3, 0)]);
	d3 = min(d3, texDepth[inXY + uint2(0, 3)]);
	d3 = min(d3, texDepth[inXY + uint2(3, 3)]);
	rwDepth0[outXY0 + uint2(1, 1)] = d3;

	rwDepth1[outXY1] = min(d0, min(d1, min(d2, d3)));
}
