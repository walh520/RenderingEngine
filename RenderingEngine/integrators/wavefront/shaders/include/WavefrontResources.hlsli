#ifndef RENDERING_ENGINE_WAVEFRONT_RESOURCES_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_RESOURCES_HLSLI

#include "WavefrontTypes.hlsli"

[[vk::binding(0, 3)]] ConstantBuffer<WfFrameConstants> gWfFrame;
[[vk::push_constant]] ConstantBuffer<WfPassConstants> gWfPass;
[[vk::binding(1, 3)]] RWStructuredBuffer<WfSharedPathState> gWfSharedPaths;
[[vk::binding(2, 3)]] RWStructuredBuffer<WfRayItem> gWfRayA;
[[vk::binding(3, 3)]] RWStructuredBuffer<WfMaterialWorkItem> gWfMaterialWork;
[[vk::binding(4, 3)]] RWStructuredBuffer<WfShadowQueueItem> gWfShadowQueue;
[[vk::binding(5, 3)]] RWStructuredBuffer<WfRayItem> gWfRayB;
[[vk::binding(6, 3)]] RWStructuredBuffer<WfPathState> gWfPaths;
[[vk::binding(7, 3)]] RWStructuredBuffer<WfNextBounceCandidate> gWfDenseNext;
[[vk::binding(8, 3)]] RWStructuredBuffer<WfShadowWorkItem> gWfDenseShadow;
[[vk::binding(9, 3)]] RWStructuredBuffer<WfNextBounceCandidate> gWfNextQueue;
[[vk::binding(10, 3)]] RWStructuredBuffer<uint2> gWfFlags;
[[vk::binding(11, 3)]] RWStructuredBuffer<uint2> gWfBasePrefix;
[[vk::binding(12, 3)]] RWStructuredBuffer<uint2> gWfScanScratch;
[[vk::binding(13, 3)]] RWStructuredBuffer<WfQueueHeader> gWfQueueHeaders;
[[vk::binding(14, 3)]] RWByteAddressBuffer gWfIndirectArgs;
[[vk::binding(15, 3)]] RWStructuredBuffer<WfBounceCounters> gWfBounceCounters;

[[vk::image_format("rgba32f")]]
[[vk::binding(16, 3)]] RWTexture2D<float4> gWfOutput;
[[vk::image_format("rgba32f")]]
[[vk::binding(17, 3)]] RWTexture2D<float4> gWfDirectDiffuse;
[[vk::image_format("rgba32f")]]
[[vk::binding(18, 3)]] RWTexture2D<float4> gWfDirectSpecular;
[[vk::image_format("rgba32f")]]
[[vk::binding(19, 3)]] RWTexture2D<float4> gWfIndirectDiffuse;
[[vk::image_format("rgba32f")]]
[[vk::binding(20, 3)]] RWTexture2D<float4> gWfIndirectSpecular;
[[vk::image_format("rgba32f")]]
[[vk::binding(21, 3)]] RWTexture2D<float4> gWfDebugOutput;
[[vk::image_format("rgba32f")]]
[[vk::binding(22, 3)]] RWTexture2D<float4> gWfCameraEmission;
[[vk::binding(23, 3)]] RWStructuredBuffer<WfShadowAovItem> gWfShadowAov;

WfSharedPathState WfPackSharedPath(uint pathIndex, WfPathState state)
{
    WfSharedPathState sharedRecord = (WfSharedPathState)0;
    sharedRecord.radiance = float4(
        state.cameraEmission.xyz + state.directDiffuse.xyz +
        state.directSpecular.xyz + state.indirectDiffuse.xyz +
        state.indirectSpecular.xyz,
        0.0f);
    sharedRecord.throughput = state.throughputEta;
    sharedRecord.previousPositionPdf = state.previousPositionPdf;
    sharedRecord.rng = uint4(
        gWfFrame.capacityModeSeed.zw,
        state.identity.z,
        gWfFrame.dispatchLimits.w);
    sharedRecord.identity = uint4(
        state.identity.x,
        pathIndex,
        state.identity.z,
        state.identity.y);
    uint pathFlags = 0u;
    if ((state.identity.w & kWfPathActive) != 0u) pathFlags |= 1u << 0u;
    if ((state.identity.w & kWfPathPreviousDelta) != 0u) pathFlags |= 1u << 1u;
    if ((state.identity.w & kWfPathTerminated) != 0u) pathFlags |= 1u << 2u;
    if ((state.identity.w & kWfPathError) != 0u) pathFlags |= 1u << 3u;
    sharedRecord.metadata = uint4(pathFlags, state.identity.w, 0u, 0u);
    return sharedRecord;
}

