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

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 pixPos = did.xy;
	float2 uv = (float2(pixPos) + 0.5) * cbScene.invScreenSize;

	// current data.
	float depth = texDepth[pixPos];
	float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
	float VD = ClipDepthToViewDepthRH(depth, cbScene.mtxViewToProj);
	float3 gi = texGI[pixPos];

	// temporal sampling.
	float4 prevClipPos = mul(cbScene.mtxProjToPrevProj, clipPos);
	prevClipPos.xyz *= (1 / prevClipPos.w);
	float2 prevUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;
	[branch]
	if (any(prevUV < 0) || any(prevUV > 1))
	{
		rwGI[pixPos] = gi;
		return;
	}

	float3 prevGI = texPrevGI.SampleLevel(samLinearClamp, prevUV, 0);

	// depth weight.
	float prevDepth = texPrevDepth.SampleLevel(samLinearClamp, prevUV, 0);
	float prevVD = ClipDepthToViewDepthRH(prevDepth, cbScene.mtxPrevViewToProj);
	float currVD = ClipDepthToViewDepthRH(prevClipPos.z, cbScene.mtxPrevViewToProj);
	float w = exp(-abs(prevVD - currVD) / (cbAO.denoiseDepthSigma + 1e-3));
	float depthWeight = isfinite(w) ? w : 0;

	// temporal denoise.
	float weight = cbAO.denoiseBaseWeight * depthWeight;
	rwGI[pixPos] = lerp(gi, prevGI, weight);
}
