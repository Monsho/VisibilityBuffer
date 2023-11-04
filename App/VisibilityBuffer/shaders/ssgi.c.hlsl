#include "cbuffer.hlsli"
#include "math.hlsli"

#ifndef ENABLE_DEINTERLEAVE
#	define  ENABLE_DEINTERLEAVE 0
#endif

ConstantBuffer<SceneCB>		cbScene		: register(b0);
ConstantBuffer<AmbOccCB>	cbAO		: register(b1);

Texture2D<float>			texDepth	: register(t0);		// current depth.
Texture2D<float4>			texGBufferC	: register(t1);		// world normal.
Texture2D<float4>			texAccum	: register(t2);		// lighting color.

SamplerState				samLinearClamp	: register(s0);

RWTexture2D<float>			rwAO		: register(u0);
RWTexture2D<float3>			rwGI		: register(u1);

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

float2 DeinterleaveUVToScreenUV(float2 uv, float2 baseUV)
{
#if ENABLE_DEINTERLEAVE == 0
	return uv;
#else
	return baseUV + frac(uv * 4.0);
#endif
}

// Visibility Bitmask
float3 LoadViewNormalFromPos(uint2 pixPos)
{
	float3 worldNormal = texGBufferC[pixPos].xyz * 2.0 - 1.0;
	return mul((float3x3)cbScene.mtxWorldToView, worldNormal);
}
float3 LoadViewNormalFromUV(float2 uv)
{
	float3 worldNormal = texGBufferC.SampleLevel(samLinearClamp, uv, 0).xyz * 2.0 - 1.0;
	return mul((float3x3)cbScene.mtxWorldToView, worldNormal);
}

void ComputeVisibilityBitmask(float2 deltaUV, float2 uv0, float2 baseUV, float4 rect, float3 viewP, float3 viewN, float numSteps, float randStep, float3 dPdu, float3 dPdv, inout float resultAO, inout float3 resultGI)
{
	const float kBitmaskSize = 32;
	const float kHalfPI = PI * 0.5;
	uint mask = 0;

	float2 uv = uv0 + randStep * deltaUV;
	float3 T = deltaUV.x * dPdu + deltaUV.y * dPdv;
	
	float3 viewVec = normalize(viewP);
	float3 thickVec = viewVec * cbAO.thickness;

	const int kBaseVecType = cbAO.baseVecType;
	float3 baseVec;
	switch (kBaseVecType)
	{
	case 1: baseVec = -viewVec; break;
	case 2:
		baseVec = -normalize(cross(dPdv, dPdu));
		break;
	default: baseVec = viewN; break;
	}

	for (uint step = 0; step < cbAO.stepCount; step++)
	{
		if (float(step) >= numSteps)
		{
			break;
		}

		if (any(uv < rect.xy) || any(uv > rect.zw))
		{
			break;
		}

		float2 screenUV = DeinterleaveUVToScreenUV(uv, baseUV);
		float3 Sf = ScreenPosToViewPos(screenUV, texDepth.SampleLevel(samLinearClamp, uv, 0));
		Sf = Sf + normalize(Sf) * cbAO.viewBias;
		float3 Vf = normalize(Sf - viewP);
		float Df = dot(baseVec, Vf);
		if (Df > 0.0)
		{
			float Tf = acos(Df);
			float3 Sb = Sf + thickVec;
			float3 Vb = normalize(Sb - viewP);
			float Tb = acos(saturate(dot(baseVec, Vb)));
			float Tmin = min(Tf, Tb);
			float Tmax = max(Tf, Tb);
			uint a = uint(floor(Tmin * kBitmaskSize / kHalfPI));
			uint b = uint(floor((Tmax - Tmin) * kBitmaskSize / kHalfPI));
			uint bit = ((2 ^ b) - 1) << a;
			float3 Cf = texAccum.SampleLevel(samLinearClamp, uv, 0).rgb;
			float3 Nf = LoadViewNormalFromUV(uv);
			float3 GI = float(countbits(bit & (~mask))) / kBitmaskSize;
			GI *= Cf * dot(baseVec, Vf) * dot(Nf, -Vf);
			resultGI += GI;
			mask = mask | bit;
		}
		uv += deltaUV;
	}

	resultAO += float(countbits(mask)) / kBitmaskSize;
}

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	float resultAO = 1.0;
	float3 resultGI = (0.0).xxx;

	float2 Res = cbScene.screenSize;
	float2 invRes = cbScene.invScreenSize;

