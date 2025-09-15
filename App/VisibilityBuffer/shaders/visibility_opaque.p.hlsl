#include "cbuffer.hlsli"
#include "math.hlsli"
#include "visibility_buffer.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
};

struct PSOutput
{
	uint	visibility	: SV_Target0;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<VisibilityCB>	cbVisibility	: register(b0, space1);

[earlydepthstencil]
PSOutput main(uint primID : SV_PrimitiveID)
{
	PSOutput Out = (PSOutput)0;

	Out.visibility = EncodeVisibility(cbVisibility.drawCallIndex, primID); 

	return Out;
}
