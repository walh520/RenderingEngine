// One-bounce terminal/direct-light/BSDF shade step for a selected Wave 5
// indirect AOV. The paired trace pass publishes its hit into unselected AOV
// images, keeping the large path state out of the traversal live range.

#ifndef PBR_L6_SIGNAL_MODE
#error PBR_L6_SIGNAL_MODE must select indirect diffuse (4), indirect specular (5), or staged Raw (6).
#endif
#if PBR_L6_SIGNAL_MODE < 4 || PBR_L6_SIGNAL_MODE > 6
#error PBR_L6_SIGNAL_MODE must be in [4, 6] for the staged shade step.
#endif

#if defined(PBR_L6_WAVE2_PRODUCTION) && \
    !defined(PBR_L6_MATERIAL_MR_SMOOTH_ONLY) && \
    !defined(PBR_L6_MATERIAL_SHOWCASE_ONLY)
#define PBR_L6_MATERIAL_SHOWCASE_ONLY
#endif

#if PBR_L6_SIGNAL_MODE == 6
#define PBR_L6_ENABLE_RESTIR_OWNERSHIP
#endif

#define CSMain PbrWave2UnusedShadeGenericEntryL6
#include "pbr_megakernel.hlsl"
#undef CSMain
#if PBR_L6_SIGNAL_MODE == 6
#define PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES
#include "pbr_reconstruction_export_v2.hlsli"
#undef PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES
#endif

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
RWStructuredBuffer<PbrWave2IndirectStateL6> gPbrIndirectStatesL6;
[[vk::push_constant]]
ConstantBuffer<PbrWave2IndirectStepConstantsL6> gPbrIndirectStepL6;

void PbrLoadIndirectHitScratchL6(
    uint2 pixel,
    out float4 positionT,
    out float4 geometricFront,
    out float4 shadingMaterial,
    out float4 emitterStatus)
{
    positionT = gPbrCameraEmissionOutputL6[pixel];
    geometricFront = gPbrDirectDiffuseOutputL6[pixel];
    shadingMaterial = gPbrDirectSpecularOutputL6[pixel];
#if PBR_L6_SIGNAL_MODE == 4
    emitterStatus = gPbrIndirectSpecularOutputL6[pixel];
#else
    emitterStatus = gPbrIndirectDiffuseOutputL6[pixel];
#endif
}

void PbrDeactivateIndirectShadeL6(
    uint pixelIndex,
    PbrWave2IndirectStateL6 state)
{
    state.throughputActive.w = 0.0f;
    gPbrIndirectStatesL6[pixelIndex] = state;
}

#if PBR_L6_SIGNAL_MODE == 6
void PbrAccumulateRawTerminalL6(
    inout PbrWave2IndirectStateL6 state,
    uint depth,
    float3 contribution)
{
    if (depth == 0u)
    {
        state.cameraEmission.xyz += contribution;
    }
    else if (state.accumulatedSignal.w < 1.5f)
    {
        state.indirectDiffuse.xyz += contribution;
    }
    else
    {
        state.indirectSpecular.xyz += contribution;
    }
}
#endif

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= gPbrFrameL6.image.x || pixel.y >= gPbrFrameL6.image.y)
    {
        return;
    }
    const uint pixelIndex = pixel.y * gPbrFrameL6.image.x + pixel.x;
    PbrWave2IndirectStateL6 state = gPbrIndirectStatesL6[pixelIndex];
    if (!(state.throughputActive.w > 0.5f))
    {
        return;
    }
    const uint depth = gPbrIndirectStepL6.depth;
    if (depth >= gPbrFrameL6.image.w)
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }

    float4 positionT;
    float4 geometricFront;
    float4 shadingMaterial;
    float4 emitterStatus;
    PbrLoadIndirectHitScratchL6(
        pixel, positionT, geometricFront, shadingMaterial, emitterStatus);

    float3 throughput = state.throughputActive.xyz;
    const float previousBsdfPdf = state.rayOriginPdf.w;
    const uint previousWasDelta =
        state.previousPositionDelta.w > 0.5f ? 1u : 0u;
    PbrLightContextL6 previousLightContext;
    previousLightContext.position = state.previousPositionDelta.xyz;
    previousLightContext.geometricNormal = state.previousGeometricNormal.xyz;
    previousLightContext.shadingNormal = state.previousShadingNormal.xyz;
    const float3 incomingDirection = state.rayDirectionEtaScale.xyz;

    if (!(emitterStatus.w > 0.5f))
    {
        float misWeight = 1.0f;
        if (previousWasDelta == 0u)
        {
            const bool restirOwnsThisEmitter =
                gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                && depth == 1u && state.previousGeometricNormal.w > 0.5f;
            if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_MIS
                || (gPbrFrameL6.sampling.w
                        == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                    && depth > 1u))
            {
                const float lightPdf = PbrSelectedLightPdfL6(
                    gPbrFrameL6.environment.x,
                    previousLightContext,
                    incomingDirection,
                    1.0e30f,
                    0.0f);
                if (!PbrTryPowerHeuristicL6(
                    previousBsdfPdf, lightPdf, misWeight))
                {
                    misWeight = 0.0f;
                }
            }
            else if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_NEE
                || restirOwnsThisEmitter)
            {
                misWeight = 0.0f;
            }
        }
        const float3 terminal = throughput
            * PbrEnvironmentRadianceL6(incomingDirection)
            * misWeight;
        state.accumulatedSignal.xyz += terminal;
