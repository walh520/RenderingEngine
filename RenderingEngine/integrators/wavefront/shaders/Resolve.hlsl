#include "include/WavefrontResources.hlsli"

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
    if (WfGlobalFatalMask() != 0u || pixelIndex >= gWfFrame.capacityModeSeed.x)
    {
        gWfOutput[pixel] = float4(1.0f, 0.0f, 1.0f, 1.0f);
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
        gWfOutput[pixel] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        gWfCameraEmission[pixel] = 0.0f;
        gWfDirectDiffuse[pixel] = 0.0f;
        gWfDirectSpecular[pixel] = 0.0f;
        gWfIndirectDiffuse[pixel] = 0.0f;
        gWfIndirectSpecular[pixel] = 0.0f;
        return;
    }
    const float3 sampleValue = cameraEmission + directDiffuse + directSpecular
        + indirectDiffuse + indirectSpecular;
    const uint sampleIndex = gWfFrame.imageSample.z;
    const float denominator = float(sampleIndex + 1u);
    const float3 previousCombined = sampleIndex == 0u ? 0.0f : gWfOutput[pixel].xyz;
    const float3 previousCameraEmission = sampleIndex == 0u
        ? 0.0f : gWfCameraEmission[pixel].xyz;
    const float3 previousDirectDiffuse = sampleIndex == 0u ? 0.0f : gWfDirectDiffuse[pixel].xyz;
    const float3 previousDirectSpecular = sampleIndex == 0u ? 0.0f : gWfDirectSpecular[pixel].xyz;
    const float3 previousIndirectDiffuse = sampleIndex == 0u ? 0.0f : gWfIndirectDiffuse[pixel].xyz;
    const float3 previousIndirectSpecular = sampleIndex == 0u ? 0.0f : gWfIndirectSpecular[pixel].xyz;

    gWfOutput[pixel] = float4(
        previousCombined + (sampleValue - previousCombined) / denominator,
        1.0f);
    gWfCameraEmission[pixel] = float4(
        previousCameraEmission + (cameraEmission - previousCameraEmission) / denominator,
        1.0f);
    gWfDirectDiffuse[pixel] = float4(
        previousDirectDiffuse + (directDiffuse - previousDirectDiffuse) / denominator,
        1.0f);
    gWfDirectSpecular[pixel] = float4(
        previousDirectSpecular + (directSpecular - previousDirectSpecular) / denominator,
        1.0f);
    gWfIndirectDiffuse[pixel] = float4(
        previousIndirectDiffuse + (indirectDiffuse - previousIndirectDiffuse) / denominator,
        1.0f);
    gWfIndirectSpecular[pixel] = float4(
        previousIndirectSpecular + (indirectSpecular - previousIndirectSpecular) / denominator,
        1.0f);
}
