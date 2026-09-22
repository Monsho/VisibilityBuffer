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

// All history attributes are fetched from the same integer pixel and validated
// before interpolation. Length is the minimum among contributing samples.
struct HistoryGather
{
	float3 gi;
	float2 moments;
	float weight;
	float length;
};

HistoryGather EmptyHistoryGather()
{
	HistoryGather h = (HistoryGather)0;
	h.length = 32.0;
	return h;
}

void AccumulateHistory(
	int2 p, uint2 dim, float expectedVD, float3 normal,
	float weight, inout HistoryGather h)
{
	if (weight <= 0.0 || any(p < 0) || any(p >= int2(dim)))
	{
		// ピクセルが無効
		return;
	}

	float depth = texPrevDepth[p];
	if (depth <= 0.0 || !isfinite(depth))
	{
		// 深度が無効
		return;
	}

	float vd = ClipDepthToViewDepthRH(depth, cbScene.mtxPrevViewToProj);
	float3 prevNormal = normalize(texPrevGBufferC[p].xyz * 2.0 - 1.0);
	float4 history = texPrevMoments[p];
	if (!isfinite(vd) || !all(isfinite(prevNormal)) || !all(isfinite(history.xyz)))
	{
		return;
	}
	if (abs(vd - expectedVD) >= cbSvgf.disocclusionDepth
		|| dot(normal, prevNormal) <= cbSvgf.disocclusionNormal
		|| history.z < 1.0)
	{
		return;
	}

	float3 gi = texPrevGI[p];
	if (!all(isfinite(gi)))
	{
		return;
	}

	h.gi += gi * weight;
	h.moments += history.xy * weight;
	h.weight += weight;
	h.length = min(h.length, history.z);
}

HistoryGather GatherHistory(float2 prevUV, uint2 dim, float expectedVD, float3 normal)
{
	// 2x2quadのヒストリー重みを求める
	float2 p = prevUV * float2(dim) - 0.5;
	int2 base = int2(floor(p));
	float2 f = frac(p);
	HistoryGather h = EmptyHistoryGather();
	[unroll]
	for (int y = 0; y < 2; ++y)
	{
		[unroll]
		for (int x = 0; x < 2; ++x)
		{
			float weight = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
			AccumulateHistory(base + int2(x, y), dim, expectedVD, normal, weight, h);
		}
	}

	// 適切なヒストリーが見つからなかった場合、3x3近傍を探索してヒストリー重みを計算する
	[branch]
	if (h.weight <= 1e-4)
	{
		h = EmptyHistoryGather();
		int2 center = int2(floor(p + 0.5));
		[unroll]
		for (int y = -1; y <= 1; ++y)
		{
			[unroll]
			for (int x = -1; x <= 1; ++x)
			{
				int2 candidate = center + int2(x, y);
				float2 offset = float2(candidate) - p;
				float weight = rcp(1.0 + dot(offset, offset));
				AccumulateHistory(candidate, dim, expectedVD, normal, weight, h);
			}
		}
	}
	if (h.weight > 1e-4)
	{
		h.gi /= h.weight;
		h.moments /= h.weight;
	}
	return h;
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
			float currVD = ClipDepthToViewDepthRH(prevClipPos.z, cbScene.mtxPrevViewToProj);
			HistoryGather history = GatherHistory(prevUV, dim, currVD, normal);
			if (history.weight > 1e-4)
			{
				historyLength = min(history.length + 1.0, 32.0);
				float historyWeight = 1.0 - rcp(historyLength);
				float temporalBlend = min(saturate(cbSvgf.temporalBlend), historyWeight);
				float momentBlend = min(saturate(cbSvgf.momentBlend), historyWeight);
				temporalGI = lerp(currGI, history.gi, temporalBlend);
				moments = lerp(moments, history.moments, momentBlend);
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
