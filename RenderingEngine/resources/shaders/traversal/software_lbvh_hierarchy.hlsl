#include "software_common.hlsli"

struct SoftwareHierarchyConfig
{
    uint primitiveCount;
    uint nodeCount;
    uint2 reserved;
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwareMortonPair> gSortedPairs;
[[vk::binding(1, 2)]] RWStructuredBuffer<SoftwareNodeRecord> gNodes;
[[vk::binding(2, 2)]] RWStructuredBuffer<uint> gParents;
[[vk::binding(3, 2)]] ConstantBuffer<SoftwareHierarchyConfig> gConfig;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> gInvalidHierarchyCounter;

int SoftwareCountLeadingZeros(const uint value)
{
    return value == 0u ? 32 : 31 - firstbithigh(value);
}

// Equivalent to clz64((morton << 32 | stableId)_a XOR key_b), without requiring
// shader int64 support. Out-of-range indices intentionally return signed -1.
int SoftwareDelta(const int first, const int second)
{
    if (first < 0 || second < 0 || first >= (int)gConfig.primitiveCount ||
        second >= (int)gConfig.primitiveCount)
    {
        return -1;
    }
    const SoftwareMortonPair a = gSortedPairs[first];
    const SoftwareMortonPair b = gSortedPairs[second];
    const uint codeDifference = a.code ^ b.code;
    if (codeDifference != 0u)
    {
        return SoftwareCountLeadingZeros(codeDifference);
    }
    const uint idDifference = a.stablePrimitiveId ^ b.stablePrimitiveId;
    return 32 + SoftwareCountLeadingZeros(idDifference);
}

[numthreads(64, 1, 1)]
void ResetCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint nodeIndex = dispatchThreadId.x;
    if (nodeIndex == 0u)
    {
        gInvalidHierarchyCounter[0u] = 0u;
    }
    if (nodeIndex >= gConfig.nodeCount)
    {
        return;
    }
    SoftwareNodeRecord node;
    node.boundsMin = float4(0.0f, 0.0f, 0.0f, 0.0f);
    node.boundsMax = float4(0.0f, 0.0f, 0.0f, 0.0f);
    node.links = uint4(SOFTWARE_INVALID_INDEX, 0u, SOFTWARE_INVALID_INDEX, SOFTWARE_INVALID_INDEX);
    gNodes[nodeIndex] = node;
    gParents[nodeIndex] = SOFTWARE_INVALID_INDEX;
}

[numthreads(64, 1, 1)]
void HierarchyCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint internalIndex = dispatchThreadId.x;
    if (gConfig.primitiveCount <= 1u || internalIndex >= gConfig.primitiveCount - 1u)
    {
        return;
    }
    const SoftwareMortonPair currentPair = gSortedPairs[internalIndex];
    const SoftwareMortonPair nextPair = gSortedPairs[internalIndex + 1u];
    if (currentPair.code == nextPair.code &&
        currentPair.stablePrimitiveId == nextPair.stablePrimitiveId)
    {
        InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
        return;
    }
    const int index = (int)internalIndex;
    const int direction = SoftwareDelta(index, index + 1) - SoftwareDelta(index, index - 1) >= 0
                              ? 1
                              : -1;
    const int minimumPrefix = SoftwareDelta(index, index - direction);

    int maximumLength = 2;
    while (SoftwareDelta(index, index + maximumLength * direction) > minimumPrefix)
    {
        maximumLength <<= 1;
    }
    int length = 0;
    for (int step = maximumLength >> 1; step > 0; step >>= 1)
    {
        if (SoftwareDelta(index, index + (length + step) * direction) > minimumPrefix)
        {
            length += step;
        }
    }
    const int rangeEnd = index + length * direction;
    const int nodePrefix = SoftwareDelta(index, rangeEnd);

    int splitOffset = 0;
    int splitStep = 1;
    while (splitStep < length)
    {
        splitStep <<= 1;
    }
    for (splitStep >>= 1; splitStep > 0; splitStep >>= 1)
    {
        const int candidate = splitOffset + splitStep;
        if (candidate < length &&
            SoftwareDelta(index, index + candidate * direction) > nodePrefix)
        {
            splitOffset = candidate;
        }
    }
    const int split = index + splitOffset * direction + min(direction, 0);
    const int rangeFirst = min(index, rangeEnd);
    const int rangeLast = max(index, rangeEnd);
    const uint leafBase = gConfig.primitiveCount - 1u;
    const uint leftChild = split == rangeFirst ? leafBase + (uint)split : (uint)split;
    const uint rightChild = split + 1 == rangeLast
                                ? leafBase + (uint)(split + 1)
                                : (uint)(split + 1);

    SoftwareNodeRecord node = gNodes[internalIndex];
    node.links = uint4(leftChild, 0u, rightChild, SOFTWARE_INVALID_INDEX);
    gNodes[internalIndex] = node;
    uint previousLeft = SOFTWARE_INVALID_INDEX;
    uint previousRight = SOFTWARE_INVALID_INDEX;
    InterlockedCompareExchange(
        gParents[leftChild],
        SOFTWARE_INVALID_INDEX,
        internalIndex,
        previousLeft);
    InterlockedCompareExchange(
        gParents[rightChild],
        SOFTWARE_INVALID_INDEX,
        internalIndex,
        previousRight);
    if (previousLeft != SOFTWARE_INVALID_INDEX || previousRight != SOFTWARE_INVALID_INDEX)
    {
        InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
    }
}
