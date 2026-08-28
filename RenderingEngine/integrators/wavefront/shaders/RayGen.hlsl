#include "include/WavefrontResources.hlsli"
#include "include/WavefrontRng.hlsli"
#include "pbr_l6_types.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;

[numthreads(8, 8, 1)]
void RayGenCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 imageSize = gPbrFrameL6.image.xy;
    if (pixel.x >= imageSize.x || pixel.y >= imageSize.y || WfGlobalFatalMask() != 0u)
    {
        return;
    }

    const uint pixelIndex = pixel.x + pixel.y * imageSize.x;
    if (pixelIndex >= gWfFrame.capacityModeSeed.x ||
        !PbrFrameSamplingIsValidL6(gPbrFrameL6) ||
        any(gWfFrame.imageSample.xyz != gPbrFrameL6.image.xyz) ||
        gWfFrame.imageSample.w != gPbrFrameL6.image.w ||
        any(gWfFrame.capacityModeSeed.zw != gPbrFrameL6.sampling.xy) ||
        gWfFrame.capacityModeSeed.y > kWfQueueModePrefixScan ||
        gWfFrame.dispatchLimits.x == 0u || gWfFrame.dispatchLimits.y == 0u ||
        gWfFrame.dispatchLimits.z != 128u ||
        gWfFrame.dispatchLimits.w != gPbrFrameL6.output.x)
    {
        WfSetFatal(kWfFatalInvalidCapacity);
        return;
    }

    const uint sampleIndex = gPbrFrameL6.image.z;
    const uint seedLow = gPbrFrameL6.sampling.x;
    const uint seedHigh = gPbrFrameL6.sampling.y;
    const float2 jitter = float2(
        WfSampleDimension(pixelIndex, sampleIndex, kWfCameraJitterX,
            gPbrFrameL6.output.x, seedLow, seedHigh),
        WfSampleDimension(pixelIndex, sampleIndex, kWfCameraJitterY,
            gPbrFrameL6.output.x, seedLow, seedHigh));
    float2 screen = (float2(pixel) + jitter) / float2(imageSize);
    screen = screen * 2.0f - 1.0f;
    screen.y = -screen.y;

    WfRayItem ray;
    ray.originTMin = float4(
        gPbrFrameL6.cameraPositionTanHalfFov.xyz,
        gPbrFrameL6.russianRoulette.w);
    ray.directionTMax = float4(normalize(
        gPbrFrameL6.cameraForwardAspect.xyz
        + gPbrFrameL6.cameraRightLensRadius.xyz
            * (screen.x * gPbrFrameL6.cameraForwardAspect.w *
                gPbrFrameL6.cameraPositionTanHalfFov.w)
        + gPbrFrameL6.cameraUpExposure.xyz
            * (screen.y * gPbrFrameL6.cameraPositionTanHalfFov.w)), 1.0e30f);
    ray.path = uint4(pixelIndex, pixelIndex, 0u, 0u);

    WfPathState state;
    state.throughputEta = float4(1.0f, 1.0f, 1.0f, 1.0f);
    state.diffuseThroughput = 0.0f;
    state.specularThroughput = 0.0f;
    state.cameraEmission = 0.0f;
    state.directDiffuse = 0.0f;
    state.directSpecular = 0.0f;
    state.indirectDiffuse = 0.0f;
    state.indirectSpecular = 0.0f;
    state.previousPositionPdf = 0.0f;
    state.previousGeometricNormal = 0.0f;
    state.previousShadingNormal = 0.0f;
    state.identity = uint4(pixelIndex, sampleIndex, 0u, kWfPathActive | kWfPathPreviousDelta);
    gWfPaths[pixelIndex] = state;

    uint queueSlot;
    if (WfTryReserve(kWfQueueRayA, queueSlot))
    {
        gWfRayA[queueSlot] = ray;
        uint ignored;
        InterlockedAdd(gWfBounceCounters[0].work.x, 1u, ignored);
    }
}