#if ENABLE_DEINTERLEAVE == 0
	uint2 pixPos = did.xy;
	uint2 outPos = did.xy;
	float4 rect = float4(0, 0, 1, 1);
	float2 baseUV = 0;
#else
	uint2 uRes = uint2(Res);
	uint2 diSize = uRes / 4;
	uint2 pixPos = did.xy;
	uint2 diIndex = pixPos / diSize;
	uint2 diOffset = pixPos % diSize;
	uint2 outPos = diOffset * 4 + diIndex;
	float2 rect_lt = (float2(diIndex * diSize) + 0.5) * invRes;
	float2 rect_rb = (float2((diIndex + 1) * diSize) - 0.5) * invRes;
	float4 rect = float4(rect_lt, rect_rb);
	float2 baseUV = (float2(diIndex) - 1.5) * invRes;
#endif
	float2 pixUV = (float2(pixPos) + 0.5) * invRes;
	float2 outUV = (float2(outPos) + 0.5) * invRes;

	// gather depth.
	float4 vLU = texDepth.GatherRed(samLinearClamp, pixUV, int2(-1, -1));
	float4 vRB = texDepth.GatherRed(samLinearClamp, pixUV);
	float depthC = vLU.y;
	float depthL = vLU.x;
	float depthU = vLU.z;
	float depthR = vRB.z;
	float depthB = vRB.x;
	
	// compute view space position.
	float3 viewPosC = ScreenPosToViewPos(outUV, depthC);

	float radiusInPixels = cbAO.worldSpaceRadius * cbAO.ndcPixelSize / -viewPosC.z;
	[branch]
	if (radiusInPixels > 1.0)
	{
		resultAO = 0.0;

		float3 viewNormal = LoadViewNormalFromPos(pixPos);

#if ENABLE_DEINTERLEAVE == 0
		float2 R = Res;
		float2 iR = invRes;
		float2 uvOffset = invRes;
#else
		float2 R = Res * (1.0 / 4.0);
		float2 iR = invRes * 4.0;
		float2 uvOffset = invRes * 4.0;
#endif
		float3 viewPosL = ScreenPosToViewPos(outUV + float2(-uvOffset.x, 0), depthL);
		float3 viewPosU = ScreenPosToViewPos(outUV + float2(0, -uvOffset.y), depthU);
		float3 viewPosR = ScreenPosToViewPos(outUV + float2(uvOffset.x, 0), depthR);
		float3 viewPosB = ScreenPosToViewPos(outUV + float2(0, uvOffset.y), depthB);
		
		float3 dPdu = MinDiff(viewPosC, viewPosR, viewPosL);
		float3 dPdv = MinDiff(viewPosC, viewPosU, viewPosB) * R.y * iR.x;

#if ENABLE_DEINTERLEAVE == 0
		float2 noise = SpatioTemporalNoise(outPos, cbAO.temporalIndex);
#else
		float2 noise = SpatioTemporalNoise(outPos, cbAO.temporalIndex);
		//float2 noise = SpatioTemporalNoise(diIndex, cbAO.temporalIndex);
#endif

		float numSteps;
		float2 stepSize;
		CalcNumSteps(stepSize, numSteps, radiusInPixels, noise.y);

#if ENABLE_DEINTERLEAVE != 0
		stepSize *= (1.0 / 4.0);
#endif
		for (uint slice = 0; slice < cbAO.sliceCount; slice++)
		{
			float sliceK = (float(slice) + noise.x) / float(cbAO.sliceCount);
			float phi = sliceK * PI * 2;
			float cosPhi = cos(phi);
			float sinPhi = sin(phi);
			float2 dir = float2(cosPhi, sinPhi);
			float2 deltaUV = dir * stepSize;

			ComputeVisibilityBitmask(deltaUV, pixUV, baseUV, rect, viewPosC, viewNormal, numSteps, noise.y, dPdu, dPdv, resultAO, resultGI);
		}
		resultAO = 1.0 - saturate(resultAO / float(cbAO.sliceCount) * cbAO.intensity);
		resultGI = resultGI * cbAO.giIntensity / float(cbAO.sliceCount);
	}

	rwAO[outPos] = saturate(resultAO);
	rwGI[outPos] = resultGI;
}
