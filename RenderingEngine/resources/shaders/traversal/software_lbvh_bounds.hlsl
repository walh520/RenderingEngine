#include "software_common.hlsli"

struct SoftwareBoundsConfig
{
    uint primitiveCount;
    uint nodeCount;
    uint currentDepth;
    uint maximumDepthLimit;
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwareMortonPair> gSortedPairs;
[[vk::binding(1, 2)]] StructuredBuffer<SoftwarePrimitiveRecord> gSourcePrimitives;
[[vk::binding(2, 2)]] RWStructuredBuffer<SoftwarePrimitiveRecord> gSortedPrimitives;
[[vk::binding(3, 2)]] RWStructuredBuffer<SoftwareNodeRecord> gNodes;
[[vk::binding(4, 2)]] StructuredBuffer<uint> gParents;
[[vk::binding(5, 2)]] RWStructuredBuffer<uint> gDepths;
[[vk::binding(6, 2)]] RWStructuredBuffer<uint> gMaximumDepth;
[[vk::binding(7, 2)]] RWStructuredBuffer<uint> gInvalidHierarchyCounter;
[[vk::binding(8, 2)]] ConstantBuffer<SoftwareBoundsConfig> gConfig;

[numthreads(1, 1, 1)]
void ResetBoundsValidationCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x == 0u)
    {
        gMaximumDepth[0u] = 0u;
        gInvalidHierarchyCounter[0u] = 0u;
    }
}

[numthreads(64, 1, 1)]
void EmitLeavesCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint sortedIndex = dispatchThreadId.x;
    if (sortedIndex >= gConfig.primitiveCount)
    {
        return;
    }
    const uint sourceIndex = gSortedPairs[sortedIndex].sourceIndex;
    if (sourceIndex >= gConfig.primitiveCount)
    {
        InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
        return;
    }
    const SoftwarePrimitiveRecord primitive = gSourcePrimitives[sourceIndex];
    gSortedPrimitives[sortedIndex] = primitive;
    float3 boundsMin;
    float3 boundsMax;
    SoftwarePrimitiveBounds(primitive, boundsMin, boundsMax);
    const uint leafIndex = gConfig.primitiveCount - 1u + sortedIndex;
    SoftwareNodeRecord leaf = gNodes[leafIndex];
    leaf.boundsMin = float4(boundsMin, 0.0f);
    leaf.boundsMax = float4(boundsMax, 0.0f);
    leaf.links = uint4(sortedIndex, 1u, SOFTWARE_INVALID_INDEX, gParents[leafIndex]);
    gNodes[leafIndex] = leaf;
}

[numthreads(64, 1, 1)]
void ComputeDepthsCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint nodeIndex = dispatchThreadId.x;
    if (nodeIndex >= gConfig.nodeCount)
    {
        return;
    }
    if ((nodeIndex == 0u) != (gParents[nodeIndex] == SOFTWARE_INVALID_INDEX))
    {
        InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
        return;
    }
    uint depth = 0u;
    uint cursor = nodeIndex;
    const uint maximumSteps = min(gConfig.maximumDepthLimit, gConfig.nodeCount);
    while (cursor != 0u && depth < maximumSteps)
    {
        const uint child = cursor;
        const uint parent = gParents[child];
        if (parent == SOFTWARE_INVALID_INDEX || parent >= gConfig.nodeCount)
        {
            InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
            return;
        }
        const SoftwareNodeRecord parentNode = gNodes[parent];
        if (parentNode.links.y != 0u ||
            (parentNode.links.x != child && parentNode.links.z != child))
        {
            InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
            return;
        }
        cursor = parent;
        ++depth;
    }
    if (cursor != 0u)
    {
        InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
        return;
    }
    gDepths[nodeIndex] = depth;
    InterlockedMax(gMaximumDepth[0u], depth);
    SoftwareNodeRecord node = gNodes[nodeIndex];
    node.links.w = gParents[nodeIndex];
    gNodes[nodeIndex] = node;
}

// Dispatch once per depth, deepest internal level to root. A Vulkan
// compute-write -> compute-read buffer barrier is required between dispatches;
// group barriers are intentionally not used as a device-wide substitute.
[numthreads(64, 1, 1)]
void InternalBoundsCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint internalIndex = dispatchThreadId.x;
    if (gConfig.primitiveCount <= 1u || internalIndex >= gConfig.primitiveCount - 1u ||
        gDepths[internalIndex] != gConfig.currentDepth)
    {
        return;
    }
    SoftwareNodeRecord node = gNodes[internalIndex];
    if (node.links.y != 0u || node.links.x >= gConfig.nodeCount ||
        node.links.z >= gConfig.nodeCount)
    {
        InterlockedAdd(gInvalidHierarchyCounter[0u], 1u);
        return;
    }
    const SoftwareNodeRecord left = gNodes[node.links.x];
    const SoftwareNodeRecord right = gNodes[node.links.z];
    node.boundsMin = float4(min(left.boundsMin.xyz, right.boundsMin.xyz), 0.0f);
    node.boundsMax = float4(max(left.boundsMax.xyz, right.boundsMax.xyz), 0.0f);
    gNodes[internalIndex] = node;
}
