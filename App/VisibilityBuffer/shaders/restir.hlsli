#ifndef RESTIR_HLSLI
#define RESTIR_HLSLI

struct Reservoir
{
	float3	sampleRadiance;
	float	weightSum;
	float3	samplePosition;
	float	targetPdf;
	float3	sampleNormal;
	float	ucw;
	uint	M;
	uint	isValid;
	uint	pad0;
	uint	pad1;
};

static const float kEpsPdf = 1e-6;
static const float kMaxReservoirWeightSum = 1e6;
static const uint  kMaxReservoirM = 64;

float Hash(uint v)
{
	v ^= 2747636419u;
	v *= 2654435769u;
	v ^= v >> 16;
	v *= 2654435769u;
	v ^= v >> 16;
	v *= 2654435769u;
	return (float)(v & 0x00ffffffu) * (1.0 / 16777216.0);
}

void ReservoirUpdateCandidate(
	inout Reservoir r,
	float3 sampleRadiance,
	float3 samplePosition,
	float3 sampleNormal,
	float targetPdf,
	float w,
	uint  m,
	float rnd)
{
	[branch]
	if (w <= 0.0 || m == 0 || targetPdf <= 0.0)
		return;

	if (!isfinite(w))
		return;

	r.weightSum = min(r.weightSum + w, kMaxReservoirWeightSum);
	r.M = min(r.M + m, kMaxReservoirM);
	[branch]
	if (rnd < (w / max(r.weightSum, kEpsPdf)))
	{
		r.sampleRadiance = sampleRadiance;
		r.samplePosition = samplePosition;
		r.sampleNormal = sampleNormal;
		r.targetPdf = targetPdf;
		r.isValid = 1;
	}
}

void ReservoirFinalize(inout Reservoir r)
{
	[branch]
	if (r.isValid == 0 || r.M == 0 || r.targetPdf <= 0.0)
	{
		r.ucw = 0.0;
		return;
	}

	float denom = (float)r.M * max(r.targetPdf, kEpsPdf);
	r.ucw = min(r.weightSum / max(denom, kEpsPdf), kMaxReservoirWeightSum);
	if (!isfinite(r.ucw))
	{
		r.ucw = 0.0;
		r.isValid = 0;
	}
}

#endif // RESTIR_HLSLI
