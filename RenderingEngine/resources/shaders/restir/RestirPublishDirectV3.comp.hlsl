#include "RestirProductionV3.hlsli"

[[vk::binding(24, 3)]] StructuredBuffer<GpuPrimarySurfaceV2> gPrimarySurfacesV3;
[[vk::binding(28, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gSpatialReservoirsV3;
[[vk::binding(24, 5)]] RWStructuredBuffer<float4> gDirectDiffuseV3;
[[vk::binding(25, 5)]] RWStructuredBuffer<float4> gDirectSpecularV3;
[[vk::binding(4, 4)]] RWStructuredBuffer<GpuReconstructionSignalV2>
    gReconstructionRawSignalV3;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pixelIndex = dispatchThreadId.x;
    if (pixelIndex >= gRestirParametersV3.extentAndCandidates.z) return;
    const GpuRestirReservoirV3 reservoir = gSpatialReservoirsV3[pixelIndex];
    float3 diffuse = 0.0f;
    float3 specular = 0.0f;
    if ((reservoir.state.z & kRestirReservoirFlagValidV3) != 0u
        && (reservoir.state.z & kRestirReservoirFlagFinalVisibilityEvaluatedV3) != 0u
        && (reservoir.state.z & kRestirReservoirFlagVisibilityValidV3) != 0u)
    {
        float target;
        RestirEvaluateUnshadowedV3(
            reservoir.selected, gPrimarySurfacesV3[pixelIndex],
            diffuse, specular, target);
        diffuse *= reservoir.selectedTerms.w;
        specular *= reservoir.selectedTerms.w;
    }
    gDirectDiffuseV3[pixelIndex] = float4(diffuse, 1.0f);
    gDirectSpecularV3[pixelIndex] = float4(specular, 1.0f);

    // Wavefront Resolve publishes camera-visible emission, delta-chain direct
    // transport, and all indirect transport before this pass. ReSTIR owns only
    // the primary non-delta direct term, so merge that term into ABI-v2 instead
    // of replacing the real Wavefront signal.
    GpuReconstructionSignalV2 rawSignal =
        gReconstructionRawSignalV3[pixelIndex];
    rawSignal.directDiffuse.xyz += diffuse;
    rawSignal.directSpecular.xyz += specular;
    gReconstructionRawSignalV3[pixelIndex] = rawSignal;
}
