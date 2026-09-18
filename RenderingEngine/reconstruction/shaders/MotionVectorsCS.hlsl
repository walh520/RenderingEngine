#include "ReconstructionCommon.hlsli"

struct L8MotionInput
{
    float3 objectPosition;
    uint currentTransformIndex;
    uint previousTransformIndex;
    uint3 reserved;
};

[[vk::binding(1, 4)]] StructuredBuffer<L8MotionInput> gMotionInputs;
[[vk::binding(2, 4)]] StructuredBuffer<row_major float4x4> gCurrentObjectToWorld;
[[vk::binding(3, 4)]] StructuredBuffer<row_major float4x4> gPreviousObjectToWorld;
[[vk::binding(0, 4)]] RWStructuredBuffer<GpuGBufferRecordV2> gMotionGBuffer;

[[vk::binding(16, 4)]] cbuffer L8MotionConstants
{
    row_major float4x4 gCurrentViewProjection;
    row_major float4x4 gPreviousViewProjection;
    row_major float4x4 gPreviousWorldToView;
    float2 gCurrentJitterUv;
    float2 gPreviousJitterUv;
    uint2 gMotionExtent;
    uint gCurrentTransformCount;
    uint gPreviousTransformCount;
};

void L8ClearMotion(uint index)
{
    gMotionGBuffer[index].motion.motionExpectedDepth = 0.0f.xxxx;
    gMotionGBuffer[index].motion.currentPreviousUv = 0.0f.xxxx;
    gMotionGBuffer[index].motion.identity.w = kMotionFlagNoneV2;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gMotionExtent)) return;
    const uint index = L8LinearIndex(pixel, gMotionExtent);
    // Misses have no surface to reproject. Check this before reading the motion
    // input or either transform SSBO, and always leave a finite payload.
    if ((gMotionGBuffer[index].primary.identity.w &
        kPrimarySurfaceFlagValidV2) == 0u)
    {
        L8ClearMotion(index);
        return;
    }
    const L8MotionInput input = gMotionInputs[index];
    if (input.currentTransformIndex >= gCurrentTransformCount ||
        input.previousTransformIndex >= gPreviousTransformCount ||
        !all(isfinite(input.objectPosition)))
    {
        L8ClearMotion(index);
        return;
    }

    const float4 objectPosition = float4(input.objectPosition, 1.0f);
    const float4 currentWorld = mul(gCurrentObjectToWorld[input.currentTransformIndex], objectPosition);
    const float4 previousWorld = mul(gPreviousObjectToWorld[input.previousTransformIndex], objectPosition);
    const float4 currentClip = mul(gCurrentViewProjection, currentWorld);
    const float4 previousClip = mul(gPreviousViewProjection, previousWorld);
    const float4 previousView = mul(gPreviousWorldToView, previousWorld);
    const bool projectionValid = currentClip.w > 1.0e-8f && previousClip.w > 1.0e-8f &&
        all(isfinite(currentWorld)) && all(isfinite(previousWorld)) &&
        all(isfinite(currentClip)) && all(isfinite(previousClip)) && all(isfinite(previousView));
    if (!projectionValid)
    {
        L8ClearMotion(index);
        return;
    }

    const float2 currentNdc = currentClip.xy / currentClip.w;
    const float2 previousNdc = previousClip.xy / previousClip.w;
    const float2 currentUv = float2(currentNdc.x * 0.5f + 0.5f, 0.5f - currentNdc.y * 0.5f) + gCurrentJitterUv;
    const float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f) + gPreviousJitterUv;
    const float previousLinearDepth = -previousView.z;
    const bool resultValid = all(isfinite(currentUv)) && all(isfinite(previousUv)) &&
        isfinite(previousLinearDepth) && previousLinearDepth >= 0.0f;
    gMotionGBuffer[index].motion.motionExpectedDepth = float4(
        resultValid ? previousUv - currentUv : 0.0f.xx,
        resultValid ? previousLinearDepth : 0.0f,
        0.0f);
    gMotionGBuffer[index].motion.currentPreviousUv = float4(
        currentUv,
        resultValid ? previousUv : currentUv);
    gMotionGBuffer[index].motion.identity = uint4(
        gMotionGBuffer[index].primary.identity.xyz,
        resultValid ? kMotionFlagValidV2 : kMotionFlagNoneV2);
}
