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

	r.weightSum += w;
	r.M += m;
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

	r.ucw = r.weightSum / ((float)r.M * max(r.targetPdf, kEpsPdf));
}

#endif // RESTIR_HLSLI
