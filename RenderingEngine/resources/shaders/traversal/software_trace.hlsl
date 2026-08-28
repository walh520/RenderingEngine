#include "software_common.hlsli"

static const uint SOFTWARE_STACK_CAPACITY = 64u;
static const uint COUNTER_NODE_TESTS = 0u;
static const uint COUNTER_TRIANGLE_TESTS = 1u;
static const uint COUNTER_STACK_OVERFLOW = 2u;
static const uint COUNTER_INVALID_RAY = 3u;
static const uint COUNTER_INVALID_HIT = 4u;
static const uint COUNTER_MAX_STACK = 5u;
static const uint COUNTER_LEAF_VISITS = 6u;
static const uint COUNTER_LEAF_PRIMITIVES = 7u;
static const uint COUNTER_MAX_LEAF_OCCUPANCY = 8u;
static const uint COUNTER_COUNT = 9u;

struct SoftwareTraceConfig
{
    uint nodeCount;
    uint primitiveCount;
    uint rayCount;
    uint stackCapacity;
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwareNodeRecord> gNodes;
[[vk::binding(1, 2)]] StructuredBuffer<SoftwarePrimitiveRecord> gPrimitives;
[[vk::binding(2, 2)]] StructuredBuffer<SoftwareRayRecord> gRays;
[[vk::binding(3, 2)]] RWStructuredBuffer<SoftwareHitRecord> gHits;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> gCounters;
[[vk::binding(5, 2)]] ConstantBuffer<SoftwareTraceConfig> gConfig;

// Additive uint32 readback counters saturate instead of silently wrapping.
void SoftwareCounterAdd(const uint counterIndex, const uint increment)
{
    uint observed = gCounters[counterIndex];
    while (observed != 0xffffffffu)
    {
        const uint desired = increment > 0xffffffffu - observed
                                 ? 0xffffffffu
                                 : observed + increment;
        uint original = 0u;
        InterlockedCompareExchange(gCounters[counterIndex], observed, desired, original);
        if (original == observed)
        {
            return;
        }
        observed = original;
    }
}

[numthreads(9, 1, 1)]
void ResetCountersCS(const uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupThreadId.x < COUNTER_COUNT)
    {
        gCounters[groupThreadId.x] = 0u;
    }
}

void SoftwareWriteMiss(const uint rayIndex, const SoftwareRayRecord ray, const uint status)
{
    SoftwareHitRecord result;
    result.tBary = float4(ray.directionTMax.w, 0.0f, 0.0f, 0.0f);
    result.identity = uint4(SOFTWARE_INVALID_INDEX, ray.query.x, status, 0u);
    gHits[rayIndex] = result;
}

