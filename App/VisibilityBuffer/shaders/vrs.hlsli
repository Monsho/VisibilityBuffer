#ifndef _VRS_HLSLI_
#define _VRS_HLSLI_

struct GenCB
{
    uint2 screenSize;
    float intensityThreshold;
    float depthThreshold;
};

#define VRS_1x1 0x00
#define VRS_1x2 0x01
#define VRS_2x1 0x04
#define VRS_2x2 0x05
#define VRS_INVALID 0x8

#define VRS_TILE_X 8
#define VRS_TILE_Y 8

uint GetVRSTypeFromImage(Texture2D<uint> tex, uint2 fullScreenPixelPos)
{
    uint2 vrsPixelPos = fullScreenPixelPos >> 1;
    uint vrsValue = tex[vrsPixelPos];
    return vrsValue == VRS_INVALID ? VRS_1x1 : vrsValue;
}

#endif
