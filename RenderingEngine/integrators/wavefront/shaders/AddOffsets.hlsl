#include "include/WavefrontResources.hlsli"

[numthreads(128, 1, 1)]
void AddOffsetsCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint elementCount = gWfPass.params0.y;
    const uint index = WfLinearQueueIndex(groupId, groupIndex, elementCount);
    if (index >= elementCount || WfGlobalFatalMask() != 0u)
    {
        return;
    }

    const uint parentIndex = index / 128u;
    const uint2 parentOffset = gWfScanScratch[gWfPass.params1.y + parentIndex];
    if ((gWfPass.params1.z & kWfScanChildIsBase) != 0u)
    {
        gWfBasePrefix[index] += parentOffset;
    }
    else
    {
        gWfScanScratch[gWfPass.params0.w + index] += parentOffset;
    }
}
