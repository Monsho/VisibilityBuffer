#include "constant_defs.h"
#include "cbuffer.hlsli"
#include "visibility_buffer.hlsli"
#include "vrs.hlsli"

// common
ConstantBuffer<SceneCB>         cbScene             : register(b0);
ConstantBuffer<GenCB>           cbGen               : register(b1);
RWTexture2D<uint>               texOutput           : register(u0);

// geneate
Texture2D                       texInput            : register(t0);
Texture2D<uint>					texVis				: register(t1);
Texture2D<float>				texDepth			: register(t2);
StructuredBuffer<SubmeshData>	rSubmeshData		: register(t3);
StructuredBuffer<MeshletData>	rMeshletData		: register(t4);
StructuredBuffer<DrawCallData>	rDrawCallData		: register(t5);

// reprojection
Texture2D<uint>                 texPrevOutput       : register(t0);
Texture2D<float>                texPrevDepth        : register(t1);
Texture2D<float>                texCurrDepth        : register(t2);
SamplerState                    samClampLinear      : register(s0);

float GetIntensity(float4 color)
{
    return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
}

int GetMaterialID(uint vis, float depth)
{
    if (depth <= 0.0)
    {
        return -1;
    }
    
    uint drawCallIndex, primID;
    DecodeVisibility(vis, drawCallIndex, primID);
    DrawCallData dc = rDrawCallData[drawCallIndex];
    MeshletData ml = rMeshletData[dc.meshletIndex];
    SubmeshData sm = rSubmeshData[ml.submeshIndex];
    return sm.materialIndex;
}

float3 ScreenPosToViewPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
    float4 viewPos = mul(cbScene.mtxProjToView, clipPos);
    return viewPos.xyz / viewPos.w;
}

groupshared uint2 shPixelVRS_XY[VRS_TILE_X * VRS_TILE_Y];
groupshared float shIntensity[(VRS_TILE_X + 2) * (VRS_TILE_Y + 2)];
groupshared int shMaterialID[(VRS_TILE_X + 2) * (VRS_TILE_Y + 2)];

