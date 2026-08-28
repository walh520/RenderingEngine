#include "pbr_l6_types.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;
[[vk::binding(1, 0)]] StructuredBuffer<PbrMaterialGpuL6> gPbrMaterialsL6;

#include "pbr_light_sampling.hlsli"
#include "../include/bsdf/PbrBsdf.hlsli"
#include "pbr_material_bsdf.hlsli"

[[vk::image_format("rgba32f")]]
[[vk::binding(12, 0)]] RWTexture2D<float4> gPbrRawOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(13, 0)]] RWTexture2D<float4> gPbrCameraEmissionOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(14, 0)]] RWTexture2D<float4> gPbrDirectDiffuseOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(15, 0)]] RWTexture2D<float4> gPbrDirectSpecularOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(16, 0)]] RWTexture2D<float4> gPbrIndirectDiffuseOutputL6;
[[vk::image_format("rgba32f")]]
[[vk::binding(17, 0)]] RWTexture2D<float4> gPbrIndirectSpecularOutputL6;
[[vk::binding(18, 0)]] RWStructuredBuffer<uint> gPbrCountersL6;

struct PbrDirectEstimateL6
{
    float3 diffuse;
    float3 specular;
    float3 total;
    uint valid;
};

void PbrIncrementCounterL6(uint counterIndex)
{
    uint ignoredOriginalValue;
    InterlockedAdd(gPbrCountersL6[counterIndex], 1u, ignoredOriginalValue);
}

PbrPathSignalsL6 PbrZeroSignalsL6()
{
    PbrPathSignalsL6 signals;
    signals.raw = 0.0f;
    signals.cameraEmission = 0.0f;
    signals.directDiffuse = 0.0f;
    signals.directSpecular = 0.0f;
    signals.indirectDiffuse = 0.0f;
    signals.indirectSpecular = 0.0f;
    return signals;
}

PbrDirectEstimateL6 PbrZeroDirectEstimateL6()
{
    PbrDirectEstimateL6 estimate;
    estimate.diffuse = 0.0f;
    estimate.specular = 0.0f;
    estimate.total = 0.0f;
    estimate.valid = 0u;
    return estimate;
}

PbrLightRandomL6 PbrMakeLightRandomL6(
    uint pixelIndex,
    uint sampleIndex,
    uint depth,
    uint streamTag,
    uint2 seed)
{
    PbrLightRandomL6 randomSample;
    randomSample.selection = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_LIGHT_SELECTION), streamTag, seed);
    randomSample.shape0 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_LIGHT_SHAPE_0), streamTag, seed);
    randomSample.shape1 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_LIGHT_SHAPE_1), streamTag, seed);
    randomSample.shape2 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_LIGHT_SHAPE_2), streamTag, seed);
    randomSample.shape3 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_LIGHT_SHAPE_3), streamTag, seed);
    return randomSample;
}

float3 PbrMakeBsdfRandomL6(
    uint pixelIndex,
    uint sampleIndex,
    uint depth,
    uint streamTag,
    uint2 seed)
{
    return float3(
        PbrCounterRandomL6(
            pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_BSDF_LOBE), streamTag, seed),
        PbrCounterRandomL6(
            pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_BSDF_U), streamTag, seed),
        PbrCounterRandomL6(
            pixelIndex, sampleIndex, PbrBounceDimensionL6(depth, PBR_L6_DIM_BSDF_V), streamTag, seed));
}

