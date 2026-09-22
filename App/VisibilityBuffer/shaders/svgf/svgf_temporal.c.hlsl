#include "common.hlsli"
#include "math.hlsli"
#include "../cbuffer.hlsli"

ConstantBuffer<SceneCB>			cbScene		: REG(b0);
ConstantBuffer<SvgfCB>			cbSvgf		: REG(b1);
ConstantBuffer<SvgfHistoryCB>	cbHistory	: REG(b2);

Texture2D<float>		texDepth		: REG(t0);
Texture2D<float>		texPrevDepth	: REG(t1);
Texture2D<float4>		texGBufferC		: REG(t2);
Texture2D<float4>		texPrevGBufferC	: REG(t3);
Texture2D<float3>		texGI			: REG(t4);
Texture2D<float3>		texPrevGI		: REG(t5);
Texture2D<float4>		texPrevMoments	: REG(t6);	// xy: temporal luminance moments, z: history length, w: filtering variance.

RWTexture2D<float3>		rwTemporalGI	: REG(u0);
RWTexture2D<float4>		rwMoments		: REG(u1);

SamplerState	samLinearClamp		: REG(s0);


float Luma(float3 c)
{
	return dot(c, float3(0.299, 0.587, 0.114));
}

float EstimateSpatialVariance(uint2 pixel, uint2 dim, float depth, float3 normal, float centerLuma)
{
	float centerVD = ClipDepthToViewDepthRH(depth, cbScene.mtxViewToProj);
	float sumW = 0.0;
	float sumDelta = 0.0;
	float sumDelta2 = 0.0;
	// Work on the firefly-suppressed signal. Centered differences avoid subtracting
	// large, nearly equal raw moments for bright, almost constant neighborhoods.
	[loop]
	for (int y = -3; y <= 3; ++y)
	{
		[loop]
		for (int x = -3; x <= 3; ++x)
		{
			int2 p = int2(pixel) + int2(x, y);
			if (any(p < 0) || any(p >= int2(dim)))
			{
				continue;
			}

			float neighborDepth = texDepth[p];
			if (neighborDepth <= 0.0)
			{
				continue;
			}

			float3 neighborNormal = normalize(texGBufferC[p].xyz * 2.0 - 1.0);
			float normalCos = saturate(dot(normal, neighborNormal));
			float vd = ClipDepthToViewDepthRH(neighborDepth, cbScene.mtxViewToProj);
			if (abs(vd - centerVD) >= cbSvgf.disocclusionDepth
				|| normalCos <= cbSvgf.disocclusionNormal)
			{
				continue;
			}

			float w = exp(-abs(vd - centerVD) * cbSvgf.phiDepth)
				* pow(normalCos, cbSvgf.phiNormal);
			float delta = Luma(texGI[p]) - centerLuma;
			sumW += w;
			sumDelta += w * delta;
			sumDelta2 += w * delta * delta;
		}
	}
	float invW = rcp(max(sumW, 1e-6));
	float meanDelta = sumDelta * invW;
	return max(0.0, sumDelta2 * invW - meanDelta * meanDelta);
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 pixPos = did.xy;
	uint2 dim;
	rwTemporalGI.GetDimensions(dim.x, dim.y);
	if (any(pixPos >= dim)) return;

	float depth = texDepth[pixPos];
	float3 currGI = texGI[pixPos];
	if (depth <= 0.0)
	{
		rwTemporalGI[pixPos] = currGI;
		rwMoments[pixPos] = 0.0;
		return;
	}
	float3 normal = normalize(texGBufferC[pixPos].xyz * 2.0 - 1.0);
	float luma = Luma(currGI);
	float2 moments = float2(luma, luma * luma);
	float historyLength = 1.0;
	float3 temporalGI = currGI;

	float2 uv = (float2(pixPos) + 0.5) * cbScene.invScreenSize;
	float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
	float4 prevClipPos = mul(cbScene.mtxProjToPrevProj, clipPos);
	// Do not read fallback history descriptors on initialization or resize.
	if (cbHistory.valid && !cbSvgf.resetHistory && prevClipPos.w > 0.0)
	{
		prevClipPos.xyz /= prevClipPos.w;
		float2 prevUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;
		if (all(prevUV >= 0.0) && all(prevUV < 1.0))
		{
			float prevDepth = texPrevDepth.SampleLevel(samLinearClamp, prevUV, 0);
			float prevVD = ClipDepthToViewDepthRH(prevDepth, cbScene.mtxPrevViewToProj);
			float currVD = ClipDepthToViewDepthRH(prevClipPos.z, cbScene.mtxPrevViewToProj);
			uint2 prevPix = min(uint2(prevUV * float2(dim)), dim - 1);
			float3 prevNormal = normalize(texPrevGBufferC[prevPix].xyz * 2.0 - 1.0);
			// Discrete history length uses the same sample as the existing normal test.
			float prevLength = texPrevMoments[prevPix].z;
			bool validHistory = prevDepth > 0.0 && prevLength >= 1.0
				&& abs(prevVD - currVD) < cbSvgf.disocclusionDepth
				&& dot(normal, prevNormal) > cbSvgf.disocclusionNormal;
			if (validHistory)
			{
				historyLength = min(prevLength + 1.0, 32.0);
				float historyWeight = 1.0 - rcp(historyLength);
				float temporalBlend = min(saturate(cbSvgf.temporalBlend), historyWeight);
				float momentBlend = min(saturate(cbSvgf.momentBlend), historyWeight);
				temporalGI = lerp(currGI, texPrevGI.SampleLevel(samLinearClamp, prevUV, 0), temporalBlend);
				moments = lerp(moments, texPrevMoments.SampleLevel(samLinearClamp, prevUV, 0).xy, momentBlend);
			}
		}
	}

	float variance = max(0.0, moments.y - moments.x * moments.x);
	if (historyLength < 4.0 || cbSvgf.momentBlend <= 0.0)
	{
		float spatialVariance = EstimateSpatialVariance(pixPos, dim, depth, normal, luma);
		// Boost filtering while too few temporal observations are available.
		float varianceHistory = cbSvgf.momentBlend <= 0.0 ? 1.0 : historyLength;
		variance = max(variance, spatialVariance * (4.0 / varianceHistory));
	}
	rwTemporalGI[pixPos] = temporalGI;
	// Keep spatial variance out of the temporal moments fed to the next frame.
	rwMoments[pixPos] = float4(moments, historyLength, variance);
}
