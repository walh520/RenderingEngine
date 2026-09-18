#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(28, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(29, 5)]] RWStructuredBuffer<GpuRestirReservoirV3>
    gPublishedReservoirsV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelIndex = dispatchThreadId.x;
    if (pixelIndex >= gRestirParametersV3.extentAndCandidates.z) return;
    GpuRestirReservoirV3 reservoir = gSpatialReservoirsV3[pixelIndex];
    reservoir.history.surfaceIdentity = uint4(
        gPrimarySurfacesV3[pixelIndex].identity.xyz, 0u);
    reservoir.history.sceneIdentity.xy = gRestirParametersV3.generations.xy;
    const uint historyFlags =
        (reservoir.state.z & kRestirReservoirFlagValidV3) != 0u
        && (reservoir.state.z & kRestirReservoirFlagFinalVisibilityEvaluatedV3) != 0u
        ? kRestirHistoryFlagValidV3 : kRestirHistoryFlagResetV3;
    reservoir.history.frameIdentity = uint4(
        gRestirParametersV3.generations.w,
        gRestirParametersV3.modeAndFlags.w,
        gRestirParametersV3.generations.z,
        historyFlags);
    reservoir.history.reprojection = uint4(
        pixelIndex,
        gRestirParametersV3.extentAndCandidates.x,
        gRestirParametersV3.extentAndCandidates.y,
        reservoir.history.reprojection.w);
    gPublishedReservoirsV3[pixelIndex] = reservoir;
}