void WfPublishSharedPath(uint pathIndex, WfPathState state)
{
    gWfSharedPaths[pathIndex] = WfPackSharedPath(pathIndex, state);
}

void WfPackShadowQueue(
    uint queueIndex,
    WfShadowWorkItem source,
    out WfShadowQueueItem sharedRecord,
    out WfShadowAovItem aov)
{
    sharedRecord.originTMin = source.originTMin;
    sharedRecord.directionTMax = source.directionTMax;
    sharedRecord.contribution = float4(
        source.diffuseContributionPdf.xyz +
        source.specularContributionLight.xyz,
        1.0f);
    sharedRecord.identity = uint4(
        queueIndex,
        source.identity.x,
        source.identity.z,
        0xffffffffu);
    const uint finiteDistance = source.directionTMax.w < 1.0e29f ? 1u : 0u;
    sharedRecord.metadata = uint4(
        source.identity.y,
        finiteDistance,
        source.identity.w,
        source.sampling.x);
    aov.diffuseContributionPdf = source.diffuseContributionPdf;
    aov.specularContributionLight = source.specularContributionLight;
}

WfShadowWorkItem WfUnpackShadowQueue(
    WfShadowQueueItem sharedRecord,
    WfShadowAovItem aov)
{
    WfShadowWorkItem result;
    result.originTMin = sharedRecord.originTMin;
    result.directionTMax = sharedRecord.directionTMax;
    result.diffuseContributionPdf = aov.diffuseContributionPdf;
    result.specularContributionLight = aov.specularContributionLight;
    result.identity = uint4(
        sharedRecord.identity.y,
        sharedRecord.metadata.x,
        sharedRecord.identity.z,
        sharedRecord.metadata.z);
    result.sampling = uint4(sharedRecord.metadata.w, 0u, 0u, 0u);
    return result;
}

uint WfFatalBitForQueue(uint queueId)
{
    if (queueId == kWfQueueRayA) return kWfFatalRayAOverflow;
    if (queueId == kWfQueueRayB) return kWfFatalRayBOverflow;
    if (queueId == kWfQueueNext) return kWfFatalNextOverflow;
    if (queueId == kWfQueueShadow) return kWfFatalShadowOverflow;
    return kWfFatalMaterialOverflow;
}

uint WfGlobalFatalMask()
{
    return gWfQueueHeaders[kWfQueueGlobal].overflowCount;
}

void WfSetFatal(uint fatalBit)
{
    uint ignored;
    InterlockedOr(gWfQueueHeaders[kWfQueueGlobal].overflowCount, fatalBit, ignored);
    if (gWfFrame.imageSample.w != 0u)
    {
        const uint bounce = min(gWfPass.pass.x, gWfFrame.imageSample.w - 1u);
        InterlockedOr(gWfBounceCounters[bounce].errors.x, fatalBit, ignored);
    }
}

bool WfTryReserve(uint queueId, out uint slot)
{
    InterlockedAdd(gWfQueueHeaders[queueId].attemptedCount, 1u, slot);
    const uint capacity = gWfQueueHeaders[queueId].capacity;
    if (slot < capacity)
    {
        return true;
    }

    uint ignored;
    InterlockedAdd(gWfQueueHeaders[queueId].overflowCount, 1u, ignored);
    WfSetFatal(WfFatalBitForQueue(queueId));
    return false;
}

uint WfReadActiveCount(uint queueId)
{
    return min(gWfQueueHeaders[queueId].attemptedCount, gWfQueueHeaders[queueId].capacity);
}

uint WfQueueGroupCount(uint itemCount)
{
    return itemCount / 128u + (itemCount % 128u != 0u ? 1u : 0u);
}

uint WfLinearQueueIndex(uint3 groupId, uint localIndex, uint itemCount)
{
    const uint groupCount = WfQueueGroupCount(itemCount);
    const uint groupsX = min(groupCount, max(gWfFrame.dispatchLimits.x, 1u));
    return (groupId.y * groupsX + groupId.x) * 128u + localIndex;
}

bool WfFinite3(float3 value)
{
    return all(isfinite(value));
}

bool WfFinite4(float4 value)
{
    return all(isfinite(value));
}

bool WfFiniteNonNegative3(float3 value)
{
    return WfFinite3(value) && all(value >= 0.0f);
}

#endif
