#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
// Motion is owned by the ABI-v2 GBuffer at set4/binding0.  Binding1 is the
// 32-byte L8 object-motion input and must never be reinterpreted as the
// 48-byte GpuMotionVectorV2 record.
[[vk::binding(0, 4)]] StructuredBuffer<GpuGBufferRecordV2> gMotionGBufferV3;
[[vk::binding(5, 5)]] StructuredBuffer<GpuPrimarySurfaceV2> gHistorySurfacesV3;
[[vk::binding(26, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gInitialReservoirsV3;
[[vk::binding(30, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gPublishedHistoryReservoirsV3;
// uint2 = current light-table index, current per-light generation.
[[vk::binding(21, 5)]] StructuredBuffer<uint2> gPreviousToCurrentLightIndexV3;
[[vk::binding(27, 5)]] RWStructuredBuffer<GpuRestirReservoirV3>
    gTemporalReservoirsV3;
[[vk::binding(15, 5)]] RWStructuredBuffer<uint> gValidationReasonsV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelIndex = dispatchThreadId.x;
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    if (pixelIndex >= pixelCount) return;

    GpuRestirReservoirV3 result = gInitialReservoirsV3[pixelIndex];
    uint rejection = kRestirRejectNoneV3;
    const bool historyEnabled = gRestirParametersV3.modeAndFlags.z != 0u;
    if (!historyEnabled)
    {
        // A disabled ablation stage did not reject any attempted reuse.
        rejection = kRestirRejectNoneV3;
    }
    else
    {
        const GpuMotionVectorV2 motion = gMotionGBufferV3[pixelIndex].motion;
        const float2 previousUv = motion.currentPreviousUv.zw;
        uint priorPixel = kRestirInvalidIndexV3;
        if ((motion.identity.w & kMotionFlagValidV2) == 0u)
            rejection = kRestirRejectMotionInvalidV3;
        else if (any(previousUv < 0.0f) || any(previousUv >= 1.0f))
            rejection = kRestirRejectReprojectionOutsideV3;
        else
        {
            const uint2 previousPixel = min(
                (uint2)(previousUv * float2(
                    gRestirParametersV3.extentAndCandidates.xy)),
                gRestirParametersV3.extentAndCandidates.xy - 1u);
            priorPixel = previousPixel.y
                * gRestirParametersV3.extentAndCandidates.x + previousPixel.x;
        }

        GpuRestirReservoirV3 history = RestirEmptyReservoirV3();
        if (priorPixel < pixelCount)
            history = gPublishedHistoryReservoirsV3[priorPixel];
        if (rejection == kRestirRejectNoneV3
            && ((history.state.z & kRestirReservoirFlagValidV3) == 0u
                || history.state.x == 0u
                || (history.history.frameIdentity.w
                    & kRestirHistoryFlagValidV3) == 0u))
            rejection = kRestirRejectInvalidCandidateV3;
        uint2 expectedPreviousFrame = uint2(
            gRestirParametersV3.generations.w,
            gRestirParametersV3.modeAndFlags.w);
        if (expectedPreviousFrame.x == 0u)
        {
            expectedPreviousFrame.x = 0xffffffffu;
            expectedPreviousFrame.y -= 1u;
        }
        else
        {
            expectedPreviousFrame.x -= 1u;
        }
        if (rejection == kRestirRejectNoneV3
            && (any(history.history.frameIdentity.xy != expectedPreviousFrame)
                || history.history.frameIdentity.z
                    != gRestirParametersV3.generations.z))
            rejection = kRestirRejectInvalidCandidateV3;
        else if (rejection == kRestirRejectNoneV3
            && history.history.sceneIdentity.x
            != gRestirParametersV3.generations.x)
            rejection = kRestirRejectSceneGenerationV3;
        else if (rejection == kRestirRejectNoneV3
            && history.history.sceneIdentity.y
            != gRestirParametersV3.generations.y)
            rejection = kRestirRejectLightGenerationV3;
        else if (rejection == kRestirRejectNoneV3
            && history.history.sceneIdentity.z
                != gRestirParametersV3.historyGenerations.x)
            rejection = kRestirRejectCameraCutV3;
        else if (rejection == kRestirRejectNoneV3
            && history.history.sceneIdentity.w
                != gRestirParametersV3.historyGenerations.y)
            rejection = kRestirRejectResizeV3;
        else if (rejection == kRestirRejectNoneV3
            && history.state.y >= gRestirParametersV3.reuseLimits.z)
            rejection = kRestirRejectAgeV3;
        else if (rejection == kRestirRejectNoneV3 && priorPixel >= pixelCount)
            rejection = kRestirRejectReprojectionOutsideV3;
        else if (rejection == kRestirRejectNoneV3)
            rejection = RestirValidateSurfacePairV3(
                gPrimarySurfacesV3[pixelIndex], gHistorySurfacesV3[priorPixel]);
        if (rejection == kRestirRejectNoneV3)
        {
            const float expectedDepth = motion.motionExpectedDepth.z;
            const float historyDepth = gHistorySurfacesV3[priorPixel]
                .worldPositionLinearDepth.w;
            const float depthScale = max(1.0f, max(abs(expectedDepth), abs(historyDepth)));
            if (abs(expectedDepth - historyDepth) / depthScale
                > gRestirParametersV3.validation.y)
                rejection = kRestirRejectDepthV3;
        }

        const uint previousLightIndex = history.selected.metadata.w;
        if (rejection == kRestirRejectNoneV3)
        {
            if (previousLightIndex >= gRestirParametersV3.lightTableCounts.y)
                rejection = kRestirRejectLightGenerationV3;
            else
            {
                const uint2 currentLight =
                    gPreviousToCurrentLightIndexV3[previousLightIndex];
                if (currentLight.x == kRestirInvalidIndexV3
                    || currentLight.x >= gRestirParametersV3.lightTableCounts.x
                || currentLight.y != history.selected.generation.x)
                    rejection = kRestirRejectLightGenerationV3;
            }
        }

        if (rejection == kRestirRejectNoneV3)
        {
            // The current production debug provider keeps the light table and
            // light geometry immutable between published history frames.  The
            // retained world-space light sample is therefore exact; only its
            // direction/distance relative to the current surface must be
            // refreshed. Dynamic-light providers must invalidate the light
            // generation until they publish a true geometry remap.
            GpuPersistentLightSampleV3 remapped = history.selected;
            const uint currentLightIndex =
                gPreviousToCurrentLightIndexV3[previousLightIndex].x;
            if (remapped.positionDistance.w < 1.0e29f)
            {
                const float3 delta = remapped.positionDistance.xyz
                    - gPrimarySurfacesV3[pixelIndex]
                        .worldPositionLinearDepth.xyz;
                const float distanceToLight = length(delta);
                if (isfinite(distanceToLight) && distanceToLight > 1.0e-6f)
                {
                    remapped.positionDistance.w = distanceToLight;
                    remapped.directionCombinedPdf.xyz =
                        delta / distanceToLight;
                }
                else
                {
                    remapped.metadata.y &= ~kRestirSampleFlagValidV3;
                }
            }
            remapped.metadata.w = currentLightIndex;
            if (remapped.identity.x != history.selected.identity.x
                || remapped.identity.y != history.selected.identity.y
                || remapped.generation.x != history.selected.generation.x)
            {
                rejection = kRestirRejectLightGenerationV3;
            }
            if (rejection == kRestirRejectNoneV3)
            {
                GpuRestirCandidateV3 candidate = RestirEvaluateCandidateV3(
                    remapped,
                    gPrimarySurfacesV3[pixelIndex],
                    history.provenance.x,
                    kRestirReuseTemporalV3,
                    priorPixel);
                candidate.targetProposalSupportCorrection.w = history.weightState.y;
                const float mergeWeight =
                    candidate.targetProposalSupportCorrection.x
                    * candidate.targetProposalSupportCorrection.z
                    * history.weightState.y * (float)history.state.x;
                uint randomState = RestirHashV3(
                    pixelIndex ^ gRestirParametersV3.generations.w ^ 0x63d83595u);
                if (RestirCandidateValidV3(candidate)
                    && isfinite(mergeWeight) && mergeWeight >= 0.0f)
                {
                    RestirUpdateReservoirV3(
                        result,
                        candidate,
                        mergeWeight,
                        history.state.x,
                        RestirRandomV3(randomState));
                    result.state.z |= kRestirReservoirFlagTemporalAcceptedV3;
                    result.state.y = min(history.state.y + 1u,
                        gRestirParametersV3.reuseLimits.z);
                    uint ignored;
                    InterlockedAdd(gStatisticsV3[0].reuseCounts.y, 1u, ignored);
                }
                else
                {
                    rejection = kRestirRejectInvalidCandidateV3;
                }
            }
        }
    }

    if (historyEnabled)
    {
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].reuseCounts.x, 1u, ignored);
    }
    RestirClampMV3(result);
    RestirFinalizeReservoirV3(result);
    result.state.w = rejection;
    result.history.reprojection.w = rejection;
    gValidationReasonsV3[pixelIndex] = rejection;
    gTemporalReservoirsV3[pixelIndex] = result;
}
