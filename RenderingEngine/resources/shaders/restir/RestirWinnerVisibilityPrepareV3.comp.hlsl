#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(28, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(23, 5)]] RWStructuredBuffer<GpuRayQueueRecordV1> gShadowRaysV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    const uint raysPerVisibility = RestirShadowRayCountV3();
    const uint rayIndex = dispatchThreadId.x;
    if (rayIndex >= pixelCount * raysPerVisibility) return;

    const uint pixelIndex = rayIndex / raysPerVisibility;
    const uint tapIndex = rayIndex % raysPerVisibility;

    const GpuRestirReservoirV3 reservoir = gSpatialReservoirsV3[pixelIndex];
    const GpuPrimarySurfaceV2 surface = gPrimarySurfacesV3[pixelIndex];
    GpuRayQueueRecordV1 ray = (GpuRayQueueRecordV1)0;
    ray.identity = uint4(rayIndex, pixelIndex, 0u, 0u);
    if ((reservoir.state.z
            & kRestirReservoirFlagFinalVisibilityEvaluatedV3) == 0u)
    {
        ray = RestirMakeVisibilityRayV3(
            rayIndex, pixelIndex, tapIndex, reservoir, surface);
    }
    if (ray.identity.w != 0u)
    {
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].visibilityCounts.x, 1u, ignored);
    }
    gShadowRaysV3[rayIndex] = ray;
}
