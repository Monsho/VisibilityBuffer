#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD;
};

struct PSOutput
{
	float4	color	: SV_TARGET0;
};

Texture2D			texAccum		: register(t0);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	Out.color = float4(pow(texAccum[uint2(In.position.xy)].rgb, 1/2.2), 1);

	return Out;
}
