#include "RestirProductionV3.hlsli"

[[vk::binding(28, 5)]] RWStructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(10, 5)]] StructuredBuffer<GpuHitQueueRecordV1>
    gWinnerVisibilityV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

float RestirResolveWinnerVisibilityV3(
    uint queryIndex,
    GpuRestirReservoirV3 reservoir)
{
    const uint shadowMode = gRestirParametersV3.historyGenerations.w;
    const uint raysPerVisibility = RestirShadowRayCountV3();
    const uint baseRayIndex = queryIndex * raysPerVisibility;
    if (shadowMode == kRestirShadowPhysicalV3)
    {
        return RestirBinaryVisibilityV3(
            gWinnerVisibilityV3[baseRayIndex], baseRayIndex, queryIndex);
    }
    if (shadowMode == kRestirShadowPcfV3)
    {
        float visibility = 0.0f;
        [unroll]
        for (uint tap = 0u; tap < kRestirPcfFilterRayCountV3; ++tap)
        {
            visibility += RestirBinaryVisibilityV3(
                gWinnerVisibilityV3[baseRayIndex + tap],
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
        const GpuHitQueueRecordV1 hit = gWinnerVisibilityV3[rayIndex];
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
            gWinnerVisibilityV3[rayIndex], rayIndex, queryIndex);
        ++evaluated;
    }
    return evaluated != 0u ? visibility / float(evaluated) : 0.0f;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelIndex = dispatchThreadId.x;
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    if (pixelIndex >= pixelCount) return;

    GpuRestirReservoirV3 reservoir = gSpatialReservoirsV3[pixelIndex];
    const bool valid =
        (reservoir.state.z & kRestirReservoirFlagValidV3) != 0u;
    const bool duplicate =
        (reservoir.state.z & kRestirReservoirFlagFinalVisibilityEvaluatedV3) != 0u;
    if (valid && !duplicate)
    {
        const float visibility = RestirResolveWinnerVisibilityV3(
            pixelIndex, reservoir);
        const float finalWeight = reservoir.weightState.y * visibility;
        const bool finiteResult = isfinite(visibility)
            && visibility >= 0.0f && visibility <= 1.0f
            && isfinite(finalWeight) && finalWeight >= 0.0f;
        const bool visible = finiteResult && visibility > 0.0f;
        reservoir.state.z |= kRestirReservoirFlagFinalVisibilityEvaluatedV3;
        reservoir.selectedTerms.w = finiteResult ? finalWeight : 0.0f;
        if (!finiteResult)
        {
            reservoir.state.z &= ~kRestirReservoirFlagValidV3;
            reservoir.state.w = kRestirRejectInvalidCandidateV3;
            reservoir.history.reprojection.w = kRestirRejectInvalidCandidateV3;
        }
        if (visible)
            reservoir.state.z |= kRestirReservoirFlagVisibilityValidV3;
        else
            reservoir.state.z &= ~kRestirReservoirFlagVisibilityValidV3;
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].visibilityCounts.y, 1u, ignored);
        if (visible)
            InterlockedAdd(gStatisticsV3[0].visibilityCounts.z, 1u, ignored);
    }
    else if (duplicate)
    {
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].visibilityCounts.w, 1u, ignored);
    }
    gSpatialReservoirsV3[pixelIndex] = reservoir;
}
