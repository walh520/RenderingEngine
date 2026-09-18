#ifndef RENDERING_ENGINE_PBR_RAY_QUERY_TRAVERSAL_HLSLI
#define RENDERING_ENGINE_PBR_RAY_QUERY_TRAVERSAL_HLSLI

#include "../include/contracts/SceneAbiV0.hlsli"
#include "pbr_traversal_resources.hlsli"

[[vk::binding(0, 1)]] ConstantBuffer<GpuSceneConstantsV0> gPbrSceneConstantsL6;
[[vk::binding(1, 1)]] StructuredBuffer<GpuVertexV0> gPbrVerticesL6;
[[vk::binding(2, 1)]] StructuredBuffer<uint> gPbrIndicesL6;
[[vk::binding(3, 1)]] StructuredBuffer<GpuGeometryV0> gPbrGeometriesL6;
[[vk::binding(4, 1)]] StructuredBuffer<GpuInstanceV0> gPbrInstancesL6;
[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gPbrCanonicalMaterialsL6;

[[vk::binding(0, 2)]] RaytracingAccelerationStructure gPbrSceneAccelerationStructureL6;
[[vk::binding(8, 2)]] Texture2DArray<float4> gPbrAlphaAtlasL6;
[[vk::binding(9, 2)]] SamplerState gPbrAlphaSamplerL6;

void PbrInitializeRayQueryHitL6(float tMaximum, out PbrHitL6 hit)
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

bool PbrReturnInvalidRayQueryTraversalL6(float tMaximum, out PbrHitL6 hit)
{
    PbrInitializeRayQueryHitL6(tMaximum, hit);
    return true;
}

uint PbrCanonicalGeometryIndexL6(uint instanceIndex, uint localGeometryIndex)
{
    return gPbrInstancesL6[instanceIndex].metadata.x + localGeometryIndex;
}

void PbrFetchCanonicalTriangleL6(
    uint geometryIndex,
    uint localPrimitiveIndex,
    out GpuVertexV0 v0,
    out GpuVertexV0 v1,
    out GpuVertexV0 v2)
{
    const GpuGeometryV0 geometry = gPbrGeometriesL6[geometryIndex];
    const uint firstIndex = geometry.indexRange.x + localPrimitiveIndex * 3u;
    const uint vertexOffset = geometry.indexRange.z;
    v0 = gPbrVerticesL6[gPbrIndicesL6[firstIndex + 0u] + vertexOffset];
    v1 = gPbrVerticesL6[gPbrIndicesL6[firstIndex + 1u] + vertexOffset];
    v2 = gPbrVerticesL6[gPbrIndicesL6[firstIndex + 2u] + vertexOffset];
}

float3 PbrTransformCanonicalPositionL6(AbiMat4Rows transform, float3 position)
{
    return AbiMul(transform, float4(position, 1.0f)).xyz;
}

float3 PbrTransformCanonicalNormalL6(AbiMat4Rows worldToObject, float3 objectNormal)
{
    return normalize(float3(
        worldToObject.row0.x * objectNormal.x +
            worldToObject.row1.x * objectNormal.y +
            worldToObject.row2.x * objectNormal.z,
        worldToObject.row0.y * objectNormal.x +
            worldToObject.row1.y * objectNormal.y +
            worldToObject.row2.y * objectNormal.z,
        worldToObject.row0.z * objectNormal.x +
            worldToObject.row1.z * objectNormal.y +
            worldToObject.row2.z * objectNormal.z));
}

bool PbrAcceptRayQueryCandidateL6(
    inout RayQuery<RAY_FLAG_NONE> query,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId,
    bool shadowQuery,
    float tMinimum,
    float tMaximum,
    out bool mappingValid)
{
    mappingValid = true;
    // Vulkan permits implementation-dependent handling at the traversal
    // interval endpoints.  The shared ABI is stricter: both endpoints are
    // open, so candidate confirmation owns the final interval decision.
    const float candidateT = query.CandidateTriangleRayT();
    if (!(candidateT > tMinimum && candidateT < tMaximum))
    {
        return false;
    }
    const uint instanceIndex = query.CandidateInstanceID();
    if (instanceIndex >= gPbrSceneConstantsL6.counts0.w)
    {
        mappingValid = false;
        return false;
    }
    const uint localGeometryIndex = query.CandidateGeometryIndex();
    if (localGeometryIndex >= gPbrInstancesL6[instanceIndex].metadata.y)
    {
        mappingValid = false;
        return false;
    }
    const uint geometryIndex = PbrCanonicalGeometryIndexL6(
        instanceIndex, localGeometryIndex);
    if (geometryIndex >= gPbrSceneConstantsL6.counts0.z)
    {
        mappingValid = false;
        return false;
    }
    const GpuGeometryV0 geometry = gPbrGeometriesL6[geometryIndex];
    const uint localPrimitiveIndex = query.CandidatePrimitiveIndex();
    if (localPrimitiveIndex >= geometry.indexRange.y / 3u ||
        geometry.identity.z >= gPbrSceneConstantsL6.counts1.x)
    {
        mappingValid = false;
        return false;
    }
    const uint primitiveId = geometry.indexRange.w + localPrimitiveIndex;
    const uint stableInstanceId = gPbrInstancesL6[instanceIndex].metadata.z;
    if (shadowQuery &&
        (gPbrInstancesL6[instanceIndex].metadata.w & kInstanceFlagCastsShadowV0) == 0u)
    {
        return false;
    }
    if (stableInstanceId == ignoredInstanceId &&
        primitiveId == ignoredPrimitiveId)
    {
        return false;
    }
    const GpuMaterialV0 material = gPbrCanonicalMaterialsL6[geometry.identity.z];
    const bool doubleSided =
        (geometry.identity.w & kGeometryFlagDoubleSidedV0) != 0u ||
        (material.metadata.y & kMaterialFlagDoubleSidedV0) != 0u;
    if (!doubleSided && !query.CandidateTriangleFrontFace())
    {
        return false;
    }

    const bool alphaMasked =
        (geometry.identity.w & kGeometryFlagAlphaMaskV0) != 0u ||
        (material.metadata.y & kMaterialFlagAlphaMaskV0) != 0u;
    if (!alphaMasked)
    {
        return true;
    }

    GpuVertexV0 v0;
    GpuVertexV0 v1;
    GpuVertexV0 v2;
    PbrFetchCanonicalTriangleL6(geometryIndex, localPrimitiveIndex, v0, v1, v2);
    const float2 bary = query.CandidateTriangleBarycentrics();
    const float weight0 = 1.0f - bary.x - bary.y;
    const float2 uv = weight0 * v0.texcoord0.xy +
        bary.x * v1.texcoord0.xy + bary.y * v2.texcoord0.xy;
    float alpha = material.baseColorFactor.w;
    const uint alphaLayer = material.textureImageIndices.x;
    const uint samplerId = material.textureSamplerIndices.x;
    if (alphaLayer == PBR_L6_INVALID_INDEX)
    {
        mappingValid = samplerId == PBR_L6_INVALID_INDEX;
    }
    else if (alphaLayer >= gPbrFrameL6.traversal.w || samplerId != gPbrFrameL6.output.y)
    {
        mappingValid = false;
    }
    else
    {
        alpha *= gPbrAlphaAtlasL6.SampleLevel(
            gPbrAlphaSamplerL6, float3(uv, float(alphaLayer)), 0.0f).a;
    }
    return mappingValid && alpha >= material.surfaceParams.w;
}

bool PbrCommittedRayQueryHitL6(
    inout RayQuery<RAY_FLAG_NONE> query,
    PbrRayL6 ray,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId,
    float tMaximum,
    out PbrHitL6 hit)
{
    PbrInitializeRayQueryHitL6(tMaximum, hit);
    const uint instanceIndex = query.CommittedInstanceID();
    if (instanceIndex >= gPbrSceneConstantsL6.counts0.w)
    {
        return PbrReturnInvalidRayQueryTraversalL6(tMaximum, hit);
    }
    const uint localGeometryIndex = query.CommittedGeometryIndex();
    if (localGeometryIndex >= gPbrInstancesL6[instanceIndex].metadata.y)
    {
        return PbrReturnInvalidRayQueryTraversalL6(tMaximum, hit);
    }
    const uint geometryIndex = PbrCanonicalGeometryIndexL6(
        instanceIndex, localGeometryIndex);
    if (geometryIndex >= gPbrSceneConstantsL6.counts0.z)
    {
        return PbrReturnInvalidRayQueryTraversalL6(tMaximum, hit);
    }
    const GpuGeometryV0 geometry = gPbrGeometriesL6[geometryIndex];
    const uint localPrimitiveIndex = query.CommittedPrimitiveIndex();
    if (localPrimitiveIndex >= geometry.indexRange.y / 3u ||
        geometry.identity.z >= gPbrSceneConstantsL6.counts1.x)
    {
        return PbrReturnInvalidRayQueryTraversalL6(tMaximum, hit);
    }
    const uint primitiveId = geometry.indexRange.w + localPrimitiveIndex;
    const uint stableInstanceId = gPbrInstancesL6[instanceIndex].metadata.z;
    if (stableInstanceId == ignoredInstanceId &&
        primitiveId == ignoredPrimitiveId)
    {
        return false;
    }

    GpuVertexV0 v0;
    GpuVertexV0 v1;
    GpuVertexV0 v2;
    PbrFetchCanonicalTriangleL6(geometryIndex, localPrimitiveIndex, v0, v1, v2);
    const GpuInstanceV0 instance = gPbrInstancesL6[instanceIndex];
    const float2 bary = query.CommittedTriangleBarycentrics();
    const float weight0 = 1.0f - bary.x - bary.y;
    const float3 p0 = PbrTransformCanonicalPositionL6(
        instance.objectToWorld, v0.position.xyz);
    const float3 p1 = PbrTransformCanonicalPositionL6(
        instance.objectToWorld, v1.position.xyz);
    const float3 p2 = PbrTransformCanonicalPositionL6(
        instance.objectToWorld, v2.position.xyz);
    float3 geometricNormal = normalize(cross(p1 - p0, p2 - p0));
    float3 shadingNormal = PbrTransformCanonicalNormalL6(
        instance.worldToObject,
        normalize(weight0 * v0.normal.xyz + bary.x * v1.normal.xyz + bary.y * v2.normal.xyz));
    const bool frontFace = query.CommittedTriangleFrontFace();
    if (!frontFace)
    {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }
    hit.t = query.CommittedRayT();
    hit.position = ray.origin + ray.direction * hit.t;
    hit.geometricNormal = geometricNormal;
    hit.shadingNormal = shadingNormal;
    hit.materialIndex = geometry.identity.z;
    hit.instanceId = stableInstanceId;
    hit.primitiveId = primitiveId;
    hit.emitterLightIndex = PbrResolveEmitterLightL6(
        hit.instanceId, primitiveId, PBR_L6_INVALID_INDEX);
    hit.frontFace = frontFace ? 1u : 0u;
    return true;
}

[noinline]
bool PbrTraceRayQueryIgnoringL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId,
    bool anyHit,
    out PbrHitL6 hit)
{
    PbrInitializeRayQueryHitL6(tMaximum, hit);
    const float directionLengthSquared = dot(ray.direction, ray.direction);
    if (!all(isfinite(ray.origin)) || !all(isfinite(ray.direction)) ||
        !isfinite(directionLengthSquared) ||
        abs(directionLengthSquared - 1.0f) > 1.0e-4f ||
        !(tMinimum >= 0.0f && tMinimum < tMaximum))
    {
        return PbrReturnInvalidRayQueryTraversalL6(tMaximum, hit);
    }

    RayDesc description;
    description.Origin = ray.origin;
    description.TMin = tMinimum;
    description.Direction = ray.direction;
    description.TMax = tMaximum;
    // Every triangle must reach candidate confirmation.  Besides alpha and
    // one-sided semantics, shadow rays use this seam to reject the sampled
    // emitter primitive without allowing an opaque auto-commit to terminate
    // traversal before a real occluder is considered.
    const uint rayFlags = RAY_FLAG_FORCE_NON_OPAQUE |
        (anyHit ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : 0u);
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(
        gPbrSceneAccelerationStructureL6,
        rayFlags,
        0xffu,
        description);

    bool mappingValid = true;
    [loop]
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            bool candidateMappingValid = true;
            if (PbrAcceptRayQueryCandidateL6(
                query,
                ignoredInstanceId,
                ignoredPrimitiveId,
                anyHit,
                tMinimum,
                tMaximum,
                candidateMappingValid))
            {
                query.CommitNonOpaqueTriangleHit();
            }
            mappingValid = mappingValid && candidateMappingValid;
        }
    }
    if (!mappingValid)
    {
        return PbrReturnInvalidRayQueryTraversalL6(tMaximum, hit);
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return false;
    }
    // Visibility queries need only the first accepted occluder. Reconstructing
    // a full shading hit here defeats ACCEPT_FIRST_HIT_AND_END_SEARCH and made
    // Wavefront shadow batches expensive enough to trip Windows TDR.
    if (anyHit)
    {
        return true;
    }
    return PbrCommittedRayQueryHitL6(
        query, ray, ignoredInstanceId, ignoredPrimitiveId, tMaximum, hit);
}

bool PbrTraceClosestRayQueryL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    out PbrHitL6 hit)
{
    return PbrTraceRayQueryIgnoringL6(
        ray, tMinimum, tMaximum,
        PBR_L6_INVALID_INDEX, PBR_L6_INVALID_INDEX, false, hit);
}

bool PbrTraceAnyRayQueryL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    uint ignoredInstanceId,
    uint ignoredPrimitiveId)
{
    PbrHitL6 ignoredHit;
    return PbrTraceRayQueryIgnoringL6(
        ray, tMinimum, tMaximum,
        ignoredInstanceId, ignoredPrimitiveId, true, ignoredHit);
}

#define PBR_L6_TRACE_CLOSEST PbrTraceClosestRayQueryL6
#define PBR_L6_TRACE_ANY PbrTraceAnyRayQueryL6

#endif
