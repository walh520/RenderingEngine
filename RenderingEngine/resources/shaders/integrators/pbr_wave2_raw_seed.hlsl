// Camera-ray seed for the finite staged Raw path used by every Wave 2 scene.

#include "pbr_l6_types.hlsli"
#include "pbr_reconstruction_export_v2.hlsli"

[[vk::binding(0, 0)]]
ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;

struct PbrWave2RawStateL6
{
    float4 rayOriginPdf;
    float4 rayDirectionEtaScale;
    float4 throughputActive;
    float4 previousPositionDelta;
    float4 previousGeometricNormal;
    float4 previousShadingNormal;
    float4 accumulatedSignal;
    float4 cameraEmission;
    float4 directDiffuse;
    float4 directSpecular;
    float4 indirectDiffuse;
    float4 indirectSpecular;
};

[[vk::binding(19, 0)]]
RWStructuredBuffer<PbrWave2RawStateL6> gPbrRawStatesL6;

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= gPbrFrameL6.image.x || pixel.y >= gPbrFrameL6.image.y)
    {
        return;
    }
    const uint pixelIndex = pixel.y * gPbrFrameL6.image.x + pixel.x;
    PbrClearReconstructionInputsV2(pixelIndex);
    PbrWave2RawStateL6 state;
    state.rayOriginPdf = 0.0f;
    state.rayDirectionEtaScale = 0.0f;
    state.throughputActive = 0.0f;
    state.previousPositionDelta = float4(0.0f, 0.0f, 0.0f, 1.0f);
    state.previousGeometricNormal = 0.0f;
    state.previousShadingNormal = 0.0f;
    state.accumulatedSignal = 0.0f;
    state.cameraEmission = 0.0f;
    state.directDiffuse = 0.0f;
    state.directSpecular = 0.0f;
    state.indirectDiffuse = 0.0f;
    state.indirectSpecular = 0.0f;
    if (gPbrFrameL6.image.x == 0u || gPbrFrameL6.image.y == 0u
        || gPbrFrameL6.image.z == 0xffffffffu || gPbrFrameL6.image.w == 0u)
    {
        gPbrRawStatesL6[pixelIndex] = state;
        return;
    }

    const uint sampleIndex = gPbrFrameL6.image.z;
    const uint2 seed = gPbrFrameL6.sampling.xy;
    const uint streamTag = gPbrFrameL6.output.x;
    const float2 jitter = float2(
        PbrCounterRandomL6(pixelIndex, sampleIndex, 0u, streamTag, seed),
        PbrCounterRandomL6(pixelIndex, sampleIndex, 1u, streamTag, seed));
    const float2 uv = (float2(pixel) + jitter) / float2(gPbrFrameL6.image.xy);
    const float2 ndc = float2(2.0f * uv.x - 1.0f, 1.0f - 2.0f * uv.y);
    const float3 direction = normalize(
        gPbrFrameL6.cameraForwardAspect.xyz
        + gPbrFrameL6.cameraRightLensRadius.xyz
            * (ndc.x * gPbrFrameL6.cameraForwardAspect.w
                * gPbrFrameL6.cameraPositionTanHalfFov.w)
        + gPbrFrameL6.cameraUpExposure.xyz
            * (ndc.y * gPbrFrameL6.cameraPositionTanHalfFov.w));
    if (!all(isfinite(direction)))
    {
        gPbrRawStatesL6[pixelIndex] = state;
        return;
    }
    state.rayOriginPdf = float4(
        gPbrFrameL6.cameraPositionTanHalfFov.xyz, 0.0f);
    state.rayDirectionEtaScale = float4(direction, 1.0f);
    state.throughputActive = float4(1.0f, 1.0f, 1.0f, 1.0f);
    gPbrRawStatesL6[pixelIndex] = state;
}
