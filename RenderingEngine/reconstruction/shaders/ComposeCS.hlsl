#include "ReconstructionCommon.hlsli"

[[vk::binding(0, 0)]] StructuredBuffer<L8GBufferRecord> gComposeGBuffer : register(t0);
[[vk::binding(1, 0)]] StructuredBuffer<L8SignalRecord> gComposeRaw : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<L8SignalRecord> gComposeTemporal : register(t2);
[[vk::binding(3, 0)]] StructuredBuffer<L8SignalRecord> gComposeATrous : register(t3);
[[vk::binding(4, 0)]] StructuredBuffer<L8SignalRecord> gComposeSvgf : register(t4);
[[vk::binding(5, 0)]] StructuredBuffer<L8HistoryRecord> gComposeHistory : register(t5);
[[vk::binding(6, 0)]] StructuredBuffer<L8TemporalDebugRecord> gComposeDebug : register(t6);
[[vk::binding(7, 0)]] RWTexture2D<float4> gComposeOutput : register(u0);

[[vk::binding(8, 0)]] cbuffer L8ComposeConstants : register(b0)
{
    uint2 gComposeExtent;
    uint gOutputMode;
    uint gMaxHistoryLength;
    float gMinimumAlbedo;
    uint gDemodulateSpecular;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gComposeExtent)) return;
    const uint index = L8LinearIndex(pixel, gComposeExtent);
    const L8GBufferRecord gbuffer = gComposeGBuffer[index];
    float3 output = 0.0f.xxx;
    if (gOutputMode == L8_OUTPUT_RAW)
    {
        output = gComposeRaw[index].diffuse + gComposeRaw[index].specular;
    }
    else if (gOutputMode == L8_OUTPUT_TEMPORAL || gOutputMode == L8_OUTPUT_ATROUS || gOutputMode == L8_OUTPUT_SVGF)
    {
        L8SignalRecord signal = (L8SignalRecord)0;
        if (gOutputMode == L8_OUTPUT_TEMPORAL) signal = gComposeTemporal[index];
        else if (gOutputMode == L8_OUTPUT_ATROUS) signal = gComposeATrous[index];
        else signal = gComposeSvgf[index];
        signal = L8Remodulate(signal, gbuffer, gMinimumAlbedo, gDemodulateSpecular);
        output = signal.diffuse + signal.specular;
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
    gComposeOutput[pixel] = float4(output, 1.0f);
}
