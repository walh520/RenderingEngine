#include "include/WavefrontResources.hlsli"

[numthreads(128, 1, 1)]
void ScatterCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint elementCount = gWfPass.params0.y;
    const uint index = WfLinearQueueIndex(groupId, groupIndex, elementCount);
    const uint sourceQueue = gWfPass.params1.w;
    if (sourceQueue >= kWfQueueCount)
    {
        WfSetFatal(kWfFatalInvalidCapacity);
        return;
    }
    const uint sourceCount = gWfQueueHeaders[sourceQueue].activeCount;
    if (index >= elementCount || index >= sourceCount ||
        WfGlobalFatalMask() != 0u)
    {
        return;
    }

    const uint2 flags = gWfFlags[index];
    const uint2 prefix = gWfBasePrefix[index];
    if (flags.x != 0u)
    {
        if (prefix.x < gWfQueueHeaders[kWfQueueNext].capacity)
        {
            gWfNextQueue[prefix.x] = gWfDenseNext[index];
            uint ignored;
            InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].work.w, 1u, ignored);
        }
        else
        {
            WfSetFatal(kWfFatalNextOverflow | kWfFatalScanOverflow);
        }
    }
    if (flags.y != 0u)
    {
        if (prefix.y < gWfQueueHeaders[kWfQueueShadow].capacity)
        {
            gWfShadowQueue[prefix.y] = gWfDenseShadow[index];
            uint ignored;
            InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].work.z, 1u, ignored);
        }
        else
        {
            WfSetFatal(kWfFatalShadowOverflow | kWfFatalScanOverflow);
        }
    }
}
