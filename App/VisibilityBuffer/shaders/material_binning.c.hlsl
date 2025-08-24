#include "constant_defs.h"
#include "cbuffer.hlsli"
#include "visibility_buffer.hlsli"
#include "vrs.hlsli"

struct BinningCB
{
    uint2 screenSize;
    uint numMaterials;
};

ConstantBuffer<BinningCB>       cbBinning           : register(b0);

Texture2D<uint>					texVis				: register(t0);
StructuredBuffer<SubmeshData>	rSubmeshData		: register(t1);
StructuredBuffer<MeshletData>	rMeshletData		: register(t2);
StructuredBuffer<DrawCallData>	rDrawCallData		: register(t3);
Texture2D<float>				texDepth			: register(t4);
Texture2D<uint>					texVRS      		: register(t5);

RWStructuredBuffer<uint>  		rwCount 			: register(u0);
RWStructuredBuffer<uint>		rwOffset 			: register(u1);
RWByteAddressBuffer             rwIndirectArg       : register(u2);
RWStructuredBuffer<uint>        rwPixBuffer         : register(u3);

#define ARG_STRIDE (3 * 4)


bool IsValidPixelByVRS(uint2 pixelPos, out uint vrsType)
{
    vrsType = GetVRSTypeFromImage(texVRS, pixelPos);
    uint2 bitValue = pixelPos & 0x01;
    if (vrsType == VRS_2x2) return all(bitValue == 0);
    if (vrsType == VRS_2x1) return bitValue.x == 0;
    if (vrsType == VRS_1x2) return bitValue.y == 0;
    return true;
}

[numthreads(32, 1, 1)]
void InitCountCS(uint dtid : SV_DispatchThreadID)
{
    if (dtid < cbBinning.numMaterials)
    {
        rwCount[dtid] = 0;
        rwIndirectArg.Store(dtid * ARG_STRIDE, 0);
        rwIndirectArg.Store(dtid * ARG_STRIDE + 4, 1);
        rwIndirectArg.Store(dtid * ARG_STRIDE + 8, 1);
    }
}

#define MAX_MATERIAL 32 * 1024 / 4
groupshared uint sMaterialCount[MAX_MATERIAL];

