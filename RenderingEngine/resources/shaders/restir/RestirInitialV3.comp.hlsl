#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(1, 5)]] StructuredBuffer<GpuRestirCandidateV3> gCandidatesV3;
[[vk::binding(26, 5)]] RWStructuredBuffer<GpuRestirReservoirV3>
    gInitialReservoirsV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelIndex = dispatchThreadId.x;
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    const uint candidateCount = gRestirParametersV3.extentAndCandidates.w;
    if (pixelIndex >= pixelCount || candidateCount == 0u) return;

    GpuRestirReservoirV3 reservoir = RestirEmptyReservoirV3();
    uint randomState = RestirHashV3(
        pixelIndex ^ gRestirParametersV3.generations.w ^ 0xa511e9b3u);
    for (uint candidateIndex = 0u; candidateIndex < candidateCount; ++candidateIndex)
    {
        const GpuRestirCandidateV3 candidate =
            gCandidatesV3[pixelIndex * candidateCount + candidateIndex];
        RestirUpdateReservoirV3(
            reservoir,
            candidate,
            RestirCandidateWeightV3(candidate),
            1u,
            RestirRandomV3(randomState));
    }
    RestirClampMV3(reservoir);
    RestirFinalizeReservoirV3(reservoir);
    reservoir.state.y = 0u;
    reservoir.history.surfaceIdentity = uint4(
        gPrimarySurfacesV3[pixelIndex].identity.xyz, 0u);
    reservoir.history.sceneIdentity = uint4(
        gRestirParametersV3.generations.x,
        gRestirParametersV3.generations.y,
        gRestirParametersV3.historyGenerations.x,
        gRestirParametersV3.historyGenerations.y);
    reservoir.history.frameIdentity = uint4(
        gRestirParametersV3.generations.w,
        gRestirParametersV3.modeAndFlags.w,
        gRestirParametersV3.generations.z,
        kRestirHistoryFlagValidV3);
    reservoir.history.reprojection = uint4(
        pixelIndex,
        gRestirParametersV3.extentAndCandidates.x,
        gRestirParametersV3.extentAndCandidates.y,
        kRestirRejectNoneV3);
    gInitialReservoirsV3[pixelIndex] = reservoir;

    if ((reservoir.state.z & kRestirReservoirFlagMClampedV3) != 0u)
    {
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].candidateCounts.w, 1u, ignored);
    }
}
