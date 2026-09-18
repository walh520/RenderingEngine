#include "RestirProductionV3.hlsli"

[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId != 0u)) return;
    gStatisticsV3[0] = (GpuRestirStatisticsV3)0;
}
