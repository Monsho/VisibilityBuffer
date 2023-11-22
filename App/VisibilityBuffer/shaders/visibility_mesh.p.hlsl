#include "cbuffer.hlsli"
#include "math.hlsli"
#include "visibility_buffer.hlsli"
#include "mesh_shader.hlsli"

struct PSOutput
{
	uint	visibility	: SV_Target0;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);

PSOutput main(VSOutput In, uint primID : SV_PrimitiveID)
{
	PSOutput Out = (PSOutput)0;

	Out.visibility = EncodeVisibility(In.meshletIndex, primID); 

	return Out;
}
