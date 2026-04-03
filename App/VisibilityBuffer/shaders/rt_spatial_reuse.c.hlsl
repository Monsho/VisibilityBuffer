#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"
#include "restir.hlsli"

ConstantBuffer<SceneCB>				cbScene			: REG(b0);
Texture2D<float4>					texGBufferC		: REG(t0);
Texture2D<float>					texDepth		: REG(t1);
StructuredBuffer<Reservoir>			inputReservoirs	: REG(t2);
RWStructuredBuffer<Reservoir>		outputReservoirs	: REG(u0);

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

	if (depth <= 0.0 || !IsReservoirValid(center))
	{
		outputReservoirs[pixelIndex] = center;
		return;
	}

	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);

	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	Reservoir merged = (Reservoir)0;
	float rnd = Hash(pixelIndex * 13 + cbScene.frameIndex * 31u + 7u);

	float3 dirL = normalize(center.samplePosition - worldPos.xyz);
	float selectedPdf = ReservoirGetGIPdf(center.sampleRadiance, max(dot(normal, dirL), 0.0));
	ReservoirCombine(merged, center, selectedPdf, 0.5);

	[loop]
	for (int i = 0; i < 8; ++i)
	{
		int2 npos = (int2)pixelPos + kSpatialOffsets[i];
		[branch]
		if (any(npos < 0) || any((uint2)npos >= dim))
			continue;

		float nDepth = texDepth[npos];
		float3 nNormal = normalize(texGBufferC[npos].xyz * 2.0 - 1.0);
		bool IsDepthValid = abs(nDepth - depth) <= SPATIAL_DEPTH_EPS;
		bool IsNormalValid = dot(nNormal, normal) >= SPATIAL_NORMAL_COS;
		[branch]
		if (!IsDepthValid || !IsNormalValid)
			continue;

		uint nIndex = (uint)npos.x + (uint)npos.y * dim.x;
		Reservoir nRes = inputReservoirs[nIndex];
		[branch]
		if (!IsReservoirValid(nRes))
			continue;

		float3 nWorldPos = GetWorldPos(npos, nDepth, cbScene.screenSize, cbScene.mtxProjToWorld);
		float Jacobian = ComputeJacobian(worldPos, nWorldPos, nRes.samplePosition, nRes.sampleNormal);
		if (!IsValidateJacobian(Jacobian))
			continue;

		float3 dirN = normalize(nRes.samplePosition - worldPos.xyz);
		float targetPdfN = ReservoirGetGIPdf(nRes.sampleRadiance, max(dot(normal, dirN), 0.0));

		bool IsNSelection = ReservoirCombine(merged, nRes, targetPdfN * Jacobian, rnd);
		if (IsNSelection)
		{
			selectedPdf = targetPdfN;
		}
	}

	float normalizeN = 1.0;
	float normalizeD = merged.M * selectedPdf;
	ReservoirFinalizeResampling(merged, normalizeN, normalizeD);
	if (IsReservoirValid(merged))
	{
		merged = center;
	}

	outputReservoirs[pixelIndex] = merged;
}

// EOF
