// Final current-frame resolve after all one-bounce continuation steps.
// No integrator-owned accumulation; L8 owns the Progressive Film.

#ifndef PBR_L6_SIGNAL_MODE
#error PBR_L6_SIGNAL_MODE must select indirect diffuse (4), indirect specular (5), or staged Raw (6).
#endif
#if PBR_L6_SIGNAL_MODE < 4 || PBR_L6_SIGNAL_MODE > 6
#error PBR_L6_SIGNAL_MODE must be in [4, 6] for the staged resolve pass.
#endif

#include "pbr_l6_types.hlsli"
#if PBR_L6_SIGNAL_MODE == 6
#include "pbr_reconstruction_export_v2.hlsli"
#endif

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(12, 0)]] RWTexture2D<float4> gPbrRawOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(16, 0)]] RWTexture2D<float4> gPbrIndirectDiffuseOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(17, 0)]] RWTexture2D<float4> gPbrIndirectSpecularOutputL6;

struct PbrWave2IndirectSeedL6
{
    float4 rayOriginPdf;
    float4 rayDirectionEtaScale;
    float4 throughputActive;
    float4 previousPositionDelta;
    float4 previousGeometricNormal;
    float4 previousShadingNormal;
    float4 accumulatedSignal;
#if PBR_L6_SIGNAL_MODE == 6
    float4 cameraEmission;
    float4 directDiffuse;
    float4 directSpecular;
    float4 indirectDiffuse;
    float4 indirectSpecular;
#endif
};

[[vk::binding(19, 0)]]
StructuredBuffer<PbrWave2IndirectSeedL6> gPbrIndirectSeedsL6;

void PbrStoreWave2IndirectCurrentL6(uint2 pixel, float3 sampleValue)
{
#if PBR_L6_SIGNAL_MODE == 4
    gPbrIndirectDiffuseOutputL6[pixel] = float4(sampleValue, 1.0f);
#elif PBR_L6_SIGNAL_MODE == 5
    gPbrIndirectSpecularOutputL6[pixel] = float4(sampleValue, 1.0f);
#else
    gPbrRawOutputL6[pixel] = float4(sampleValue, 1.0f);
#endif
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= gPbrFrameL6.image.x || pixel.y >= gPbrFrameL6.image.y)
    {
        return;
    }
    const uint pixelIndex = pixel.y * gPbrFrameL6.image.x + pixel.x;
    const PbrWave2IndirectSeedL6 state =
        gPbrIndirectSeedsL6[pixelIndex];
    float3 signal = state.accumulatedSignal.xyz;
    if (!all(isfinite(signal)))
    {
        signal = 0.0f;
    }
    signal = max(signal, 0.0f);
    PbrStoreWave2IndirectCurrentL6(pixel, signal);
#if PBR_L6_SIGNAL_MODE == 6
    PbrPublishReconstructionSignalV2(
        pixelIndex,
        max(state.cameraEmission.xyz, 0.0f),
        max(state.directDiffuse.xyz, 0.0f),
        max(state.directSpecular.xyz, 0.0f),
        max(state.indirectDiffuse.xyz, 0.0f),
        max(state.indirectSpecular.xyz, 0.0f));
#endif
}
