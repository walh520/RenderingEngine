#include "contracts/AbiTypesV0.hlsli"
#include "contracts/AbiVersionV0.hlsli"
#include "contracts/AbiVersionV1.hlsli"
#include "contracts/SceneAbiV0.hlsli"
#include "contracts/RayHitAbiV0.hlsli"
#include "contracts/GpuRecordsAbiV1.hlsli"

[[vk::binding(0, 1)]] ConstantBuffer<GpuSceneConstantsV0> gSceneConstants;
[[vk::binding(1, 1)]] StructuredBuffer<GpuVertexV0> gVertices;
[[vk::binding(2, 1)]] StructuredBuffer<uint> gIndices;
[[vk::binding(3, 1)]] StructuredBuffer<GpuGeometryV0> gGeometries;
[[vk::binding(4, 1)]] StructuredBuffer<GpuInstanceV0> gInstances;
[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gMaterials;

[[vk::binding(0, 2)]] RaytracingAccelerationStructure gSceneAccelerationStructure;
[[vk::binding(1, 2)]] StructuredBuffer<GpuRayQueueRecordV1> gRays;
[[vk::binding(2, 2)]] RWStructuredBuffer<GpuHitQueueRecordV1> gHits;
[[vk::binding(8, 2)]] Texture2DArray<float4> gAlphaAtlas;
[[vk::binding(9, 2)]] SamplerState gAlphaSampler;

struct RayQueryPushConstants
{
    uint rayCount;
    uint queryMode;
    uint alphaAtlasLayerCount;
    uint alphaSamplerId;
    uint rayOffset;
    uint hitOffset;
};

[[vk::push_constant]] RayQueryPushConstants gBatch;

static const uint kQueryClosest = 0u;
static const uint kQueryAny = 1u;
static const uint kInvalidId = kInvalidIdV1;

uint CanonicalRayFlagsToVulkan(bool anyHit)
{
    uint result = 0u;
    if (anyHit)
    {
        result |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    }
    return result;
}

uint CanonicalGeometryIndex(uint instanceIndex, uint localGeometryIndex)
{
    return gInstances[instanceIndex].metadata.x + localGeometryIndex;
}

void FetchTriangle(
    uint geometryIndex,
    uint localPrimitiveIndex,
    out uint3 vertexIndices,
    out GpuVertexV0 v0,
    out GpuVertexV0 v1,
    out GpuVertexV0 v2)
{
    GpuGeometryV0 geometry = gGeometries[geometryIndex];
    uint firstIndex = geometry.indexRange.x + localPrimitiveIndex * 3u;
    vertexIndices = uint3(
        gIndices[firstIndex + 0u],
        gIndices[firstIndex + 1u],
        gIndices[firstIndex + 2u]);
    vertexIndices += geometry.indexRange.z;
    v0 = gVertices[vertexIndices.x];
    v1 = gVertices[vertexIndices.y];
    v2 = gVertices[vertexIndices.z];
}

bool IsCandidateAccepted(
    inout RayQuery<RAY_FLAG_NONE> query,
    GpuRayQueueRecordV1 ray,
    out bool samplerMappingValid)
{
    samplerMappingValid = true;
    // Vulkan traversal may treat a candidate exactly at tMax as either the
    // closest hit or a dropped hit. ABI v1 deliberately defines an open
    // interval, so enforce both endpoints before any material decision.
    const float candidateT = query.CandidateTriangleRayT();
    if (!(candidateT > ray.originTMin.w && candidateT < ray.directionTMax.w))
    {
        return false;
    }
    uint instanceIndex = query.CandidateInstanceID();
    if (instanceIndex >= gSceneConstants.counts0.w)
    {
        samplerMappingValid = false;
        return false;
    }
    const uint localGeometryIndex = query.CandidateGeometryIndex();
    if (localGeometryIndex >= gInstances[instanceIndex].metadata.y)
    {
        samplerMappingValid = false;
        return false;
    }
    uint geometryIndex = CanonicalGeometryIndex(instanceIndex, localGeometryIndex);
    if (geometryIndex >= gSceneConstants.counts0.z)
    {
        samplerMappingValid = false;
        return false;
    }
    GpuGeometryV0 geometry = gGeometries[geometryIndex];
    if (geometry.identity.z >= gSceneConstants.counts1.x)
    {
        samplerMappingValid = false;
        return false;
    }
    const uint localPrimitiveIndex = query.CandidatePrimitiveIndex();
    if (localPrimitiveIndex >= geometry.indexRange.y / 3u)
    {
        samplerMappingValid = false;
        return false;
    }
    GpuMaterialV0 material = gMaterials[geometry.identity.z];
    bool doubleSided = ((geometry.identity.w & kGeometryFlagDoubleSidedV0) != 0u) ||
        ((material.metadata.y & kMaterialFlagDoubleSidedV0) != 0u);
    if (!doubleSided && !query.CandidateTriangleFrontFace())
    {
        return false;
    }

    bool alphaMasked = ((geometry.identity.w & kGeometryFlagAlphaMaskV0) != 0u) ||
        ((material.metadata.y & kMaterialFlagAlphaMaskV0) != 0u);
    if (!alphaMasked)
    {
        return true;
    }
    uint3 ignoredIndices;
    GpuVertexV0 v0;
    GpuVertexV0 v1;
    GpuVertexV0 v2;
    FetchTriangle(geometryIndex, localPrimitiveIndex, ignoredIndices, v0, v1, v2);
    float2 bary = query.CandidateTriangleBarycentrics();
    float weight0 = 1.0f - bary.x - bary.y;
    float2 uv = weight0 * v0.texcoord0.xy + bary.x * v1.texcoord0.xy + bary.y * v2.texcoord0.xy;
    float alpha = material.baseColorFactor.w;
    uint alphaLayer = material.textureImageIndices.x;
    uint samplerId = material.textureSamplerIndices.x;
    if (alphaLayer == kInvalidId)
    {
        samplerMappingValid = samplerId == kInvalidId;
    }
    else if (alphaLayer >= gBatch.alphaAtlasLayerCount || samplerId != gBatch.alphaSamplerId)
    {
        samplerMappingValid = false;
    }
    else
    {
        alpha *= gAlphaAtlas.SampleLevel(gAlphaSampler, float3(uv, float(alphaLayer)), 0.0f).a;
    }
    return samplerMappingValid && alpha >= material.surfaceParams.w;
}

float3 TransformPosition(AbiMat4Rows transform, float3 position)
{
    return AbiMul(transform, float4(position, 1.0f)).xyz;
}

float3 TransformNormalFromWorldToObject(AbiMat4Rows worldToObject, float3 objectNormal)
{
    float3 worldNormal = float3(
        worldToObject.row0.x * objectNormal.x + worldToObject.row1.x * objectNormal.y + worldToObject.row2.x * objectNormal.z,
        worldToObject.row0.y * objectNormal.x + worldToObject.row1.y * objectNormal.y + worldToObject.row2.y * objectNormal.z,
        worldToObject.row0.z * objectNormal.x + worldToObject.row1.z * objectNormal.y + worldToObject.row2.z * objectNormal.z);
    return normalize(worldNormal);
}

GpuHitQueueRecordV1 MakeMiss(GpuRayQueueRecordV1 ray)
{
    GpuHitQueueRecordV1 hit = (GpuHitQueueRecordV1)0;
    hit.positionT.w = ray.directionTMax.w;
    hit.ids = uint4(kInvalidId, kInvalidId, kInvalidId, kInvalidId);
    hit.metadata = uint4(ray.identity.x, kHitKindMissV0, kHitFlagNoneV0, ray.identity.y);
    return hit;
}

GpuHitQueueRecordV1 MakeInvalid(GpuRayQueueRecordV1 ray)
{
    GpuHitQueueRecordV1 hit = (GpuHitQueueRecordV1)0;
    hit.ids = uint4(kInvalidId, kInvalidId, kInvalidId, kInvalidId);
    hit.metadata = uint4(ray.identity.x, kHitKindInvalidV0, kHitFlagNoneV0, ray.identity.y);
    return hit;
}

bool IsCanonicalRayValid(GpuRayQueueRecordV1 ray)
{
    bool finite = all(isfinite(ray.originTMin)) && all(isfinite(ray.directionTMax));
    float directionLengthSquared = dot(ray.directionTMax.xyz, ray.directionTMax.xyz);
    bool normalized = abs(directionLengthSquared - 1.0f) <= 1.0e-4f;
    // ABI v1 defines both bounds as exclusive.  An empty or reversed interval
    // is malformed rather than a canonical miss.
    bool rangeValid = ray.originTMin.w >= 0.0f && ray.originTMin.w < ray.directionTMax.w;
    bool metadataValid = ray.identity.w <= 0xffu;
    return finite && normalized && rangeValid && metadataValid;
}

GpuHitQueueRecordV1 MakeCommittedHit(
    inout RayQuery<RAY_FLAG_NONE> query,
    GpuRayQueueRecordV1 ray)
{
    uint instanceIndex = query.CommittedInstanceID();
    uint geometryIndex = CanonicalGeometryIndex(instanceIndex, query.CommittedGeometryIndex());
    GpuGeometryV0 geometry = gGeometries[geometryIndex];
    GpuInstanceV0 instance = gInstances[instanceIndex];
    GpuMaterialV0 material = gMaterials[geometry.identity.z];
    bool alphaTested =
        (((geometry.identity.w & kGeometryFlagAlphaMaskV0) != 0u) ||
         ((material.metadata.y & kMaterialFlagAlphaMaskV0) != 0u));
    uint3 ignoredIndices;
    GpuVertexV0 v0;
    GpuVertexV0 v1;
    GpuVertexV0 v2;
    FetchTriangle(geometryIndex, query.CommittedPrimitiveIndex(), ignoredIndices, v0, v1, v2);

    float2 bary = query.CommittedTriangleBarycentrics();
    float weight0 = 1.0f - bary.x - bary.y;
    float3 p0 = TransformPosition(instance.objectToWorld, v0.position.xyz);
    float3 p1 = TransformPosition(instance.objectToWorld, v1.position.xyz);
    float3 p2 = TransformPosition(instance.objectToWorld, v2.position.xyz);
    float3 geometricNormal = normalize(cross(p1 - p0, p2 - p0));
    float3 objectShadingNormal = normalize(weight0 * v0.normal.xyz + bary.x * v1.normal.xyz + bary.y * v2.normal.xyz);
    float3 shadingNormal = TransformNormalFromWorldToObject(instance.worldToObject, objectShadingNormal);
    bool frontFace = query.CommittedTriangleFrontFace();
    if (!frontFace)
    {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }

    float hitT = query.CommittedRayT();
    GpuHitQueueRecordV1 hit = (GpuHitQueueRecordV1)0;
    hit.positionT = float4(ray.originTMin.xyz + ray.directionTMax.xyz * hitT, hitT);
    hit.geometricNormalBaryU = float4(geometricNormal, bary.x);
    hit.shadingNormalBaryV = float4(shadingNormal, bary.y);
    hit.ids = uint4(
        instance.metadata.z,
        geometry.indexRange.w + query.CommittedPrimitiveIndex(),
        geometry.identity.x,
        material.metadata.z);
    uint hitFlags = frontFace ? kHitFlagFrontFaceV0 : kHitFlagNoneV0;
    if (alphaTested)
    {
        hitFlags |= kHitFlagAlphaTestedV0;
    }
    hit.metadata = uint4(ray.identity.x, kHitKindTriangleV0, hitFlags, ray.identity.y);
    return hit;
}

[numthreads(64, 1, 1)]
void RayQueryMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localRayIndex = dispatchThreadId.x;
    if (localRayIndex >= gBatch.rayCount)
    {
        return;
    }
    uint rayIndex = gBatch.rayOffset + localRayIndex;
    uint hitIndex = gBatch.hitOffset + localRayIndex;

    GpuRayQueueRecordV1 ray = gRays[rayIndex];
    if (!IsCanonicalRayValid(ray))
    {
        gHits[hitIndex] = MakeInvalid(ray);
        return;
    }
    if (gBatch.queryMode != kQueryClosest && gBatch.queryMode != kQueryAny)
    {
        gHits[hitIndex] = MakeInvalid(ray);
        return;
    }
    bool anyHit = gBatch.queryMode == kQueryAny;
    // Candidate confirmation owns canonical back-face, alpha-mask, and open
    // ray-interval semantics. Force even AS-opaque triangles through it.
    uint rayFlags = CanonicalRayFlagsToVulkan(anyHit) | RAY_FLAG_FORCE_NON_OPAQUE;
    RayDesc rayDescription;
    rayDescription.Origin = ray.originTMin.xyz;
    rayDescription.TMin = ray.originTMin.w;
    rayDescription.Direction = ray.directionTMax.xyz;
    rayDescription.TMax = ray.directionTMax.w;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(
        gSceneAccelerationStructure,
        rayFlags,
        ray.identity.w,
        rayDescription);

    bool samplerMappingValid = true;
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            bool candidateMappingValid = true;
            if (IsCandidateAccepted(query, ray, candidateMappingValid))
            {
                query.CommitNonOpaqueTriangleHit();
            }
            samplerMappingValid = samplerMappingValid && candidateMappingValid;
        }
    }

    if (!samplerMappingValid)
    {
        gHits[hitIndex] = MakeInvalid(ray);
    }
    else if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        gHits[hitIndex] = MakeCommittedHit(query, ray);
    }
    else
    {
        gHits[hitIndex] = MakeMiss(ray);
    }
}
