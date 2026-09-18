#include "pbr_l6_types.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<PbrFrameConstantsGpuL6> gPbrFrameL6;
#include "pbr_light_sampling.hlsli"
#include "../restir/RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
// uint2 = previous light-table index (or invalid), current per-light generation.
[[vk::binding(20, 5)]] StructuredBuffer<uint2> gCurrentToPreviousLightIndexV3;
[[vk::binding(1, 5)]] RWStructuredBuffer<GpuRestirCandidateV3> gCandidatesV3;
[[vk::binding(9, 5)]] RWStructuredBuffer<GpuRestirStatisticsV3> gStatisticsV3;

PbrLightRandomL6 PbrMakeRestirLightRandomV3(
    uint pixelIndex,
    uint candidateIndex)
{
    const uint candidateCount = gRestirParametersV3.extentAndCandidates.w;
    const uint sampleIndex = gPbrFrameL6.image.z * max(candidateCount, 1u)
        + candidateIndex;
    const uint streamTag = gPbrFrameL6.output.x ^ 0x52335354u;
    const uint2 seed = gPbrFrameL6.sampling.xy;
    PbrLightRandomL6 randomSample;
    randomSample.selection = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PBR_L6_DIM_LIGHT_SELECTION, streamTag, seed);
    randomSample.shape0 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PBR_L6_DIM_LIGHT_SHAPE_0, streamTag, seed);
    randomSample.shape1 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PBR_L6_DIM_LIGHT_SHAPE_1, streamTag, seed);
    randomSample.shape2 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PBR_L6_DIM_LIGHT_SHAPE_2, streamTag, seed);
    randomSample.shape3 = PbrCounterRandomL6(
        pixelIndex, sampleIndex, PBR_L6_DIM_LIGHT_SHAPE_3, streamTag, seed);
    return randomSample;
}

GpuPersistentLightSampleV3 PbrToPersistentLightSampleV3(
    PbrLightSampleL6 source,
    uint flattenedCandidateIndex)
{
    GpuPersistentLightSampleV3 destination = (GpuPersistentLightSampleV3)0;
    if ((source.flags & PBR_L6_SAMPLE_VALID) == 0u
        || source.lightIndex >= gPbrFrameL6.trace.w)
        return destination;

    const PbrLightGpuL6 light = gPbrLightsL6[source.lightIndex];
    destination.positionDistance = float4(source.position, source.distance);
    destination.directionCombinedPdf = float4(source.wi, source.combinedPdfW);
    destination.radianceDiscretePdf = float4(source.Li, source.selectionPmf);
    if (source.measure == PBR_L6_MEASURE_AREA)
        destination.conditionalPdf.x = source.conditionalPdf;
    else if (source.measure == PBR_L6_MEASURE_SOLID_ANGLE)
        destination.conditionalPdf.y = source.conditionalPdf;
    destination.identity = uint4(
        light.identity.z,
        source.primitiveId,
        flattenedCandidateIndex,
        gRestirParametersV3.generations.w);
    destination.generation = uint4(
        gCurrentToPreviousLightIndexV3[source.lightIndex].y,
        gRestirParametersV3.generations.x,
        gRestirParametersV3.generations.w,
        0u);
    uint flags = kRestirSampleFlagValidV3;
    if ((source.flags & PBR_L6_SAMPLE_DELTA) != 0u)
        flags |= kRestirSampleFlagDeltaV3;
    if (source.measure == PBR_L6_MEASURE_AREA)
        flags |= kRestirSampleFlagHasAreaPdfV3;
    if (source.measure == PBR_L6_MEASURE_SOLID_ANGLE)
        flags |= kRestirSampleFlagHasSolidAnglePdfV3;
    destination.metadata = uint4(
        PbrPrivateLightMeasureToAbiV1L6(source.measure),
        flags,
        light.identity.y,
        source.lightIndex);
    return destination;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelCount = gRestirParametersV3.extentAndCandidates.z;
    const uint candidateCount = gRestirParametersV3.extentAndCandidates.w;
    const uint flattenedCandidateIndex = dispatchThreadId.x;
    if (candidateCount == 0u
        || flattenedCandidateIndex >= pixelCount * candidateCount)
        return;

    const uint pixelIndex = flattenedCandidateIndex / candidateCount;
    const uint candidateIndex = flattenedCandidateIndex % candidateCount;
    const GpuPrimarySurfaceV2 surface = gPrimarySurfacesV3[pixelIndex];
    PbrLightContextL6 context;
    context.position = surface.worldPositionLinearDepth.xyz;
    context.geometricNormal = surface.geometricNormalRoughness.xyz;
    context.shadingNormal = surface.shadingNormalMetallic.xyz;
    const PbrLightSampleL6 sample = PbrSampleOneLightL6(
        context, PbrMakeRestirLightRandomV3(pixelIndex, candidateIndex));
    GpuPersistentLightSampleV3 persistent =
        PbrToPersistentLightSampleV3(sample, flattenedCandidateIndex);
    persistent.sourceIdentity = uint4(pixelIndex, surface.identity.xyz);
    const GpuRestirCandidateV3 candidate = RestirEvaluateCandidateV3(
        persistent,
        surface,
        gRestirParametersV3.modeAndFlags.y,
        kRestirReuseNoneV3,
        pixelIndex);
    gCandidatesV3[flattenedCandidateIndex] = candidate;

    uint ignored;
    InterlockedAdd(gStatisticsV3[0].candidateCounts.x, 1u, ignored);
    if (RestirCandidateValidV3(candidate))
        InterlockedAdd(gStatisticsV3[0].candidateCounts.y, 1u, ignored);
    else
        InterlockedAdd(gStatisticsV3[0].candidateCounts.z, 1u, ignored);
}
