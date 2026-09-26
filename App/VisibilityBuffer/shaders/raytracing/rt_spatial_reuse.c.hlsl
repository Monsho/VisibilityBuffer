#include "common.hlsli"
#include "math.hlsli"
#include "../cbuffer.hlsli"
#include "restir.hlsli"

ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<RestirCB>			cbRestir		: REG(b1);

Texture2D<float4>					texGBufferC		: REG(t0);
Texture2D<float>					texDepth		: REG(t1);
StructuredBuffer<Reservoir>			inputReservoirs	: REG(t2);

RWStructuredBuffer<Reservoir>		outputReservoirs	: REG(u0);

// Separate random dimensions for the disk offset and reservoir selection.
float2 MapToDisk(uint seed, float radius)
{
	float phi = 2.0 * PI * Hash(seed);
	// Preserve the previous maximum offset (half of spatialRadius).
	float r = 0.5 * radius * sqrt(Hash(seed + 1u));
	return r * float2(cos(phi), sin(phi));
}

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
	float cVD = ClipDepthToViewDepthRH(depth, cbScene.mtxViewToProj);
	Reservoir center = inputReservoirs[pixelIndex];

	if (MATH_VERIFY_MODE)
	{
		outputReservoirs[pixelIndex] = center;
		return;
	}
	if (cbRestir.spatialSampleCount == 0 || depth <= 0.0 || !IsReservoirValid(center))
	{
		outputReservoirs[pixelIndex] = center;
		return;
	}

	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);

	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	Reservoir merged = ReservoirEmpty();
	uint spatialSeed = pixelIndex * 0x85ebca6bu + cbScene.frameIndex * 0x9e3779b9u;

	float3 dirL = normalize(center.samplePosition - worldPos.xyz);
	float selectedPdf = ReservoirGetGIPdf(center.sampleRadiance, max(dot(normal, dirL), 0.0));
	ReservoirCombine(merged, center, selectedPdf, 0.5);

	[loop]
	for (int i = 0; i < cbRestir.spatialSampleCount; ++i)
	{
		uint candidateSeed = spatialSeed + uint(i) * 3u;
		int2 pixelOffset = int2(MapToDisk(candidateSeed, cbRestir.spatialRadius));
		int2 npos = (int2)pixelPos + pixelOffset;
		[branch]
		if (any(npos < 0) || any((uint2)npos >= dim))
			continue;

		float nDepth = texDepth[npos];
		float nVD = ClipDepthToViewDepthRH(nDepth, cbScene.mtxViewToProj);
		float3 nNormal = normalize(texGBufferC[npos].xyz * 2.0 - 1.0);
		bool IsDepthValid = (nDepth > 0.0) && (abs(nVD - cVD) <= cbRestir.spatialDepthEps);
		bool IsNormalValid = dot(nNormal, normal) >= cbRestir.spatialNormalCos;
		[branch]
		if (!IsDepthValid || !IsNormalValid)
			continue;

		uint nIndex = (uint)npos.x + (uint)npos.y * dim.x;
		Reservoir nRes = inputReservoirs[nIndex];
		[branch]
		if (!IsReservoirValid(nRes))
			continue;

		float3 nWorldPos = GetWorldPos(npos, nDepth, cbScene.screenSize, cbScene.mtxProjToWorld);
		float Jacobian = 1.0;
		if (cbRestir.computeJacobian)
		{
			Jacobian = ComputeJacobian(worldPos, nWorldPos, nRes.samplePosition, nRes.sampleNormal);
			if (!IsValidateJacobian(Jacobian))
				continue;
		}

		float3 dirN = normalize(nRes.samplePosition - worldPos.xyz);
		float targetPdfN = ReservoirGetGIPdf(nRes.sampleRadiance, max(dot(normal, dirN), 0.0));

		float rnd = Hash(candidateSeed + 2u);
		bool IsNSelection = ReservoirCombine(merged, nRes, targetPdfN * Jacobian, rnd);
		if (IsNSelection)
		{
			selectedPdf = targetPdfN;
		}
	}

	float normalizeN = 1.0;
	float normalizeD = merged.M * selectedPdf;
	ReservoirFinalizeResampling(merged, normalizeN, normalizeD);

	outputReservoirs[pixelIndex] = merged;
}

// EOF
