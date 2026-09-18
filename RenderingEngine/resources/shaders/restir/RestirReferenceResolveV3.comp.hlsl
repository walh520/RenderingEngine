#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(27, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gTemporalReservoirsV3;
[[vk::binding(22, 5)]] StructuredBuffer<uint> gNeighborIndicesV3;
[[vk::binding(19, 5)]] StructuredBuffer<GpuHitQueueRecordV1>
    gReferenceVisibilityV3;
[[vk::binding(28, 5)]] RWStructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

float RestirResolveReferenceVisibilityV3(
    uint queryIndex,
    GpuRestirReservoirV3 reservoir)
{
    const uint shadowMode = gRestirParametersV3.historyGenerations.w;
    const uint raysPerVisibility = RestirShadowRayCountV3();
    const uint baseRayIndex = queryIndex * raysPerVisibility;
    if (shadowMode == kRestirShadowPhysicalV3)
    {
        return RestirBinaryVisibilityV3(
            gReferenceVisibilityV3[baseRayIndex], baseRayIndex, queryIndex);
    }
    if (shadowMode == kRestirShadowPcfV3)
    {
        float visibility = 0.0f;
        [unroll]
        for (uint tap = 0u; tap < kRestirPcfFilterRayCountV3; ++tap)
        {
            visibility += RestirBinaryVisibilityV3(
                gReferenceVisibilityV3[baseRayIndex + tap],
                baseRayIndex + tap, queryIndex);
        }
        return visibility / float(kRestirPcfFilterRayCountV3);
    }

    float blockerDistanceSum = 0.0f;
    uint blockerCount = 0u;
    [unroll]
    for (uint tap = 0u; tap < kRestirPcssBlockerRayCountV3; ++tap)
    {
        const uint rayIndex = baseRayIndex + tap;
        const GpuHitQueueRecordV1 hit = gReferenceVisibilityV3[rayIndex];
        if (!RestirVisibilityHitMatchesV3(hit, rayIndex, queryIndex))
            return 0.0f;
        if (hit.metadata.y != kHitKindMissV0)
        {
            if (!isfinite(hit.positionT.w) || !(hit.positionT.w > 0.0f))
                return 0.0f;
            blockerDistanceSum += hit.positionT.w;
            ++blockerCount;
        }
    }
    if (blockerCount == 0u) return 1.0f;

    const float blockerDistance = blockerDistanceSum / float(blockerCount);
    const float receiverDistance =
        isfinite(reservoir.selected.positionDistance.w)
            && reservoir.selected.positionDistance.w < 1.0e20f
        ? reservoir.selected.positionDistance.w
        : blockerDistance * 2.0f;
    const float penumbraRatio = clamp(
        (receiverDistance - blockerDistance) / max(blockerDistance, 1.0e-4f),
        0.25f, 4.0f);
    float visibility = 0.0f;
    uint evaluated = 0u;
    [unroll]
    for (uint tap = 0u; tap < kRestirPcssFilterRayCountV3; ++tap)
    {
        if (RestirPcssFilterKernelRatioV3(tap) > penumbraRatio + 1.0e-5f)
            continue;
        const uint rayIndex = baseRayIndex
            + kRestirPcssBlockerRayCountV3 + tap;
        visibility += RestirBinaryVisibilityV3(
            gReferenceVisibilityV3[rayIndex], rayIndex, queryIndex);
        ++evaluated;
    }
    return evaluated != 0u ? visibility / float(evaluated) : 0.0f;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint centerPixel = dispatchThreadId.x;
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    const uint sourceCount = gRestirParametersV3.reuseLimits.x + 1u;
    if (centerPixel >= pixelCount) return;

    GpuRestirReservoirV3 reservoir = gSpatialReservoirsV3[centerPixel];
    if ((reservoir.state.z & kRestirReservoirFlagValidV3) == 0u)
    {
        gSpatialReservoirsV3[centerPixel] = reservoir;
        return;
    }

    float denominator = 0.0f;
    float selectedSourceTarget = 0.0f;
    float currentTarget = 0.0f;
    uint evaluated = 0u;
    uint visibleCount = 0u;
    for (uint sourceOffset = 0u; sourceOffset < sourceCount; ++sourceOffset)
    {
        uint sourcePixel = centerPixel;
        if (sourceOffset > 0u)
        {
            sourcePixel = gNeighborIndicesV3[
                centerPixel * (sourceCount - 1u) + sourceOffset - 1u];
        }
        if (sourcePixel >= pixelCount) continue;
        const uint visibilityIndex = centerPixel * sourceCount + sourceOffset;
        const float visibility = RestirResolveReferenceVisibilityV3(
            visibilityIndex, reservoir);
        const uint visible = visibility > 0.0f ? 1u : 0u;
        float3 diffuse;
        float3 specular;
        float target;
        RestirEvaluateUnshadowedV3(
            reservoir.selected, gPrimarySurfacesV3[sourcePixel],
            diffuse, specular, target);
        const uint sourceM = max(gTemporalReservoirsV3[sourcePixel].state.x, 1u);
        denominator += (float)sourceM * target * visibility;
        if (sourcePixel == centerPixel) currentTarget = target;
        if (sourcePixel == reservoir.provenance.z)
            selectedSourceTarget = target * visibility;
        ++evaluated;
        visibleCount += visible;
    }

    const float normalizationDenominator = currentTarget * denominator;
    reservoir.weightState.y = normalizationDenominator > 0.0f
        ? reservoir.weightState.x * selectedSourceTarget
            / normalizationDenominator
        : 0.0f;
    reservoir.selectedTerms.w = reservoir.weightState.y;
    reservoir.state.z |= kRestirReservoirFlagReferenceModeV3;
    if (!(reservoir.weightState.y > 0.0f) || !isfinite(reservoir.weightState.y))
        reservoir.state.z &= ~kRestirReservoirFlagValidV3;
    gSpatialReservoirsV3[centerPixel] = reservoir;

    uint ignored;
    InterlockedAdd(gStatisticsV3[0].visibilityCounts.y, evaluated, ignored);
    InterlockedAdd(gStatisticsV3[0].visibilityCounts.z, visibleCount, ignored);
}
