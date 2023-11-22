#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD;
};

Texture2D				texDepth		: register(t0);

float main(PSInput In)	: SV_TARGET0
{
	uint2 outXY = uint2(In.position.xy);
	uint2 inXY = outXY * 2;
	float d = texDepth[inXY];
	d = min(d, texDepth[inXY + uint2(1, 0)]);
	d = min(d, texDepth[inXY + uint2(0, 1)]);
	d = min(d, texDepth[inXY + uint2(1, 1)]);
	return d;
}