[numthreads(64, 1, 1)]
void CSMain(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint rayIndex = dispatchThreadId.x;
    if (rayIndex >= gConfig.rayCount)
    {
        return;
    }
    const SoftwareRayRecord ray = gRays[rayIndex];
    if (!SoftwareRayIsValid(ray) || gConfig.stackCapacity == 0u ||
        gConfig.stackCapacity > SOFTWARE_STACK_CAPACITY)
    {
        SoftwareCounterAdd(COUNTER_INVALID_RAY, 1u);
        SoftwareWriteMiss(rayIndex, ray, 1u);
        return;
    }
    if (gConfig.nodeCount == 0u)
    {
        SoftwareWriteMiss(rayIndex, ray, 0u);
        return;
    }

    uint stackNodes[SOFTWARE_STACK_CAPACITY];
    float stackNear[SOFTWARE_STACK_CAPACITY];
    uint stackSize = 0u;
    float rootNear = 0.0f;
    SoftwareCounterAdd(COUNTER_NODE_TESTS, 1u);
    if (!SoftwareIntersectBounds(
            ray,
            gNodes[0u].boundsMin.xyz,
            gNodes[0u].boundsMax.xyz,
            ray.directionTMax.w,
            false,
            rootNear))
    {
        SoftwareWriteMiss(rayIndex, ray, 0u);
        return;
    }
    stackNodes[stackSize] = 0u;
    stackNear[stackSize] = rootNear;
    ++stackSize;
    InterlockedMax(gCounters[COUNTER_MAX_STACK], stackSize);

    bool hasHit = false;
    float bestDistance = ray.directionTMax.w;
    float bestU = 0.0f;
    float bestV = 0.0f;
    bool bestFrontFace = false;
    uint bestPrimitive = SOFTWARE_INVALID_INDEX;
    uint visitedNodes = 0u;

    while (stackSize != 0u)
    {
        --stackSize;
        ++visitedNodes;
        if (visitedNodes > gConfig.nodeCount)
        {
            SoftwareCounterAdd(COUNTER_INVALID_HIT, 1u);
            SoftwareWriteMiss(rayIndex, ray, 3u);
            return;
        }
        const uint nodeIndex = stackNodes[stackSize];
        const float nodeNear = stackNear[stackSize];
        if (hasHit ? nodeNear > bestDistance : nodeNear >= bestDistance)
        {
            continue;
        }
        if (nodeIndex >= gConfig.nodeCount)
        {
            SoftwareCounterAdd(COUNTER_INVALID_HIT, 1u);
            SoftwareWriteMiss(rayIndex, ray, 3u);
            return;
        }
        const SoftwareNodeRecord node = gNodes[nodeIndex];
        if (node.links.y != 0u)
        {
            SoftwareCounterAdd(COUNTER_LEAF_VISITS, 1u);
            SoftwareCounterAdd(COUNTER_LEAF_PRIMITIVES, node.links.y);
            InterlockedMax(gCounters[COUNTER_MAX_LEAF_OCCUPANCY], node.links.y);
            if (node.links.x > gConfig.primitiveCount ||
                node.links.y > gConfig.primitiveCount - node.links.x)
            {
                SoftwareCounterAdd(COUNTER_INVALID_HIT, 1u);
                SoftwareWriteMiss(rayIndex, ray, 3u);
                return;
            }
            for (uint offset = 0u; offset < node.links.y; ++offset)
            {
                SoftwareCounterAdd(COUNTER_TRIANGLE_TESTS, 1u);
                const SoftwarePrimitiveRecord primitive = gPrimitives[node.links.x + offset];
                float distance = 0.0f;
                float baryU = 0.0f;
                float baryV = 0.0f;
                bool frontFace = false;
                if (!SoftwareIntersectTriangle(
                        ray,
                        primitive,
                        bestDistance,
                        hasHit,
                        distance,
                        baryU,
                        baryV,
                        frontFace))
                {
                    continue;
                }
                if (ray.query.y == SOFTWARE_QUERY_ANY)
                {
                    SoftwareHitRecord anyHit;
                    anyHit.tBary = float4(distance, baryU, baryV, frontFace ? 1.0f : 0.0f);
                    anyHit.identity = uint4(primitive.identity.x, ray.query.x, 0u, 0u);
                    gHits[rayIndex] = anyHit;
                    return;
                }
                if (!hasHit || distance < bestDistance ||
                    (distance == bestDistance && primitive.identity.x < bestPrimitive))
                {
                    hasHit = true;
                    bestDistance = distance;
                    bestU = baryU;
                    bestV = baryV;
                    bestFrontFace = frontFace;
                    bestPrimitive = primitive.identity.x;
                }
            }
            continue;
        }

        if (node.links.x >= gConfig.nodeCount || node.links.z >= gConfig.nodeCount)
        {
            SoftwareCounterAdd(COUNTER_INVALID_HIT, 1u);
            SoftwareWriteMiss(rayIndex, ray, 3u);
            return;
        }
        float leftNear = 0.0f;
        float rightNear = 0.0f;
        SoftwareCounterAdd(COUNTER_NODE_TESTS, 2u);
        const bool hitLeft = SoftwareIntersectBounds(
            ray,
            gNodes[node.links.x].boundsMin.xyz,
            gNodes[node.links.x].boundsMax.xyz,
            bestDistance,
            hasHit,
            leftNear);
        const bool hitRight = SoftwareIntersectBounds(
            ray,
            gNodes[node.links.z].boundsMin.xyz,
            gNodes[node.links.z].boundsMax.xyz,
            bestDistance,
            hasHit,
            rightNear);
        const uint requiredEntries = (hitLeft ? 1u : 0u) + (hitRight ? 1u : 0u);
        if (stackSize + requiredEntries > gConfig.stackCapacity)
        {
            SoftwareCounterAdd(COUNTER_STACK_OVERFLOW, 1u);
            SoftwareWriteMiss(rayIndex, ray, 2u);
            return;
        }
        if (hitLeft && hitRight)
        {
            const bool leftFirst = leftNear < rightNear ||
                                   (!(rightNear < leftNear) && node.links.x < node.links.z);
            stackNodes[stackSize] = leftFirst ? node.links.z : node.links.x;
            stackNear[stackSize] = leftFirst ? rightNear : leftNear;
            ++stackSize;
            stackNodes[stackSize] = leftFirst ? node.links.x : node.links.z;
            stackNear[stackSize] = leftFirst ? leftNear : rightNear;
            ++stackSize;
        }
        else if (hitLeft)
        {
            stackNodes[stackSize] = node.links.x;
            stackNear[stackSize] = leftNear;
            ++stackSize;
        }
        else if (hitRight)
        {
            stackNodes[stackSize] = node.links.z;
            stackNear[stackSize] = rightNear;
            ++stackSize;
        }
        InterlockedMax(gCounters[COUNTER_MAX_STACK], stackSize);
    }

    if (!hasHit)
    {
        SoftwareWriteMiss(rayIndex, ray, 0u);
        return;
    }
    SoftwareHitRecord closestHit;
    closestHit.tBary = float4(bestDistance, bestU, bestV, bestFrontFace ? 1.0f : 0.0f);
    closestHit.identity = uint4(bestPrimitive, ray.query.x, 0u, 0u);
    gHits[rayIndex] = closestHit;
}
