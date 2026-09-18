#include "software_common.hlsli"

struct CanonicalTraversalTriangle
{
    float4 positions[3];
    float4 normals[3];
    float4 texcoords[3];
    uint4 identity;
    uint4 metadata;
};

struct SoftwareCanonicalReorderConfig
{
    uint primitiveCount;
    uint3 reserved;
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwareMortonPair> gSortedPairs;
[[vk::binding(1, 2)]] StructuredBuffer<CanonicalTraversalTriangle> gSourceTriangles;
[[vk::binding(2, 2)]] RWStructuredBuffer<CanonicalTraversalTriangle> gSortedTriangles;
[[vk::binding(3, 2)]] RWStructuredBuffer<uint> gInvalidHierarchyCounter;
[[vk::binding(4, 2)]] ConstantBuffer<SoftwareCanonicalReorderConfig> gConfig;

[numthreads(64, 1, 1)]
void ReorderCanonicalTrianglesV1(const uint3 dispatchThreadId : SV_DispatchThreadID)
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
    gSortedTriangles[sortedIndex] = gSourceTriangles[sourceIndex];
}