[numthreads(VRS_TILE_X, VRS_TILE_Y, 1)]
void GenerateVrsCS(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
    int2 srcPixelPos = dtid;

    const int kTileWidth = VRS_TILE_X + 2;
    const int kTileHeight = VRS_TILE_Y + 2;
    const int kLoadCount = kTileWidth * kTileHeight;
    const int2 kLeftTop = int2(gid) * int2(VRS_TILE_X, VRS_TILE_Y) - int2(1, 1);
    for (int i = gtid.y * VRS_TILE_X + gtid.x; i < kLoadCount; i += (VRS_TILE_X * VRS_TILE_Y))
    {
        int y = i / kTileWidth;
        int x = i % kTileHeight;
        int2 pos = kLeftTop + int2(x, y);
        shIntensity[i] = GetIntensity(texInput[pos]);
        shMaterialID[i] = GetMaterialID(texVis[pos], texDepth[pos]);
    }
    GroupMemoryBarrierWithGroupSync();

    int sharedIndex = gtid.x + gtid.y * VRS_TILE_X;
    if (any(srcPixelPos >= cbGen.screenSize))
    {
        shPixelVRS_XY[sharedIndex] = 0;
        return;
    }

    const int2 kTileLeftTop = int2(gtid);
    const int kIndex[9] = {
        (kTileLeftTop.y + 0) * kTileWidth + (kTileLeftTop.x + 0),
        (kTileLeftTop.y + 0) * kTileWidth + (kTileLeftTop.x + 1),
        (kTileLeftTop.y + 0) * kTileWidth + (kTileLeftTop.x + 2),
        (kTileLeftTop.y + 1) * kTileWidth + (kTileLeftTop.x + 0),
        (kTileLeftTop.y + 1) * kTileWidth + (kTileLeftTop.x + 1),
        (kTileLeftTop.y + 1) * kTileWidth + (kTileLeftTop.x + 2),
        (kTileLeftTop.y + 2) * kTileWidth + (kTileLeftTop.x + 0),
        (kTileLeftTop.y + 2) * kTileWidth + (kTileLeftTop.x + 1),
        (kTileLeftTop.y + 2) * kTileWidth + (kTileLeftTop.x + 2),
    };
    float dIX = abs(shIntensity[kIndex[0]] + shIntensity[kIndex[3]] * 2.0 + shIntensity[kIndex[6]] - shIntensity[kIndex[2]] - shIntensity[kIndex[5]] * 2.0 - shIntensity[kIndex[8]]);
    float dIY = abs(shIntensity[kIndex[0]] + shIntensity[kIndex[1]] * 2.0 + shIntensity[kIndex[2]] - shIntensity[kIndex[6]] - shIntensity[kIndex[7]] * 2.0 - shIntensity[kIndex[8]]);
    float threshold = cbGen.intensityThreshold;
    shPixelVRS_XY[sharedIndex] = uint2(
            dIX > threshold ? 0 : VRS_2x1,
            dIY > threshold ? 0 : VRS_1x2);

    bool bSameXMat = shMaterialID[4] == shMaterialID[3] && shMaterialID[4] == shMaterialID[5];
    bool bSameYMat = shMaterialID[4] == shMaterialID[1] && shMaterialID[4] == shMaterialID[7];
    shPixelVRS_XY[sharedIndex].x = min(shPixelVRS_XY[sharedIndex].x, bSameXMat ? VRS_2x1 : 0);
    shPixelVRS_XY[sharedIndex].y = min(shPixelVRS_XY[sharedIndex].y, bSameYMat ? VRS_1x2 : 0);
    
    GroupMemoryBarrierWithGroupSync();

    if ((srcPixelPos.x & 0x1) == 0 && (srcPixelPos.y & 0x1) == 0)
    {
        uint2 vrs = min(shPixelVRS_XY[sharedIndex], min(shPixelVRS_XY[sharedIndex + 1], min(shPixelVRS_XY[sharedIndex + VRS_TILE_X], shPixelVRS_XY[sharedIndex + VRS_TILE_X + 1])));
        texOutput[srcPixelPos / 2] = vrs.x | vrs.y;
    }
}

[numthreads(VRS_TILE_X, VRS_TILE_Y, 1)]
void ClearCS(uint2 dtid : SV_DispatchThreadID)
{
    uint2 pixelPos = dtid;
    if (any(pixelPos >= cbGen.screenSize))
        return;
    
    texOutput[dtid] = VRS_1x1;
}

[numthreads(VRS_TILE_X, VRS_TILE_Y, 1)]
void ReprojectionCS(uint2 dtid : SV_DispatchThreadID)
{
    uint2 pixelPos = dtid;
    if (any(pixelPos >= cbGen.screenSize))
        return;
    
    float2 uv = (float2(pixelPos) + 0.5) / float2(cbGen.screenSize);

    // current UV to prev UV.
    float currDepth = texCurrDepth.SampleLevel(samClampLinear, uv, 0);
    float4 currClipPos = float4(uv * float2(2, -2) + float2(-1, 1), currDepth, 1);
    float4 prevClipPos = mul(cbScene.mtxProjToPrevProj, currClipPos);
    prevClipPos.xyz *= (1 / prevClipPos.w);
    float2 prevUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;
    if (any(prevUV < 0.0) || any(prevUV > 1.0))
        return;

    float prevDepth = texPrevDepth.SampleLevel(samClampLinear, prevUV, 0);
    float prevZ = ScreenPosToViewPos(prevUV, prevDepth).z;
    float currZ = ScreenPosToViewPos(prevUV, prevClipPos.z).z;
    if (abs(prevZ - currZ) > cbGen.depthThreshold)
        return;

    uint2 prevPixelPos = uint2(prevUV * float2(cbGen.screenSize));
    texOutput[pixelPos] = texPrevOutput[prevPixelPos];
}
