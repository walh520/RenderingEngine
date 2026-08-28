#include "software_common.hlsli"

static const uint SOFTWARE_RADIX_SIZE = 16u;
static const uint SOFTWARE_RADIX_THREADS = 128u;

struct SoftwareRadixConfig
{
    uint elementCount;
    uint groupCount;
    uint shift;
    uint field; // 0 stable primitive ID, 1 Morton code
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwareMortonPair> gInputPairs;
[[vk::binding(1, 2)]] RWStructuredBuffer<SoftwareMortonPair> gOutputPairs;
[[vk::binding(2, 2)]] RWStructuredBuffer<uint> gGroupHistograms;
[[vk::binding(3, 2)]] RWStructuredBuffer<uint> gGroupOffsets;
[[vk::binding(4, 2)]] ConstantBuffer<SoftwareRadixConfig> gConfig;
[[vk::binding(5, 2)]] RWStructuredBuffer<uint> gInvalidStableIdCounter;

groupshared uint sHistogram[SOFTWARE_RADIX_SIZE];
groupshared uint sBucket[SOFTWARE_RADIX_THREADS];

uint SoftwareRadixField(const SoftwareMortonPair pair)
{
    return gConfig.field == 0u ? pair.stablePrimitiveId : pair.code;
}

[numthreads(1, 1, 1)]
void ResetStableIdValidationCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x == 0u)
    {
        gInvalidStableIdCounter[0u] = 0u;
    }
}

// Dispatch after all stable-ID radix shifts and before the Morton shifts. At
// that point equal stable IDs are adjacent even when their Morton codes differ.
[numthreads(SOFTWARE_RADIX_THREADS, 1, 1)]
void ValidateStableIdsCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index == 0u || index >= gConfig.elementCount)
    {
        return;
    }
    if (gInputPairs[index - 1u].stablePrimitiveId ==
        gInputPairs[index].stablePrimitiveId)
    {
        InterlockedAdd(gInvalidStableIdCounter[0u], 1u);
    }
}

[numthreads(SOFTWARE_RADIX_THREADS, 1, 1)]
void HistogramCS(
    const uint3 groupId : SV_GroupID,
    const uint3 groupThreadId : SV_GroupThreadID,
    const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (groupThreadId.x < SOFTWARE_RADIX_SIZE)
    {
        sHistogram[groupThreadId.x] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (dispatchThreadId.x < gConfig.elementCount)
    {
        const uint bucket = (SoftwareRadixField(gInputPairs[dispatchThreadId.x]) >> gConfig.shift) & 0x0fu;
        InterlockedAdd(sHistogram[bucket], 1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupThreadId.x < SOFTWARE_RADIX_SIZE && groupId.x < gConfig.groupCount)
    {
        gGroupHistograms[groupId.x * SOFTWARE_RADIX_SIZE + groupThreadId.x] =
            sHistogram[groupThreadId.x];
    }
}

[numthreads(1, 1, 1)]
void PrefixCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }
    uint runningOffset = 0u;
    for (uint bucket = 0u; bucket < SOFTWARE_RADIX_SIZE; ++bucket)
    {
        for (uint group = 0u; group < gConfig.groupCount; ++group)
        {
            const uint index = group * SOFTWARE_RADIX_SIZE + bucket;
            gGroupOffsets[index] = runningOffset;
            runningOffset += gGroupHistograms[index];
        }
    }
}

[numthreads(SOFTWARE_RADIX_THREADS, 1, 1)]
void ScatterCS(
    const uint3 groupId : SV_GroupID,
    const uint3 groupThreadId : SV_GroupThreadID,
    const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const bool active = dispatchThreadId.x < gConfig.elementCount;
    SoftwareMortonPair pair = (SoftwareMortonPair)0;
    uint bucket = SOFTWARE_RADIX_SIZE;
    if (active)
    {
        pair = gInputPairs[dispatchThreadId.x];
        bucket = (SoftwareRadixField(pair) >> gConfig.shift) & 0x0fu;
    }
    sBucket[groupThreadId.x] = bucket;
    GroupMemoryBarrierWithGroupSync();
    if (!active)
    {
        return;
    }
    uint localRank = 0u;
    for (uint lane = 0u; lane < groupThreadId.x; ++lane)
    {
        localRank += sBucket[lane] == bucket ? 1u : 0u;
    }
    const uint outputIndex =
        gGroupOffsets[groupId.x * SOFTWARE_RADIX_SIZE + bucket] + localRank;
    gOutputPairs[outputIndex] = pair;
}