PbrDirectEstimateL6 PbrEstimateDirectL6(
    PbrHitL6 hit,
    BsdfContextL6 bsdfContext,
    BsdfParamsL6 bsdfParameters,
    float3 wo,
    PbrLightRandomL6 randomSample)
{
    PbrDirectEstimateL6 estimate = PbrZeroDirectEstimateL6();
    if (gPbrFrameL6.sampling.w == 0u || gPbrFrameL6.distribution.x == 0u)
    {
        return estimate;
    }

    PbrLightContextL6 lightContext;
    lightContext.position = hit.position;
    lightContext.geometricNormal = hit.geometricNormal;
    lightContext.shadingNormal = hit.shadingNormal;
    const PbrLightSampleL6 lightSample = PbrSampleOneLightL6(lightContext, randomSample);
    if ((lightSample.flags & PBR_L6_SAMPLE_VALID) == 0u)
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_LIGHT_SAMPLES);
        return estimate;
    }
    PbrIncrementCounterL6(PBR_L6_COUNTER_VALID_LIGHT_SAMPLES);

    const BsdfEvalL6 evaluation = EvaluateBsdfL6(
        bsdfContext,
        bsdfParameters,
        wo,
        lightSample.wi);
    // A delta BSDF, or an ordinary direction outside a finite lobe's support,
    // has no light-sampling contribution. Neither is an invalid evaluation.
    if (evaluation.isValid == 0u)
    {
        return estimate;
    }
    if (!PbrIsFinite3L6(evaluation.value))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_BSDF_EVALUATION);
        return estimate;
    }
    if (!PbrIsFiniteFloatL6(evaluation.pdf))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_PDF);
        return estimate;
    }
    if (evaluation.pdf < 0.0f)
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NEGATIVE_PDF);
        return estimate;
    }

    // EvaluateBsdfL6 already applies shading-normal correction. The transport
    // cosine therefore remains the geometric-normal cosine.
    const float cosine = abs(dot(hit.geometricNormal, lightSample.wi));
    if (!(cosine > 0.0f))
    {
        return estimate;
    }
    const bool deltaLight = (lightSample.flags & PBR_L6_SAMPLE_DELTA) != 0u;
    const float estimatorPdf = deltaLight
        ? lightSample.selectionPmf
        : lightSample.combinedPdfW;
    if (!PbrIsFiniteFloatL6(estimatorPdf))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_PDF);
        return estimate;
    }
    if (estimatorPdf < 0.0f)
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NEGATIVE_PDF);
        return estimate;
    }
    if (!(estimatorPdf > 0.0f))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_ZERO_PDF);
        return estimate;
    }

    PbrRayL6 shadowRay;
    shadowRay.origin = PbrOffsetRayOriginL6(hit.position, hit.geometricNormal, lightSample.wi);
    shadowRay.direction = lightSample.wi;
    const float shadowMaximum = (lightSample.flags & PBR_L6_SAMPLE_INFINITE) != 0u
        ? 1.0e30f
        : lightSample.distance - gPbrFrameL6.russianRoulette.w;
    if (!(shadowMaximum > gPbrFrameL6.russianRoulette.w))
    {
        return estimate;
    }
    PbrIncrementCounterL6(PBR_L6_COUNTER_SHADOW_RAYS);
    if (PBR_L6_TRACE_ANY(
        shadowRay,
        gPbrFrameL6.russianRoulette.w,
        shadowMaximum,
        lightSample.primitiveId))
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_OCCLUDED_LIGHT_SAMPLES);
        return estimate;
    }

    const float misWeight = deltaLight
        ? 1.0f
        : PbrPowerHeuristicL6(lightSample.combinedPdfW, evaluation.pdf);
    const float3 common = lightSample.Li * (cosine * misWeight / estimatorPdf);
    estimate.diffuse = evaluation.diffuseValue * common;
    estimate.specular = evaluation.specularValue * common;
    estimate.total = evaluation.value * common;
    estimate.valid = PbrIsFinite3L6(estimate.total) ? 1u : 0u;
    if (estimate.valid == 0u)
    {
        PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_RADIANCE);
        return PbrZeroDirectEstimateL6();
    }
    return estimate;
}

void PbrAccumulateTerminalL6(
    inout PbrPathSignalsL6 signals,
    uint depth,
    float3 beta,
    float3 betaDiffuse,
    float3 betaSpecular,
    float3 emittedRadiance,
    float misWeight)
{
    const float3 weightedEmission = emittedRadiance * misWeight;
    const float3 total = beta * weightedEmission;
    signals.raw += total;
    if (depth == 0u)
    {
        signals.cameraEmission += total;
    }
    else if (depth == 1u)
    {
        signals.directDiffuse += betaDiffuse * weightedEmission;
        signals.directSpecular += betaSpecular * weightedEmission;
    }
    else
    {
        signals.indirectDiffuse += betaDiffuse * weightedEmission;
        signals.indirectSpecular += betaSpecular * weightedEmission;
    }
}

