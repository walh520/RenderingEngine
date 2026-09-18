#ifndef RENDERING_ENGINE_WAVEFRONT_PBR_SHADING_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_PBR_SHADING_HLSLI

// L7 consumes the L6-private PBR bridge directly until a shared ABI is
// published. Set 0 is the L6 scene/material/light distribution; set 3 remains
// exclusively owned by Wavefront queues.
#include "pbr_l6_types.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;
[[vk::binding(1, 0)]] StructuredBuffer<PbrMaterialGpuL6> gPbrMaterialsL6;

#include "pbr_light_sampling.hlsli"
#include "include/bsdf/PbrBsdf.hlsli"
#include "pbr_material_bsdf.hlsli"

PbrHitL6 WfToPbrHit(WfMaterialWorkItem hit)
{
    PbrHitL6 result;
    result.t = hit.positionT.w;
    result.position = hit.positionT.xyz;
    result.geometricNormal = hit.geometricNormalBaryU.xyz;
    result.shadingNormal = hit.shadingNormalBaryV.xyz;
    result.materialIndex = hit.ids.w;
    result.instanceId = hit.ids.z;
    result.primitiveId = hit.ids.y;
    result.emitterLightIndex = hit.ids.x;
    result.frontFace = hit.metadata.z & 1u;
    return result;
}

PbrLightContextL6 WfPreviousLightContext(WfPathState state)
{
    PbrLightContextL6 context;
    context.position = state.previousPositionPdf.xyz;
    context.geometricNormal = state.previousGeometricNormal.xyz;
    context.shadingNormal = state.previousShadingNormal.xyz;
    return context;
}

PbrLightRandomL6 WfPbrLightRandom(uint pathIndex, uint sampleIndex, uint bounce)
{
    const uint streamTag = gPbrFrameL6.output.x;
    const uint seedLow = gPbrFrameL6.sampling.x;
    const uint seedHigh = gPbrFrameL6.sampling.y;
    PbrLightRandomL6 randomSample;
    randomSample.selection = WfSampleDimension(
        pathIndex, sampleIndex, WfBounceDimension(bounce, kWfLightSelection),
        streamTag, seedLow, seedHigh);
    randomSample.shape0 = WfSampleDimension(
        pathIndex, sampleIndex, WfBounceDimension(bounce, kWfLightShape0),
        streamTag, seedLow, seedHigh);
    randomSample.shape1 = WfSampleDimension(
        pathIndex, sampleIndex, WfBounceDimension(bounce, kWfLightShape1),
        streamTag, seedLow, seedHigh);
    randomSample.shape2 = WfSampleDimension(
        pathIndex, sampleIndex, WfBounceDimension(bounce, kWfLightShape2),
        streamTag, seedLow, seedHigh);
    randomSample.shape3 = WfSampleDimension(
        pathIndex, sampleIndex, WfBounceDimension(bounce, kWfLightShape3),
        streamTag, seedLow, seedHigh);
    return randomSample;
}

float3 WfPbrBsdfRandom(uint pathIndex, uint sampleIndex, uint bounce)
{
    const uint streamTag = gPbrFrameL6.output.x;
    const uint seedLow = gPbrFrameL6.sampling.x;
    const uint seedHigh = gPbrFrameL6.sampling.y;
    return float3(
        WfSampleDimension(
            pathIndex, sampleIndex, WfBounceDimension(bounce, kWfBsdfLobe),
            streamTag, seedLow, seedHigh),
        WfSampleDimension(
            pathIndex, sampleIndex, WfBounceDimension(bounce, kWfBsdfU),
            streamTag, seedLow, seedHigh),
        WfSampleDimension(
            pathIndex, sampleIndex, WfBounceDimension(bounce, kWfBsdfV),
            streamTag, seedLow, seedHigh));
}

void WfPbrRecordNonFinite(uint bounce)
{
    uint ignored;
    InterlockedAdd(gWfBounceCounters[bounce].errors.y, 1u, ignored);
}

