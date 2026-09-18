#include "software_common.hlsli"
#include "../include/contracts/GpuRecordsAbiV1.hlsli"
#include "../include/contracts/RayHitAbiV0.hlsli"
#include "../include/contracts/SceneAbiV0.hlsli"

static const uint SOFTWARE_V1_STACK_CAPACITY = 64u;
static const uint COUNTER_NODE_TESTS = 0u;
static const uint COUNTER_TRIANGLE_TESTS = 1u;
static const uint COUNTER_STACK_OVERFLOW = 2u;
static const uint COUNTER_INVALID_RAY = 3u;
static const uint COUNTER_INVALID_HIT = 4u;
static const uint COUNTER_MAX_STACK = 5u;
static const uint COUNTER_LEAF_VISITS = 6u;
static const uint COUNTER_LEAF_PRIMITIVES = 7u;
static const uint COUNTER_MAX_LEAF_OCCUPANCY = 8u;
static const uint TRAVERSAL_MODE_FLATTENED_SAH = 0u;
static const uint TRAVERSAL_MODE_CANONICAL_LINEAR = 1u;

struct CanonicalTraversalTriangle
{
    float4 positions[3];
    float4 normals[3];
    float4 texcoords[3];
    uint4 identity;
    uint4 metadata;
};

struct SoftwareTracePushConstantsV1
{
    uint nodeCount;
    uint triangleCount;
    uint rayCount;
    uint queryMode;
    uint rayOffset;
    uint hitOffset;
    uint stackCapacity;
    uint alphaAtlasLayerCount;
    uint alphaSamplerId;
    uint reserved;
};