PbrPathSignalsL6 PbrTraceMegakernelPathL6(
    PbrRayL6 ray,
    uint pixelIndex,
    uint sampleIndex)
{
    PbrPathSignalsL6 signals = PbrZeroSignalsL6();
    float3 beta = 1.0f;
    float3 betaDiffuse = 0.0f;
    float3 betaSpecular = 0.0f;
    float etaScale = 1.0f;
    float previousBsdfPdf = 0.0f;
    uint previousWasDelta = 1u;
    PbrLightContextL6 previousLightContext;
    previousLightContext.position = 0.0f;
    previousLightContext.geometricNormal = 0.0f;
    previousLightContext.shadingNormal = 0.0f;
    const uint2 seed = gPbrFrameL6.sampling.xy;
    const uint streamTag = gPbrFrameL6.output.x;
    const uint maximumDepth = gPbrFrameL6.image.w;

    [loop]
    for (uint depth = 0u; depth < maximumDepth; ++depth)
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
            if (depth > 0u && previousWasDelta == 0u && gPbrFrameL6.sampling.w != 0u)
            {
                const float lightPdf = PbrSelectedLightPdfL6(
                    gPbrFrameL6.environment.x,
                    previousLightContext,
                    ray.direction,
                    1.0e30f,
                    0.0f);
                misWeight = PbrPowerHeuristicL6(previousBsdfPdf, lightPdf);
                PbrIncrementCounterL6(PBR_L6_COUNTER_MIS_EMITTER_HITS);
            }
            PbrAccumulateTerminalL6(
                signals, depth, beta, betaDiffuse, betaSpecular, environment, misWeight);
            break;
        }
        PbrIncrementCounterL6(PBR_L6_COUNTER_SURFACE_HITS);
        if (hit.materialIndex >= gPbrFrameL6.trace.z)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_BSDF);
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
            else if (previousWasDelta != 0u)
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_DELTA_EMITTER_HITS);
            }
            else if (gPbrFrameL6.sampling.w != 0u)
            {
                const float lightPdf = PbrSelectedLightPdfL6(
                    hit.emitterLightIndex,
                    previousLightContext,
                    ray.direction,
                    hit.t,
                    hit.geometricNormal);
                misWeight = lightPdf > 0.0f
                    ? PbrPowerHeuristicL6(previousBsdfPdf, lightPdf)
                    : 1.0f;
                PbrIncrementCounterL6(PBR_L6_COUNTER_MIS_EMITTER_HITS);
            }
            PbrAccumulateTerminalL6(
                signals, depth, beta, betaDiffuse, betaSpecular, emission, misWeight);
        }
        if ((material.metadata.w & PBR_L6_MATERIAL_PURE_EMITTER) != 0u)
        {
            break;
        }

        const BsdfParamsL6 bsdfParameters = PbrMaterialToBsdfParamsL6(material);
        const BsdfContextL6 bsdfContext = PbrBuildBsdfContextL6(hit, material);
        if (!ValidateBsdfParamsL6(bsdfContext, bsdfParameters))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_INVALID_MATERIAL);
            break;
        }
        const float3 wo = -ray.direction;
        const PbrDirectEstimateL6 direct = PbrEstimateDirectL6(
            hit,
            bsdfContext,
            bsdfParameters,
            wo,
            PbrMakeLightRandomL6(pixelIndex, sampleIndex, depth, streamTag, seed));
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

        if (depth + 1u >= maximumDepth)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_MAXIMUM_DEPTH);
            break;
        }

        const BsdfSampleL6 bsdfSample = SampleBsdfL6(
            bsdfContext,
            bsdfParameters,
            wo,
            PbrMakeBsdfRandomL6(pixelIndex, sampleIndex, depth, streamTag, seed));
        if (bsdfSample.isValid == 0u)
        {
            // VNDF reflection rejection and zero-support lobes are ordinary
            // absorption events, not malformed BSDF samples.
            break;
        }
        if (!PbrIsFiniteFloatL6(bsdfSample.pdf))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_PDF);
            break;
        }
        if (bsdfSample.pdf < 0.0f)
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NEGATIVE_PDF);
            break;
        }
        if (!(bsdfSample.pdf > 0.0f))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_ZERO_PDF);
            break;
        }
        if (!PbrIsFinite3L6(bsdfSample.value)
            || any(bsdfSample.value < 0.0f)
            || !PbrIsFinite3L6(bsdfSample.direction))
        {
            PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_BSDF);
            break;
        }

        const float cosine = abs(dot(hit.geometricNormal, bsdfSample.direction));
        const float3 factor = bsdfSample.value * (cosine / bsdfSample.pdf);
        if (depth == 0u)
        {
            betaDiffuse = beta * bsdfSample.diffuseValue * (cosine / bsdfSample.pdf);
            betaSpecular = beta * bsdfSample.specularValue * (cosine / bsdfSample.pdf);
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

        if ((bsdfSample.lobeFlags & kBsdfLobeTransmissionL6) != 0u)
        {
            etaScale *= bsdfSample.eta * bsdfSample.eta;
        }
        previousBsdfPdf = bsdfSample.pdf;
        previousWasDelta = bsdfSample.isDelta;
        previousLightContext.position = hit.position;
        previousLightContext.geometricNormal = hit.geometricNormal;
        previousLightContext.shadingNormal = hit.shadingNormal;

        const uint nextDepth = depth + 1u;
        if (float(nextDepth) >= gPbrFrameL6.russianRoulette.x)
        {
            const float rrMagnitude = PbrMaxComponentL6(beta * etaScale);
            if (!PbrIsFiniteFloatL6(rrMagnitude))
            {
                PbrIncrementCounterL6(PBR_L6_COUNTER_NONFINITE_THROUGHPUT);
                break;
            }
            if (!(rrMagnitude > 0.0f))
            {
                break;
            }
            const float continuationProbability = clamp(
                rrMagnitude,
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
            hit.position, hit.geometricNormal, bsdfSample.direction);
        ray.direction = normalize(bsdfSample.direction);
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
    return signals;
}

void PbrStoreOnlineMeanL6(
    RWTexture2D<float4> outputTexture,
    uint2 pixel,
    float3 sampleValue,
    uint sampleIndex)
{
    const float blend = rcp(float(sampleIndex + 1u));
    const float3 previous = sampleIndex == 0u ? 0.0f : outputTexture[pixel].rgb;
    outputTexture[pixel] = float4(lerp(previous, sampleValue, blend), 1.0f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (!PbrFrameSamplingIsValidL6(gPbrFrameL6))
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

    PbrRayL6 cameraRay;
    cameraRay.origin = gPbrFrameL6.cameraPositionTanHalfFov.xyz;
    cameraRay.direction = normalize(
        gPbrFrameL6.cameraForwardAspect.xyz
        + gPbrFrameL6.cameraRightLensRadius.xyz
            * (ndc.x * gPbrFrameL6.cameraForwardAspect.w * gPbrFrameL6.cameraPositionTanHalfFov.w)
        + gPbrFrameL6.cameraUpExposure.xyz
            * (ndc.y * gPbrFrameL6.cameraPositionTanHalfFov.w));
    PbrIncrementCounterL6(PBR_L6_COUNTER_CAMERA_RAYS);
    const PbrPathSignalsL6 signals = PbrTraceMegakernelPathL6(
        cameraRay, pixelIndex, sampleIndex);

    PbrStoreOnlineMeanL6(gPbrRawOutputL6, pixel, signals.raw, sampleIndex);
    PbrStoreOnlineMeanL6(gPbrCameraEmissionOutputL6, pixel, signals.cameraEmission, sampleIndex);
    PbrStoreOnlineMeanL6(gPbrDirectDiffuseOutputL6, pixel, signals.directDiffuse, sampleIndex);
    PbrStoreOnlineMeanL6(gPbrDirectSpecularOutputL6, pixel, signals.directSpecular, sampleIndex);
    PbrStoreOnlineMeanL6(gPbrIndirectDiffuseOutputL6, pixel, signals.indirectDiffuse, sampleIndex);
    PbrStoreOnlineMeanL6(gPbrIndirectSpecularOutputL6, pixel, signals.indirectSpecular, sampleIndex);
}
