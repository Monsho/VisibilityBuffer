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

[numthreads(32, 1, 1)]
void InitBufferCS(uint dtid : SV_DispatchThreadID)
{
    if (dtid < cbPrefixScan.numBlocks)
    {
        rwStatus[dtid] = uint2(0, 0);
    }
}

groupshared uint sBlockSums[BLOCK_SIZE];
groupshared uint sWaveSums[BLOCK_SIZE / 32];
groupshared uint sPrefix;

// Prefix sum per blocks.
void BlockScan(uint3 Gid, uint3 GTid)
{
    uint local_id = GTid.x;
    uint global_id = Gid.x * BLOCK_SIZE + GTid.x;
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
void PrefixScanCS(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    // compute prefix scan in every block. 
    BlockScan(Gid, GTid);

    // get block sum.
    uint block_sum;
    {
        block_sum = rwInput[Gid.x * BLOCK_SIZE + BLOCK_SIZE - 1] + sBlockSums[BLOCK_SIZE - 1];
    }
    GroupMemoryBarrierWithGroupSync();

    // decoupled look-back.
    if (GTid.x == 0)
    {
        uint blockNo = Gid.x;
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
            rwStatus[blockNo] = uint2(block_sum + sPrefix, 2);
        }
        else
        {
            rwStatus[blockNo] = uint2(block_sum, 2);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // output.
    if (DTid.x < cbPrefixScan.numItems)
        rwOutput[DTid.x] = sBlockSums[GTid.x] + sPrefix;
}