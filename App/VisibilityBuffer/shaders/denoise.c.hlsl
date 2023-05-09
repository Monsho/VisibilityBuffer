#include "cbuffer.hlsli"
#include "math.hlsli"

ConstantBuffer<SceneCB>		cbScene		: register(b0);
ConstantBuffer<AmbOccCB>	cbAO		: register(b1);

Texture2D<float>			texDepth	: register(t0);
Texture2D<float>			texAO		: register(t1);
Texture2D<float>			texPrevDepth: register(t2);
Texture2D<float>			texPrevAO	: register(t3);

SamplerState				samLinearClamp	: register(s0);

RWTexture2D<float4>			rwOutput	: register(u0);

float ClipDepthToViewDepth(float D, float4x4 mtxViewToClip)
{
	return (D * mtxViewToClip[3][3] - mtxViewToClip[2][3]) / (mtxViewToClip[2][2] - D * mtxViewToClip[3][2]);
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 pixPos = did.xy;
	float2 uv = (float2(pixPos) + 0.5) * cbScene.invScreenSize;

	// current depth.
	float depth = texDepth[pixPos];
	float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
	float VD = ClipDepthToViewDepth(depth, cbScene.mtxViewToProj);

	// spatio denoise.
	const int kMaxSpatioPixel = 5;
	float radiusInPixels = cbAO.denoiseRadius * cbAO.ndcPixelSize / -VD;
	int R = min(max(ceil(radiusInPixels), 1), kMaxSpatioPixel);
	float totalWeight = 1.0f;
	float ao = texAO[pixPos];
	for (int x = -R; x <= R; x++)
	{
		for (int y = -R; y <= R; y++)
		{
			if (x != 0 || y != 0)
			{
				int2 p = pixPos + int2(x, y);
				float nd = texDepth[p];
				float nVD = ClipDepthToViewDepth(nd, cbScene.mtxViewToProj);
				float w = 1.0 - smoothstep(1.0, 2.0, abs(VD - nVD));
				ao += texAO[p] * w;
				totalWeight += w;
			}
		}
	}
	ao = ao / totalWeight;

	// sample prev ao.
	float4 prevClipPos = mul(cbScene.mtxProjToPrevProj, clipPos);
	prevClipPos.xyz *= (1 / prevClipPos.w);
	float2 prevUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;
	float prevAO = texPrevAO.SampleLevel(samLinearClamp, prevUV, 0);

	// depth weight.
	float prevDepth = texPrevDepth.SampleLevel(samLinearClamp, prevUV, 0);
	float prevVD = ClipDepthToViewDepth(prevDepth, cbScene.mtxPrevViewToProj);
	float currVD = ClipDepthToViewDepth(prevClipPos.z, cbScene.mtxPrevViewToProj);
	float w = exp(-abs(prevVD - currVD) / (cbAO.denoiseDepthSigma + 1e-3));
	float depthWeight = w;

	// temporal denoise.
	float weight = cbAO.denoiseBaseWeight * depthWeight;
	rwOutput[pixPos] = lerp(ao, prevAO, weight);
}
