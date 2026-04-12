#include "common.hlsli"
#include "math.hlsli"
#include "cbuffer.hlsli"
#include "restir.hlsli"

ConstantBuffer<SceneCB>				cbScene			: REG(b0);
ConstantBuffer<RestirCB>			cbRestir		: REG(b1);

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

float Halton(int i, int b)
{
	float f = 1.0;
	float r = 0.0;
	while (i > 0)
	{
		f = f / float(b);
		r = r + f * float(i % b);
		i = i / b;
	}
	return r;
}

// Halton<2, 3> 16
float2 Jitter(uint2 fragCoord, uint frame)
{
	int num = 8;
	return (float2(
		Halton(frame % num + int(fragCoord.x) % num + 1, 2),
		Halton(frame % num + int(fragCoord.y) % num + 1, 3)) - 0.5);
}

float2 MapToDisk(uint2 fragCoord, uint frame, float radius)
{
	float2 uv = Jitter(fragCoord, frame);

	if (uv.x == 0.0f && uv.y == 0.0f)
	{
		return float2(0, 0);
	}

	float phi, r;
	if (abs(uv.x) > abs(uv.y))
	{
		r = uv.x;
		phi = (PI / 4.0f) * (uv.y / uv.x);
	}
	else
	{
		r = uv.y;
		phi = (PI / 2.0f) - (PI / 4.0f) * (uv.x / uv.y);
	}

	return r * radius * float2(cos(phi), sin(phi));
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

	const int numSamples = 8;
	[loop]
	for (int i = 0; i < numSamples; ++i)
	{
		//int2 pixelOffset = kSpatialOffsets[i];
		int2 pixelOffset = int2(MapToDisk(pixelPos, cbScene.frameIndex * numSamples * i, cbRestir.spatialRadius));
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
