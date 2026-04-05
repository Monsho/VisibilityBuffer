#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"

ConstantBuffer<SceneCB>		cbScene		: REG(b0);
ConstantBuffer<AmbOccCB>	cbAO		: REG(b1);

Texture2D<float>			texDepth	: REG(t0);
Texture2D<float>			texPrevDepth: REG(t1);
Texture2D<float3>			texGI		: REG(t2);
Texture2D<float3>			texPrevGI	: REG(t3);

SamplerState				samLinearClamp	: REG(s0);

RWTexture2D<float3>			rwGI		: REG(u0);

float ClipDepthToViewDepth(float D, float4x4 mtxViewToClip)
{
	return (D * mtxViewToClip[3][3] - mtxViewToClip[2][3]) / (mtxViewToClip[2][2] - D * mtxViewToClip[3][2]);
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 pixPos = did.xy;
	rwGI[pixPos] = texGI[pixPos];
}
