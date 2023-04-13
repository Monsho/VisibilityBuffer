#include "cbuffer.hlsli"
#include "surface_gradient.hlsli"

Texture2D							texNormal			: register(t0);
RWTexture2D<float2>					rwDerivative		: register(u0);

[numthreads(8, 8, 1)]
void main(
	uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;

	float3 N = texNormal[pixelPos].xyz * 2.0 - 1.0;
	rwDerivative[pixelPos] = EncodeSurfaceGradientDeriv(NormalInTS2SurfaceGradientDeriv(N));
}
