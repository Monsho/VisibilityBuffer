#include "cbuffer.hlsli"

struct PSOutput
{
	float4	accum	: SV_TARGET0;
};

ConstantBuffer<WaterCB>	cbWater	: register(b1);

[earlydepthstencil]
PSOutput main()
{
	PSOutput Out = (PSOutput)0;
	Out.accum = cbWater.color;
	return Out;
}

//	EOF
