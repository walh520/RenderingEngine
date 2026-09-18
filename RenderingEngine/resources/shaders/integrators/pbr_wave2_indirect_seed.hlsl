// Wave 5 selected indirect-AOV seed pass.
//
// The primary hit and BSDF classification are intentionally separated from
// continuation tracing. This bounds live shader state on drivers that lose the
// device when both phases are JITed into one Ray Query Megakernel.

#ifndef PBR_L6_SIGNAL_MODE
#error PBR_L6_SIGNAL_MODE must select indirect diffuse (4) or indirect specular (5).
#endif
#if PBR_L6_SIGNAL_MODE < 4 || PBR_L6_SIGNAL_MODE > 5
#error PBR_L6_SIGNAL_MODE must be 4 or 5 for the indirect seed pass.
#endif

#if defined(PBR_L6_WAVE2_PRODUCTION) && \
    !defined(PBR_L6_MATERIAL_MR_SMOOTH_ONLY) && \
    !defined(PBR_L6_MATERIAL_SHOWCASE_ONLY)
#define PBR_L6_MATERIAL_SHOWCASE_ONLY
#endif

#define CSMain PbrWave2UnusedSeedGenericEntryL6
#include "pbr_megakernel.hlsl"
#undef CSMain

struct PbrWave2IndirectSeedL6
{
    float4 rayOriginPdf;
    float4 rayDirectionEtaScale;
    float4 throughputActive;
    float4 previousPositionDelta;
    float4 previousGeometricNormal;
    float4 previousShadingNormal;
    float4 accumulatedSignal;
};

[[vk::binding(19, 0)]]
RWStructuredBuffer<PbrWave2IndirectSeedL6> gPbrIndirectSeedsL6;

PbrWave2IndirectSeedL6 PbrZeroIndirectSeedL6()
{
    PbrWave2IndirectSeedL6 result;
    result.rayOriginPdf = 0.0f;
    result.rayDirectionEtaScale = 0.0f;
    result.throughputActive = 0.0f;
    result.previousPositionDelta = 0.0f;
    result.previousGeometricNormal = 0.0f;
    result.previousShadingNormal = 0.0f;
    result.accumulatedSignal = 0.0f;
    return result;
}

bool PbrWave2SeedFrameIsValidL6()
{
    bool traversalValid = false;
#if defined(PBR_L6_TRAVERSAL_SOFTWARE)
    traversalValid =
        gPbrFrameL6.traversal.x == PBR_L6_TRAVERSAL_FLATTENED_SAH &&
        gPbrFrameL6.traversal.y > 0u &&
        gPbrFrameL6.traversal.z > 0u;
#elif defined(PBR_L6_TRAVERSAL_RAY_QUERY)
    traversalValid =
        gPbrFrameL6.traversal.x == PBR_L6_TRAVERSAL_HARDWARE_RAY_QUERY;
#endif
    return gPbrFrameL6.image.x != 0u &&
        gPbrFrameL6.image.y != 0u &&
        gPbrFrameL6.image.z != 0xffffffffu &&
        gPbrFrameL6.image.w != 0u &&
        traversalValid;
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
    PbrWave2IndirectSeedL6 outputSeed = PbrZeroIndirectSeedL6();
    if (!PbrWave2SeedFrameIsValidL6() || gPbrFrameL6.image.w <= 1u)
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
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
    PbrRayL6 ray;
    ray.origin = gPbrFrameL6.cameraPositionTanHalfFov.xyz;
    ray.direction = normalize(
        gPbrFrameL6.cameraForwardAspect.xyz +
        gPbrFrameL6.cameraRightLensRadius.xyz *
            (ndc.x * gPbrFrameL6.cameraForwardAspect.w *
             gPbrFrameL6.cameraPositionTanHalfFov.w) +
        gPbrFrameL6.cameraUpExposure.xyz *
            (ndc.y * gPbrFrameL6.cameraPositionTanHalfFov.w));

    PbrHitL6 hit;
    if (!PBR_L6_TRACE_CLOSEST(
        ray, gPbrFrameL6.russianRoulette.w, 1.0e30f, hit)
        || hit.materialIndex >= gPbrFrameL6.trace.z)
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
        return;
    }
    const PbrMaterialGpuL6 material = gPbrMaterialsL6[hit.materialIndex];
    if (!PbrMaterialAuxiliaryIsValidL6(material)
        || (material.metadata.w & PBR_L6_MATERIAL_PURE_EMITTER) != 0u)
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
        return;
    }

    float3 attenuation = 1.0f;
    if (hit.frontFace == 0u && material.transmissionIor.x > 0.0f
        && !PbrInteriorAttenuationFactorL6(material, hit.t, attenuation))
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
        return;
    }
    const BsdfParamsL6 parameters = PbrMaterialToBsdfParamsL6(material);
    const BsdfContextL6 context = PbrBuildBsdfContextL6(hit, material);
    if (!ValidateBsdfParamsL6(context, parameters))
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
        return;
    }
    const BsdfSampleL6 bsdfSample = SampleBsdfL6(
        context,
        parameters,
        -ray.direction,
        PbrMakeBsdfRandomL6(pixelIndex, sampleIndex, 0u, streamTag, seed));
    if (bsdfSample.isValid == 0u || !(bsdfSample.pdf > 0.0f)
        || !PbrIsFiniteFloatL6(bsdfSample.pdf)
        || !PbrIsFinite3L6(bsdfSample.direction))
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
        return;
    }

    const float inversePdfCosine =
        abs(dot(hit.geometricNormal, bsdfSample.direction)) / bsdfSample.pdf;
#if PBR_L6_SIGNAL_MODE == 4
    const float3 throughput =
        attenuation * bsdfSample.diffuseValue * inversePdfCosine;
#else
    const float3 throughput =
        attenuation * bsdfSample.specularValue * inversePdfCosine;
#endif
    if (!PbrIsFinite3L6(throughput) || any(throughput < 0.0f)
        || !(PbrMaxComponentL6(throughput) > 0.0f))
    {
        gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
        return;
    }

    outputSeed.rayOriginPdf = float4(
        PbrOffsetRayOriginL6(
            hit.position, hit.geometricNormal, bsdfSample.direction),
        bsdfSample.pdf);
    outputSeed.rayDirectionEtaScale = float4(
        normalize(bsdfSample.direction),
        (bsdfSample.lobeFlags & kBsdfLobeTransmissionL6) != 0u
            ? bsdfSample.eta * bsdfSample.eta
            : 1.0f);
    outputSeed.throughputActive = float4(throughput, 1.0f);
    outputSeed.previousPositionDelta = float4(
        hit.position, bsdfSample.isDelta != 0u ? 1.0f : 0.0f);
    outputSeed.previousGeometricNormal = float4(hit.geometricNormal, 0.0f);
    outputSeed.previousShadingNormal = float4(hit.shadingNormal, 0.0f);
    gPbrIndirectSeedsL6[pixelIndex] = outputSeed;
}
