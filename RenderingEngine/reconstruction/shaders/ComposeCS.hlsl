#include "ReconstructionCommon.hlsli"
#include "../../resources/shaders/include/contracts/SceneAbiV0.hlsli"

[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gComposeMaterials;
[[vk::binding(0, 4)]] StructuredBuffer<GpuGBufferRecordV2> gComposeGBuffer;
[[vk::binding(4, 4)]] StructuredBuffer<GpuReconstructionSignalV2> gComposeRaw;
[[vk::binding(8, 4)]] StructuredBuffer<L8SignalRecord> gComposeTemporal;
[[vk::binding(12, 4)]] StructuredBuffer<L8SignalRecord> gComposeFiltered;
[[vk::binding(7, 4)]] StructuredBuffer<L8HistoryRecord> gComposeHistory;
[[vk::binding(9, 4)]] StructuredBuffer<L8TemporalDebugRecord> gComposeDebug;
[[vk::binding(13, 4)]] RWTexture2D<float4> gComposeOutput;
[[vk::binding(22, 4)]] RWStructuredBuffer<float4> gComposeFilm;
[[vk::binding(23, 4)]] StructuredBuffer<float4> gComposeTerminal;

[[vk::binding(21, 4)]] cbuffer L8ComposeConstants
{
    uint2 gComposeExtent;
    uint gOutputMode;
    uint gMaxHistoryLength;
    float gMinimumAlbedo;
    uint gDemodulateSpecular;
    uint gFilmSampleCount;
    uint gComposeReserved;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gComposeExtent)) return;
    const uint index = L8LinearIndex(pixel, gComposeExtent);
    const GpuGBufferRecordV2 publishedGBuffer = gComposeGBuffer[index];
    const L8GBufferRecord gbuffer = L8LoadGBuffer(publishedGBuffer);
    float3 output = 0.0f.xxx;
    if (gOutputMode == L8_OUTPUT_RAW || gOutputMode == L8_OUTPUT_PROGRESSIVE_MEAN)
    {
        const L8SignalRecord raw = L8LoadRawSignal(gComposeRaw[index]);
        output = raw.diffuse + raw.specular + gComposeTerminal[index].xyz;
        if (gOutputMode == L8_OUTPUT_PROGRESSIVE_MEAN)
        {
            // One complete current-frame estimate, after primary-direct
            // replacement, enters this independent film exactly once.
            const float3 previous = gFilmSampleCount == 0u
                ? 0.0f.xxx : gComposeFilm[index].xyz;
            output = previous + (output - previous) / float(gFilmSampleCount + 1u);
            gComposeFilm[index] = float4(output, float(gFilmSampleCount + 1u));
        }
    }
    else if (gOutputMode == L8_OUTPUT_TEMPORAL || gOutputMode == L8_OUTPUT_ATROUS || gOutputMode == L8_OUTPUT_SVGF)
    {
        L8SignalRecord signal = (L8SignalRecord)0;
        if (gOutputMode == L8_OUTPUT_TEMPORAL) signal = gComposeTemporal[index];
        else signal = gComposeFiltered[index];
        signal = L8Remodulate(signal, gbuffer, gMinimumAlbedo, gDemodulateSpecular);
        output = signal.diffuse + signal.specular + gComposeTerminal[index].xyz;
    }
    else if (gOutputMode == L8_OUTPUT_MOTION)
    {
        output = float3(0.5f.xx + gbuffer.motion * 16.0f, 0.5f);
    }
    else if (gOutputMode == L8_OUTPUT_HISTORY_LENGTH)
    {
        output = (float(gComposeHistory[index].historyLength) / float(max(gMaxHistoryLength, 1u))).xxx;
    }
    else if (gOutputMode == L8_OUTPUT_MOMENTS)
    {
        output = float3(gComposeHistory[index].moments, 0.0f);
    }
    else if (gOutputMode == L8_OUTPUT_VARIANCE)
    {
        output = sqrt(max(gComposeHistory[index].variance, 0.0f)).xxx;
    }
    else if (gOutputMode == L8_OUTPUT_ACCEPTANCE)
    {
        output = gComposeDebug[index].accepted != 0u ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    }
    else if (gOutputMode == L8_OUTPUT_REJECT_REASONS)
    {
        output = L8RejectReasonColor(gComposeDebug[index].rejectReasons);
    }
    else if (gOutputMode == L8_OUTPUT_BASE_COLOR)
    {
        uint materialCount;
        uint materialStride;
        gComposeMaterials.GetDimensions(materialCount, materialStride);
        const uint materialId = publishedGBuffer.primary.identity.x;
        if ((publishedGBuffer.primary.identity.w
                & kPrimarySurfaceFlagValidV2) != 0u
            && materialId < materialCount)
        {
            output = saturate(
                gComposeMaterials[materialId].baseColorFactor.xyz);
        }
    }
    else if (gOutputMode == L8_OUTPUT_NORMAL)
    {
        output = L8SafeNormalize(
            publishedGBuffer.primary.shadingNormalMetallic.xyz) * 0.5f + 0.5f;
    }
    else if (gOutputMode == L8_OUTPUT_ROUGHNESS)
    {
        output = saturate(
            publishedGBuffer.primary.geometricNormalRoughness.w).xxx;
    }
    else if (gOutputMode == L8_OUTPUT_METALLIC)
    {
        output = saturate(
            publishedGBuffer.primary.shadingNormalMetallic.w).xxx;
    }
    else if (gOutputMode == L8_OUTPUT_EMISSIVE)
    {
        uint materialCount;
        uint materialStride;
        gComposeMaterials.GetDimensions(materialCount, materialStride);
        const uint materialId = publishedGBuffer.primary.identity.x;
        if ((publishedGBuffer.primary.identity.w
                & kPrimarySurfaceFlagValidV2) != 0u
            && materialId < materialCount)
        {
            const float4 emissive =
                gComposeMaterials[materialId].emissiveFactorStrength;
            output = max(emissive.xyz * emissive.w, 0.0f.xxx);
        }
    }
    gComposeOutput[pixel] = float4(output, 1.0f);
}
