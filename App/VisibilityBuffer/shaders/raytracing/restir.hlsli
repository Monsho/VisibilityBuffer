#ifndef RESTIR_HLSLI
#define RESTIR_HLSLI

#define MATH_VERIFY_MODE 0	// 数式確認モード

struct Reservoir
{
	float3	sampleRadiance;
	float	weightSum;
	float3	samplePosition;
	uint	M;
	float3	sampleNormal;
	uint	age;
};

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

void ComputeJacobian_Partial(in float3 RecieverPos, in float3 SampledPos, in float3 SampledNormal,
	out float DistToSurfaceSqr, out float CosAngle)
{
	const float3 Vec = RecieverPos - SampledPos;

	DistToSurfaceSqr = dot(Vec, Vec);
	CosAngle = saturate(dot(SampledNormal, Vec * rsqrt(DistToSurfaceSqr)));
}

// Equation 11 from ReSTIR GI paper.
float ComputeJacobian(float3 basePos, float3 neighborPos, float3 sampledPos, float3 sampledNormal)
{
	float OrigDistSqr = 0.0, OrigCos = 0.0;
	float NewDistSqr = 0.0, NewCos = 0.0;
	ComputeJacobian_Partial(basePos, sampledPos, sampledNormal, NewDistSqr, NewCos);
	ComputeJacobian_Partial(neighborPos, sampledPos, sampledNormal, OrigDistSqr, OrigCos);

	float Jacobian = (NewCos * OrigDistSqr) / (OrigCos * NewDistSqr);

	if (isinf(Jacobian) || isnan(Jacobian))
		Jacobian = 0;

	return Jacobian;
}

bool IsValidateJacobian(inout float jacobian)
{
	if (jacobian > 10.0 || jacobian < 1 / 10.0)
	{
		return false;
	}

	jacobian = clamp(jacobian, 1 / 1.5, 1.5);

	return true;
}

float3 GetWorldPos(uint2 pixelPos, float depth, float2 screenSize, float4x4 mtxProjToWorld)
{
	float2 screenPos = ((float2)pixelPos + 0.5) / screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(mtxProjToWorld, float4(clipSpacePos, depth, 1));
	return worldPos.xyz /= worldPos.w;
}

#endif // RESTIR_HLSLI
