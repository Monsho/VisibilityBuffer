#include "cbuffer.hlsli"

struct VSInput
{
	float3	position	: POSITION;
	float2	texcoord	: TEXCOORD0;
};

struct VSOutput
{
	float4	position	: SV_POSITION;
	float2	texcoord	: TEXCOORD0;
};

ConstantBuffer<ShadowCB>	cbShadow		: register(b0);
ConstantBuffer<MeshCB>		cbMesh			: register(b1);

VSOutput main(const VSInput In)
{
	VSOutput Out = (VSOutput)0;

	float4x4 mtxLocalToProj = mul(cbShadow.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

	Out.position = mul(mtxLocalToProj, float4(In.position, 1));
	Out.texcoord = In.texcoord;

	return Out;
}

//	EOF
