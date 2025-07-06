#include "cbuffer.hlsli"
#include "visibility_buffer.hlsli"

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

RWStructuredBuffer<uint>  		rwCount 			: register(u0);
RWStructuredBuffer<uint>		rwOffset 			: register(u1);
RWByteAddressBuffer             rwIndirectArg       : register(u2);
RWStructuredBuffer<uint2>       rwPixBuffer         : register(u3);

#define ARG_STRIDE (3 * 4)

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

#define TILE_X 256
#define TILE_Y 1
[numthreads(TILE_X, TILE_Y, 1)]
void CountCS(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
#if 0
    // brute force.
    uint2 pixelPos = dtid;
    uint globalID = dtid.y * cbBinning.screenSize.x + dtid.x;
    uint buffID = globalID % 4;
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
            InterlockedAdd(rwCount[sm.materialIndex], 1);
        }
    }
#else
    // use LDS
    uint maxMaterials = min(MAX_MATERIAL, cbBinning.numMaterials);
    uint globalIndex = gtid.y * 8 + gtid.x;
    for (uint i = globalIndex; i < maxMaterials; i += TILE_X * TILE_Y)
    {
        if (globalIndex >= maxMaterials)
            break;
        sMaterialCount[i] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // multiple thread group version.
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
            if (materialNo < maxMaterials)
                InterlockedAdd(sMaterialCount[materialNo], 1);
            else
                InterlockedAdd(rwCount[materialNo], 1);
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
void BinningCS(uint2 dtid : SV_DispatchThreadID)
{
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
            //rwIndirectArg.InterlockedAdd(0, 1, storeIndex);
            rwIndirectArg.InterlockedAdd(matIndex * ARG_STRIDE, 1, storeIndex);
            storeIndex += rwOffset[matIndex];
            rwPixBuffer[storeIndex] = pixelPos;
        }
    }
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