[[vk::binding(0, 1)]] ConstantBuffer<GpuSceneConstantsV0> gSceneConstantsV1;
[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gMaterialsV1;
[[vk::binding(0, 2)]] StructuredBuffer<SoftwareNodeRecord> gNodesV1;
[[vk::binding(1, 2)]] StructuredBuffer<GpuRayQueueRecordV1> gRaysV1;
[[vk::binding(2, 2)]] RWStructuredBuffer<GpuHitQueueRecordV1> gHitsV1;
[[vk::binding(3, 2)]] StructuredBuffer<CanonicalTraversalTriangle> gTrianglesV1;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> gCountersV1;
[[vk::binding(8, 2)]] Texture2DArray<float4> gAlphaAtlasV1;
[[vk::binding(9, 2)]] SamplerState gAlphaSamplerV1;
[[vk::push_constant]] SoftwareTracePushConstantsV1 gTraceV1;

void CounterAddV1(const uint counterIndex, const uint increment)
{
    uint observed = gCountersV1[counterIndex];
    while (observed != 0xffffffffu)
    {
        const uint desired = increment > 0xffffffffu - observed
            ? 0xffffffffu : observed + increment;
        uint original = 0u;
        InterlockedCompareExchange(gCountersV1[counterIndex], observed, desired, original);
        if (original == observed) return;
        observed = original;
    }
}

bool IsRayValidV1(const GpuRayQueueRecordV1 ray)
{
    const float directionLengthSquared = dot(ray.directionTMax.xyz, ray.directionTMax.xyz);
    return all(isfinite(ray.originTMin)) && all(isfinite(ray.directionTMax))
        && isfinite(directionLengthSquared)
        && abs(directionLengthSquared - 1.0f) <= 1.0e-4f
        && ray.originTMin.w >= 0.0f && ray.originTMin.w < ray.directionTMax.w
        && (ray.identity.w & ~0xffu) == 0u;
}

GpuHitQueueRecordV1 MakeTerminalV1(
    const GpuRayQueueRecordV1 ray,
    const uint kind)
{
    GpuHitQueueRecordV1 hit = (GpuHitQueueRecordV1)0;
    hit.positionT.w = ray.directionTMax.w;
    hit.ids = uint4(SOFTWARE_INVALID_INDEX, SOFTWARE_INVALID_INDEX,
        SOFTWARE_INVALID_INDEX, SOFTWARE_INVALID_INDEX);
    hit.metadata = uint4(ray.identity.x, kind, kHitFlagNoneV0, ray.identity.y);
    return hit;
}

// Tests one canonical triangle and applies the exact same visibility,
// alpha-mask, closest-hit, and any-hit rules for both traversal modes.  The
// caller owns only candidate enumeration (linear stream versus BVH leaves).
bool EvaluateTriangleV1(
    const SoftwareRayRecord ray,
    const uint triangleIndex,
    inout bool hasHit,
    inout float bestDistance,
    inout float bestU,
    inout float bestV,
    inout bool bestFrontFace,
    inout uint bestTriangle,
    out bool invalidCandidate)
{
    invalidCandidate = false;
    CounterAddV1(COUNTER_TRIANGLE_TESTS, 1u);
    const CanonicalTraversalTriangle candidateTriangle = gTrianglesV1[triangleIndex];
    SoftwarePrimitiveRecord primitive;
    primitive.v0 = candidateTriangle.positions[0];
    primitive.v1 = candidateTriangle.positions[1];
    primitive.v2 = candidateTriangle.positions[2];
    primitive.identity = uint4(candidateTriangle.metadata.z, 0u, 0u, 0u);
    float distance = 0.0f;
    float baryU = 0.0f;
    float baryV = 0.0f;
    bool frontFace = false;
    if (!SoftwareIntersectTriangle(ray, primitive, bestDistance, hasHit,
        distance, baryU, baryV, frontFace))
    {
        return false;
    }
    const bool doubleSided =
        (candidateTriangle.metadata.x & kGeometryFlagDoubleSidedV0) != 0u
        || (candidateTriangle.metadata.y & kMaterialFlagDoubleSidedV0) != 0u;
    if (!frontFace && !doubleSided)
    {
        return false;
    }
    const bool alphaMasked =
        (candidateTriangle.metadata.x & kGeometryFlagAlphaMaskV0) != 0u
        || (candidateTriangle.metadata.y & kMaterialFlagAlphaMaskV0) != 0u;
    if (alphaMasked)
    {
        if (candidateTriangle.identity.w >= gSceneConstantsV1.counts1.x)
        {
            invalidCandidate = true;
            return false;
        }
        const GpuMaterialV0 material = gMaterialsV1[candidateTriangle.identity.w];
        const uint alphaLayer = material.textureImageIndices.x;
        const uint samplerId = material.textureSamplerIndices.x;
        float alpha = material.baseColorFactor.w;
        bool mappingValid = true;
        if (alphaLayer == SOFTWARE_INVALID_INDEX)
        {
            mappingValid = samplerId == SOFTWARE_INVALID_INDEX;
        }
        else if (alphaLayer >= gTraceV1.alphaAtlasLayerCount ||
            samplerId != gTraceV1.alphaSamplerId)
        {
            mappingValid = false;
        }
        else
        {
            const float weight0 = 1.0f - baryU - baryV;
            const float2 uv = weight0 * candidateTriangle.texcoords[0].xy +
                baryU * candidateTriangle.texcoords[1].xy +
                baryV * candidateTriangle.texcoords[2].xy;
            alpha *= gAlphaAtlasV1.SampleLevel(
                gAlphaSamplerV1, float3(uv, float(alphaLayer)), 0.0f).a;
        }
        if (!mappingValid)
        {
            invalidCandidate = true;
            return false;
        }
        if (alpha < material.surfaceParams.w)
        {
            return false;
        }
    }

    bool replaceBest = !hasHit || distance < bestDistance;
    if (!replaceBest && distance == bestDistance)
    {
        replaceBest = candidateTriangle.metadata.z
            < gTrianglesV1[bestTriangle].metadata.z;
    }
    if (gTraceV1.queryMode == SOFTWARE_QUERY_ANY || replaceBest)
    {
        hasHit = true;
        bestDistance = distance;
        bestU = baryU;
        bestV = baryV;
        bestFrontFace = frontFace;
        bestTriangle = triangleIndex;
    }
    return true;
}

[numthreads(64, 1, 1)]
void SoftwareTraceV1(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint localRayIndex = dispatchThreadId.x;
    if (localRayIndex >= gTraceV1.rayCount) return;
    const uint rayIndex = gTraceV1.rayOffset + localRayIndex;
    const uint hitIndex = gTraceV1.hitOffset + localRayIndex;
    const GpuRayQueueRecordV1 queueRay = gRaysV1[rayIndex];
    const bool flattenedSah = gTraceV1.reserved == TRAVERSAL_MODE_FLATTENED_SAH;
    const bool canonicalLinear = gTraceV1.reserved == TRAVERSAL_MODE_CANONICAL_LINEAR;
    if (!IsRayValidV1(queueRay) || (flattenedSah
        && (gTraceV1.stackCapacity == 0u
            || gTraceV1.stackCapacity > SOFTWARE_V1_STACK_CAPACITY)))
    {
        CounterAddV1(COUNTER_INVALID_RAY, 1u);
        gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
        return;
    }
    if (!flattenedSah && !canonicalLinear)
    {
        CounterAddV1(COUNTER_INVALID_HIT, 1u);
        gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
        return;
    }
    // Flattened Wave 2 instances use the same all-bits instance mask as the
    // hardware gate.  A zero ray mask therefore has an empty visibility set
    // and must miss without entering traversal.
    if (queueRay.identity.w == 0u)
    {
        gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindMissV0);
        return;
    }
    if (flattenedSah && gTraceV1.nodeCount == 0u)
    {
        gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindMissV0);
        return;
    }

    SoftwareRayRecord ray;
    ray.originTMin = queueRay.originTMin;
    ray.directionTMax = queueRay.directionTMax;
    ray.query = uint4(queueRay.identity.x, gTraceV1.queryMode, queueRay.identity.w, 0u);
    bool hasHit = false;
    float bestDistance = ray.directionTMax.w;
    float bestU = 0.0f;
    float bestV = 0.0f;
    bool bestFrontFace = false;
    uint bestTriangle = SOFTWARE_INVALID_INDEX;

    if (canonicalLinear)
    {
        // This is the CanonicalLinearGpu/canonical-linear backend: enumerate
        // the original canonical stream directly.  No flattened ordering,
        // node bounds, stack, or acceleration-structure state is consulted.
        for (uint triangleIndex = 0u;
            triangleIndex < gTraceV1.triangleCount; ++triangleIndex)
        {
            bool invalidCandidate = false;
            const bool accepted = EvaluateTriangleV1(
                ray, triangleIndex, hasHit, bestDistance, bestU, bestV,
                bestFrontFace, bestTriangle, invalidCandidate);
            if (invalidCandidate)
            {
                CounterAddV1(COUNTER_INVALID_HIT, 1u);
                gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                return;
            }
            if (accepted && gTraceV1.queryMode == SOFTWARE_QUERY_ANY)
            {
                break;
            }
        }
    }
    else
    {
        uint stackNodes[SOFTWARE_V1_STACK_CAPACITY];
        float stackNear[SOFTWARE_V1_STACK_CAPACITY];
        uint stackSize = 0u;
        float rootNear = 0.0f;
        CounterAddV1(COUNTER_NODE_TESTS, 1u);
        if (!SoftwareIntersectBounds(ray, gNodesV1[0u].boundsMin.xyz,
            gNodesV1[0u].boundsMax.xyz, ray.directionTMax.w, false, rootNear))
        {
            gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindMissV0);
            return;
        }
        stackNodes[stackSize] = 0u;
        stackNear[stackSize++] = rootNear;
        InterlockedMax(gCountersV1[COUNTER_MAX_STACK], stackSize);

        uint visitedNodes = 0u;
        while (stackSize != 0u)
        {
            --stackSize;
            if (++visitedNodes > gTraceV1.nodeCount)
            {
                CounterAddV1(COUNTER_INVALID_HIT, 1u);
                gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                return;
            }
            const uint nodeIndex = stackNodes[stackSize];
            const float nodeNear = stackNear[stackSize];
            if (hasHit ? nodeNear > bestDistance : nodeNear >= bestDistance) continue;
            if (nodeIndex >= gTraceV1.nodeCount)
            {
                CounterAddV1(COUNTER_INVALID_HIT, 1u);
                gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                return;
            }
            const SoftwareNodeRecord node = gNodesV1[nodeIndex];
            if (node.links.y != 0u)
            {
                CounterAddV1(COUNTER_LEAF_VISITS, 1u);
                CounterAddV1(COUNTER_LEAF_PRIMITIVES, node.links.y);
                InterlockedMax(gCountersV1[COUNTER_MAX_LEAF_OCCUPANCY], node.links.y);
                if (node.links.x > gTraceV1.triangleCount
                    || node.links.y > gTraceV1.triangleCount - node.links.x)
                {
                    CounterAddV1(COUNTER_INVALID_HIT, 1u);
                    gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                    return;
                }
                for (uint offset = 0u; offset < node.links.y; ++offset)
                {
                    const uint triangleIndex = node.links.x + offset;
                    bool invalidCandidate = false;
                    const bool accepted = EvaluateTriangleV1(
                        ray, triangleIndex, hasHit, bestDistance, bestU, bestV,
                        bestFrontFace, bestTriangle, invalidCandidate);
                    if (invalidCandidate)
                    {
                        CounterAddV1(COUNTER_INVALID_HIT, 1u);
                        gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                        return;
                    }
                    if (accepted && gTraceV1.queryMode == SOFTWARE_QUERY_ANY)
                    {
                        break;
                    }
                }
                if (hasHit && gTraceV1.queryMode == SOFTWARE_QUERY_ANY) break;
                continue;
            }
            if (node.links.x >= gTraceV1.nodeCount || node.links.z >= gTraceV1.nodeCount)
            {
                CounterAddV1(COUNTER_INVALID_HIT, 1u);
                gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                return;
            }
            float leftNear = 0.0f;
            float rightNear = 0.0f;
            CounterAddV1(COUNTER_NODE_TESTS, 2u);
            const bool hitLeft = SoftwareIntersectBounds(ray,
                gNodesV1[node.links.x].boundsMin.xyz, gNodesV1[node.links.x].boundsMax.xyz,
                bestDistance, hasHit, leftNear);
            const bool hitRight = SoftwareIntersectBounds(ray,
                gNodesV1[node.links.z].boundsMin.xyz, gNodesV1[node.links.z].boundsMax.xyz,
                bestDistance, hasHit, rightNear);
            const uint required = (hitLeft ? 1u : 0u) + (hitRight ? 1u : 0u);
            if (stackSize + required > gTraceV1.stackCapacity)
            {
                CounterAddV1(COUNTER_STACK_OVERFLOW, 1u);
                gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindInvalidV0);
                return;
            }
            if (hitLeft && hitRight)
            {
                const bool leftFirst = leftNear < rightNear
                    || (!(rightNear < leftNear) && node.links.x < node.links.z);
                stackNodes[stackSize] = leftFirst ? node.links.z : node.links.x;
                stackNear[stackSize++] = leftFirst ? rightNear : leftNear;
                stackNodes[stackSize] = leftFirst ? node.links.x : node.links.z;
                stackNear[stackSize++] = leftFirst ? leftNear : rightNear;
            }
            else if (hitLeft)
            {
                stackNodes[stackSize] = node.links.x;
                stackNear[stackSize++] = leftNear;
            }
            else if (hitRight)
            {
                stackNodes[stackSize] = node.links.z;
                stackNear[stackSize++] = rightNear;
            }
            InterlockedMax(gCountersV1[COUNTER_MAX_STACK], stackSize);
        }
    }

    if (!hasHit)
    {
        gHitsV1[hitIndex] = MakeTerminalV1(queueRay, kHitKindMissV0);
        return;
    }
    const CanonicalTraversalTriangle best = gTrianglesV1[bestTriangle];
    const float weight0 = 1.0f - bestU - bestV;
    float3 geometricNormal = normalize(cross(
        best.positions[1].xyz - best.positions[0].xyz,
        best.positions[2].xyz - best.positions[0].xyz));
    float3 shadingNormal = normalize(weight0 * best.normals[0].xyz
        + bestU * best.normals[1].xyz + bestV * best.normals[2].xyz);
    if (!bestFrontFace)
    {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }
    GpuHitQueueRecordV1 hit = (GpuHitQueueRecordV1)0;
    hit.positionT = float4(queueRay.originTMin.xyz
        + queueRay.directionTMax.xyz * bestDistance, bestDistance);
    hit.geometricNormalBaryU = float4(geometricNormal, bestU);
    hit.shadingNormalBaryV = float4(shadingNormal, bestV);
    hit.ids = best.identity;
    uint hitFlags = bestFrontFace ? kHitFlagFrontFaceV0 : kHitFlagNoneV0;
    const bool bestAlphaMasked =
        (best.metadata.x & kGeometryFlagAlphaMaskV0) != 0u
        || (best.metadata.y & kMaterialFlagAlphaMaskV0) != 0u;
    if (bestAlphaMasked)
    {
        hitFlags |= kHitFlagAlphaTestedV0;
    }
    hit.metadata = uint4(queueRay.identity.x, kHitKindTriangleV0,
        hitFlags, queueRay.identity.y);
    gHitsV1[hitIndex] = hit;
}
