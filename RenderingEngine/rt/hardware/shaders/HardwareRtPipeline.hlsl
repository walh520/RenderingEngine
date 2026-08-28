#include "contracts/AbiTypesV0.hlsli"
#include "contracts/SceneAbiV0.hlsli"
#include "contracts/RayHitAbiV0.hlsli"

[[vk::binding(1, 1)]] StructuredBuffer<GpuVertexV0> gVertices;
[[vk::binding(2, 1)]] StructuredBuffer<uint> gIndices;
[[vk::binding(3, 1)]] StructuredBuffer<GpuGeometryV0> gGeometries;
[[vk::binding(4, 1)]] StructuredBuffer<GpuInstanceV0> gInstances;
[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gMaterials;

[[vk::binding(0, 2)]] RaytracingAccelerationStructure gSceneAccelerationStructure;
[[vk::binding(1, 2)]] StructuredBuffer<GpuRayV0> gRays;
[[vk::binding(2, 2)]] RWStructuredBuffer<GpuHitV0> gHits;
[[vk::binding(8, 2)]] Texture2DArray<float4> gAlphaAtlas;
[[vk::binding(9, 2)]] SamplerState gAlphaSampler;

struct RayBatchPushConstants
{
    uint rayCount;
    uint queryMode;
    uint alphaAtlasLayerCount;
    uint alphaSamplerId;
    uint rayOffset;
};
[[vk::push_constant]] RayBatchPushConstants gBatch;

// Exactly 32 bytes: the pipeline deliberately keeps the payload small and
// reconstructs the canonical 96-byte hit in ray generation.
struct TracePayload
{
    float t;
    float2 barycentrics;
    uint instanceIndex;
    uint primitiveIndex;
    uint geometryIndex;
    uint flags;
    uint committed;
};

struct TriangleAttributes
{
    float2 barycentrics : SV_Barycentrics;
};

static const uint kQueryAny = 1u;
static const uint kInvalidId = 0xffffffffu;
static const uint kPayloadFrontFace = 1u << 0u;
static const uint kPayloadAlphaTested = 1u << 1u;
static const uint kPayloadForceOpaque = 1u << 2u;
static const uint kPayloadSamplerMappingInvalid = 1u << 3u;
static const uint kKnownRayFlags = kRayFlagCullFrontFaceV0 | kRayFlagCullBackFaceV0 |
    kRayFlagForceOpaqueV0;

uint CanonicalRayFlagsToVulkan(uint flags, bool anyHit)
{
    uint result = 0u;
    if ((flags & kRayFlagCullFrontFaceV0) != 0u)
    {
        result |= RAY_FLAG_CULL_FRONT_FACING_TRIANGLES;
    }
    if ((flags & kRayFlagCullBackFaceV0) != 0u)
    {
        result |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
    }
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
    out GpuVertexV0 v0,
    out GpuVertexV0 v1,
    out GpuVertexV0 v2)
{
    GpuGeometryV0 geometry = gGeometries[geometryIndex];
    uint firstIndex = geometry.indexRange.x + localPrimitiveIndex * 3u;
    uint3 vertexIndices = uint3(
        gIndices[firstIndex + 0u],
        gIndices[firstIndex + 1u],
        gIndices[firstIndex + 2u]);
    vertexIndices += geometry.indexRange.z;
    v0 = gVertices[vertexIndices.x];
    v1 = gVertices[vertexIndices.y];
    v2 = gVertices[vertexIndices.z];
}

bool AcceptSurface(
    uint instanceIndex,
    uint localGeometryIndex,
    uint primitiveIndex,
    float2 barycentrics,
    bool frontFace,
    bool forceOpaque,
    out bool alphaTested,
    out bool samplerMappingValid)
{
    alphaTested = false;
    samplerMappingValid = true;
    uint geometryIndex = CanonicalGeometryIndex(instanceIndex, localGeometryIndex);
    GpuGeometryV0 geometry = gGeometries[geometryIndex];
    GpuMaterialV0 material = gMaterials[geometry.identity.z];
    bool doubleSided = ((geometry.identity.w & kGeometryFlagDoubleSidedV0) != 0u) ||
        ((material.metadata.y & kMaterialFlagDoubleSidedV0) != 0u);
    if (!doubleSided && !frontFace)
    {
        return false;
    }
    bool alphaMasked = ((geometry.identity.w & kGeometryFlagAlphaMaskV0) != 0u) ||
        ((material.metadata.y & kMaterialFlagAlphaMaskV0) != 0u);
    if (!alphaMasked)
    {
        return true;
    }
    if (forceOpaque)
    {
        return true;
    }
    alphaTested = true;
    GpuVertexV0 v0;
    GpuVertexV0 v1;
    GpuVertexV0 v2;
    FetchTriangle(geometryIndex, primitiveIndex, v0, v1, v2);
    float weight0 = 1.0f - barycentrics.x - barycentrics.y;
    float2 uv = weight0 * v0.texcoord0.xy + barycentrics.x * v1.texcoord0.xy +
        barycentrics.y * v2.texcoord0.xy;
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
    return normalize(float3(
        worldToObject.row0.x * objectNormal.x + worldToObject.row1.x * objectNormal.y + worldToObject.row2.x * objectNormal.z,
        worldToObject.row0.y * objectNormal.x + worldToObject.row1.y * objectNormal.y + worldToObject.row2.y * objectNormal.z,
        worldToObject.row0.z * objectNormal.x + worldToObject.row1.z * objectNormal.y + worldToObject.row2.z * objectNormal.z));
}

GpuHitV0 MakeMiss(GpuRayV0 ray)
{
    GpuHitV0 hit = (GpuHitV0)0;
    hit.positionT.w = ray.directionTMax.w;
    hit.ids = uint4(kInvalidId, kInvalidId, kInvalidId, kInvalidId);
    hit.metadata = uint4(ray.query.x, kHitKindMissV0, kHitFlagNoneV0, 0u);
    return hit;
}

GpuHitV0 MakeInvalid(GpuRayV0 ray)
{
    GpuHitV0 hit = (GpuHitV0)0;
    hit.ids = uint4(kInvalidId, kInvalidId, kInvalidId, kInvalidId);
    hit.metadata = uint4(ray.query.x, kHitKindInvalidV0, kHitFlagNoneV0, 0u);
    return hit;
}

bool IsCanonicalRayValid(GpuRayV0 ray)
{
    bool finite = all(isfinite(ray.originTMin)) && all(isfinite(ray.directionTMax));
    float directionLengthSquared = dot(ray.directionTMax.xyz, ray.directionTMax.xyz);
    bool normalized = abs(directionLengthSquared - 1.0f) <= 1.0e-3f;
    bool rangeValid = ray.originTMin.w >= 0.0f && ray.originTMin.w <= ray.directionTMax.w;
    bool flagsValid = (ray.query.z & ~kKnownRayFlags) == 0u;
    bool metadataValid = ray.query.y <= 0xffu && ray.query.w == 0u &&
        all(ray.reserved0 == uint4(0u, 0u, 0u, 0u));
    return finite && normalized && rangeValid && flagsValid && metadataValid;
}

bool CullsBothTriangleFaces(uint flags)
{
    return (flags & kRayFlagCullFrontFaceV0) != 0u &&
        (flags & kRayFlagCullBackFaceV0) != 0u;
}

GpuHitV0 MakeHit(GpuRayV0 ray, TracePayload payload)
{
    uint geometryIndex = CanonicalGeometryIndex(payload.instanceIndex, payload.geometryIndex);
    GpuGeometryV0 geometry = gGeometries[geometryIndex];
    GpuInstanceV0 instance = gInstances[payload.instanceIndex];
    GpuMaterialV0 material = gMaterials[geometry.identity.z];
    GpuVertexV0 v0;
    GpuVertexV0 v1;
    GpuVertexV0 v2;
    FetchTriangle(geometryIndex, payload.primitiveIndex, v0, v1, v2);
    float weight0 = 1.0f - payload.barycentrics.x - payload.barycentrics.y;
    float3 p0 = TransformPosition(instance.objectToWorld, v0.position.xyz);
    float3 p1 = TransformPosition(instance.objectToWorld, v1.position.xyz);
    float3 p2 = TransformPosition(instance.objectToWorld, v2.position.xyz);
    float3 geometricNormal = normalize(cross(p1 - p0, p2 - p0));
    float3 objectShadingNormal = normalize(weight0 * v0.normal.xyz +
        payload.barycentrics.x * v1.normal.xyz + payload.barycentrics.y * v2.normal.xyz);
    float3 shadingNormal = TransformNormalFromWorldToObject(instance.worldToObject, objectShadingNormal);
    bool frontFace = (payload.flags & kPayloadFrontFace) != 0u;
    if (!frontFace)
    {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }

    GpuHitV0 hit = (GpuHitV0)0;
    hit.positionT = float4(ray.originTMin.xyz + ray.directionTMax.xyz * payload.t, payload.t);
    hit.geometricNormalBaryU = float4(geometricNormal, payload.barycentrics.x);
    hit.shadingNormalBaryV = float4(shadingNormal, payload.barycentrics.y);
    hit.ids = uint4(
        instance.metadata.z,
        geometry.indexRange.w + payload.primitiveIndex,
        geometry.identity.x,
        material.metadata.z);
    uint hitFlags = frontFace ? kHitFlagFrontFaceV0 : kHitFlagNoneV0;
    if ((payload.flags & kPayloadAlphaTested) != 0u)
    {
        hitFlags |= kHitFlagAlphaTestedV0;
    }
    hit.metadata = uint4(ray.query.x, kHitKindTriangleV0, hitFlags, 0u);
    return hit;
}

[shader("raygeneration")]
void RayGenerationMain()
{
    uint localRayIndex = DispatchRaysIndex().x;
    if (localRayIndex >= gBatch.rayCount)
    {
        return;
    }
    uint rayIndex = gBatch.rayOffset + localRayIndex;
    GpuRayV0 ray = gRays[rayIndex];
    if (!IsCanonicalRayValid(ray))
    {
        gHits[rayIndex] = MakeInvalid(ray);
        return;
    }
    if (CullsBothTriangleFaces(ray.query.z))
    {
        gHits[rayIndex] = MakeMiss(ray);
        return;
    }
    TracePayload payload = (TracePayload)0;
    bool forceOpaque = (ray.query.z & kRayFlagForceOpaqueV0) != 0u;
    payload.flags = forceOpaque ? kPayloadForceOpaque : 0u;
    bool anyHit = gBatch.queryMode == kQueryAny;
    uint rayFlags = CanonicalRayFlagsToVulkan(ray.query.z, anyHit);
    RayDesc rayDescription;
    rayDescription.Origin = ray.originTMin.xyz;
    rayDescription.TMin = ray.originTMin.w;
    rayDescription.Direction = ray.directionTMax.xyz;
    rayDescription.TMax = ray.directionTMax.w;
    TraceRay(
        gSceneAccelerationStructure,
        rayFlags,
        ray.query.y,
        0u,
        0u,
        0u,
        rayDescription,
        payload);
    if ((payload.flags & kPayloadSamplerMappingInvalid) != 0u)
    {
        gHits[rayIndex] = MakeInvalid(ray);
    }
    else if (payload.committed != 0u)
    {
        gHits[rayIndex] = MakeHit(ray, payload);
    }
    else
    {
        gHits[rayIndex] = MakeMiss(ray);
    }
}

[shader("miss")]
void MissMain(inout TracePayload payload)
{
    payload.committed = 0u;
}

[shader("closesthit")]
void ClosestHitMain(inout TracePayload payload, in TriangleAttributes attributes)
{
    payload.t = RayTCurrent();
    payload.barycentrics = attributes.barycentrics;
    payload.instanceIndex = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    uint canonicalGeometryIndex = CanonicalGeometryIndex(payload.instanceIndex, payload.geometryIndex);
    GpuGeometryV0 geometry = gGeometries[canonicalGeometryIndex];
    GpuMaterialV0 material = gMaterials[geometry.identity.z];
    bool forceOpaque = (payload.flags & kPayloadForceOpaque) != 0u;
    bool alphaTested = !forceOpaque &&
        (((geometry.identity.w & kGeometryFlagAlphaMaskV0) != 0u) ||
         ((material.metadata.y & kMaterialFlagAlphaMaskV0) != 0u));
    uint persistentFlags = payload.flags & kPayloadSamplerMappingInvalid;
    payload.flags = persistentFlags |
        (HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? kPayloadFrontFace : 0u);
    payload.flags |= alphaTested ? kPayloadAlphaTested : 0u;
    payload.committed = 1u;
}

[shader("anyhit")]
void AnyHitMain(inout TracePayload payload, in TriangleAttributes attributes)
{
    bool alphaTested = false;
    bool samplerMappingValid = true;
    bool forceOpaque = (payload.flags & kPayloadForceOpaque) != 0u;
    bool accepted = AcceptSurface(
        InstanceID(), GeometryIndex(), PrimitiveIndex(), attributes.barycentrics,
        HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE, forceOpaque, alphaTested,
        samplerMappingValid);
    if (!samplerMappingValid)
    {
        payload.flags |= kPayloadSamplerMappingInvalid;
        IgnoreHit();
        return;
    }
    if (!accepted)
    {
        IgnoreHit();
        return;
    }
    // ClosestHitMain derives the committed surface's alpha flag. Do not
    // persist candidate state from a farther intersection in the payload.
}
