#ifndef SURFACE_GRADIENT_HLSLI
#define SURFACE_GRADIENT_HLSLI

#include "math.hlsli"

float2 NormalInTS2SurfaceGradientDeriv(float3 n)
{
	const float kScale = 1.0 / 128.0;

	float3 ma = abs(n);
	float Zma = max(ma.z, kScale * max(ma.x, ma.y));

	// [-128; 128]
	return -float2(n.x, n.y) / Zma;
}

float2 EncodeSurfaceGradientDeriv(float2 deriv)
{
	return deriv / 256.0 + 0.5;
}

float2 DecodeSurfaceGradientDeriv(float2 v)
{
	return v * 256.0 - 128.0;
}

float3 SurfaceGradientFromTBN(float2 deriv, float3 T, float3 B)
{
	return deriv.x * T + deriv.y * B;
}

// both N and v is in same space.(world-space, object-space, etc.)
float3 SurfaceGradientFromSameSpaceNormal(float3 N, float3 v)
{
	float k = dot(N, v);
	return (k * N - v) / max(Epsilon, abs(k));
}

float3 SurfaceGradientFromVolumeGradient(float3 N, float3 grad)
{
	return grad - dot(N, grad) * N;
}

float3 SurfaceGradientFromTriplanar(float3 N, float3 weight, float2 deriv_x, float2 deriv_y, float2 deriv_z)
{
	float3 grad = float3(
		weight.z * deriv_z.x + weight.y * deriv_y.x,
		weight.z * deriv_z.y + weight.x * deriv_x.y,
		weight.x * deriv_x.x + weight.y * deriv_y.y);
	return SurfaceGradientFromVolumeGradient(N, grad);
}

float3 SurfaceGradientFromDecal(float3 N, float2 deriv, float3 axisX, float3 axisY)
{
	float3 grad = deriv.x * axisX + deriv.y * axisY;
	return SurfaceGradientFromVolumeGradient(N, grad);
}

float3 ResolveNormalFromSurfaceGradient(float3 N, float3 surfGrad)
{
	return normalize(N - surfGrad);
}

#endif // SURFACE_GRADIENT_HLSLI
