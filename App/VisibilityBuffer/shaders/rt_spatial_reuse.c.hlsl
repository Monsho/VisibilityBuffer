#include "common.hlsli"
#include "cbuffer.hlsli"
#include "restir.hlsli"

ConstantBuffer<SceneCB>				cbScene			: REG(b0);
Texture2D<float4>					texGBufferC		: REG(t0);
Texture2D<float>					texDepth		: REG(t1);
StructuredBuffer<Reservoir>			inputReservoirs	: REG(t2);
RWStructuredBuffer<Reservoir>		outputReservoirs	: REG(u0);
RWTexture2D<float3>				rwGi			: REG(u1);

static const float SPATIAL_DEPTH_EPS = 0.02;
static const float SPATIAL_NORMAL_COS = 0.75;

static const int2 kSpatialOffsets[8] = {
	int2(-1, -1), int2(0, -1), int2(1, -1),
	int2(-1,  0),               int2(1,  0),
	int2(-1,  1), int2(0,  1), int2(1,  1),
};

[numthreads(8, 8, 1)]
void main(
	uint3 gid : SV_GroupID,
	uint3 gtid : SV_GroupThreadID,
	uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;
	uint2 dim = (uint2)cbScene.screenSize;
	if (any(pixelPos >= dim))
	{
		return;
	}

	uint pixelIndex = pixelPos.x + pixelPos.y * dim.x;
	float depth = texDepth[pixelPos];
	Reservoir center = inputReservoirs[pixelIndex];

	if (depth <= 0.0 || center.isValid == 0)
	{
		outputReservoirs[pixelIndex] = center;
		rwGi[pixelPos] = 0.0;
		return;
	}

	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);

	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	Reservoir merged = (Reservoir)0;
	float rnd = Hash(pixelIndex * 13 + 7);

	ReservoirUpdateCandidate(
		merged,
		center.sampleRadiance,
		center.samplePosition,
		center.sampleNormal,
		center.targetPdf,
		center.weightSum,
		center.M,
		rnd);

	[unroll]
	for (int i = 0; i < 8; ++i)
	{
		int2 npos = (int2)pixelPos + kSpatialOffsets[i];
		[branch]
		if (any(npos < 0) || any((uint2)npos >= dim))
			continue;

		float nDepth = texDepth[npos];
		[branch]
		if (abs(nDepth - depth) > SPATIAL_DEPTH_EPS)
			continue;

		uint nIndex = (uint)npos.x + (uint)npos.y * dim.x;
		Reservoir nRes = inputReservoirs[nIndex];
		[branch]
		if (nRes.isValid == 0)
			continue;

		[branch]
		if (dot(normalize(nRes.sampleNormal), normal) < SPATIAL_NORMAL_COS)
			continue;

		float3 dirN = normalize(nRes.samplePosition - worldPos.xyz);
		float targetPdfN = max(dot(normal, dirN), 0.0) * (1.0 / 3.14159265);
		float wN = nRes.weightSum * (targetPdfN / max(nRes.targetPdf, kEpsPdf));

		ReservoirUpdateCandidate(
			merged,
			nRes.sampleRadiance,
			nRes.samplePosition,
			nRes.sampleNormal,
			targetPdfN,
			wN,
			nRes.M,
			rnd);
	}

	ReservoirFinalize(merged);
	if (merged.isValid == 0)
	{
		merged = center;
	}

	outputReservoirs[pixelIndex] = merged;
	rwGi[pixelPos] = merged.sampleRadiance * merged.ucw;
}

// EOF
