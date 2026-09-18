// Wave 2 production Megakernel with a compile-time single-signal output.
//
// This bounded megakernel owns the primary-surface traversal diagnostic and
// primary-surface signals only. Production Raw, IndirectDiffuse, and
// IndirectSpecular use finite Seed -> Trace -> Shade -> Resolve pipelines so no
// invocation carries traversal plus the complete continuation state through one
// driver-JIT unit. PBR_L6_SIGNAL_MODE selects:
//   0 PrimaryRaw diagnostic, 1 CameraEmission, 2 DirectDiffuse,
//   3 DirectSpecular.

#ifndef PBR_L6_SIGNAL_MODE
#define PBR_L6_SIGNAL_MODE 0
#endif

#if PBR_L6_SIGNAL_MODE < 0 || PBR_L6_SIGNAL_MODE > 3
#error PBR_L6_SIGNAL_MODE must be in [0, 3].
#endif

// The five connected Wave 5 scene providers publish only ABI-v0
// metallic-roughness, rough-dielectric, and smooth-dielectric materials.
// Specialize that exact production domain so the driver never JITs the three unattached BSDF models
// into each debug-signal pipeline. The broader BSDF library and sampling gate
// remain available outside this scene-provider specialization.
#if defined(PBR_L6_WAVE2_PRODUCTION) && \
    !defined(PBR_L6_MATERIAL_METALLIC_ROUGHNESS_ONLY) && \
    !defined(PBR_L6_MATERIAL_MR_SMOOTH_ONLY) && \
    !defined(PBR_L6_MATERIAL_SHOWCASE_ONLY)
#define PBR_L6_MATERIAL_SHOWCASE_ONLY
#endif

#define CSMain PbrGenericMegakernelEntryL6
#include "pbr_megakernel.hlsl"
#undef CSMain

bool PbrWave2SignalFrameIsValidL6()
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
        gPbrFrameL6.sampling.z <= PBR_L6_LIGHT_SELECTION_POWER_WEIGHTED &&
        gPbrFrameL6.environment.w <= PBR_L6_ENVIRONMENT_SAMPLER_IMPORTANCE_MAP &&
        gPbrFrameL6.sampling.w <= PBR_L6_ESTIMATOR_MIS &&
        traversalValid;
}

void PbrStoreWave2SignalL6(
    uint2 pixel,
    float3 value,
    uint sampleIndex)
{
#if PBR_L6_SIGNAL_MODE == 0
    PbrStoreCurrentFrameL6(gPbrRawOutputL6, pixel, value);
#elif PBR_L6_SIGNAL_MODE == 1
    PbrStoreCurrentFrameL6(gPbrCameraEmissionOutputL6, pixel, value);
#elif PBR_L6_SIGNAL_MODE == 2
    PbrStoreCurrentFrameL6(gPbrDirectDiffuseOutputL6, pixel, value);
#else
    PbrStoreCurrentFrameL6(gPbrDirectSpecularOutputL6, pixel, value);
#endif
}

// Indirect signals are implemented by the finite staged shaders.
[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (!PbrWave2SignalFrameIsValidL6())
    {
        if (all(dispatchThreadId == 0u))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_FRAME);
        }
        return;
    }

    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= gPbrFrameL6.image.x || pixel.y >= gPbrFrameL6.image.y)
    {
        return;
    }

    const uint pixelIndex = pixel.y * gPbrFrameL6.image.x + pixel.x;
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
    PbrIncrementCounterL6(PBR_L6_COUNTER_CAMERA_RAYS);

#if PBR_L6_SIGNAL_MODE >= 4
    float3 betaSignal = 1.0f;
#else
    float3 beta = 1.0f;
#endif
    float3 signal = 0.0f;
#if PBR_L6_SIGNAL_MODE == 0 || PBR_L6_SIGNAL_MODE >= 4
    float etaScale = 1.0f;
    float previousBsdfPdf = 0.0f;
    uint previousWasDelta = 1u;
    PbrLightContextL6 previousLightContext;
    previousLightContext.position = 0.0f;
    previousLightContext.geometricNormal = 0.0f;
    previousLightContext.shadingNormal = 0.0f;
#endif

    [loop]
    for (uint depth = 0u; depth < gPbrFrameL6.image.w; ++depth)
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_PATH_RAYS);
        PbrHitL6 hit;
        if (!PBR_L6_TRACE_CLOSEST(
            ray,
            gPbrFrameL6.russianRoulette.w,
            1.0e30f,
            hit))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_MISSES);
#if PBR_L6_SIGNAL_MODE == 0 || PBR_L6_SIGNAL_MODE == 1 || PBR_L6_SIGNAL_MODE >= 4
            const float3 environment = PbrEnvironmentRadianceL6(ray.direction);
            float misWeight = 1.0f;
