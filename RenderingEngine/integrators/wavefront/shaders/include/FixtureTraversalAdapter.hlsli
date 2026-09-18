#ifndef RENDERING_ENGINE_WAVEFRONT_FIXTURE_TRAVERSAL_ADAPTER_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_FIXTURE_TRAVERSAL_ADAPTER_HLSLI

// The default L7 fixture consumes the same private set-0 records and analytic
// corpus as L6. This makes queue execution comparable without publishing a
// replacement shared ABI.
#include "pbr_l6_types.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;

#include "pbr_fixture_traversal.hlsli"

void WfFixtureMiss(uint rayId, uint pathIndex, out WfMaterialWorkItem hit)
{
    hit.positionT = float4(0.0f, 0.0f, 0.0f, -1.0f);
    hit.geometricNormalBaryU = 0.0f;
    hit.shadingNormalBaryV = 0.0f;
    hit.ids = 0xffffffffu;
    hit.metadata = uint4(rayId, kWfHitMiss, 0u, pathIndex);
    hit.reserved0 = 0u;
}

bool WfTraceClosest(WfRayItem ray, uint rayIndex, out WfMaterialWorkItem hit)
{
    PbrRayL6 query;
    query.origin = ray.originTMin.xyz;
    query.direction = ray.directionTMax.xyz;

    PbrHitL6 pbrHit;
    const bool found = PbrTraceClosestFixtureL6(
        query, ray.originTMin.w, ray.directionTMax.w, pbrHit);
    if (!found)
    {
        WfFixtureMiss(ray.identity.x, ray.identity.y, hit);
        return false;
    }

    hit.positionT = float4(pbrHit.position, pbrHit.t);
    hit.geometricNormalBaryU = float4(pbrHit.geometricNormal, 0.0f);
    hit.shadingNormalBaryV = float4(pbrHit.shadingNormal, 0.0f);
    // ids.x is the L6-private emitter-light index for hit-MIS; ids.y/w keep
    // primitive/material identity. No shared MaterialWork ABI is implied.
    hit.ids = uint4(
        pbrHit.emitterLightIndex,
        pbrHit.primitiveId,
        0u,
        pbrHit.materialIndex);
    hit.metadata = uint4(
        ray.identity.x,
        1u,
        pbrHit.frontFace != 0u ? 1u : 0u,
        ray.identity.y);
    hit.reserved0 = 0u;
    return true;
}

bool WfTraceAny(WfShadowWorkItem ray)
{
    PbrRayL6 query;
    query.origin = ray.originTMin.xyz;
    query.direction = ray.directionTMax.xyz;
    return PbrTraceAnyFixtureL6(
        query,
        ray.originTMin.w,
        ray.directionTMax.w,
        ray.identity.w);
}

#endif
