#ifndef RESTIR_HLSLI
#define RESTIR_HLSLI

struct Reservoir
{
	float3	sampleRadiance;
	float	weightSum;
	float3	samplePosition;
	uint	M;
	float3	sampleNormal;
	uint	age;
};

static const float kEpsPdf = 1e-6;
static const uint  kMaxReservoirM = 8;
static const uint  kMaxReservoirAge = 30;

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

bool IsReservoirValid(in Reservoir r)
{
	return r.M > 0;
}

Reservoir ReservoirEmpty()
{
	Reservoir r = (Reservoir)0;
	return r;
}

void ReservoirMake(
	inout Reservoir r,
	float3 sampleRadiance,
	float3 samplePosition,
	float3 sampleNormal,
	float targetPdf)
{
	r.sampleRadiance = sampleRadiance;
	r.samplePosition = samplePosition;
	r.sampleNormal = sampleNormal;
	r.weightSum = targetPdf > 0.0 ? 1.0 / targetPdf : 0.0;
	r.M = 1;
	r.age = 0;
}

bool ReservoirCombine(
	inout Reservoir r,
	const Reservoir newR,
	float targetPdf,
	float rnd)
{
	float risWeight = targetPdf * newR.weightSum * newR.M;

	r.M += newR.M;
	r.weightSum += risWeight;

	bool bSelectSample = (rnd * r.weightSum <= risWeight);

	[branch]
	if (bSelectSample)
	{
		r.samplePosition = newR.samplePosition;
		r.sampleNormal = newR.sampleNormal;
		r.sampleRadiance = newR.sampleRadiance;
		r.age = newR.age;
	}

	return bSelectSample;
}

float ReservoirGetGIPdf(float3 Radiance, float NoL)
{
	float3 ReflectedRadiance = Radiance * NoL;
	float Luminance = dot(ReflectedRadiance, float3(0.2126f, 0.7152f, 0.0722f)) * (1.0 /  PI);
	return Luminance;
}

void ReservoirFinalizeResampling(inout Reservoir r, float normalizeN, float normalizeD)
{
	r.weightSum = (normalizeD == 0.0) ? 0.0 : (r.weightSum * normalizeN) / normalizeD;
}

#endif // RESTIR_HLSLI
