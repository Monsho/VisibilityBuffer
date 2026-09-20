#include "../cbuffer.hlsli"
#include "math.hlsli"

ConstantBuffer<SceneCB>		cbScene		: register(b0);
//ConstantBuffer<uint4>		cbHistory	: register(b1);

Texture2D<float>			texDepth	: register(t0);		// current depth.
Texture2D<float4>			texGBufferC	: register(t1);		// world normal.
Texture2D<float4>			texAccum	: register(t2);		// lighting color.

RWTexture2D<float>			rwDepth		: register(u0);
RWTexture2D<float4>			rwGBufferC	: register(u1);
RWTexture2D<float4>			rwAccum		: register(u2);

SamplerState samLinear		: register(s0);

[numthreads(8, 8, 1)]
void main(uint3 did : SV_DispatchThreadID)
{
	uint2 srcPos = did.xy;
	uint2 screenSize = uint2(cbScene.screenSize);
	uint2 deinterSize = screenSize / 4;
	uint2 deinterIndex = srcPos % 4;
	uint2 deinterOffset = srcPos / 4;
	uint2 dstPos = deinterIndex * deinterSize + deinterOffset;

	//srcPos = deinterOffset * 4;

	rwDepth[dstPos] = texDepth[srcPos];
	rwGBufferC[dstPos] = texGBufferC[srcPos];
	// LightAccumはヒストリーを利用しているためテクスチャサイズが違っている可能性がある
	float2 accumUV = (float2(srcPos) + 0.5) / float2(screenSize);
	rwAccum[dstPos] = texAccum.SampleLevel(samLinear, accumUV, 0);
}