void WfPbrRecordNegativePdf(uint bounce)
{
    uint ignored;
    InterlockedAdd(gWfBounceCounters[bounce].errors.z, 1u, ignored);
}

bool WfPbrHitIsValid(PbrHitL6 hit, WfRayItem ray)
{
    const float geometricLengthSquared = dot(hit.geometricNormal, hit.geometricNormal);
    const float shadingLengthSquared = dot(hit.shadingNormal, hit.shadingNormal);
    return PbrIsFiniteFloatL6(hit.t) &&
        hit.t >= ray.originTMin.w && hit.t <= ray.directionTMax.w &&
        PbrIsFinite3L6(hit.position) &&
        PbrIsFiniteFloatL6(geometricLengthSquared) && geometricLengthSquared > 0.0f &&
        PbrIsFiniteFloatL6(shadingLengthSquared) && shadingLengthSquared > 0.0f &&
        hit.frontFace <= 1u;
}

bool WfPbrAccumulateTerminal(
    inout WfPathState state,
    uint bounce,
    float3 emittedRadiance,
    float misWeight)
{
    if (!WfFiniteNonNegative3(emittedRadiance) ||
        !PbrIsFiniteFloatL6(misWeight) || misWeight < 0.0f ||
        !WfFiniteNonNegative3(state.throughputEta.xyz) ||
        !WfFiniteNonNegative3(state.diffuseThroughput.xyz) ||
        !WfFiniteNonNegative3(state.specularThroughput.xyz))
    {
        return false;
    }
    const float3 weightedEmission = emittedRadiance * misWeight;
    if (!WfFiniteNonNegative3(weightedEmission))
    {
        return false;
    }

    float3 accumulated;
    if (bounce == 0u)
    {
        accumulated = state.cameraEmission.xyz +
            state.throughputEta.xyz * weightedEmission;
        if (!WfFiniteNonNegative3(accumulated)) return false;
        state.cameraEmission.xyz = accumulated;
    }
    else if (bounce == 1u)
    {
        const float3 diffuse = state.directDiffuse.xyz +
            state.diffuseThroughput.xyz * weightedEmission;
        const float3 specular = state.directSpecular.xyz +
            state.specularThroughput.xyz * weightedEmission;
        if (!WfFiniteNonNegative3(diffuse) || !WfFiniteNonNegative3(specular))
        {
            return false;
        }
        state.directDiffuse.xyz = diffuse;
        state.directSpecular.xyz = specular;
    }
    else
    {
        const float3 diffuse = state.indirectDiffuse.xyz +
            state.diffuseThroughput.xyz * weightedEmission;
        const float3 specular = state.indirectSpecular.xyz +
            state.specularThroughput.xyz * weightedEmission;
        if (!WfFiniteNonNegative3(diffuse) || !WfFiniteNonNegative3(specular))
        {
            return false;
        }
        state.indirectDiffuse.xyz = diffuse;
        state.indirectSpecular.xyz = specular;
    }
    return true;
}

bool WfPbrEmitterVisible(PbrHitL6 hit)
{
    if (hit.frontFace != 0u)
    {
        return true;
    }
    return hit.emitterLightIndex < gPbrFrameL6.trace.w &&
        (gPbrLightsL6[hit.emitterLightIndex].identity.y &
         PBR_L6_LIGHT_TWO_SIDED) != 0u;
}

