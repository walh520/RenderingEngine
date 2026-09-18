#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(28, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(22, 5)]] StructuredBuffer<uint> gNeighborIndicesV3;
[[vk::binding(23, 5)]] RWStructuredBuffer<GpuRayQueueRecordV1> gShadowRaysV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    const uint sourceCount = gRestirParametersV3.reuseLimits.x + 1u;
    const uint raysPerVisibility = RestirShadowRayCountV3();
    const uint rayIndex = dispatchThreadId.x;
    if (sourceCount == 0u
        || rayIndex >= pixelCount * sourceCount * raysPerVisibility) return;

    const uint visibilityIndex = rayIndex / raysPerVisibility;
    const uint tapIndex = rayIndex % raysPerVisibility;
    const uint centerPixel = visibilityIndex / sourceCount;
    const uint sourceOffset = visibilityIndex % sourceCount;
    uint sourcePixel = centerPixel;
    if (sourceOffset > 0u)
    {
        sourcePixel = gNeighborIndicesV3[
            centerPixel * (sourceCount - 1u) + sourceOffset - 1u];
    }
    const GpuRestirReservoirV3 reservoir = gSpatialReservoirsV3[centerPixel];
    GpuRayQueueRecordV1 ray = (GpuRayQueueRecordV1)0;
    ray.identity = uint4(rayIndex, visibilityIndex, 0u, 0u);
    if (sourcePixel < pixelCount)
    {
        ray = RestirMakeVisibilityRayV3(
            rayIndex, visibilityIndex, tapIndex,
            reservoir, gPrimarySurfacesV3[sourcePixel]);
    }
    gShadowRaysV3[rayIndex] = ray;
    if (ray.identity.w != 0u)
    {
        uint ignored;
        InterlockedAdd(gStatisticsV3[0].visibilityCounts.x, 1u, ignored);
    }
}
