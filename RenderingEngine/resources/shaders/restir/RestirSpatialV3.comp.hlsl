#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(27, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gTemporalReservoirsV3;
[[vk::binding(22, 5)]] StructuredBuffer<uint> gNeighborIndicesV3;
[[vk::binding(28, 5)]] RWStructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(15, 5)]] RWStructuredBuffer<uint> gValidationReasonsV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelIndex = dispatchThreadId.x;
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    const uint neighborCount = gRestirParametersV3.reuseLimits.x;
    if (pixelIndex >= pixelCount) return;

    GpuRestirReservoirV3 result = RestirEmptyReservoirV3();
    uint randomState = RestirHashV3(
        pixelIndex ^ gRestirParametersV3.generations.w ^ 0x9e3779b9u);
    uint aggregateRejection = gValidationReasonsV3[pixelIndex];

    const GpuRestirReservoirV3 center = gTemporalReservoirsV3[pixelIndex];
    // Initial-only and Temporal-only are exact stage ablations: with no
    // spatial sources, do not resample or perturb the incoming reservoir.
    if (neighborCount == 0u)
    {
        gSpatialReservoirsV3[pixelIndex] = center;
        return;
    }
    GpuRestirCandidateV3 centerCandidate = RestirEvaluateCandidateV3(
        center.selected,
        gPrimarySurfacesV3[pixelIndex],
        center.provenance.x,
        center.provenance.y,
        pixelIndex);
    centerCandidate.targetProposalSupportCorrection.w = center.weightState.y;
    RestirUpdateReservoirV3(
        result,
        centerCandidate,
        centerCandidate.targetProposalSupportCorrection.x
            * centerCandidate.targetProposalSupportCorrection.z
            * center.weightState.y * (float)center.state.x,
        center.state.x,
        RestirRandomV3(randomState));
    result.state.z |= center.state.z
        & (kRestirReservoirFlagMClampedV3
            | kRestirReservoirFlagTemporalAcceptedV3);

    for (uint neighborOffset = 0u; neighborOffset < neighborCount; ++neighborOffset)
    {
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].reuseCounts.z, 1u, ignored);
        const uint neighborIndex =
            gNeighborIndicesV3[pixelIndex * neighborCount + neighborOffset];
        if (neighborIndex >= pixelCount)
        {
            aggregateRejection = kRestirRejectReprojectionOutsideV3;
            continue;
        }
        const uint rejection = RestirValidateSurfacePairV3(
            gPrimarySurfacesV3[pixelIndex], gPrimarySurfacesV3[neighborIndex]);
        const GpuRestirReservoirV3 neighbor = gTemporalReservoirsV3[neighborIndex];
        if (rejection != kRestirRejectNoneV3
            || (neighbor.state.z & kRestirReservoirFlagValidV3) == 0u)
        {
            aggregateRejection = rejection != kRestirRejectNoneV3
                ? rejection : kRestirRejectInvalidCandidateV3;
            continue;
        }

        GpuRestirCandidateV3 candidate = RestirEvaluateCandidateV3(
            neighbor.selected,
            gPrimarySurfacesV3[pixelIndex],
            neighbor.provenance.x,
            kRestirReuseSpatialV3,
            neighborIndex);
        candidate.targetProposalSupportCorrection.w = neighbor.weightState.y;
        const float mergeWeight =
            candidate.targetProposalSupportCorrection.x
            * candidate.targetProposalSupportCorrection.z
            * neighbor.weightState.y * (float)neighbor.state.x;
        if (RestirCandidateValidV3(candidate)
            && isfinite(mergeWeight) && mergeWeight >= 0.0f)
        {
            RestirUpdateReservoirV3(
                result,
                candidate,
                mergeWeight,
                neighbor.state.x,
                RestirRandomV3(randomState));
            result.state.z |= kRestirReservoirFlagSpatialAcceptedV3;
            InterlockedAdd(gStatisticsV3[0].reuseCounts.w, 1u, ignored);
        }
        else
        {
            aggregateRejection = kRestirRejectInvalidCandidateV3;
        }
    }

    RestirClampMV3(result);
    RestirFinalizeReservoirV3(result);
    result.state.y = center.state.y;
    result.history = center.history;
    result.history.reprojection.x = pixelIndex;
    result.history.reprojection.w = aggregateRejection;
    result.state.w = aggregateRejection;
    gValidationReasonsV3[pixelIndex] = aggregateRejection;
    gSpatialReservoirsV3[pixelIndex] = result;
}