#if PBR_L6_SIGNAL_MODE == 6
        PbrAccumulateRawTerminalL6(state, depth, terminal);
#endif
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }

    PbrHitL6 hit;
    hit.t = positionT.w;
    hit.position = positionT.xyz;
    hit.geometricNormal = geometricFront.xyz;
    hit.shadingNormal = shadingMaterial.xyz;
    hit.materialIndex = uint(shadingMaterial.w + 0.5f);
    hit.instanceId = asuint(emitterStatus.y);
    hit.primitiveId = asuint(emitterStatus.z);
    hit.emitterLightIndex = emitterStatus.x < 0.0f
        ? PBR_L6_INVALID_INDEX
        : uint(emitterStatus.x + 0.5f);
    hit.frontFace = geometricFront.w > 0.5f ? 1u : 0u;
    if (hit.materialIndex >= gPbrFrameL6.trace.z)
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }

    const PbrMaterialGpuL6 material = gPbrMaterialsL6[hit.materialIndex];
    if (!PbrMaterialAuxiliaryIsValidL6(material))
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }
    const BsdfParamsL6 parameters = PbrMaterialToBsdfParamsL6(material);
    const bool currentRestirOwned = depth == 0u
        && BsdfSupportsRestirPrimaryDirectL6(parameters);
#if PBR_L6_SIGNAL_MODE == 6
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
        float3 attenuation;
        if (!PbrInteriorAttenuationFactorL6(material, hit.t, attenuation))
        {
            PbrDeactivateIndirectShadeL6(pixelIndex, state);
            return;
        }
        throughput *= attenuation;
    }
    if (!PbrIsFinite3L6(throughput) || any(throughput < 0.0f))
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
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
        if (previousWasDelta == 0u
            && (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_NEE
                || (gPbrFrameL6.sampling.w
                        == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                    && depth == 1u
                    && state.previousGeometricNormal.w > 0.5f)))
        {
            misWeight = 0.0f;
        }
        else if (previousWasDelta == 0u
            && (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_MIS
                || (gPbrFrameL6.sampling.w
                        == PBR_L6_ESTIMATOR_RESTIR_PRIMARY
                    && depth > 1u)))
        {
            const float lightPdf = PbrSelectedLightPdfL6(
                hit.emitterLightIndex,
                previousLightContext,
                incomingDirection,
                hit.t,
                hit.geometricNormal);
            if (!PbrTryPowerHeuristicL6(
                previousBsdfPdf, lightPdf, misWeight))
            {
                misWeight = 0.0f;
            }
        }
        const float3 terminal = throughput * emission * misWeight;
        state.accumulatedSignal.xyz += terminal;
#if PBR_L6_SIGNAL_MODE == 6
        PbrAccumulateRawTerminalL6(state, depth, terminal);
