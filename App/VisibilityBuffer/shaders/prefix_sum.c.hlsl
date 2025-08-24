#include "cbuffer.hlsli"

#define BLOCK_SIZE 256

struct PrefixScanCB
{
    uint    numItems;
    uint    numBlocks;
};

ConstantBuffer<PrefixScanCB>	cbPrefixScan		: register(b0);

RWStructuredBuffer<uint>		rwInput	    		: register(u0);
RWStructuredBuffer<uint>		rwOutput			: register(u1);
globallycoherent RWStructuredBuffer<uint2>       rwStatus            : register(u2);
RWStructuredBuffer<uint>		rwBlock		    	: register(u3);

[numthreads(32, 1, 1)]
void InitBufferCS(uint dtid : SV_DispatchThreadID)
{
    if (dtid == 0)
        rwBlock[0] = 0;
    if (dtid < cbPrefixScan.numBlocks)
    {
        rwStatus[dtid] = uint2(0, 0);
    }
}

groupshared uint sBlockSums[BLOCK_SIZE];
groupshared uint sWaveSums[BLOCK_SIZE / 32];
groupshared uint sPrefix;
groupshared uint sBlockIndex;

// Prefix sum per blocks.
void BlockScan(uint BlockIndex, uint GroupThreadID)
{
    uint local_id = GroupThreadID;
    uint global_id = BlockIndex * BLOCK_SIZE + GroupThreadID;
    uint wave_count = WaveGetLaneCount();

    // load input value.
    uint value = (global_id < cbPrefixScan.numItems) ? rwInput[global_id] : 0;

    // get prefix sum in wave.
    uint wave_scan_result = WavePrefixSum(value);

    // get all value sum in wave.
    uint wave_sum = WaveActiveSum(value);
    
    uint wave_id = local_id / wave_count;
    uint lane_id = WaveGetLaneIndex();
    
    // store wave sum to shared memory.
    if (lane_id == 0)
    {
        sWaveSums[wave_id] = wave_sum;
    }
    GroupMemoryBarrierWithGroupSync();

    if (wave_id == 0)
    {
        // wave count in block.
        uint num_waves = (BLOCK_SIZE + wave_count - 1) / wave_count;

        // get wave sum.
        uint sum_to_scan = (lane_id < num_waves) ? sWaveSums[lane_id] : 0;
        
        // store prefix sum of waves.
        sWaveSums[lane_id] = WavePrefixSum(sum_to_scan) + sum_to_scan;
    }
    GroupMemoryBarrierWithGroupSync();

    // store all prefix sum in block.
    uint prefix = (wave_id > 0) ? sWaveSums[wave_id - 1] : 0;
    sBlockSums[local_id] = prefix + wave_scan_result;
}

[numthreads(BLOCK_SIZE, 1, 1)]
void PrefixScanCS(uint Gid : SV_GroupID, uint DTid : SV_DispatchThreadID, uint GTid : SV_GroupThreadID)
{
    if (GTid == 0)
    {
        uint oldValue;
        InterlockedAdd(rwBlock[0], 1, oldValue);
        sBlockIndex = oldValue;
    }
    GroupMemoryBarrierWithGroupSync();

    uint blockNo = sBlockIndex;
    uint GlobalIndex = blockNo * BLOCK_SIZE + GTid;
    
    // compute prefix scan in every block. 
    BlockScan(blockNo, GTid);

    // get block sum.
    uint block_sum;
    {
        block_sum = rwInput[blockNo * BLOCK_SIZE + BLOCK_SIZE - 1] + sBlockSums[BLOCK_SIZE - 1];
    }
    if (GTid == 0)
    {
        rwStatus[blockNo] = uint2(block_sum, 1);
    }
    GroupMemoryBarrierWithGroupSync();

    // decoupled look-back.
    if (GTid == 0)
    {
        sPrefix = 0;
        if (blockNo > 0)
        {
            uint prevBlock = blockNo - 1;
            uint2 status;
            // spin lock.
            [loop]
            while ((status = rwStatus[prevBlock]).y != 2)
            {
            }
            sPrefix = status.x;
            rwStatus[blockNo].x = block_sum + sPrefix;
            DeviceMemoryBarrier();
            rwStatus[blockNo].y = 2;
            // rwStatus[blockNo] = uint2(block_sum + sPrefix, 2);
        }
        else
        {
            rwStatus[blockNo].x = block_sum;
            DeviceMemoryBarrier();
            rwStatus[blockNo].y = 2;
            // rwStatus[blockNo] = uint2(block_sum, 2);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // output.
    if (GlobalIndex < cbPrefixScan.numItems)
        rwOutput[GlobalIndex] = sBlockSums[GTid] + sPrefix;
}