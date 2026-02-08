#include "common.hlsli"
#include "rtxgi.hlsli"
#include "cbuffer.hlsli"

struct VSInput
{
	float3	position	: POSITION;
	float3	normal		: NORMAL;
};

struct VSOutput
{
	float4	position	: SV_POSITION;
	float3	worldPos	: WORLDPOS;
	float3	normal		: NORMAL;
};

ConstantBuffer<SceneCB>		cbScene			: REG(b0);
ConstantBuffer<MeshCB>		cbMesh			: REG(b1);

StructuredBuffer<DDGIVolumeDescGPUPacked>	DDGIVolumes		: REG(t0);
Texture2DArray<float4>						ProbeData		: REG(t1);

VSOutput main(const VSInput In, uint InstanceID : SV_InstanceID)
{
	VSOutput Out = (VSOutput)0;

	float4x4 mtxLocalToWorld = mul(cbMesh.mtxLocalToWorld, cbMesh.mtxBoxTransform);
	DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGIVolumes[0]);
	float3 probeCoords = DDGIGetProbeCoords(InstanceID, volume);
	int probeIndex = DDGIGetScrollingProbeIndex(probeCoords, volume);
	float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, ProbeData);

	float4 worldPos = mul(mtxLocalToWorld, float4(In.position, 1));
	worldPos.xyz += probeWorldPosition;
	Out.position = mul(cbScene.mtxWorldToProj, worldPos);
	Out.worldPos = worldPos;
	Out.normal = In.normal;

	return Out;
}

//	EOF
