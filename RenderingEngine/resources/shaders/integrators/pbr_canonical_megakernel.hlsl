// Driver-JIT-bounded Wave 2 production Megakernel.
//
// Shared BSDF, light sampling, traversal and counter helpers remain sourced
// from pbr_megakernel.hlsl. The complete path state machine is kept inline in
// this entry because current NVIDIA Vulkan drivers can fail their internal
// NVVM JIT when the aggregate path state crosses a large helper boundary.
// Unlike the earlier fixed-corpus specialization, this entry retains every
// Wave 2 material/light model and all six radiance signals.
#define CSMain PbrGenericMegakernelEntryL6
#include "pbr_megakernel.hlsl"
#undef CSMain

#if defined(PBR_L6_ENABLE_RECONSTRUCTION_EXPORT)
#define PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES
#include "pbr_reconstruction_export_v2.hlsli"
#undef PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES
#endif

#if !defined(PBR_L6_MONOLITHIC_GROUP_SIZE)
#define PBR_L6_MONOLITHIC_GROUP_SIZE 8
#endif

bool PbrCanonicalFrameIsValidL6()
{
    bool traversalValid = false;
#if defined(PBR_L6_TRAVERSAL_SOFTWARE)
    traversalValid =
        (gPbrFrameL6.traversal.x == PBR_L6_TRAVERSAL_FLATTENED_SAH &&
            gPbrFrameL6.traversal.y > 0u &&
            gPbrFrameL6.traversal.z > 0u) ||
        (gPbrFrameL6.traversal.x == PBR_L6_TRAVERSAL_CANONICAL_LINEAR &&
            gPbrFrameL6.traversal.z > 0u);
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
#if defined(PBR_L6_WAVE2_PRODUCTION) && !defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
        gPbrFrameL6.sampling.w <= PBR_L6_ESTIMATOR_MIS &&
#else
        gPbrFrameL6.sampling.w <= PBR_L6_ESTIMATOR_RESTIR_PRIMARY &&
#endif
        traversalValid;
}

[numthreads(
    PBR_L6_MONOLITHIC_GROUP_SIZE,
    PBR_L6_MONOLITHIC_GROUP_SIZE,
    1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (!PbrCanonicalFrameIsValidL6())
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
#if defined(PBR_L6_ENABLE_RECONSTRUCTION_EXPORT)
    PbrClearReconstructionInputsV2(pixelIndex);
#endif
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

    PbrPathSignalsL6 signals = PbrZeroSignalsL6();
    float3 beta = 1.0f;
    float3 betaDiffuse = 0.0f;
    float3 betaSpecular = 0.0f;
    float etaScale = 1.0f;
    float previousBsdfPdf = 0.0f;
    uint previousWasDelta = 1u;
    bool previousRestirOwned = false;
    PbrLightContextL6 previousLightContext;
    previousLightContext.position = 0.0f;
    previousLightContext.geometricNormal = 0.0f;
    previousLightContext.shadingNormal = 0.0f;

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
            const float3 environment = PbrEnvironmentRadianceL6(ray.direction);
            float misWeight = 1.0f;
            const uint estimator = gPbrFrameL6.sampling.w;
#if defined(PBR_L6_WAVE2_PRODUCTION) && !defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
            const bool useMis = estimator == PBR_L6_ESTIMATOR_MIS;
#else
            const bool restirOwnsThisEmitter =
                estimator == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                && depth == 1u && previousRestirOwned;
            const bool useMis = estimator == PBR_L6_ESTIMATOR_MIS
                || (estimator == PBR_L6_ESTIMATOR_RESTIR_PRIMARY && depth > 1u);
#endif
            if (depth > 0u && previousWasDelta == 0u && useMis)
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
            else if (depth > 0u && previousWasDelta == 0u
                && (estimator == PBR_L6_ESTIMATOR_NEE
#if !defined(PBR_L6_WAVE2_PRODUCTION) || defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
                    || restirOwnsThisEmitter))
#else
                    ))
#endif
            {
                misWeight = 0.0f;
            }
            PbrAccumulateTerminalL6(
                signals,
                depth,
                beta,
                betaDiffuse,
                betaSpecular,
                environment,
                misWeight);
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
        const BsdfParamsL6 parameters = PbrMaterialToBsdfParamsL6(material);
        const bool currentRestirOwned = depth == 0u
            && BsdfSupportsRestirPrimaryDirectL6(parameters);
#if defined(PBR_L6_ENABLE_RECONSTRUCTION_EXPORT)
        if (depth == 0u)
        {
            PbrPublishPrimarySurfaceV2(
                pixelIndex,
                hit.position,
                dot(
                    hit.position - gPbrFrameL6.cameraPositionTanHalfFov.xyz,
                    normalize(gPbrFrameL6.cameraForwardAspect.xyz)),
                hit.geometricNormal,
                hit.shadingNormal,
                hit.materialIndex,
                hit.instanceId,
                hit.primitiveId,
                hit.frontFace,
                material.baseColorMetallic.xyz,
                material.baseColorMetallic.w,
                material.emissiveRoughness.w,
                material.transmissionIor.x,
                material.f0.xyz,
                currentRestirOwned);
        }