bool WfPbrBuildDirectShadow(
    uint pathIndex,
    uint sampleIndex,
    uint bounce,
    PbrHitL6 hit,
    BsdfContextL6 bsdfContext,
    BsdfParamsL6 bsdfParameters,
    float3 wo,
    float3 beta,
    float3 betaDiffuse,
    float3 betaSpecular,
    bool restirOwnsPrimary,
    out WfShadowWorkItem shadowWork)
{
    shadowWork = (WfShadowWorkItem)0;
    if (gPbrFrameL6.sampling.w == 0u || gPbrFrameL6.distribution.x == 0u)
    {
        return false;
    }
    // ReSTIR DI owns non-delta direct lighting at the primary surface.  The
    // Wavefront integrator still runs NEE/MIS from secondary vertices so its
    // indirect transport contract remains unchanged.
    if (gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_RESTIR_PRIMARY &&
        bounce == 0u && restirOwnsPrimary)
    {
        return false;
    }

    PbrLightContextL6 lightContext;
    lightContext.position = hit.position;
    lightContext.geometricNormal = hit.geometricNormal;
    lightContext.shadingNormal = hit.shadingNormal;
    const PbrLightSampleL6 lightSample = PbrSampleOneLightL6(
        lightContext, WfPbrLightRandom(pathIndex, sampleIndex, bounce));
    if ((lightSample.flags & PBR_L6_SAMPLE_VALID) == 0u)
    {
        return false;
    }

    const BsdfEvalL6 evaluation = EvaluateBsdfL6(
        bsdfContext, bsdfParameters, wo, lightSample.wi);
    if (evaluation.isValid == 0u)
    {
        return false;
    }
    if (!PbrIsFiniteFloatL6(evaluation.pdf) ||
        !PbrIsFinite3L6(evaluation.value))
    {
        WfPbrRecordNonFinite(bounce);
        return false;
    }
    if (evaluation.pdf < 0.0f)
    {
        WfPbrRecordNegativePdf(bounce);
        return false;
    }

    const bool deltaLight = (lightSample.flags & PBR_L6_SAMPLE_DELTA) != 0u;
    const float estimatorPdf = deltaLight
        ? lightSample.selectionPmf
        : lightSample.combinedPdfW;
    if (!PbrIsFiniteFloatL6(estimatorPdf))
    {
        WfPbrRecordNonFinite(bounce);
        return false;
    }
    if (!(estimatorPdf > 0.0f))
    {
        if (estimatorPdf < 0.0f)
        {
            WfPbrRecordNegativePdf(bounce);
        }
        return false;
    }

    const float cosine = abs(dot(hit.geometricNormal, lightSample.wi));
    if (!(cosine > 0.0f))
    {
        return false;
    }
    const float misWeight = deltaLight
        ? 1.0f
        : PbrPowerHeuristicL6(lightSample.combinedPdfW, evaluation.pdf);
    const float3 scale = lightSample.Li * (cosine * misWeight / estimatorPdf);
    const float3 diffuse = bounce == 0u
        ? beta * evaluation.diffuseValue * scale
        : betaDiffuse * evaluation.value * scale;
    const float3 specular = bounce == 0u
        ? beta * evaluation.specularValue * scale
        : betaSpecular * evaluation.value * scale;
    if (!PbrIsFinite3L6(diffuse) || !PbrIsFinite3L6(specular) ||
        any(diffuse < 0.0f) || any(specular < 0.0f))
    {
        WfPbrRecordNonFinite(bounce);
        return false;
    }

    const float shadowMaximum =
        (lightSample.flags & PBR_L6_SAMPLE_INFINITE) != 0u
        ? 1.0e30f
        : lightSample.distance - gPbrFrameL6.russianRoulette.w;
    if (!(shadowMaximum > gPbrFrameL6.russianRoulette.w))
    {
        return false;
    }

    shadowWork.originTMin = float4(
        PbrOffsetRayOriginL6(hit.position, hit.geometricNormal, lightSample.wi),
        gPbrFrameL6.russianRoulette.w);
    shadowWork.directionTMax = float4(lightSample.wi, shadowMaximum);
    shadowWork.diffuseContributionPdf = float4(diffuse, estimatorPdf);
    shadowWork.specularContributionLight = float4(specular, lightSample.combinedPdfW);
    shadowWork.identity = uint4(
        pathIndex, bounce, lightSample.lightIndex, lightSample.primitiveId);
    // Preserve the public ShadowMethod carried by the shared L6 frame through
    // dense compaction and the published abi-v1 shadow queue. TraceShadow must
    // consume this per-work value rather than infer a legacy Wave profile.
    shadowWork.sampling = uint4(gPbrFrameL6.output.w, 0u, 0u, 0u);
    return true;
}

#endif
