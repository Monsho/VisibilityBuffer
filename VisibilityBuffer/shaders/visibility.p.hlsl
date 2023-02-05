#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
};

struct PSOutput
{
	uint	visibility	: SV_Target0;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);
ConstantBuffer<VisibilityCB>	cbVisibility	: register(b1);

PSOutput main(uint primID : SV_PrimitiveID)
{
	PSOutput Out = (PSOutput)0;

	Out.visibility = ((cbVisibility.drawCallIndex & 0xffff) << 16) | (primID & 0xffff); 

	return Out;
}
