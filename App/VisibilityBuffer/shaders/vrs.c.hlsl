#include "constant_defs.h"
#include "cbuffer.hlsli"
#include "visibility_buffer.hlsli"

struct GenCB
{
    uint2 screenSize;
    float threshold;
};

ConstantBuffer<SceneCB>         cbScene             : register(b0);
ConstantBuffer<GenCB>           cbGen               : register(b1);
Texture2D                       texInput            : register(t0);
Texture2D<uint>                 texPrevOutput       : register(t0);
Texture2D<float>                texDepth            : register(t1);
RWTexture2D<uint>               texOutput           : register(u0);

#define VRS_1x1 0x00
#define VRS_1x2 0x01
#define VRS_2x1 0x04
#define VRS_2x2 0x05

#define TILE_X 8
#define TILE_Y 8

float GetIntensity(float4 color)
{
    return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
}

[numthreads(TILE_X, TILE_Y, 1)]
void GenFromIntensityCS(uint2 dtid : SV_DispatchThreadID)
{
    uint2 pixelPos = dtid * 2;
    if (any(pixelPos >= cbGen.screenSize))
        return;
    
    if (any(pixelPos + 1 >= cbGen.screenSize))
    {
        texOutput[dtid] = VRS_1x1;
        return;
    }

    float intensity[4] = {
        GetIntensity(texInput[pixelPos + uint2(0, 0)]),
        GetIntensity(texInput[pixelPos + uint2(1, 0)]),
        GetIntensity(texInput[pixelPos + uint2(0, 1)]),
        GetIntensity(texInput[pixelPos + uint2(1, 1)])
    };
    float dI_LR = max(abs(intensity[0] - intensity[1]), abs(intensity[2] - intensity[3]));
    float dI_TB = max(abs(intensity[0] - intensity[2]), abs(intensity[1] - intensity[3]));
    uint vrs = VRS_1x1;
    vrs |= (dI_LR < cbGen.threshold) ? VRS_2x1 : 0;
    vrs |= (dI_TB < cbGen.threshold) ? VRS_1x2 : 0;
    texOutput[dtid] = vrs;
}

[numthreads(TILE_X, TILE_Y, 1)]
void ClearCS(uint2 dtid : SV_DispatchThreadID)
{
    uint2 pixelPos = dtid;
    if (any(pixelPos >= cbGen.screenSize))
        return;
    
    texOutput[dtid] = VRS_1x1;
}

[numthreads(TILE_X, TILE_Y, 1)]
void ReprojectionCS(uint2 dtid : SV_DispatchThreadID)
{
    uint2 pixelPos = dtid;
    if (any(pixelPos >= cbGen.screenSize))
        return;
    
    float2 uv = (float2(pixelPos) + 0.5) / float2(cbGen.screenSize);

    // prev UV to current UV.
    float depth = texDepth[pixelPos];
    float4 prevClipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
    float4 clipPos = mul(cbScene.mtxProjToPrevProj, prevClipPos);
    clipPos.xyz *= (1 / clipPos.w);
	float2 newUV = clipPos.xy * float2(0.5, -0.5) + 0.5;
    if (any(newUV < 0.0) || any(newUV > 1.0))
        return;

    // store VRS value.
    uint2 newPixelPos = uint2(newUV * float2(cbGen.screenSize));
    texOutput[newPixelPos] = texPrevOutput[pixelPos];
}
