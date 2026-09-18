#include "RestirProductionV3.hlsli"

[[vk::binding(29, 5)]] StructuredBuffer<GpuRestirReservoirV3>
    gPublishedReservoirsV3;
[[vk::binding(8, 5)]] RWStructuredBuffer<GpuRestirDebugV3> gDebugRecordsV3;
[[vk::binding(16, 5)]] RWTexture2D<float4> gDebugImageV3;

float RestirFiniteOrZeroV3(float value)
{
    return isfinite(value) ? value : 0.0f;
}

bool RestirReservoirScalarsFiniteV3(GpuRestirReservoirV3 reservoir)
{
    return all(isfinite(reservoir.weightState))
        && all(isfinite(reservoir.selectedTerms))
        && all(isfinite(reservoir.selected.positionDistance))
        && all(isfinite(reservoir.selected.directionCombinedPdf))
        && all(isfinite(reservoir.selected.radianceDiscretePdf))
        && all(isfinite(reservoir.selected.conditionalPdf));
}

float RestirFinalVisibilityV3(GpuRestirReservoirV3 reservoir)
{
    if ((reservoir.state.z
            & kRestirReservoirFlagFinalVisibilityEvaluatedV3) == 0u
        || !(reservoir.weightState.y > 0.0f)
        || !isfinite(reservoir.weightState.y)
        || !isfinite(reservoir.selectedTerms.w))
        return 0.0f;
    return saturate(reservoir.selectedTerms.w / reservoir.weightState.y);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint width = gRestirParametersV3.extentAndCandidates.x;
    const uint height = gRestirParametersV3.extentAndCandidates.y;
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) return;
    const uint pixelIndex = dispatchThreadId.y * width + dispatchThreadId.x;
    const GpuRestirReservoirV3 reservoir = gPublishedReservoirsV3[pixelIndex];

    GpuRestirDebugV3 debugRecord = (GpuRestirDebugV3)0;
    debugRecord.identity = uint4(
        reservoir.selected.identity.xy,
        reservoir.provenance.xy);
    debugRecord.generation = reservoir.selected.generation;
    debugRecord.scalar = float4(
        RestirFiniteOrZeroV3(reservoir.weightState.x),
        RestirFiniteOrZeroV3(reservoir.weightState.y),
        RestirFiniteOrZeroV3(reservoir.weightState.z),
        RestirFiniteOrZeroV3(reservoir.selectedTerms.w));
    debugRecord.state = uint4(
        reservoir.state.x,
        reservoir.state.y,
        reservoir.state.z,
        reservoir.state.w);
    gDebugRecordsV3[pixelIndex] = debugRecord;

    const bool finite = RestirReservoirScalarsFiniteV3(reservoir);
    float4 outputValue = float4(0.0f, 0.0f, 0.0f, finite ? 1.0f : 0.0f);
    const uint debugMode = gRestirParametersV3.historyGenerations.z;
    if (debugMode == 0u)
        outputValue = float4(
            (float)reservoir.state.x,
            (float)reservoir.state.y,
            (reservoir.state.z & kRestirReservoirFlagMClampedV3) != 0u
                ? 1.0f : 0.0f,
            finite ? 1.0f : 0.0f);
    else if (debugMode == 1u)
        outputValue = float4(
            RestirFiniteOrZeroV3(reservoir.weightState.x),
            RestirFiniteOrZeroV3(reservoir.weightState.y),
            RestirFiniteOrZeroV3(reservoir.weightState.z),
            RestirFiniteOrZeroV3(reservoir.selectedTerms.w));
    else if (debugMode == 2u)
        outputValue = float4(
            (float)reservoir.selected.identity.x,
            (float)reservoir.selected.identity.y,
            (float)reservoir.selected.generation.x,
            finite ? 1.0f : 0.0f);
    else if (debugMode == 3u)
        outputValue = float4(
            (float)reservoir.provenance.x,
            (float)reservoir.provenance.z,
            (float)reservoir.selected.sourceIdentity.x,
            finite ? 1.0f : 0.0f);
    else if (debugMode == 4u)
        outputValue = float4(
            (float)reservoir.provenance.y,
            (reservoir.state.z & kRestirReservoirFlagTemporalAcceptedV3) != 0u
                ? 1.0f : 0.0f,
            (reservoir.state.z & kRestirReservoirFlagSpatialAcceptedV3) != 0u
                ? 1.0f : 0.0f,
            finite ? 1.0f : 0.0f);
    else if (debugMode == 5u)
        outputValue = float4(
            (float)reservoir.state.w,
            (float)reservoir.history.reprojection.x,
            (float)reservoir.provenance.z,
            finite ? 1.0f : 0.0f);
    else
        outputValue = float4(
            RestirFinalVisibilityV3(reservoir),
            (reservoir.state.z
                & kRestirReservoirFlagFinalVisibilityEvaluatedV3) != 0u
                    ? 1.0f : 0.0f,
            (reservoir.state.z & kRestirReservoirFlagVisibilityValidV3) != 0u
                ? 1.0f : 0.0f,
            finite ? 1.0f : 0.0f);
    // The structured debug record above remains raw. The window image uses
    // bounded encodings of those same values (documented in F10), rather than
    // clipping M/weights/IDs to white during presentation.
    if (debugMode == 0u)
        outputValue.xy /= float2(max(gRestirParametersV3.reuseLimits.y, 1u),
            max(gRestirParametersV3.reuseLimits.z, 1u));
    else if (debugMode == 1u)
        outputValue.xyz = max(outputValue.xyz, 0.0f) / (1.0f + max(outputValue.xyz, 0.0f));
    else if (debugMode == 2u || debugMode == 3u || debugMode == 4u || debugMode == 5u)
        outputValue.xyz = frac(max(outputValue.xyz, 0.0f) * float3(0.6180339f, 0.381966f, 0.7548777f));
    gDebugImageV3[dispatchThreadId.xy] = outputValue;
}
