#ifndef RENDERING_ENGINE_PBR_TRAVERSAL_RESOURCES_HLSLI
#define RENDERING_ENGINE_PBR_TRAVERSAL_RESOURCES_HLSLI

// Emissive-triangle sampling is independent from the selected visibility
// backend. These set-0 resources therefore remain common to fixture,
// flattened-SAH, and Ray Query Megakernel variants.
[[vk::binding(7, 0)]] StructuredBuffer<PbrEmitterMapEntryGpuL6> gPbrEmitterMapL6;
[[vk::binding(8, 0)]] StructuredBuffer<PbrFixtureTriangleGpuL6> gPbrFixtureTrianglesL6;
[[vk::binding(9, 0)]] StructuredBuffer<PbrFixtureSphereGpuL6> gPbrFixtureSpheresL6;

bool PbrEmitterKeyLessL6(
    uint leftInstance,
    uint leftPrimitive,
    uint rightInstance,
    uint rightPrimitive)
{
    return leftInstance < rightInstance ||
        (leftInstance == rightInstance && leftPrimitive < rightPrimitive);
}

uint PbrResolveEmitterLightL6(
    uint instanceId,
    uint primitiveId,
    uint embeddedLightIndex)
{
    if (embeddedLightIndex != PBR_L6_INVALID_INDEX)
    {
        return embeddedLightIndex < gPbrFrameL6.trace.w
            ? embeddedLightIndex
            : PBR_L6_INVALID_INDEX;
    }
    uint bufferCount;
    uint bufferStride;
    gPbrEmitterMapL6.GetDimensions(bufferCount, bufferStride);
    if (bufferStride != 16u || gPbrFrameL6.distribution.w > bufferCount)
    {
        return PBR_L6_INVALID_INDEX;
    }
    uint first = 0u;
    uint count = gPbrFrameL6.distribution.w;
    [loop]
    while (count > 0u)
    {
        const uint step = count >> 1u;
        const uint middle = first + step;
        const PbrEmitterMapEntryGpuL6 entry = gPbrEmitterMapL6[middle];
        if (PbrEmitterKeyLessL6(
            entry.instanceId, entry.primitiveId, instanceId, primitiveId))
        {
            first = middle + 1u;
            count -= step + 1u;
        }
        else
        {
            count = step;
        }
    }
    if (first < gPbrFrameL6.distribution.w)
    {
        const PbrEmitterMapEntryGpuL6 entry = gPbrEmitterMapL6[first];
        if (entry.instanceId == instanceId && entry.primitiveId == primitiveId)
        {
            return entry.lightIndex < gPbrFrameL6.trace.w
                ? entry.lightIndex
                : PBR_L6_INVALID_INDEX;
        }
    }
    return PBR_L6_INVALID_INDEX;
}

#endif
