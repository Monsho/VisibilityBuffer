#include "cbuffer.hlsli"

struct VSInput
{
	float3	position	: POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD;
};

struct VSOutput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD;
	float3	worldPos	: WORLDPOS;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<MeshCB>		cbMesh			: register(b1);

VSOutput main(const VSInput In)
{
	VSOutput Out = (VSOutput)0;

	float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform));

	Out.position = mul(mtxLocalToProj, float4(In.position, 1));
	Out.normal = normalize(mul((float3x3)cbMesh.mtxLocalToWorld, In.normal));
	Out.tangent.xyz = normalize(mul((float3x3)cbMesh.mtxLocalToWorld, In.tangent.xyz));
	Out.tangent.w = In.tangent.w;
	Out.uv = In.uv;
	Out.worldPos = mul(cbMesh.mtxLocalToWorld, float4(In.position, 1)).xyz;

	return Out;
}

//	EOF
