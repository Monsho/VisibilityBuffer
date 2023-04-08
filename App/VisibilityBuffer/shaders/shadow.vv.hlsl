#include "cbuffer.hlsli"

struct VSInput
{
	float3	position	: POSITION;
};

struct VSOutput
{
	float4	position	: SV_POSITION;
};

ConstantBuffer<ShadowCB>	cbShadow		: register(b0);
ConstantBuffer<MeshCB>		cbMesh			: register(b1);

VSOutput main(const VSInput In)
{
	VSOutput Out = (VSOutput)0;

	float4x4 mtxLocalToProj = mul(cbShadow.mtxWorldToProj, cbMesh.mtxLocalToWorld);

	Out.position = mul(mtxLocalToProj, float4(In.position, 1));

	return Out;
}

//	EOF
