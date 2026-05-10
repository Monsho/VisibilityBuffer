#include "cbuffer.hlsli"

struct VSOutput
{
	float4	position	: SV_POSITION;
	float3	worldPos	: WORLDPOS;
};

ConstantBuffer<SceneCB>		cbScene	: register(b0);
ConstantBuffer<WaterCB>	cbWater	: register(b1);

VSOutput main(uint vertexID : SV_VertexID)
{
	VSOutput Out = (VSOutput)0;

	float2 corners[6] = {
		float2(0.0, 0.0),
		float2(0.0, 1.0),
		float2(1.0, 0.0),
		float2(1.0, 0.0),
		float2(0.0, 1.0),
		float2(1.0, 1.0),
	};

	float2 xz = lerp(cbWater.aabbMinHeight.xz, cbWater.aabbMax.xz, corners[vertexID]);
	float3 worldPos = float3(xz.x, cbWater.aabbMinHeight.w, xz.y);
	Out.position = mul(cbScene.mtxWorldToProj, float4(worldPos, 1.0));
	Out.worldPos = worldPos;

	return Out;
}

//	EOF