#if PBR_L6_SIGNAL_MODE == 0 || PBR_L6_SIGNAL_MODE >= 4
            if (depth > 0u && previousWasDelta == 0u)
            {
                if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_MIS)
                {
                    const float lightPdf = PbrSelectedLightPdfL6(
                        gPbrFrameL6.environment.x,
                        previousLightContext,
                        ray.direction,
                        1.0e30f,
                        0.0f);
                    if (!PbrTryPowerHeuristicL6(
                        previousBsdfPdf, lightPdf, misWeight))
                    {
                        misWeight = 0.0f;
                    }
                    PbrIncrementCounterL6(PBR_L6_COUNTER_MIS_EMITTER_HITS);
                }
                else if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_NEE)
                {
                    misWeight = 0.0f;
                }
            }
#endif
#if PBR_L6_SIGNAL_MODE == 0
            signal += beta * environment * misWeight;
#elif PBR_L6_SIGNAL_MODE == 1
            if (depth == 0u)
            {
                signal += beta * environment;
            }
#elif PBR_L6_SIGNAL_MODE >= 4
            if (depth > 0u)
            {
                signal += betaSignal * environment * misWeight;
            }
#endif
#endif
            break;
        }

        PbrIncrementCounterL6(PBR_L6_COUNTER_SURFACE_HITS);
        if (hit.materialIndex >= gPbrFrameL6.trace.z)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
            break;
        }
        const PbrMaterialGpuL6 material = gPbrMaterialsL6[hit.materialIndex];
        if (!PbrMaterialAuxiliaryIsValidL6(material))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
            break;
        }
        if (hit.frontFace == 0u && material.transmissionIor.x > 0.0f)
        {
#if PBR_L6_SIGNAL_MODE >= 4
            float3 attenuation;
            if (!PbrInteriorAttenuationFactorL6(material, hit.t, attenuation))
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
                break;
            }
            betaSignal *= attenuation;
#else
            if (!PbrApplyInteriorAttenuationL6(material, hit.t, beta))
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
                break;
            }
#endif
        }
#if PBR_L6_SIGNAL_MODE >= 4
        if (!PbrIsFinite3L6(betaSignal))
#else
        if (!PbrIsFinite3L6(beta))
#endif
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_THROUGHPUT);
            break;
        }

        const float3 emission = material.emissiveRoughness.xyz;
        bool emissionVisible = hit.frontFace != 0u;
        if (!emissionVisible && hit.emitterLightIndex < gPbrFrameL6.trace.w)
        {
            emissionVisible =
                (gPbrLightsL6[hit.emitterLightIndex].identity.y &
                 PBR_L6_LIGHT_TWO_SIDED) != 0u;
        }
        if (emissionVisible && PbrMaxComponentL6(emission) > 0.0f)
        {
            float misWeight = 1.0f;
            if (depth == 0u)
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_CAMERA_EMITTER_HITS);
            }
#if PBR_L6_SIGNAL_MODE == 0 || PBR_L6_SIGNAL_MODE >= 4
            else if (previousWasDelta != 0u)
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_DELTA_EMITTER_HITS);
            }
            else if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_NEE)
            {
                misWeight = 0.0f;
            }
            else if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_MIS)
            {
                const float lightPdf = PbrSelectedLightPdfL6(
                    hit.emitterLightIndex,
                    previousLightContext,
                    ray.direction,
                    hit.t,
                    hit.geometricNormal);
                if (!PbrTryPowerHeuristicL6(
                    previousBsdfPdf, lightPdf, misWeight))
                {
                    misWeight = 0.0f;
                }
                PbrIncrementCounterL6(PBR_L6_COUNTER_MIS_EMITTER_HITS);
            }
#endif
#if PBR_L6_SIGNAL_MODE == 0
            signal += beta * emission * misWeight;
#elif PBR_L6_SIGNAL_MODE == 1
            if (depth == 0u)
            {
                signal += beta * emission;
            }
#elif PBR_L6_SIGNAL_MODE >= 4
            if (depth > 0u)
            {
                signal += betaSignal * emission * misWeight;
            }
#endif
        }
        if ((material.metadata.w & PBR_L6_MATERIAL_PURE_EMITTER) != 0u)
        {
            break;
        }

        const BsdfParamsL6 parameters = PbrMaterialToBsdfParamsL6(material);
        const BsdfContextL6 context = PbrBuildBsdfContextL6(hit, material);
        if (!ValidateBsdfParamsL6(context, parameters))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
            break;
        }
        const float3 wo = -ray.direction;

#if PBR_L6_SIGNAL_MODE != 1
        bool evaluateDirect = true;
#if PBR_L6_SIGNAL_MODE >= 4
        evaluateDirect = depth > 0u;
