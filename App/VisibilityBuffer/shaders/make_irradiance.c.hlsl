#include "cbuffer.hlsli"
#include "math.hlsli"
#include "pbr.hlsli"

cbuffer cbMake : register(b0)
{
	uint Width;
	uint Height;
	uint IterCount;
	float MipLevel;
}

Texture2D							texHDRI				: register(t0);
RWTexture2D<float4>					rwOutput			: register(u0);
SamplerState						samplerLinear		: register(s0);

float3x3 OrthogonalMatrix(float3 normal)
{
	float3 tangent, binormal;
	if (normal.z < -0.999999f)
	{
		tangent = float3(0, -1, 0);
		binormal = float3(-1, 0, 0);
	}
	else
	{
		float a = 1.0f / (1.0f + normal.z);
		float b = -normal.x * normal.y * a;
		tangent = float3(1.0f - normal.x * normal.x * a, b, -normal.x);
		binormal = float3(b, 1.0f - normal.y * normal.y * a, -normal.y);
	}
	return float3x3(tangent, binormal, normal);
}

float2 Hammersley(uint i, uint N)
{
	uint bits = i << 16u | i >> 16u;
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float2(float(i) / float(N), float(bits) * 2.3283064365386963e-10f);
}

float3 CosineHemisphere(float2 uv)
{
	float phi = 2.0 * PI * uv.y;
	float cosTheta = sqrt(uv.x);
	float sinTheta = sqrt(1.0 - uv.x);
	float sinPhi = sin(phi);
	float cosPhi = cos(phi);
	return float3(cosPhi * sinTheta, sinPhi * sinTheta, cosTheta);
}

[numthreads(8, 8, 1)]
void main(
	uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;
	if (any(pixelPos >= uint2(Width, Height)))
		return;

	float2 UV = (float2(pixelPos) + 0.5) / float2(Width, Height);
	float3 Normal = LatLongToCartesian(UV);
	float3x3 TBN = OrthogonalMatrix(Normal);
	float3 Accum = 0;
	for (uint i = 0; i < IterCount; ++i)
	{
		float2 SampleUV = Hammersley(i, IterCount);
		// float3 HemisphereDir = float3(0, 0, 1);
		float3 HemisphereDir = CosineHemisphere(SampleUV);
		float3 SampleDir = mul(HemisphereDir, TBN);
		Accum += texHDRI.SampleLevel(samplerLinear, CartesianToLatLong(SampleDir), (float)MipLevel).rgb;
	}

	Accum /= float(IterCount);
	rwOutput[pixelPos] = float4(Accum, 1.0f);
}