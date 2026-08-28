#include "include/WavefrontResources.hlsli"
#include "include/TraversalAdapter.hlsli"

[numthreads(128, 1, 1)]
void IntersectCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint sourceQueue = gWfPass.pass.y;
    const uint activeCount = gWfQueueHeaders[sourceQueue].activeCount;
    const uint index = WfLinearQueueIndex(groupId, groupIndex, activeCount);
    if (index >= activeCount || WfGlobalFatalMask() != 0u)
    {
        return;
    }
    if (index >= gWfQueueHeaders[kWfQueueMaterial].capacity)
    {
        WfSetFatal(kWfFatalMaterialOverflow);
        return;
    }

    WfRayItem ray;
    if (sourceQueue == kWfQueueRayA)
    {
        ray = gWfRayA[index];
    }
    else
    {
        ray = gWfRayB[index];
    }
    WfMaterialWorkItem hit;
    const bool found = WfTraceClosest(ray, index, hit);
    gWfMaterialWork[index] = hit;
    if (found)
    {
        uint ignored;
        InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].work.y, 1u, ignored);
    }
}
