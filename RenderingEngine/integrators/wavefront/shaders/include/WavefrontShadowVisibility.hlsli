#ifndef RENDERING_ENGINE_WAVEFRONT_SHADOW_VISIBILITY_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_SHADOW_VISIBILITY_HLSLI

// Include after TraversalAdapter.hlsli. The selected adapter supplies
// gPbrFrameL6, WfTraceClosest and WfTraceAny; this helper reuses the same L6
// light records and deterministic RNG dimensions used when Shade chose the
// light sample.
#include "WavefrontRng.hlsli"

[[vk::binding(2, 0)]] StructuredBuffer<PbrLightGpuL6> gWfShadowLightsL6;

static const uint kWfShadowPcfFilterTaps = 8u;
static const uint kWfShadowPcssBlockerTaps = 4u;
static const uint kWfShadowPcssFilterTaps = 12u;

void WfBuildShadowBasis(
    float3 normal,
    out float3 tangent,
    out float3 bitangent)
{
    const float signValue = normal.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (signValue + normal.z);
    const float b = normal.x * normal.y * a;
    tangent = float3(
        1.0f + signValue * normal.x * normal.x * a,
        signValue * b,
        -signValue * normal.x);
    bitangent = float3(
        b,
        signValue + normal.y * normal.y * a,
        -normal.y);
}

float3 WfShadowTapDirection(
    float3 centerDirection,
    float3 tangent,
    float3 bitangent,
    uint tap,
    uint tapCount,
    float angularRadius,
    float rotation)
{
    const float radial = sqrt((float(tap) + 0.5f) / float(tapCount));
    const float angle = rotation
        + PBR_L6_TWO_PI * (float(tap) * 0.61803398875f);
    const float2 disk = radial * float2(cos(angle), sin(angle));
    return normalize(centerDirection
        + angularRadius * (disk.x * tangent + disk.y * bitangent));
}

float WfShadowKernelRadius(WfShadowWorkItem work)
{
    float worldRadius = 0.0f;
    if (work.identity.z < gPbrFrameL6.trace.w)
    {
        const PbrLightGpuL6 light = gWfShadowLightsL6[work.identity.z];
        if (light.identity.x == PBR_L6_LIGHT_SPHERE_AREA)
        {
            worldRadius = max(light.shapeParams.x, 0.0f);
        }
        else if (light.identity.x == PBR_L6_LIGHT_EMISSIVE_TRIANGLE)
        {
            worldRadius = sqrt(max(light.shapeParams.z, 0.0f) / PBR_L6_PI);
        }
    }

    const float finiteAngularRadius = work.directionTMax.w < 1.0e20f
        ? worldRadius / max(work.directionTMax.w, 1.0e-4f)
        : 0.0f;
    // Delta/infinite lights receive a small teaching kernel in PCF/PCSS; the
    // Physical mode never calls this function and retains the sampled ray.
    return clamp(max(finiteAngularRadius, 0.0025f), 0.0005f, 0.08f);
}

float WfShadowRotation(WfShadowWorkItem work)
{
    const float sample = WfSampleDimension(
        work.identity.x,
        gPbrFrameL6.image.z,
        WfBounceDimension(work.identity.y, kWfLightShape3),
        gPbrFrameL6.output.x,
        gPbrFrameL6.sampling.x,
        gPbrFrameL6.sampling.y);
    return PBR_L6_TWO_PI * sample;
}

bool WfTraceShadowClosest(
    WfShadowWorkItem work,
    uint queryIndex,
    out float blockerDistance)
{
    WfRayItem ray = (WfRayItem)0;
    ray.originTMin = work.originTMin;
    ray.directionTMax = work.directionTMax;
    ray.identity = uint4(
        queryIndex,
        work.identity.x,
        work.identity.y,
        0xffffffffu);

    WfMaterialWorkItem hit = (WfMaterialWorkItem)0;
    if (!WfTraceClosest(ray, queryIndex, hit)
        || hit.ids.y == work.identity.w
        || !isfinite(hit.positionT.w)
        || !(hit.positionT.w > work.originTMin.w)
        || !(hit.positionT.w < work.directionTMax.w))
    {
        blockerDistance = 0.0f;
        return false;
    }
    blockerDistance = hit.positionT.w;
    return true;
}

float WfEvaluateShadowVisibility(WfShadowWorkItem work, uint queryIndex)
{
    const uint shadowMethod = work.sampling.x;
    if (shadowMethod == kWfShadowMethodPhysical)
    {
        return WfTraceAny(work) ? 0.0f : 1.0f;
    }

    float3 tangent;
    float3 bitangent;
    WfBuildShadowBasis(work.directionTMax.xyz, tangent, bitangent);
    const float rotation = WfShadowRotation(work);
    float angularRadius = WfShadowKernelRadius(work);

    if (shadowMethod == kWfShadowMethodPcss)
    {
        float blockerDistanceSum = 0.0f;
        uint blockerCount = 0u;
        [unroll]
        for (uint tap = 0u; tap < kWfShadowPcssBlockerTaps; ++tap)
        {
            WfShadowWorkItem blockerRay = work;
            blockerRay.directionTMax.xyz = WfShadowTapDirection(
                work.directionTMax.xyz,
                tangent,
                bitangent,
                tap,
                kWfShadowPcssBlockerTaps,
                angularRadius * 0.5f,
                rotation);
            float blockerDistance;
            if (WfTraceShadowClosest(blockerRay, queryIndex, blockerDistance))
            {
                blockerDistanceSum += blockerDistance;
                ++blockerCount;
            }
        }
        if (blockerCount == 0u)
        {
            return 1.0f;
        }

        const float blockerDistance = blockerDistanceSum / float(blockerCount);
        const float receiverDistance = work.directionTMax.w < 1.0e20f
            ? work.directionTMax.w
            : blockerDistance * 2.0f;
        const float penumbra = max(
            (receiverDistance - blockerDistance)
                / max(blockerDistance, work.originTMin.w),
            0.0f);
        angularRadius *= clamp(penumbra, 0.25f, 4.0f);
    }

    const uint filterTapCount = shadowMethod == kWfShadowMethodPcss
        ? kWfShadowPcssFilterTaps
        : kWfShadowPcfFilterTaps;
    float visible = 0.0f;
    [loop]
    for (uint tap = 0u; tap < filterTapCount; ++tap)
    {
        WfShadowWorkItem filterRay = work;
        filterRay.directionTMax.xyz = WfShadowTapDirection(
            work.directionTMax.xyz,
            tangent,
            bitangent,
            tap,
            filterTapCount,
            angularRadius,
            rotation);
        visible += WfTraceAny(filterRay) ? 0.0f : 1.0f;
    }
    return visible / float(filterTapCount);
}

#endif
