#ifndef RENDERING_ENGINE_PBR_SOFTWARE_TRAVERSAL_HLSLI
#define RENDERING_ENGINE_PBR_SOFTWARE_TRAVERSAL_HLSLI

#include "../traversal/software_common.hlsli"
#include "../include/contracts/SceneAbiV0.hlsli"
#include "pbr_traversal_resources.hlsli"

struct PbrCanonicalTraversalTriangleL6
{
    float4 positions[3];
    float4 normals[3];
    float4 texcoords[3];
    uint4 identity; // stable instance, primitive, geometry, material.
    uint4 metadata; // geometry flags, material flags, traversal ID, instance flags.
};

[[vk::binding(0, 2)]] StructuredBuffer<SoftwareNodeRecord> gPbrSoftwareNodesL6;
[[vk::binding(3, 2)]] StructuredBuffer<PbrCanonicalTraversalTriangleL6>
    gPbrSoftwareTrianglesL6;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> gPbrSoftwareCountersL6;
[[vk::binding(0, 1)]] ConstantBuffer<GpuSceneConstantsV0> gPbrSceneConstantsL6;
[[vk::binding(4, 1)]] StructuredBuffer<GpuInstanceV0> gPbrInstancesL6;
[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gPbrCanonicalMaterialsL6;
[[vk::binding(8, 2)]] Texture2DArray<float4> gPbrAlphaAtlasL6;
[[vk::binding(9, 2)]] SamplerState gPbrAlphaSamplerL6;

static const uint PBR_L4_COUNTER_NODE_TESTS = 0u;
static const uint PBR_L4_COUNTER_TRIANGLE_TESTS = 1u;
static const uint PBR_L4_COUNTER_STACK_OVERFLOW = 2u;
static const uint PBR_L4_COUNTER_INVALID_RAY = 3u;
static const uint PBR_L4_COUNTER_INVALID_HIT = 4u;
static const uint PBR_L4_COUNTER_MAX_STACK = 5u;
static const uint PBR_L4_COUNTER_LEAF_VISITS = 6u;
static const uint PBR_L4_COUNTER_LEAF_PRIMITIVES = 7u;
static const uint PBR_L4_COUNTER_MAX_LEAF_OCCUPANCY = 8u;

// F3 profiling variants use native device atomics. This deliberately avoids
// the former saturating compare/exchange loop in the production Megakernel.

void PbrSoftwareCounterAddL6(const uint counterIndex, const uint increment)
{
#if defined(PBR_L6_DISABLE_PROFILER_COUNTERS)
    return;
#else
    uint previous = 0u;
    InterlockedAdd(gPbrSoftwareCountersL6[counterIndex], increment, previous);
#endif
}

void PbrSoftwareCounterMaxL6(const uint counterIndex, const uint value)
{
#if defined(PBR_L6_DISABLE_PROFILER_COUNTERS)
    return;
#else
    uint previous = 0u;
    InterlockedMax(gPbrSoftwareCountersL6[counterIndex], value, previous);
#endif
}

void PbrInitializeTraversalHitL6(float tMaximum, out PbrHitL6 hit)
{
    hit.t = tMaximum;
    hit.position = 0.0f;
    hit.geometricNormal = 0.0f;
    hit.shadingNormal = 0.0f;
    hit.materialIndex = PBR_L6_INVALID_INDEX;
    hit.instanceId = PBR_L6_INVALID_INDEX;
    hit.primitiveId = PBR_L6_INVALID_INDEX;
    hit.emitterLightIndex = PBR_L6_INVALID_INDEX;
    hit.frontFace = 0u;
}

bool PbrReturnInvalidSoftwareTraversalL6(float tMaximum, out PbrHitL6 hit)
{
    PbrSoftwareCounterAddL6(PBR_L4_COUNTER_INVALID_HIT, 1u);
    PbrInitializeTraversalHitL6(tMaximum, hit);
    // A true result with an invalid material is the fail-closed error encoding
    // understood by the Megakernel's existing invalid-material counter.
    return true;
}

void PbrPopulateSoftwareTraversalHitL6(
    PbrRayL6 inputRay,
    float bestDistance,
    float bestU,
    float bestV,
    bool bestFrontFace,
    uint bestTriangleIndex,
    out PbrHitL6 hit)
{
    const PbrCanonicalTraversalTriangleL6 best =
        gPbrSoftwareTrianglesL6[bestTriangleIndex];
    const float weight0 = 1.0f - bestU - bestV;
    float3 geometricNormal = normalize(cross(
        best.positions[1].xyz - best.positions[0].xyz,
        best.positions[2].xyz - best.positions[0].xyz));
    float3 shadingNormal = normalize(
        weight0 * best.normals[0].xyz +
        bestU * best.normals[1].xyz +
        bestV * best.normals[2].xyz);
    if (!bestFrontFace)
    {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }
    hit.t = bestDistance;
    hit.position = inputRay.origin + inputRay.direction * bestDistance;
    hit.geometricNormal = geometricNormal;
    hit.shadingNormal = shadingNormal;
    hit.materialIndex = best.identity.w;
    hit.instanceId = best.identity.x;
    hit.primitiveId = best.identity.y;
    hit.emitterLightIndex = PbrResolveEmitterLightL6(
        hit.instanceId,
        hit.primitiveId,
        PBR_L6_INVALID_INDEX);
    hit.frontFace = bestFrontFace ? 1u : 0u;
}

[noinline]
bool PbrTraceCanonicalLinearIgnoringL6(
    PbrRayL6 inputRay,
    float tMinimum,
    float tMaximum,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId,
    bool shadowQuery,
    out PbrHitL6 hit)
{
    PbrInitializeTraversalHitL6(tMaximum, hit);
    const uint triangleCount = gPbrFrameL6.traversal.z;
    if (triangleCount == 0u)
    {
        return false;
    }

    SoftwareRayRecord ray;
    ray.originTMin = float4(inputRay.origin, tMinimum);
    ray.directionTMax = float4(inputRay.direction, tMaximum);
    ray.query = uint4(0u, SOFTWARE_QUERY_CLOSEST, 0xffu, 0u);
    if (!SoftwareRayIsValid(ray))
    {
        PbrSoftwareCounterAddL6(PBR_L4_COUNTER_INVALID_RAY, 1u);
        PbrInitializeTraversalHitL6(tMaximum, hit);
        return true;
    }

    bool found = false;
    float bestDistance = tMaximum;
    float bestU = 0.0f;
    float bestV = 0.0f;
    bool bestFrontFace = false;
    uint bestTriangleIndex = SOFTWARE_INVALID_INDEX;
    uint bestInstanceId = SOFTWARE_INVALID_INDEX;
    uint bestPrimitiveId = SOFTWARE_INVALID_INDEX;

    // This is the deliberately unaccelerated legacy traversal baseline.  It
    // consumes the same canonical triangle/material buffers as the flattened
    // SAH path, so changing only the backend does not rewrite any other axis.
    [loop]
    for (uint triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex)
    {
        const PbrCanonicalTraversalTriangleL6 candidate =
            gPbrSoftwareTrianglesL6[triangleIndex];
        if (candidate.identity.x == ignoredInstanceId &&
            candidate.identity.y == ignoredPrimitiveId)
        {
            continue;
        }
        if (shadowQuery &&
            (candidate.metadata.w & kInstanceFlagCastsShadowV0) == 0u)
        {
            continue;
        }

        PbrSoftwareCounterAddL6(PBR_L4_COUNTER_TRIANGLE_TESTS, 1u);
        SoftwarePrimitiveRecord primitive;
        primitive.v0 = candidate.positions[0];
        primitive.v1 = candidate.positions[1];
        primitive.v2 = candidate.positions[2];
        primitive.identity = uint4(candidate.metadata.z, 0u, 0u, 0u);
        float distance = 0.0f;
        float baryU = 0.0f;
        float baryV = 0.0f;
        bool frontFace = false;
        if (!SoftwareIntersectTriangle(
            ray,
            primitive,
            bestDistance,
            found,
            distance,
            baryU,
            baryV,
            frontFace))
        {
            continue;
        }

        const bool doubleSided =
            (candidate.metadata.x & kGeometryFlagDoubleSidedV0) != 0u ||
            (candidate.metadata.y & kMaterialFlagDoubleSidedV0) != 0u;
        if (!frontFace && !doubleSided)
        {
            continue;
        }

        const bool alphaMasked =
            (candidate.metadata.x & kGeometryFlagAlphaMaskV0) != 0u ||
            (candidate.metadata.y & kMaterialFlagAlphaMaskV0) != 0u;
        if (alphaMasked)
        {
            if (candidate.identity.w >= gPbrSceneConstantsL6.counts1.x)
            {
                return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
            }
            const GpuMaterialV0 material =
                gPbrCanonicalMaterialsL6[candidate.identity.w];
            const uint alphaLayer = material.textureImageIndices.x;
            const uint samplerId = material.textureSamplerIndices.x;
            float alpha = material.baseColorFactor.w;
            bool mappingValid = true;
            if (alphaLayer == PBR_L6_INVALID_INDEX)
            {
                mappingValid = samplerId == PBR_L6_INVALID_INDEX;
            }
            else if (alphaLayer >= gPbrFrameL6.traversal.w ||
                samplerId != gPbrFrameL6.output.y)
            {
                mappingValid = false;
            }
            else
            {
                const float weight0 = 1.0f - baryU - baryV;
                const float2 uv = weight0 * candidate.texcoords[0].xy +
                    baryU * candidate.texcoords[1].xy +
                    baryV * candidate.texcoords[2].xy;
                alpha *= gPbrAlphaAtlasL6.SampleLevel(
                    gPbrAlphaSamplerL6,
                    float3(uv, float(alphaLayer)), 0.0f).a;
            }
            if (!mappingValid)
            {
                return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
            }
            if (alpha < material.surfaceParams.w)
            {
                continue;
            }
        }

        if (shadowQuery)
        {
            return true;
        }
        const bool equalDistanceLowerKey = distance == bestDistance &&
            (candidate.identity.x < bestInstanceId ||
             (candidate.identity.x == bestInstanceId &&
              candidate.identity.y < bestPrimitiveId));
        if (!found || distance < bestDistance || equalDistanceLowerKey)
        {
            found = true;
            bestDistance = distance;
            bestU = baryU;
            bestV = baryV;
            bestFrontFace = frontFace;
            bestTriangleIndex = triangleIndex;
            bestInstanceId = candidate.identity.x;
            bestPrimitiveId = candidate.identity.y;
        }
    }

    if (!found)
    {
        return false;
    }
    PbrPopulateSoftwareTraversalHitL6(
        inputRay,
        bestDistance,
        bestU,
        bestV,
        bestFrontFace,
        bestTriangleIndex,
        hit);
    return true;
}

[noinline]
bool PbrTraceSoftwareIgnoringL6(
    PbrRayL6 inputRay,
    float tMinimum,
    float tMaximum,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId,
    bool shadowQuery,
    out PbrHitL6 hit)
{
    PbrInitializeTraversalHitL6(tMaximum, hit);
    const uint nodeCount = gPbrFrameL6.traversal.y;
    const uint triangleCount = gPbrFrameL6.traversal.z;
    if (gPbrFrameL6.traversal.x == PBR_L6_TRAVERSAL_CANONICAL_LINEAR)
    {
        return PbrTraceCanonicalLinearIgnoringL6(
            inputRay,
            tMinimum,
            tMaximum,
            ignoredInstanceId,
            ignoredPrimitiveId,
            shadowQuery,
            hit);
    }
    if (nodeCount == 0u || triangleCount == 0u)
    {
        return false;
    }

    SoftwareRayRecord ray;
    ray.originTMin = float4(inputRay.origin, tMinimum);
    ray.directionTMax = float4(inputRay.direction, tMaximum);
    ray.query = uint4(0u, SOFTWARE_QUERY_CLOSEST, 0xffu, 0u);
    if (!SoftwareRayIsValid(ray))
    {
        PbrSoftwareCounterAddL6(PBR_L4_COUNTER_INVALID_RAY, 1u);
        PbrInitializeTraversalHitL6(tMaximum, hit);
        return true;
    }

    bool found = false;
    float bestDistance = tMaximum;
    float bestU = 0.0f;
    float bestV = 0.0f;
    bool bestFrontFace = false;
    uint bestTriangleIndex = SOFTWARE_INVALID_INDEX;
    uint bestInstanceId = SOFTWARE_INVALID_INDEX;
    uint bestPrimitiveId = SOFTWARE_INVALID_INDEX;
    uint previousNode = SOFTWARE_INVALID_INDEX;
    uint nodeIndex = 0u;
    uint traversalSteps = 0u;
    const uint maximumTraversalSteps = nodeCount <= 0x55555555u
        ? nodeCount * 3u
        : 0xffffffffu;

    // The flattened record already publishes parent links in links.w. A
    // parent-pointer depth-first walk avoids a dynamically indexed private
    // stack inside the full Megakernel while retaining exact closest-hit
    // semantics. Each interior node is entered from its parent, left child,
    // and right child at most once.
    [loop]
    while (nodeIndex != SOFTWARE_INVALID_INDEX)
    {
        if (nodeIndex >= nodeCount || ++traversalSteps > maximumTraversalSteps)
        {
            return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
        }
        PbrSoftwareCounterAddL6(PBR_L4_COUNTER_NODE_TESTS, 1u);
        const SoftwareNodeRecord node = gPbrSoftwareNodesL6[nodeIndex];
        const uint parentNode = node.links.w;
        uint nextNode = SOFTWARE_INVALID_INDEX;
        if (previousNode == parentNode)
        {
            float nodeNear = 0.0f;
            const bool hitBounds = SoftwareIntersectBounds(
                ray,
                node.boundsMin.xyz,
                node.boundsMax.xyz,
                bestDistance,
                found,
                nodeNear);
            if (!hitBounds)
            {
                nextNode = parentNode;
            }
            else if (node.links.y != 0u)
            {
                if (node.links.x > triangleCount ||
                    node.links.y > triangleCount - node.links.x)
                {
                    return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
                }
                PbrSoftwareCounterAddL6(PBR_L4_COUNTER_LEAF_VISITS, 1u);
                PbrSoftwareCounterAddL6(
                    PBR_L4_COUNTER_LEAF_PRIMITIVES, node.links.y);
                PbrSoftwareCounterMaxL6(
                    PBR_L4_COUNTER_MAX_LEAF_OCCUPANCY, node.links.y);
                [loop]
                for (uint offset = 0u; offset < node.links.y; ++offset)
                {
                    const uint triangleIndex = node.links.x + offset;
                    const PbrCanonicalTraversalTriangleL6 candidate =
                        gPbrSoftwareTrianglesL6[triangleIndex];
                    if (candidate.identity.x == ignoredInstanceId &&
                        candidate.identity.y == ignoredPrimitiveId)
                    {
                        continue;
                    }
                    if (shadowQuery &&
                        (candidate.metadata.w & kInstanceFlagCastsShadowV0) == 0u)
                    {
                        continue;
                    }
                    PbrSoftwareCounterAddL6(
                        PBR_L4_COUNTER_TRIANGLE_TESTS, 1u);
                    SoftwarePrimitiveRecord primitive;
                    primitive.v0 = candidate.positions[0];
                    primitive.v1 = candidate.positions[1];
                    primitive.v2 = candidate.positions[2];
                    primitive.identity = uint4(candidate.metadata.z, 0u, 0u, 0u);
                    float distance = 0.0f;
                    float baryU = 0.0f;
                    float baryV = 0.0f;
                    bool frontFace = false;
                    if (!SoftwareIntersectTriangle(
                        ray,
                        primitive,
                        bestDistance,
                        found,
                        distance,
                        baryU,
                        baryV,
                        frontFace))
                    {
                        continue;
                    }
                    const bool doubleSided =
                        (candidate.metadata.x & kGeometryFlagDoubleSidedV0) != 0u ||
                        (candidate.metadata.y & kMaterialFlagDoubleSidedV0) != 0u;
                    if (!frontFace && !doubleSided)
                    {
                        continue;
                    }
                    const bool alphaMasked =
                        (candidate.metadata.x & kGeometryFlagAlphaMaskV0) != 0u ||
                        (candidate.metadata.y & kMaterialFlagAlphaMaskV0) != 0u;
                    if (alphaMasked)
                    {
                        if (candidate.identity.w >= gPbrSceneConstantsL6.counts1.x)
                        {
                            return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
                        }
                        const GpuMaterialV0 material =
                            gPbrCanonicalMaterialsL6[candidate.identity.w];
                        const uint alphaLayer = material.textureImageIndices.x;
                        const uint samplerId = material.textureSamplerIndices.x;
                        float alpha = material.baseColorFactor.w;
                        bool mappingValid = true;
                        if (alphaLayer == PBR_L6_INVALID_INDEX)
                        {
                            mappingValid = samplerId == PBR_L6_INVALID_INDEX;
                        }
                        else if (alphaLayer >= gPbrFrameL6.traversal.w ||
                            samplerId != gPbrFrameL6.output.y)
                        {
                            mappingValid = false;
                        }
                        else
                        {
                            const float weight0 = 1.0f - baryU - baryV;
                            const float2 uv = weight0 * candidate.texcoords[0].xy +
                                baryU * candidate.texcoords[1].xy +
                                baryV * candidate.texcoords[2].xy;
                            alpha *= gPbrAlphaAtlasL6.SampleLevel(
                                gPbrAlphaSamplerL6,
                                float3(uv, float(alphaLayer)), 0.0f).a;
                        }
                        if (!mappingValid)
                        {
                            return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
                        }
                        if (alpha < material.surfaceParams.w)
                        {
                            continue;
                        }
                    }
                    // The any-hit contract is visibility-only. Once culling,
                    // casts-shadow and alpha-mask acceptance have all passed,
                    // no closest-hit ordering or full hit reconstruction is
                    // needed. This also keeps software and Ray Query shadow
                    // behavior aligned.
                    if (shadowQuery)
                    {
                        return true;
                    }
                    const bool equalDistanceLowerKey = distance == bestDistance &&
                        (candidate.identity.x < bestInstanceId ||
                         (candidate.identity.x == bestInstanceId &&
                          candidate.identity.y < bestPrimitiveId));
                    if (!found || distance < bestDistance || equalDistanceLowerKey)
                    {
                        found = true;
                        bestDistance = distance;
                        bestU = baryU;
                        bestV = baryV;
                        bestFrontFace = frontFace;
                        bestTriangleIndex = triangleIndex;
                        bestInstanceId = candidate.identity.x;
                        bestPrimitiveId = candidate.identity.y;
                    }
                }
                nextNode = parentNode;
            }
            else
            {
                if (node.links.x >= nodeCount || node.links.z >= nodeCount)
                {
                    return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
                }
                nextNode = node.links.x;
            }
        }
        else if (node.links.y == 0u && previousNode == node.links.x)
        {
            if (node.links.z >= nodeCount)
            {
                return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
            }
            nextNode = node.links.z;
        }
        else if (node.links.y == 0u && previousNode == node.links.z)
        {
            nextNode = parentNode;
        }
        else
        {
            return PbrReturnInvalidSoftwareTraversalL6(tMaximum, hit);
        }
        previousNode = nodeIndex;
        nodeIndex = nextNode;
    }

    if (!found)
    {
        return false;
    }
    PbrPopulateSoftwareTraversalHitL6(
        inputRay,
        bestDistance,
        bestU,
        bestV,
        bestFrontFace,
        bestTriangleIndex,
        hit);
    return true;
}

bool PbrTraceClosestSoftwareL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    out PbrHitL6 hit)
{
    return PbrTraceSoftwareIgnoringL6(
        ray,
        tMinimum,
        tMaximum,
        PBR_L6_INVALID_INDEX,
        PBR_L6_INVALID_INDEX,
        false,
        hit);
}

bool PbrTraceAnySoftwareL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId)
{
    PbrHitL6 ignoredHit;
    return PbrTraceSoftwareIgnoringL6(
        ray,
        tMinimum,
        tMaximum,
        ignoredInstanceId,
        ignoredPrimitiveId,
        true,
        ignoredHit);
}

#define PBR_L6_TRACE_CLOSEST PbrTraceClosestSoftwareL6
#define PBR_L6_TRACE_ANY PbrTraceAnySoftwareL6

#endif
