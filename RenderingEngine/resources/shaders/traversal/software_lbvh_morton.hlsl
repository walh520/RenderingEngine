#include "software_common.hlsli"

struct SoftwareMortonConfig
{
    float4 sceneBoundsMin;
    float4 sceneBoundsMax;
    uint primitiveCount;
    uint3 reserved;
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwarePrimitiveRecord> gPrimitives;
[[vk::binding(1, 2)]] RWStructuredBuffer<SoftwareMortonPair> gMortonPairs;
[[vk::binding(2, 2)]] ConstantBuffer<SoftwareMortonConfig> gConfig;
[[vk::binding(3, 2)]] RWStructuredBuffer<uint> gInvalidMortonCounter;

[numthreads(1, 1, 1)]
void ResetMortonValidationCS(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x == 0u)
    {
        gInvalidMortonCounter[0u] = 0u;
    }
}

[numthreads(64, 1, 1)]
void CSMain(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gConfig.primitiveCount)
    {
        return;
    }
    const SoftwarePrimitiveRecord primitive = gPrimitives[index];
    const float3 edge0 = primitive.v1.xyz - primitive.v0.xyz;
    const float3 edge1 = primitive.v2.xyz - primitive.v0.xyz;
    const float3 areaVector = cross(edge0, edge1);
    const float areaSquared = dot(areaVector, areaVector);
    const float edgeScale = max(dot(edge0, edge0), dot(edge1, edge1));
    float3 primitiveBoundsMin;
    float3 primitiveBoundsMax;
    SoftwarePrimitiveBounds(primitive, primitiveBoundsMin, primitiveBoundsMax);
    const float3 centerPoint = float3(
        SoftwareFiniteMidpoint(primitiveBoundsMin.x, primitiveBoundsMax.x),
        SoftwareFiniteMidpoint(primitiveBoundsMin.y, primitiveBoundsMax.y),
        SoftwareFiniteMidpoint(primitiveBoundsMin.z, primitiveBoundsMax.z));
    SoftwareMortonPair pair;
    if (primitive.identity.x == SOFTWARE_INVALID_INDEX ||
        !all(isfinite(primitive.v0.xyz)) || !all(isfinite(primitive.v1.xyz)) ||
        !all(isfinite(primitive.v2.xyz)) || !isfinite(areaSquared) ||
        !(areaSquared > edgeScale * edgeScale * 1.0e-14f) ||
        !all(isfinite(centerPoint)) || !all(isfinite(gConfig.sceneBoundsMin.xyz)) ||
        !all(isfinite(gConfig.sceneBoundsMax.xyz)) ||
        any(gConfig.sceneBoundsMin.xyz > gConfig.sceneBoundsMax.xyz))
    {
        InterlockedAdd(gInvalidMortonCounter[0u], 1u);
        pair.code = 0u;
        pair.stablePrimitiveId = primitive.identity.x;
        pair.sourceIndex = index;
        pair.reserved = 1u;
        gMortonPairs[index] = pair;
        return;
    }
    pair.code = SoftwareMortonCode(
        centerPoint,
        gConfig.sceneBoundsMin.xyz,
        gConfig.sceneBoundsMax.xyz);
    pair.stablePrimitiveId = primitive.identity.x;
    pair.sourceIndex = index;
    pair.reserved = 0u;
    gMortonPairs[index] = pair;
}
