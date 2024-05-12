#include "cbuffer.hlsli"

RWTexture2D<uint2>	rwClearTarget	: register(u0);

[numthreads(8, 8, 1)]
void ClearCS(uint3 did : SV_DispatchThreadID)
{
	rwClearTarget[did.xy] = uint2(0xff, 0xff);
}


Texture2D<uint2>	FeedbackTex		: register(t0);
RWByteAddressBuffer	rwMaterialMip	: register(u0);

[numthreads(8, 8, 1)]
void FeedbackCS(uint3 did : SV_DispatchThreadID)
{
	uint2 size;
	FeedbackTex.GetDimensions(size.x, size.y);
	if (any(did.xy >= size))
	{
		return;
	}
	
	uint2 value = FeedbackTex[did.xy];
	uint materialIndex = value.x;
	uint miplevel = value.y;

	uint ov;
	rwMaterialMip.InterlockedMin(materialIndex * 4, miplevel, ov);
}
