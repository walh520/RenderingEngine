#include "include/WavefrontResources.hlsli"

[numthreads(64, 1, 1)]
void ResetCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    const uint resetMask = gWfPass.params0.x;
    const uint resetFlags = gWfPass.params2.z;
    if (index < kWfQueueCount && ((resetMask >> index) & 1u) != 0u)
    {
        WfQueueHeader header;
        header.attemptedCount = 0u;
        header.activeCount = 0u;
        header.capacity = index == kWfQueueGlobal ? 0u : gWfFrame.capacityModeSeed.x;
        header.overflowCount = 0u;
        gWfQueueHeaders[index] = header;
    }

    if ((resetFlags & kWfResetCounters) != 0u &&
        index < gWfFrame.imageSample.w)
    {
        WfBounceCounters counters;
        counters.work = 0u;
        counters.errors = 0u;
        gWfBounceCounters[index] = counters;
    }

    if ((resetFlags & kWfResetIndirectArguments) != 0u &&
        index < gWfPass.params2.w)
    {
        gWfIndirectArgs.Store4(index * 16u, uint4(0u, 1u, 1u, 0u));
    }

    DeviceMemoryBarrierWithGroupSync();

    if (index == 0u && (resetFlags & kWfResetValidateFrame) != 0u)
    {
        const uint width = gWfFrame.imageSample.x;
        const uint height = gWfFrame.imageSample.y;
        // The published host contract limits both dimensions to 16384. Keep
        // the device-side guard explicit and avoid a dynamic uint division;
        // NVIDIA's Vulkan NVVM path rejects that division when combined with
        // this reset kernel's storage-buffer atomics.
        const bool outsideProductionExtent =
            width > 16384u || height > 16384u;
        if (width == 0u || height == 0u || outsideProductionExtent
            || gWfFrame.capacityModeSeed.x < width * height)
        {
            WfSetFatal(kWfFatalResetFrameCapacity);
        }
    }
}
