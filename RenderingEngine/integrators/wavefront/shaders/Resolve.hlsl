#include "include/WavefrontResources.hlsli"
#include "include/WavefrontReconstructionExport.hlsli"

[numthreads(8, 8, 1)]
void ResolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 imageSize = gWfFrame.imageSample.xy;
    if (pixel.x >= imageSize.x || pixel.y >= imageSize.y)
    {
        return;
    }

    const uint pixelIndex = pixel.x + pixel.y * imageSize.x;
    const uint fatalMask = WfGlobalFatalMask();
    if (fatalMask != 0u || pixelIndex >= gWfFrame.capacityModeSeed.x)
    {
        // Stable colors identify the first frame-global failure class without
        // requiring a GPU debugger: RGB=ray A/B/next, yellow=shadow,
        // cyan=material, white=dispatch, magenta=capacity/contract.
        float3 fatalColor = float3(0.5f, 0.5f, 0.5f);
        if ((fatalMask & kWfFatalRayAOverflow) != 0u) fatalColor = float3(1.0f, 0.0f, 0.0f);
        else if ((fatalMask & kWfFatalRayBOverflow) != 0u) fatalColor = float3(0.0f, 1.0f, 0.0f);
        else if ((fatalMask & kWfFatalNextOverflow) != 0u) fatalColor = float3(0.0f, 0.0f, 1.0f);
        else if ((fatalMask & kWfFatalShadowOverflow) != 0u) fatalColor = float3(1.0f, 1.0f, 0.0f);
        else if ((fatalMask & kWfFatalMaterialOverflow) != 0u) fatalColor = float3(0.0f, 1.0f, 1.0f);
        else if ((fatalMask & kWfFatalDispatchOverflow) != 0u) fatalColor = float3(1.0f, 1.0f, 1.0f);
        else if ((fatalMask & kWfFatalInvalidCapacity) != 0u) fatalColor = float3(1.0f, 0.0f, 1.0f);
        else if ((fatalMask & kWfFatalRayGenPathCapacity) != 0u) fatalColor = float3(1.0f, 0.25f, 0.0f);
        else if ((fatalMask & kWfFatalRayGenPbrFrame) != 0u) fatalColor = float3(0.5f, 0.0f, 1.0f);
        else if ((fatalMask & kWfFatalRayGenImageContract) != 0u) fatalColor = float3(0.0f, 0.5f, 1.0f);
        else if ((fatalMask & kWfFatalRayGenSeedContract) != 0u) fatalColor = float3(0.0f, 1.0f, 0.5f);
        else if ((fatalMask & kWfFatalRayGenQueueMode) != 0u) fatalColor = float3(0.5f, 1.0f, 0.0f);
        else if ((fatalMask & kWfFatalRayGenDispatchContract) != 0u) fatalColor = float3(1.0f, 0.5f, 0.0f);
        else if ((fatalMask & kWfFatalRayGenStreamContract) != 0u) fatalColor = float3(1.0f, 0.0f, 0.5f);
        else if ((fatalMask & kWfFatalResetFrameCapacity) != 0u) fatalColor = float3(0.75f, 0.1f, 0.1f);
        else if ((fatalMask & kWfFatalShadeSourceQueue) != 0u) fatalColor = float3(0.1f, 0.75f, 0.1f);
        else if ((fatalMask & kWfFatalShadePathIndex) != 0u) fatalColor = float3(0.1f, 0.1f, 0.75f);
        else if ((fatalMask & kWfFatalNextPathIndex) != 0u) fatalColor = float3(0.75f, 0.75f, 0.1f);
        else if ((fatalMask & kWfFatalShadowPathIndex) != 0u) fatalColor = float3(0.1f, 0.75f, 0.75f);
        gWfOutput[pixel] = float4(fatalColor, 1.0f);
        gWfCameraEmission[pixel] = 0.0f;
        gWfDirectDiffuse[pixel] = 0.0f;
        gWfDirectSpecular[pixel] = 0.0f;
        gWfIndirectDiffuse[pixel] = 0.0f;
        gWfIndirectSpecular[pixel] = 0.0f;
        return;
    }

    const WfPathState state = gWfPaths[pixelIndex];
    const float3 cameraEmission = state.cameraEmission.xyz;
    const float3 directDiffuse = state.directDiffuse.xyz;
    const float3 directSpecular = state.directSpecular.xyz;
    const float3 indirectDiffuse = state.indirectDiffuse.xyz;
    const float3 indirectSpecular = state.indirectSpecular.xyz;
    if (!WfFiniteNonNegative3(cameraEmission) ||
        !WfFiniteNonNegative3(directDiffuse) ||
        !WfFiniteNonNegative3(directSpecular) ||
        !WfFiniteNonNegative3(indirectDiffuse) ||
        !WfFiniteNonNegative3(indirectSpecular))
    {
        const uint bounce = min(state.identity.z, gWfFrame.imageSample.w - 1u);
        uint ignored;
        InterlockedAdd(gWfBounceCounters[bounce].errors.y, 1u, ignored);
        gWfPaths[pixelIndex].identity.w |= kWfPathError;
        WfPublishSharedPath(pixelIndex, gWfPaths[pixelIndex]);
        gWfOutput[pixel] = float4(1.0f, 1.0f, 0.0f, 1.0f);
        gWfCameraEmission[pixel] = 0.0f;
        gWfDirectDiffuse[pixel] = 0.0f;
        gWfDirectSpecular[pixel] = 0.0f;
        gWfIndirectDiffuse[pixel] = 0.0f;
        gWfIndirectSpecular[pixel] = 0.0f;
        return;
    }
    const float3 sampleValue = cameraEmission + directDiffuse + directSpecular
        + indirectDiffuse + indirectSpecular;
    WfPublishReconstructionSignalV2(
        pixelIndex,
        cameraEmission,
        directDiffuse,
        directSpecular,
        indirectDiffuse,
        indirectSpecular);
    gWfOutput[pixel] = float4(sampleValue, 1.0f);
    gWfCameraEmission[pixel] = float4(cameraEmission, 1.0f);
    gWfDirectDiffuse[pixel] = float4(directDiffuse, 1.0f);
    gWfDirectSpecular[pixel] = float4(directSpecular, 1.0f);
    gWfIndirectDiffuse[pixel] = float4(indirectDiffuse, 1.0f);
    gWfIndirectSpecular[pixel] = float4(indirectSpecular, 1.0f);
}