#endif
        if (evaluateDirect)
        {
            const PbrDirectEstimateL6 direct = PbrEstimateDirectL6(
                hit,
                context,
                parameters,
                wo,
                PbrMakeLightRandomL6(
                    pixelIndex, sampleIndex, depth, streamTag, seed));
            if (direct.valid != 0u)
            {
#if PBR_L6_SIGNAL_MODE == 0
                signal += beta * direct.total;
#elif PBR_L6_SIGNAL_MODE == 2
                signal += beta * direct.diffuse;
#elif PBR_L6_SIGNAL_MODE == 3
                signal += beta * direct.specular;
#elif PBR_L6_SIGNAL_MODE >= 4
                signal += betaSignal * direct.total;
#endif
            }
        }
#endif

#if PBR_L6_SIGNAL_MODE <= 3
        // Camera-emission and direct-light signals are fully known at the
        // primary surface.  Their variants do not carry unused secondary-path
        // state through the driver.
        break;
#else
        if (depth + 1u >= gPbrFrameL6.image.w)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_MAXIMUM_DEPTH);
            break;
        }

        const BsdfSampleL6 sample = SampleBsdfL6(
            context,
            parameters,
            wo,
            PbrMakeBsdfRandomL6(
                pixelIndex, sampleIndex, depth, streamTag, seed));
        if (sample.isValid == 0u)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_BSDF_SAMPLE);
            break;
        }
        if (!PbrIsFiniteFloatL6(sample.pdf))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_PDF);
            break;
        }
        if (sample.pdf < 0.0f)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NEGATIVE_PDF);
            break;
        }
        if (!(sample.pdf > 0.0f))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_ZERO_PDF);
            break;
        }
        if (!PbrIsFinite3L6(sample.value) || any(sample.value < 0.0f)
            || !PbrIsFinite3L6(sample.direction))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_BSDF);
            break;
        }

        const float cosine = abs(dot(hit.geometricNormal, sample.direction));
        const float inversePdfCosine = cosine / sample.pdf;
        const float3 factor = sample.value * inversePdfCosine;
#if PBR_L6_SIGNAL_MODE == 4
        if (depth == 0u)
        {
            betaSignal *= sample.value * inversePdfCosine;
        }
        else
        {
            betaSignal *= factor;
        }
#elif PBR_L6_SIGNAL_MODE == 5
        if (depth == 0u)
        {
            betaSignal *= sample.specularValue * inversePdfCosine;
        }
        else
        {
            betaSignal *= factor;
        }
#endif
#if PBR_L6_SIGNAL_MODE < 4
        beta *= factor;
        if (!PbrIsFinite3L6(beta) || any(beta < 0.0f))
#else
        if (!PbrIsFinite3L6(betaSignal) || any(betaSignal < 0.0f))
#endif
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_THROUGHPUT);
            break;
        }

        if ((sample.lobeFlags & kBsdfLobeTransmissionL6) != 0u)
        {
            etaScale *= sample.eta * sample.eta;
        }
        previousBsdfPdf = sample.pdf;
        previousWasDelta = sample.isDelta;
        previousLightContext.position = hit.position;
        previousLightContext.geometricNormal = hit.geometricNormal;
        previousLightContext.shadingNormal = hit.shadingNormal;

        if (float(depth + 1u) >= gPbrFrameL6.russianRoulette.x)
        {
#if PBR_L6_SIGNAL_MODE >= 4
            const float magnitude = PbrMaxComponentL6(betaSignal * etaScale);
#else
            const float magnitude = PbrMaxComponentL6(beta * etaScale);
#endif
            if (!PbrIsFiniteFloatL6(magnitude))
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_THROUGHPUT);
                break;
            }
            if (!(magnitude > 0.0f))
            {
                break;
            }
            const float continuationProbability = clamp(
                magnitude,
                gPbrFrameL6.russianRoulette.y,
                gPbrFrameL6.russianRoulette.z);
            PbrIncrementCounterL6(PBR_L6_COUNTER_RR_TESTS);
            const float rouletteSample = PbrCounterRandomL6(
                pixelIndex,
                sampleIndex,
                PbrBounceDimensionL6(depth, PBR_L6_DIM_RUSSIAN_ROULETTE),
                streamTag,
                seed);
            if (rouletteSample >= continuationProbability)
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_RR_TERMINATIONS);
                break;
            }
#if PBR_L6_SIGNAL_MODE >= 4
            betaSignal /= continuationProbability;
#else
            beta /= continuationProbability;
#endif
        }

        ray.origin = PbrOffsetRayOriginL6(
            hit.position, hit.geometricNormal, sample.direction);
        ray.direction = normalize(sample.direction);
#endif
    }

    if (!PbrIsFinite3L6(signal))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_RADIANCE);
        signal = 0.0f;
    }
    if (any(signal < 0.0f))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NEGATIVE_CONTRIBUTION);
        signal = max(signal, 0.0f);
    }
    PbrStoreWave2SignalL6(pixel, signal, sampleIndex);
}
