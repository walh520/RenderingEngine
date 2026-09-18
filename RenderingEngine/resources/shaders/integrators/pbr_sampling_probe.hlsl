#include "pbr_megakernel.hlsl"

// Compile/readback probe for the L6-private BSDF, counter RNG, alias/light
// bridge, and split-value contract. It is not a production render entry point.
[[vk::binding(19, 0)]] RWStructuredBuffer<float4> gPbrSamplingProbeOutputL6;

static const uint PBR_L6_PROBE_RECORD_STRIDE = 7u;

[numthreads(64, 1, 1)]
void ProbeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint probeIndex = dispatchThreadId.x;
    if (probeIndex >= gPbrFrameL6.output.z)
    {
        return;
    }

    const uint2 seed = gPbrFrameL6.sampling.xy;
    const uint sampleIndex = gPbrFrameL6.image.z + probeIndex;
    const float rng0 = PbrCounterRandomL6(
        0u, sampleIndex, 0u, gPbrFrameL6.output.x, seed);
    const float rng1 = PbrCounterRandomL6(
        0u,
        sampleIndex,
        PbrBounceDimensionL6(0u, PBR_L6_DIM_LIGHT_SELECTION),
        gPbrFrameL6.output.x,
        seed);
    const float rng2 = PbrCounterRandomL6(
        0u, sampleIndex, PbrBounceDimensionL6(0u, PBR_L6_DIM_BSDF_U),
        gPbrFrameL6.output.x, seed);
    const float rng3 = PbrCounterRandomL6(
        0u, sampleIndex, PbrBounceDimensionL6(0u, PBR_L6_DIM_BSDF_V),
        gPbrFrameL6.output.x, seed);
    const float rngLobe = PbrCounterRandomL6(
        0u, sampleIndex, PbrBounceDimensionL6(0u, PBR_L6_DIM_BSDF_LOBE),
        gPbrFrameL6.output.x, seed);
    const float3 uniformDirection = BsdfSampleUniformHemisphereL6(
        float2(rng0, rng1));

    PbrLightContextL6 lightContext;
    lightContext.position = 0.0f;
    lightContext.geometricNormal = float3(0.0f, 1.0f, 0.0f);
    lightContext.shadingNormal = lightContext.geometricNormal;
    const PbrLightSampleL6 lightSample = PbrSampleOneLightL6(
        lightContext,
        PbrMakeLightRandomL6(0u, sampleIndex, 0u, gPbrFrameL6.output.x, seed));

    BsdfEvalL6 evaluation = InvalidBsdfEvalL6();
    BsdfSampleL6 bsdfSample = InvalidBsdfSampleL6();
    float independentPdf = 0.0f;
    if (gPbrFrameL6.trace.z > 0u)
    {
        const uint materialIndex = probeIndex % gPbrFrameL6.trace.z;
        const PbrMaterialGpuL6 material = gPbrMaterialsL6[materialIndex];
        const BsdfParamsL6 parameters = PbrMaterialToBsdfParamsL6(material);
        BsdfContextL6 context;
        context.geometricNormal = float3(0.0f, 1.0f, 0.0f);
        context.etaIncident = 1.0f;
        context.shadingNormal = context.geometricNormal;
        context.etaTransmitted = material.transmissionIor.y;
        context.tangent = float3(1.0f, 0.0f, 0.0f);
        context.transportMode = kBsdfTransportRadianceL6;
        evaluation = EvaluateBsdfL6(
            context,
            parameters,
            float3(0.0f, 1.0f, 0.0f),
            normalize(float3(0.3f, 0.9f, 0.1f)));
        bsdfSample = SampleBsdfL6(
            context,
            parameters,
            float3(0.0f, 1.0f, 0.0f),
            float3(rngLobe, rng2, rng3));
        if (bsdfSample.isValid != 0u && bsdfSample.isDelta == 0u)
        {
            independentPdf = PdfBsdfL6(
                context, parameters, float3(0.0f, 1.0f, 0.0f),
                bsdfSample.direction);
        }
    }

    const uint output = probeIndex * PBR_L6_PROBE_RECORD_STRIDE;
    gPbrSamplingProbeOutputL6[output + 0u] = float4(
        rng0, rng1, lightSample.selectionPmf, lightSample.combinedPdfW);
    gPbrSamplingProbeOutputL6[output + 1u] = float4(evaluation.value, evaluation.pdf);
    gPbrSamplingProbeOutputL6[output + 2u] = float4(
        evaluation.diffuseValue, evaluation.isValid);
    gPbrSamplingProbeOutputL6[output + 3u] = float4(bsdfSample.value, bsdfSample.pdf);
    gPbrSamplingProbeOutputL6[output + 4u] = float4(
        uniformDirection,
        BsdfUniformHemispherePdfL6(uniformDirection));
    gPbrSamplingProbeOutputL6[output + 5u] = float4(
        bsdfSample.direction, bsdfSample.isValid);
    gPbrSamplingProbeOutputL6[output + 6u] = float4(
        independentPdf, float(bsdfSample.measure), float(bsdfSample.isDelta),
        float(bsdfSample.lobeFlags));
}
