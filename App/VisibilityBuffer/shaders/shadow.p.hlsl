#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
};

ConstantBuffer<ShadowCB>	cbShadow		: register(b0);
Texture2D<float>			texShadowDepth	: register(t0);

float4 main(PSInput In)	: SV_TARGET0
{
	float depth = 1.0 - texShadowDepth[In.position.xy];

	float p = exp(cbShadow.exponent.x * depth);
	float n = -exp(-cbShadow.exponent.y * depth);

	return float4(p, p * p, n, n * n);
}
