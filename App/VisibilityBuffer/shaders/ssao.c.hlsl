#include "cbuffer.hlsli"
#include "math.hlsli"

#ifndef ENABLE_VISIBILITY_BITMASK
#define ENABLE_VISIBILITY_BITMASK 1
#endif

ConstantBuffer<SceneCB>		cbScene		: register(b0);
ConstantBuffer<AmbOccCB>	cbAO		: register(b1);

Texture2D<float>			texDepth	: register(t0);
Texture2D<float4>			texGBufferC	: register(t1);

SamplerState				samLinearClamp	: register(s0);

RWTexture2D<float4>			rwOutput	: register(u0);

uint Hash32(uint x)
{
	x ^= x >> 16;
	x *= uint(0x7feb352d);
	x ^= x >> 15;
	x *= uint(0x846ca68b);
	x ^= x >> 16;
	return x;
}
float Hash32ToFloat(uint hash)
{ 
	return hash / 4294967296.0;
}
uint Hash32Combine(const uint seed, const uint value)
{
	return seed ^ (Hash32(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}
float2 SpatioTemporalNoise(uint2 pixPos, uint temporalIndex)
{
	uint baseHash = Hash32(pixPos.x + (pixPos.y << 15));
	baseHash = Hash32Combine(baseHash, temporalIndex);
	return float2(Hash32ToFloat(baseHash), Hash32ToFloat(Hash32(baseHash)));
}

float3 ScreenPosToViewPos(float2 uv, float depth)
{
	float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
	float4 viewPos = mul(cbScene.mtxProjToView, clipPos);
	return viewPos.xyz / viewPos.w;
}

float3 MinDiff(float3 P, float3 Pr, float3 Pl)
{
	float3 v0 = Pr - P;
	float3 v1 = P - Pl;
	return (dot(v0, v0) < dot(v1, v1)) ? v0 : v1;
}

void CalcNumSteps(out float2 stepSizeInUV, out float numSteps, float radiusInPixels, float rnd)
{
	numSteps = min(float(cbAO.stepCount), radiusInPixels);
	float stepSizeInPixels = radiusInPixels / (numSteps + 1.0);
	float maxNumSteps = cbAO.maxPixelRadius / stepSizeInPixels;
	if (maxNumSteps < numSteps)
	{
		numSteps = floor(maxNumSteps + rnd);
		numSteps = max(numSteps, 1);
		stepSizeInPixels = cbAO.maxPixelRadius / numSteps;
	}
	stepSizeInUV = stepSizeInPixels * cbScene.invScreenSize;
}

// HBAO
float Tangent(float3 T)
{
	return T.z * rsqrt(dot(T.xy, T.xy));
}

float BiasedTangent(float3 T)
{
	return T.z * rsqrt(dot(T.xy, T.xy)) + cbAO.tangentBias;
}

float TanToSin(float x) 
{
	return x * rsqrt(x * x + 1);
}

float FalloffFactor(float d2, float R2) 
{
	return 1 - d2 / R2;
}

float IntegrateOcclusion(float2 uv0, float2 duv, float3 viewP, float3 dPdu, float3 dPdv, inout float tanH)
{
	float ao = 0.0;

	float3 T = duv.x * dPdu + duv.y * dPdv;
	float tanT = BiasedTangent(T);
	float sinT = TanToSin(tanT);
	float3 S = ScreenPosToViewPos(uv0 + duv, texDepth.SampleLevel(samLinearClamp, uv0 + duv, 0));
	float3 diff = S - viewP;
	float tanS = Tangent(diff);
	float sinS = TanToSin(tanS);
	float d2 = dot(diff, diff);
	float R2 = cbAO.worldSpaceRadius * cbAO.worldSpaceRadius;
	if ((d2 < R2) && (tanS > tanT))
	{
		ao = FalloffFactor(d2, R2) * saturate(sinS - sinT);
		tanH = max(tanH, tanS);
	}

	return ao;
}

float CalcHorizonOcclusion(float2 deltaUV, float2 texelDeltaUV, float2 uv0, float3 viewP, float numSteps, float randStep, float3 dPdu, float3 dPdv)
{
	float ao = 0;

	float2 uv = uv0 + randStep * deltaUV;
	float3 T = deltaUV.x * dPdu + deltaUV.y * dPdv;
	float tanH = BiasedTangent(T);

	// first step
	float2 duv = randStep * deltaUV + texelDeltaUV;
	ao = IntegrateOcclusion(uv0, duv, viewP, dPdu, dPdv, tanH);
	--numSteps;

	float sinH = TanToSin(tanH);
	for (uint step = 1; step < cbAO.stepCount; step++)
	{
		if (float(step) >= numSteps)
		{
			break;
		}

		uv += deltaUV;
		float3 S = ScreenPosToViewPos(uv, texDepth.SampleLevel(samLinearClamp, uv, 0));
		float3 diff = S - viewP;
		float tanS = Tangent(diff);
		float d2 = dot(diff, diff);
		float R2 = cbAO.worldSpaceRadius * cbAO.worldSpaceRadius;
		if ((d2 < R2) && (tanS > tanH)) 
		{
			float sinS = TanToSin(tanS);
			ao += FalloffFactor(d2, R2) * saturate(sinS - sinH);

			tanH = tanS;
			sinH = sinS;
		}
	}

	return ao;
}

// Visibility Bitmask
float3 LoadViewNormal(uint2 pixPos)
{
	float3 worldNormal = texGBufferC[pixPos].xyz * 2.0 - 1.0;
	return mul((float3x3)cbScene.mtxWorldToView, worldNormal);
}

float CalcVisibilityBitmaskOcclusion(float2 deltaUV, float2 texelDeltaUV, float2 uv0, float3 viewP, float3 viewN, float numSteps, float randStep, float3 dPdu, float3 dPdv)
{
	const float kBitmaskSize = 8;
	const float kHalfPI = PI * 0.5;
	uint mask = 0;

	float2 uv = uv0 + randStep * deltaUV;
	float3 T = deltaUV.x * dPdu + deltaUV.y * dPdv;
	float tanH = BiasedTangent(T);
	
	float3 viewVec = normalize(viewP);
	float3 thickVec = viewVec * cbAO.thickness;

	const int kBaseVecType = cbAO.baseVecType;
	float3 baseVec;
	switch (kBaseVecType)
	{
	case 1: baseVec = -viewVec; break;
	case 2:
		baseVec = normalize(cross(dPdv, dPdu));
		break;
	default: baseVec = viewN; break;
	}
	for (uint step = 0; step < cbAO.stepCount; step++)
	{
		if (float(step) >= numSteps)
		{
			break;
		}

		uv += deltaUV;
		float3 Sf = ScreenPosToViewPos(uv, texDepth.SampleLevel(samLinearClamp, uv, 0));
		float3 Sb = Sf + thickVec;
		float3 Vf = normalize(Sf - viewP);
		float3 Vb = normalize(Sb - viewP);
		float Tf = acos(dot(baseVec, Vf));
		float Tb = acos(dot(baseVec, Vb));
		float Tmin = min(Tf, Tb);
		float Tmax = max(Tf, Tb);
		uint a = uint(floor(Tmin * kBitmaskSize / kHalfPI));
		uint b = uint(floor((Tmax - Tmin) * kBitmaskSize / kHalfPI));
		uint bit = ((2 ^ b) - 1) << a;
		mask = mask | bit;
	}

	return float(countbits(mask)) / kBitmaskSize;
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	float resultAO = 1.0;
	
	uint2 pixPos = did.xy;
	float2 Res = cbScene.screenSize;
	float2 invRes = cbScene.invScreenSize;
	float2 uv = (float2(pixPos) + 0.5) / Res;

	// gather depth.
	float4 vLU = texDepth.GatherRed(samLinearClamp, uv, int2(-1, -1));
	float4 vRB = texDepth.GatherRed(samLinearClamp, uv);
	float depthC = vLU.y;
	float depthL = vLU.x;
	float depthU = vLU.z;
	float depthR = vRB.z;
	float depthB = vRB.x;
	
	// compute view space position.
	float3 viewPosC = ScreenPosToViewPos(uv, depthC);

	float radiusInPixels = cbAO.worldSpaceRadius * cbAO.ndcPixelSize / -viewPosC.z;
	[branch]
	if (radiusInPixels > 1.0)
	{
		resultAO = 0.0;

#if ENABLE_VISIBILITY_BITMASK == 1
		float3 viewNormal = LoadViewNormal(pixPos);
#endif
		
		float3 viewPosL = ScreenPosToViewPos(uv + float2(-invRes.x, 0), depthC);
		float3 viewPosU = ScreenPosToViewPos(uv + float2(0, -invRes.y), depthC);
		float3 viewPosR = ScreenPosToViewPos(uv + float2(invRes.x, 0), depthC);
		float3 viewPosB = ScreenPosToViewPos(uv + float2(0, invRes.y), depthC);
		
		float3 dPdu = MinDiff(viewPosC, viewPosR, viewPosL);
		float3 dPdv = MinDiff(viewPosC, viewPosU, viewPosB) * Res.y * invRes.x;

		float2 noise = SpatioTemporalNoise(pixPos, cbAO.temporalIndex);

		float numSteps;
		float2 stepSize;
		CalcNumSteps(stepSize, numSteps, radiusInPixels, noise.y);

		for (uint slice = 0; slice < cbAO.sliceCount; slice++)
		{
			float sliceK = (float(slice) + noise.x) / float(cbAO.sliceCount);
			float phi = sliceK * PI * 2;
			float cosPhi = cos(phi);
			float sinPhi = sin(phi);
			float2 dir = float2(cosPhi, sinPhi);
			float2 deltaUV = dir * stepSize;
			float2 texelDeltaUV = dir * invRes;

#if ENABLE_VISIBILITY_BITMASK == 0
			// HBAO
			resultAO += CalcHorizonOcclusion(deltaUV, texelDeltaUV, uv, viewPosC, numSteps, noise.y, dPdu, dPdv);
#else
			resultAO += CalcVisibilityBitmaskOcclusion(deltaUV, texelDeltaUV, uv, viewPosC, viewNormal, numSteps, noise.y, dPdu, dPdv);
#endif
		}
		resultAO = 1.0 - saturate(resultAO / float(cbAO.sliceCount) * cbAO.intensity);
	}

	rwOutput[pixPos] = saturate(resultAO);
}
