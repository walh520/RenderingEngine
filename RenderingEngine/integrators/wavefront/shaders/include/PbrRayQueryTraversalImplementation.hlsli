#ifndef RENDERING_ENGINE_WAVEFRONT_PBR_RAY_QUERY_TRAVERSAL_IMPLEMENTATION_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_PBR_RAY_QUERY_TRAVERSAL_IMPLEMENTATION_HLSLI

// Production L7 adapter over the exact L5/L6 Ray Query implementation.
#include "pbr_l6_types.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;

#include "pbr_ray_query_traversal.hlsli"

void WfWriteRayQueryMiss(
    uint rayId,
    uint pathIndex,
    float tMaximum,
    out WfMaterialWorkItem hit)
{
    hit = (WfMaterialWorkItem)0;
    hit.positionT.w = tMaximum;
    hit.ids = 0xffffffffu;
    hit.metadata = uint4(rayId, kWfHitMiss, 0u, pathIndex);
}

void WfWriteRayQueryHit(
    WfRayItem ray,
    PbrHitL6 source,
    out WfMaterialWorkItem hit)
{
    hit = (WfMaterialWorkItem)0;
    hit.positionT = float4(source.position, source.t);
    hit.geometricNormalBaryU = float4(source.geometricNormal, 0.0f);
    hit.shadingNormalBaryV = float4(source.shadingNormal, 0.0f);
    hit.ids = uint4(
        source.emitterLightIndex,
        source.primitiveId,
        source.instanceId,
        source.materialIndex);
    hit.metadata = uint4(
        ray.identity.x,
        1u,
        source.frontFace != 0u ? 1u : 0u,
        ray.identity.y);
}

bool WfRayQueryTraceClosest(
    WfRayItem ray,
    uint rayIndex,
    out WfMaterialWorkItem hit)
{
    PbrRayL6 query;
    query.origin = ray.originTMin.xyz;
    query.direction = ray.directionTMax.xyz;
    PbrHitL6 source;
    const bool found = PbrTraceClosestRayQueryL6(
        query, ray.originTMin.w, ray.directionTMax.w, source);
    if (!found)
    {
        WfWriteRayQueryMiss(
            ray.identity.x, ray.identity.y, ray.directionTMax.w, hit);
        return false;
    }
    WfWriteRayQueryHit(ray, source, hit);
    return true;
}

bool WfRayQueryTraceAny(WfShadowWorkItem ray)
{
    PbrRayL6 query;
    query.origin = ray.originTMin.xyz;
    query.direction = ray.directionTMax.xyz;
    return PbrTraceAnyRayQueryL6(
        query,
        ray.originTMin.w,
        ray.directionTMax.w,
        0xffffffffu,
        ray.identity.w);
}

#endif