#endif
    }
    if ((material.metadata.w & PBR_L6_MATERIAL_PURE_EMITTER) != 0u)
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }

    const BsdfContextL6 context = PbrBuildBsdfContextL6(hit, material);
    if (!ValidateBsdfParamsL6(context, parameters))
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }
    const float3 wo = -incomingDirection;
    const uint sampleIndex = gPbrFrameL6.image.z;
    const uint2 randomSeed = gPbrFrameL6.sampling.xy;
    const uint streamTag = gPbrFrameL6.output.x;
    const PbrDirectEstimateL6 direct = PbrEstimateDirectL6(
        hit,
        context,
        parameters,
        wo,
        PbrMakeLightRandomL6(
            pixelIndex, sampleIndex, depth, streamTag, randomSeed)
#if defined(PBR_L6_ENABLE_RESTIR_OWNERSHIP)
        ,
        depth,
        currentRestirOwned
#endif
        );
    if (direct.valid != 0u)
    {
        state.accumulatedSignal.xyz += throughput * direct.total;
#if PBR_L6_SIGNAL_MODE == 6
        if (depth == 0u)
        {
            state.directDiffuse.xyz += throughput * direct.diffuse;
            state.directSpecular.xyz += throughput * direct.specular;
        }
        else if (state.accumulatedSignal.w < 1.5f)
        {
            state.indirectDiffuse.xyz += throughput * direct.total;
        }
        else
        {
            state.indirectSpecular.xyz += throughput * direct.total;
        }
#endif
    }
    if (depth + 1u >= gPbrFrameL6.image.w)
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }

    // Whitted is a genuinely different integrator, not a display alias for
    // the stochastic PBR path tracer.  It evaluates local direct illumination
    // with the full material, then continues only ideal specular reflection or
    // transmission chains.  Diffuse and rough-glossy surfaces terminate after
    // their direct term, matching the classic recursive Whitted construction.
    BsdfParamsL6 continuationParameters = parameters;
    if (gPbrFrameL6.output.z == PBR_L6_TRANSPORT_WHITTED)
    {
        continuationParameters.allowedLobes &=
            kBsdfLobeSpecularL6 |
            kBsdfLobeReflectionL6 |
            kBsdfLobeTransmissionL6;
    }
    const BsdfSampleL6 bsdfSample = SampleBsdfL6(
        context,
        continuationParameters,
        wo,
        PbrMakeBsdfRandomL6(
            pixelIndex, sampleIndex, depth, streamTag, randomSeed));
    if (bsdfSample.isValid == 0u || !(bsdfSample.pdf > 0.0f)
        || !PbrIsFiniteFloatL6(bsdfSample.pdf)
        || !PbrIsFinite3L6(bsdfSample.value)
        || any(bsdfSample.value < 0.0f)
        || !PbrIsFinite3L6(bsdfSample.direction))
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }
    throughput *= bsdfSample.value
        * (abs(dot(hit.geometricNormal, bsdfSample.direction))
            / bsdfSample.pdf);
    if (!PbrIsFinite3L6(throughput) || any(throughput < 0.0f))
    {
        PbrDeactivateIndirectShadeL6(pixelIndex, state);
        return;
    }
    float etaScale = state.rayDirectionEtaScale.w;
    if ((bsdfSample.lobeFlags & kBsdfLobeTransmissionL6) != 0u)
    {
        etaScale *= bsdfSample.eta * bsdfSample.eta;
    }
    if (float(depth + 1u) >= gPbrFrameL6.russianRoulette.x)
    {
        const float magnitude = PbrMaxComponentL6(throughput * etaScale);
        if (!PbrIsFiniteFloatL6(magnitude) || !(magnitude > 0.0f))
        {
            PbrDeactivateIndirectShadeL6(pixelIndex, state);
            return;
        }
        const float continuationProbability = clamp(
            magnitude,
            gPbrFrameL6.russianRoulette.y,
            gPbrFrameL6.russianRoulette.z);
        const float rouletteSample = PbrCounterRandomL6(
            pixelIndex,
            sampleIndex,
            PbrBounceDimensionL6(depth, PBR_L6_DIM_RUSSIAN_ROULETTE),
            streamTag,
            randomSeed);
        if (rouletteSample >= continuationProbability)
        {
            PbrDeactivateIndirectShadeL6(pixelIndex, state);
            return;
        }
        throughput /= continuationProbability;
    }

    state.rayOriginPdf = float4(
        PbrOffsetRayOriginL6(
            hit.position, hit.geometricNormal, bsdfSample.direction),
        bsdfSample.pdf);
    state.rayDirectionEtaScale = float4(
        normalize(bsdfSample.direction), etaScale);
    state.throughputActive = float4(throughput, 1.0f);
    state.previousPositionDelta = float4(
        hit.position, bsdfSample.isDelta != 0u ? 1.0f : 0.0f);
    state.previousGeometricNormal = float4(
        hit.geometricNormal, currentRestirOwned ? 1.0f : 0.0f);
    state.previousShadingNormal = float4(hit.shadingNormal, 0.0f);
#if PBR_L6_SIGNAL_MODE == 6
    if (depth == 0u)
    {
        state.accumulatedSignal.w =
            (bsdfSample.lobeFlags & kBsdfLobeDiffuseL6) != 0u
                ? 1.0f : 2.0f;
    }
#endif
    gPbrIndirectStatesL6[pixelIndex] = state;
}