#define USE_WAVE_MATCH_IN_COUNT 1
#if USE_WAVE_MATCH_IN_COUNT
#   define TILE_X 8
#   define TILE_Y 4
#else
#   define TILE_X 16
#   define TILE_Y 16
#endif
[numthreads(TILE_X, TILE_Y, 1)]
void CountCS(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
#if !USE_WAVE_MATCH_IN_COUNT
    uint maxMaterials = min(MAX_MATERIAL, cbBinning.numMaterials);
    uint globalIndex = gtid.y * TILE_X + gtid.x;
    for (uint i = globalIndex; i < maxMaterials; i += TILE_X * TILE_Y)
    {
        if (globalIndex >= maxMaterials)
            break;
        sMaterialCount[i] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixelPos = dtid;
    [branch]
    if (all(pixelPos < cbBinning.screenSize))
    {
        float depth = texDepth[pixelPos];
        [branch]
        if (depth > 0.0)
        {
            uint drawCallIndex, primID;
            DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
            DrawCallData dc = rDrawCallData[drawCallIndex];
            MeshletData ml = rMeshletData[dc.meshletIndex];
            SubmeshData sm = rSubmeshData[ml.submeshIndex];
            uint materialNo = sm.materialIndex;
            InterlockedAdd(sMaterialCount[materialNo], 1);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint i = globalIndex; i < maxMaterials; i += TILE_X * TILE_Y)
    {
        if (globalIndex >= maxMaterials)
            break;
        [branch]
        if (sMaterialCount[i] > 0)
            InterlockedAdd(rwCount[i], sMaterialCount[i]);
    }
#else
    uint2 pixelPos = dtid;
    [branch]
    if (all(pixelPos < cbBinning.screenSize))
    {
    	float depth = texDepth[pixelPos];
        uint vrsType;
        [branch]
        if (depth > 0.0 && IsValidPixelByVRS(pixelPos, vrsType))
        {
            uint drawCallIndex, primID;
            DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
            DrawCallData dc = rDrawCallData[drawCallIndex];
            MeshletData ml = rMeshletData[dc.meshletIndex];
            SubmeshData sm = rSubmeshData[ml.submeshIndex];
            uint materialNo = sm.materialIndex;
            uint4 match = WaveMatch(materialNo);
            uint matCount = countbits(match.x);
            uint firstThreadNo = firstbitlow(match.x);
            if (WaveGetLaneIndex() == firstThreadNo)
            {
                InterlockedAdd(rwCount[materialNo], matCount);
            }
        }
    }
#endif
}

[numthreads(32, 1, 1)]
void CountSumCS(uint dtid : SV_DispatchThreadID)
{
    if (dtid < cbBinning.numMaterials)
    {
    }
}

[numthreads(TILE_X, TILE_Y, 1)]
void BinningCS(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
#if 0 // brute force
    uint2 pixelPos = dtid;
    if (all(pixelPos < cbBinning.screenSize))
    {
        float depth = texDepth[pixelPos];
        [branch]
        if (depth > 0.0)
        {
            uint drawCallIndex, primID;
            DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
            DrawCallData dc = rDrawCallData[drawCallIndex];
            MeshletData ml = rMeshletData[dc.meshletIndex];
            SubmeshData sm = rSubmeshData[ml.submeshIndex];
            uint matIndex = sm.materialIndex;

            uint storeIndex;
            rwIndirectArg.InterlockedAdd(matIndex * ARG_STRIDE, 1, storeIndex);
            storeIndex += rwOffset[matIndex];
            rwPixBuffer[storeIndex] = EncodePixelPos(pixelPos, 0);
        }
    }
#else // use LDS
    // count materials in group.
    uint2 pixelPos = dtid;
    bool bIsValid = false;
    uint vrsType;
    uint materialNo = 0;
    uint storeIndex = 0;
    [branch]
    if (all(pixelPos < cbBinning.screenSize))
    {
        float depth = texDepth[pixelPos];
        [branch]
        if (depth > 0.0 && IsValidPixelByVRS(pixelPos, vrsType))
        {
            bIsValid = true;
            
            uint drawCallIndex, primID;
            DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
            DrawCallData dc = rDrawCallData[drawCallIndex];
            MeshletData ml = rMeshletData[dc.meshletIndex];
            SubmeshData sm = rSubmeshData[ml.submeshIndex];
            materialNo = sm.materialIndex;
            uint4 match = WaveMatch(materialNo);
            uint matCount = countbits(match.x);
            uint firstThreadNo = firstbitlow(match.x);
            if (WaveGetLaneIndex() == firstThreadNo)
            {
                rwIndirectArg.InterlockedAdd(materialNo * ARG_STRIDE, matCount, storeIndex);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (bIsValid)
    {
        uint4 match = WaveMatch(materialNo);
        uint baseLane = firstbitlow(match.x);
        uint laneBit = 0x1 << WaveGetLaneIndex();
        uint index = WaveReadLaneAt(storeIndex, baseLane) + countbits(match.x & (laneBit - 1));
        index += rwOffset[materialNo];
        rwPixBuffer[index] = EncodePixelPos(pixelPos, vrsType);
    }
#endif
}

[numthreads(32, 1, 1)]
void FinalizeCS(uint dtid : SV_DispatchThreadID)
{
    if (dtid < cbBinning.numMaterials)
    {
        uint cnt = rwIndirectArg.Load(dtid * ARG_STRIDE);
        cnt = (cnt + 32 - 1) / 32;
        rwIndirectArg.Store(dtid * ARG_STRIDE, cnt);
    }
}

// Tiled binnning.
struct TileBinningCB
{
    uint2 screenSize;
    uint2 tileCount;
    uint tileMax;
    uint numMaterials;
};

ConstantBuffer<TileBinningCB>   cbTileBinning       : register(b0);

RWStructuredBuffer<uint>  		rwMaterialIndex		: register(u0); // resolutionX * resolutionY
RWStructuredBuffer<uint>		rwPixelInfo			: register(u1); // resolutionX * resolutionY
RWStructuredBuffer<uint>        rwPixelInTiles      : register(u2); // tileMax
RWStructuredBuffer<uint>        rwTileIndex         : register(u3); // tileMax * materialMax
RWByteAddressBuffer             rwTileIndirectArg   : register(u4); // materialMax

groupshared uint shMaterialFlag[CLASSIFY_MATERIAL_CHUNK_MAX];
groupshared uint shPixelCount;

void ProcessPixel(uint2 LeftTop, uint StoreBaseIndex)
{
    // TODO: Software VRS.
    if (true)
    {
        for (int y = 0; y < PIXEL_QUAD_WIDTH; ++y)
        {
            for (int x = 0; x < PIXEL_QUAD_WIDTH; ++x)
            {
                uint2 pixelPos = LeftTop + uint2(x, y);
                [branch]
                if (all(pixelPos < cbTileBinning.screenSize))
                {
                    float depth = texDepth[pixelPos];
                    [branch]
                    if (depth > 0.0)
                    {
                        uint storeIndex;
                        InterlockedAdd(shPixelCount, 1, storeIndex);
                        storeIndex += StoreBaseIndex;
                        
                        uint drawCallIndex, primID;
                        DecodeVisibility(texVis[pixelPos], drawCallIndex, primID);
                        DrawCallData dc = rDrawCallData[drawCallIndex];
                        MeshletData ml = rMeshletData[dc.meshletIndex];
                        SubmeshData sm = rSubmeshData[ml.submeshIndex];
                        uint materialNo = sm.materialIndex;

                        rwMaterialIndex[storeIndex] = materialNo;
                        rwPixelInfo[storeIndex] = EncodePixelPos(pixelPos, 0);

                        uint chunkIndex = materialNo / 32;
                        uint chunkBit = materialNo % 32;
                        uint orig;
                        InterlockedOr(shMaterialFlag[chunkIndex], 0x1 << chunkBit, orig);
                    }
                }
            }
        }
    }
}

[numthreads(32, 1, 1)]
void InitBinningTileCS(uint dtid : SV_DispatchThreadID)
{
    if (dtid < cbTileBinning.numMaterials)
    {
        rwTileIndirectArg.Store3(dtid * ARG_STRIDE, uint3(0, 1, 1));
    }
}

[numthreads(TILE_THREADS_WIDTH, TILE_THREADS_WIDTH, 1)]
void BinningTileCS(uint2 gid : SV_GroupID, uint2 gtid : SV_GroupThreadID, uint gidx : SV_GroupIndex)
{
    // clear shared memory.
    const uint kMaxThreads = TILE_THREADS_WIDTH * TILE_THREADS_WIDTH;
    uint chunkIndex = gidx;
    for (; chunkIndex < CLASSIFY_MATERIAL_CHUNK_MAX; chunkIndex += kMaxThreads)
    {
        shMaterialFlag[chunkIndex] = 0;
    }
    if (gidx == 0)
    {
        shPixelCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // pixel process.
    uint2 LeftTop = gid * TILE_PIXEL_WIDTH + gtid * PIXEL_QUAD_WIDTH;
    uint TileIndex = gid.y * cbTileBinning.tileCount.x + gid.x;
    ProcessPixel(LeftTop, TileIndex * TILE_PIXEL_WIDTH * TILE_PIXEL_WIDTH);
    GroupMemoryBarrierWithGroupSync();

    // compute tile draw args.
    chunkIndex = gidx;
    for (; chunkIndex < CLASSIFY_MATERIAL_CHUNK_MAX; chunkIndex += kMaxThreads)
    {
        if (shMaterialFlag[chunkIndex] != 0)
        {
            const uint kMatBaseIndex = chunkIndex * 32;
            uint bits = shMaterialFlag[chunkIndex];
            while (bits != 0)
            {
                uint firstBit = firstbitlow(bits);
                uint matIndex = kMatBaseIndex + firstBit;
                bits &= ~(0x1 << firstBit);

                uint argAddr = matIndex * ARG_STRIDE;
                uint storeCount = 0;
                rwTileIndirectArg.InterlockedAdd(argAddr, 1, storeCount);

                uint storeAddr = ((matIndex * cbTileBinning.tileMax) + storeCount);
                rwTileIndex[storeAddr] = TileIndex;
            }
        }
    }
    // store pixel count in tile.
    if (gidx == 0)
    {
        rwPixelInTiles[TileIndex] = shPixelCount;
    }
}
