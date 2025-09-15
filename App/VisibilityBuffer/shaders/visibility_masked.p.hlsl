#include "cbuffer.hlsli"
#include "math.hlsli"
#include "visibility_buffer.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	texcoord	: TEXCOORD0;
};

struct PSOutput
{
	uint	visibility	: SV_Target0;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<VisibilityCB>	cbVisibility	: register(b0, space1);

Texture2D						texBaseColor	: register(t0);
SamplerState					samLinearWrap	: register(s0);

PSOutput main(PSInput In, uint primID : SV_PrimitiveID)
{
	PSOutput Out = (PSOutput)0;

	float opacity = texBaseColor.Sample(samLinearWrap, In.texcoord).a;
	if (opacity < 0.333)
	{
		discard;
	}

	Out.visibility = EncodeVisibility(cbVisibility.drawCallIndex, primID); 

	return Out;
}
