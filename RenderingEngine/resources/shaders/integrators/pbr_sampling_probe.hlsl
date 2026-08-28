#include "pbr_megakernel.hlsl"

// Compile/readback probe for the L6-private BSDF, counter RNG, alias/light
// bridge, and split-value contract. It is not a production render entry point.
[[vk::binding(19, 0)]] RWStructuredBuffer<float4> gPbrSamplingProbeOutputL6;

[numthreads(1, 1, 1)]
void ProbeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId != 0u))
    {
        return;
    }

    const uint2 seed = gPbrFrameL6.sampling.xy;
    const float rng0 = PbrCounterRandomL6(0u, gPbrFrameL6.image.z, 0u, gPbrFrameL6.output.x, seed);
    const float rng1 = PbrCounterRandomL6(
        0u,
        gPbrFrameL6.image.z,
        PbrBounceDimensionL6(0u, PBR_L6_DIM_LIGHT_SELECTION),
        gPbrFrameL6.output.x,
        seed);
    const float3 uniformDirection = BsdfSampleUniformHemisphereL6(
        float2(rng0, rng1));

    PbrLightContextL6 lightContext;
    lightContext.position = 0.0f;
    lightContext.geometricNormal = float3(0.0f, 1.0f, 0.0f);
    lightContext.shadingNormal = lightContext.geometricNormal;
    const PbrLightSampleL6 lightSample = PbrSampleOneLightL6(
        lightContext,
        PbrMakeLightRandomL6(0u, gPbrFrameL6.image.z, 0u, gPbrFrameL6.output.x, seed));

    BsdfEvalL6 evaluation = InvalidBsdfEvalL6();
    BsdfSampleL6 bsdfSample = InvalidBsdfSampleL6();
    if (gPbrFrameL6.trace.z > 0u)
    {
        const PbrMaterialGpuL6 material = gPbrMaterialsL6[0u];
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
            float3(rng1, 0.25f, 0.75f));
    }

    gPbrSamplingProbeOutputL6[0u] = float4(rng0, rng1, lightSample.selectionPmf, lightSample.combinedPdfW);
    gPbrSamplingProbeOutputL6[1u] = float4(evaluation.value, evaluation.pdf);
    gPbrSamplingProbeOutputL6[2u] = float4(evaluation.diffuseValue, evaluation.isValid);
    gPbrSamplingProbeOutputL6[3u] = float4(bsdfSample.value, bsdfSample.pdf);
    gPbrSamplingProbeOutputL6[4u] = float4(
        uniformDirection,
        BsdfUniformHemispherePdfL6(uniformDirection));
}