#endif
        if (hit.frontFace == 0u && material.transmissionIor.x > 0.0f)
        {
            if (!PbrApplyInteriorAttenuationL6(
                material, hit.t, beta, betaDiffuse, betaSpecular))
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
                break;
            }
        }
        if (!PbrIsFinite3L6(beta))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_THROUGHPUT);
            break;
        }

        const float3 emission = material.emissiveRoughness.xyz;
        bool emissionVisible = hit.frontFace != 0u;
        if (!emissionVisible && hit.emitterLightIndex < gPbrFrameL6.trace.w)
        {
            emissionVisible =
                (gPbrLightsL6[hit.emitterLightIndex].identity.y
                    & PBR_L6_LIGHT_TWO_SIDED) != 0u;
        }
        if (emissionVisible && PbrMaxComponentL6(emission) > 0.0f)
        {
            float misWeight = 1.0f;
            if (depth == 0u)
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_CAMERA_EMITTER_HITS);
            }
            else if (previousWasDelta != 0u)
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_DELTA_EMITTER_HITS);
            }
            else if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_NEE
#if !defined(PBR_L6_WAVE2_PRODUCTION) || defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
                || (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                    && depth == 1u && previousRestirOwned))
#else
                )
#endif
            {
                misWeight = 0.0f;
            }
            else if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_MIS
#if !defined(PBR_L6_WAVE2_PRODUCTION) || defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
                || (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                    && depth > 1u))
#else
                )
#endif
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
            PbrAccumulateTerminalL6(
                signals,
                depth,
                beta,
                betaDiffuse,
                betaSpecular,
                emission,
                misWeight);
        }
        if ((material.metadata.w & PBR_L6_MATERIAL_PURE_EMITTER) != 0u)
        {
            break;
        }

        const BsdfContextL6 context = PbrBuildBsdfContextL6(hit, material);
        if (!ValidateBsdfParamsL6(context, parameters))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
            break;
        }
        const float3 wo = -ray.direction;
        const PbrDirectEstimateL6 direct = PbrEstimateDirectL6(
            hit,
            context,
            parameters,
            wo,
            PbrMakeLightRandomL6(
                pixelIndex, sampleIndex, depth, streamTag, seed)
#if !defined(PBR_L6_WAVE2_PRODUCTION) || defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
            ,
            depth,
            currentRestirOwned);
#else
            );
#endif
        if (direct.valid != 0u)
        {
            const float3 total = beta * direct.total;
            signals.raw += total;
            if (depth == 0u)
            {
                signals.directDiffuse += beta * direct.diffuse;
                signals.directSpecular += beta * direct.specular;
            }
            else
            {
                signals.indirectDiffuse += betaDiffuse * direct.total;
                signals.indirectSpecular += betaSpecular * direct.total;
            }
        }

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
        if (!PbrIsFinite3L6(sample.value)
            || any(sample.value < 0.0f)
            || !PbrIsFinite3L6(sample.direction))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_BSDF);
            break;
        }

        const float cosine = abs(dot(hit.geometricNormal, sample.direction));
        const float3 factor = sample.value * (cosine / sample.pdf);
        if (depth == 0u)
        {
            betaDiffuse = beta * sample.diffuseValue * (cosine / sample.pdf);
            betaSpecular = beta * sample.specularValue * (cosine / sample.pdf);
        }
        else
        {
            betaDiffuse *= factor;
            betaSpecular *= factor;
        }
        beta = betaDiffuse + betaSpecular;
        if (!PbrIsFinite3L6(beta) || any(beta < 0.0f))
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
        previousRestirOwned = currentRestirOwned;
        previousLightContext.position = hit.position;
        previousLightContext.geometricNormal = hit.geometricNormal;
        previousLightContext.shadingNormal = hit.shadingNormal;

        if (float(depth + 1u) >= gPbrFrameL6.russianRoulette.x)
        {
            const float magnitude = PbrMaxComponentL6(beta * etaScale);
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
            beta /= continuationProbability;
            betaDiffuse /= continuationProbability;
            betaSpecular /= continuationProbability;
        }

        ray.origin = PbrOffsetRayOriginL6(
            hit.position, hit.geometricNormal, sample.direction);
        ray.direction = normalize(sample.direction);
    }

    const float3 reconstructed = signals.cameraEmission
        + signals.directDiffuse
        + signals.directSpecular
        + signals.indirectDiffuse
        + signals.indirectSpecular;
    if (!PbrIsFinite3L6(signals.raw) || !PbrIsFinite3L6(reconstructed))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_RADIANCE);
    }
    if (any(signals.raw < 0.0f) || any(reconstructed < 0.0f))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NEGATIVE_CONTRIBUTION);
    }

#if defined(PBR_L6_ENABLE_RECONSTRUCTION_EXPORT)
    PbrPublishReconstructionSignalV2(
        pixelIndex,
        max(signals.cameraEmission, 0.0f.xxx),
        max(signals.directDiffuse, 0.0f.xxx),
        max(signals.directSpecular, 0.0f.xxx),
        max(signals.indirectDiffuse, 0.0f.xxx),
        max(signals.indirectSpecular, 0.0f.xxx));
#endif

    PbrStoreCurrentFrameL6(gPbrRawOutputL6, pixel, signals.raw);
    PbrStoreCurrentFrameL6(gPbrCameraEmissionOutputL6, pixel, signals.cameraEmission);
    PbrStoreCurrentFrameL6(gPbrDirectDiffuseOutputL6, pixel, signals.directDiffuse);
    PbrStoreCurrentFrameL6(gPbrDirectSpecularOutputL6, pixel, signals.directSpecular);
    PbrStoreCurrentFrameL6(gPbrIndirectDiffuseOutputL6, pixel, signals.indirectDiffuse);
    PbrStoreCurrentFrameL6(gPbrIndirectSpecularOutputL6, pixel, signals.indirectSpecular);
}
