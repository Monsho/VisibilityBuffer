#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD;
};

ConstantBuffer<BlurCB>	cbBlur			: register(b0);
Texture2D				texColor		: register(t0);
SamplerState			samLinearClamp	: register(s0);


float4 main(PSInput In)	: SV_TARGET0
{
	const float kKernels[4] = {cbBlur.kernel0.y, cbBlur.kernel0.z, cbBlur.kernel0.w, cbBlur.kernel1.x};
	float4 ret = texColor.SampleLevel(samLinearClamp, In.uv, 0) * cbBlur.kernel0.x;
	for (int i = 1; i <= 4; i++)
	{
		ret += texColor.SampleLevel(samLinearClamp, In.uv + (float)i * cbBlur.offset, 0) * kKernels[i - 1];
		ret += texColor.SampleLevel(samLinearClamp, In.uv - (float)i * cbBlur.offset, 0) * kKernels[i - 1];
	}
	return ret;
}
