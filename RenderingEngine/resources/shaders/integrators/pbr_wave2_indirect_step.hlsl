// One-bounce closest-hit step for a selected Wave 5 indirect AOV.
//
// This pass deliberately carries only the continuation ray across traversal.
// The complete path state remains in binding 19 and the hit is published into
// four unselected debug images. The paired shade pass consumes that scratch.

#ifndef PBR_L6_SIGNAL_MODE
#error PBR_L6_SIGNAL_MODE must select indirect diffuse (4), indirect specular (5), or staged Raw (6).
#endif
#if PBR_L6_SIGNAL_MODE < 4 || PBR_L6_SIGNAL_MODE > 6
#error PBR_L6_SIGNAL_MODE must be in [4, 6] for the staged trace step.
#endif

#if defined(PBR_L6_WAVE2_PRODUCTION) && \
    !defined(PBR_L6_MATERIAL_MR_SMOOTH_ONLY) && \
    !defined(PBR_L6_MATERIAL_SHOWCASE_ONLY)
#define PBR_L6_MATERIAL_SHOWCASE_ONLY
#endif

#define CSMain PbrWave2UnusedTraceGenericEntryL6
#include "pbr_megakernel.hlsl"
#undef CSMain

struct PbrWave2IndirectStateL6
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

struct PbrWave2IndirectStepConstantsL6
{
    uint depth;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

[[vk::binding(19, 0)]]
StructuredBuffer<PbrWave2IndirectStateL6> gPbrIndirectStatesL6;
[[vk::push_constant]]
ConstantBuffer<PbrWave2IndirectStepConstantsL6> gPbrIndirectStepL6;

void PbrStoreIndirectHitScratchL6(
    uint2 pixel,
    float4 positionT,
    float4 geometricFront,
    float4 shadingMaterial,
    float4 emitterStatus)
{
    gPbrCameraEmissionOutputL6[pixel] = positionT;
    gPbrDirectDiffuseOutputL6[pixel] = geometricFront;
    gPbrDirectSpecularOutputL6[pixel] = shadingMaterial;
#if PBR_L6_SIGNAL_MODE == 4
    gPbrIndirectSpecularOutputL6[pixel] = emitterStatus;
#else
    gPbrIndirectDiffuseOutputL6[pixel] = emitterStatus;
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
    if (!(gPbrIndirectStatesL6[pixelIndex].throughputActive.w > 0.5f))
    {
        return;
    }
    const uint depth = gPbrIndirectStepL6.depth;
    if (depth >= gPbrFrameL6.image.w)
    {
        return;
    }

    PbrRayL6 ray;
    ray.origin = gPbrIndirectStatesL6[pixelIndex].rayOriginPdf.xyz;
    ray.direction = gPbrIndirectStatesL6[pixelIndex].rayDirectionEtaScale.xyz;
    PbrHitL6 hit;
    const bool hitFound = PBR_L6_TRACE_CLOSEST(
        ray, gPbrFrameL6.russianRoulette.w, 1.0e30f, hit);

    float4 positionT = 0.0f;
    float4 geometricFront = 0.0f;
    float4 shadingMaterial = 0.0f;
    float4 emitterStatus = 0.0f;
    if (hitFound)
    {
        positionT = float4(hit.position, hit.t);
        geometricFront = float4(
            hit.geometricNormal, hit.frontFace != 0u ? 1.0f : 0.0f);
        shadingMaterial = float4(hit.shadingNormal, float(hit.materialIndex));
        emitterStatus = float4(
            hit.emitterLightIndex == PBR_L6_INVALID_INDEX
                ? -1.0f
                : float(hit.emitterLightIndex),
            asfloat(hit.instanceId),
            asfloat(hit.primitiveId),
            1.0f);
    }
    PbrStoreIndirectHitScratchL6(
        pixel, positionT, geometricFront, shadingMaterial, emitterStatus);
}
