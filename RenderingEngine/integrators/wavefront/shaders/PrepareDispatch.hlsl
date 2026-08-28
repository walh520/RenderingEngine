#include "include/WavefrontResources.hlsli"

[numthreads(1, 1, 1)]
void PrepareDispatchCS()
{
    const uint queueId = gWfPass.pass.y;
    const uint commandSlot = gWfPass.pass.w;
    WfQueueHeader header = gWfQueueHeaders[queueId];
    if (header.capacity == 0u || header.attemptedCount > header.capacity)
    {
        WfSetFatal(queueId == kWfQueueGlobal
            ? kWfFatalInvalidCapacity
            : WfFatalBitForQueue(queueId));
    }

    const uint activeCount = min(header.attemptedCount, header.capacity);
    header.activeCount = activeCount;
    gWfQueueHeaders[queueId] = header;

    uint4 command = uint4(0u, 1u, 1u, 0u);
    if (WfGlobalFatalMask() == 0u && activeCount != 0u)
    {
        const uint groups = WfQueueGroupCount(activeCount);
        const uint maxX = gWfFrame.dispatchLimits.x;
        const uint maxY = gWfFrame.dispatchLimits.y;
        const uint requiredRows = maxX == 0u
            ? 0xffffffffu
            : groups / maxX + (groups % maxX != 0u ? 1u : 0u);
        if (maxX == 0u || maxY == 0u || requiredRows > maxY)
        {
            WfSetFatal(kWfFatalDispatchOverflow);
        }
        else
        {
            command.x = min(groups, maxX);
            command.y = requiredRows;
            uint ignored;
            InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].errors.w, 1u, ignored);
        }
    }
    gWfIndirectArgs.Store4(commandSlot * 16u, command);
}
