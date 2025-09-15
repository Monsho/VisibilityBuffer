#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD0;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);

Texture2D			texColor		: register(t0);
SamplerState		samLinearWrap	: register(s0);

void main(PSInput In)
{
	float4 baseColor = texColor.Sample(samLinearWrap, In.uv);
	if (baseColor.a < 0.333)
	{
		discard;
	}
}
