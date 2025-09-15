#include "cbuffer.hlsli"
#include "math.hlsli"
#include "visibility_buffer.hlsli"
#include "mesh_shader.hlsli"

#ifndef MATERIAL_TYPE
#	define MATERIAL_TYPE 0 // 0: Opacity, 1: Masked
#endif

#if MATERIAL_TYPE == 0
#	define VSOutput VSOpacityOutput
#elif MATERIAL_TYPE == 1
#	define VSOutput VSMaskedOutput
#else
#	error "Invalid material type"
#endif

struct PSOutput
{
	uint	visibility	: SV_Target0;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);
#if MATERIAL_TYPE == 1
Texture2D						texBaseColor	: register(t0);
SamplerState					samLinearWrap	: register(s0);
#endif

#if MATERIAL_TYPE == 0
[earlydepthstencil]
#endif
PSOutput main(VSOutput In, uint primID : SV_PrimitiveID)
{
	PSOutput Out = (PSOutput)0;

#if MATERIAL_TYPE == 1
	float opacity = texBaseColor.Sample(samLinearWrap, In.texcoord).a;
	if (opacity < 0.333)
	{
		discard;
	}
#endif
	
	Out.visibility = EncodeVisibility(In.meshletIndex, primID); 

	return Out;
}
