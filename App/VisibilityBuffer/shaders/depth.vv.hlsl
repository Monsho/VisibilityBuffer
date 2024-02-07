#include "cbuffer.hlsli"

struct VSInput
{
	float3	position	: POSITION;
};

struct VSOutput
{
	float4	position	: SV_POSITION;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<MeshCB>		cbMesh			: register(b1);

VSOutput main(const VSInput In)
{
	VSOutput Out = (VSOutput)0;

	float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

	Out.position = mul(mtxLocalToProj, float4(In.position, 1));

	return Out;
}

//	EOF
