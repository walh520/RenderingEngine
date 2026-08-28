#include "include/WavefrontResources.hlsli"

[numthreads(8, 8, 1)]
void VisualizeCountersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 imageSize = gWfFrame.imageSample.xy;
    if (pixel.x >= imageSize.x || pixel.y >= imageSize.y)
    {
        return;
    }

    if (WfGlobalFatalMask() != 0u)
    {
        gWfDebugOutput[pixel] = float4(1.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const uint bounceCount = max(gWfFrame.imageSample.w, 1u);
    const uint bounce = min(pixel.y * bounceCount / max(imageSize.y, 1u), bounceCount - 1u);
    const WfBounceCounters counters = gWfBounceCounters[bounce];
    const float capacity = float(max(gWfFrame.capacityModeSeed.x, 1u));
    const float x = float(pixel.x) / float(max(imageSize.x - 1u, 1u));
    const float active = float(counters.work.x) / capacity;
    const float shadow = float(counters.work.z) / capacity;
    const float next = float(counters.work.w) / capacity;
    float3 color = float3(0.025f, 0.025f, 0.025f);
    color.r += x <= active ? 0.85f : 0.0f;
    color.g += x <= next ? 0.75f : 0.0f;
    color.b += x <= shadow ? 0.90f : 0.0f;
    gWfDebugOutput[pixel] = float4(color, 1.0f);
}
