#include "include/WavefrontResources.hlsli"

groupshared uint2 gWfScanShared[128];

[numthreads(128, 1, 1)]
void ScanBlocksCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint elementCount = gWfPass.params0.y;
    const uint groupCount = WfQueueGroupCount(elementCount);
    const uint groupsX = min(groupCount, max(gWfFrame.dispatchLimits.x, 1u));
    const uint linearGroup = groupId.y * groupsX + groupId.x;
    const uint index = linearGroup * 128u + groupIndex;
    const uint scanFlags = gWfPass.params1.z;
    uint2 value = 0u;
    if (index < elementCount)
    {
        if ((scanFlags & kWfScanInputIsFlags) != 0u)
        {
            const uint sourceQueue = gWfPass.params1.w;
            const uint sourceCount = sourceQueue < kWfQueueCount
                ? gWfQueueHeaders[sourceQueue].activeCount
                : 0u;
            if (sourceQueue >= kWfQueueCount)
            {
                WfSetFatal(kWfFatalInvalidCapacity);
            }
            // The hierarchy is allocated for full capacity. Mask inactive tail
            // elements so stale flags from an earlier bounce cannot create work.
            value = index < sourceCount ? gWfFlags[index] : uint2(0u, 0u);
        }
        else
        {
            value = gWfScanScratch[gWfPass.params0.z + index];
        }
        if (any(value > 1u) && (scanFlags & kWfScanInputIsFlags) != 0u)
        {
            WfSetFatal(kWfFatalScanOverflow);
            value = 0u;
        }
    }
    gWfScanShared[groupIndex] = value;

    [unroll]
    for (uint stride = 1u; stride < 128u; stride <<= 1u)
    {
        GroupMemoryBarrierWithGroupSync();
        const uint target = (groupIndex + 1u) * stride * 2u - 1u;
        if (target < 128u)
        {
            gWfScanShared[target] += gWfScanShared[target - stride];
        }
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 blockTotal = gWfScanShared[127u];
    if (groupIndex == 0u)
    {
        gWfScanScratch[gWfPass.params1.x + linearGroup] = blockTotal;
        gWfScanShared[127u] = 0u;

        if ((scanFlags & kWfScanTopLevel) != 0u)
        {
            const uint nextCapacity = gWfQueueHeaders[kWfQueueNext].capacity;
            const uint shadowCapacity = gWfQueueHeaders[kWfQueueShadow].capacity;
            WfQueueHeader nextHeader = gWfQueueHeaders[kWfQueueNext];
            WfQueueHeader shadowHeader = gWfQueueHeaders[kWfQueueShadow];
            nextHeader.attemptedCount = blockTotal.x;
            nextHeader.activeCount = min(blockTotal.x, nextCapacity);
            shadowHeader.attemptedCount = blockTotal.y;
            shadowHeader.activeCount = min(blockTotal.y, shadowCapacity);
            if (blockTotal.x > nextCapacity)
            {
                ++nextHeader.overflowCount;
                WfSetFatal(kWfFatalNextOverflow | kWfFatalScanOverflow);
            }
            if (blockTotal.y > shadowCapacity)
            {
                ++shadowHeader.overflowCount;
                WfSetFatal(kWfFatalShadowOverflow | kWfFatalScanOverflow);
            }
            gWfQueueHeaders[kWfQueueNext] = nextHeader;
            gWfQueueHeaders[kWfQueueShadow] = shadowHeader;
        }
    }

    [unroll]
    for (uint stride = 64u; stride > 0u; stride >>= 1u)
    {
        GroupMemoryBarrierWithGroupSync();
        const uint target = (groupIndex + 1u) * stride * 2u - 1u;
        if (target < 128u)
        {
            const uint2 left = gWfScanShared[target - stride];
            gWfScanShared[target - stride] = gWfScanShared[target];
            gWfScanShared[target] += left;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (index < elementCount)
    {
        if ((scanFlags & kWfScanPrefixIsBase) != 0u)
        {
            gWfBasePrefix[index] = gWfScanShared[groupIndex];
        }
        else
        {
            gWfScanScratch[gWfPass.params0.w + index] = gWfScanShared[groupIndex];
        }
    }
}
