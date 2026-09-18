#include "ReconstructionCommon.hlsli"

[[vk::binding(0, 4)]] StructuredBuffer<GpuGBufferRecordV2> gGBuffer;
[[vk::binding(4, 4)]] StructuredBuffer<GpuReconstructionSignalV2> gRawSignal;
[[vk::binding(5, 4)]] RWStructuredBuffer<L8SignalRecord> gDemodulatedSignal;

[[vk::binding(17, 4)]] cbuffer L8PrepareConstants
{
    uint2 gPrepareExtent;
    float gMinimumAlbedo;
    uint gDemodulateSpecular;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gPrepareExtent)) return;
    const uint index = L8LinearIndex(pixel, gPrepareExtent);
    gDemodulatedSignal[index] = L8Demodulate(
        L8LoadRawSignal(gRawSignal[index]), L8LoadGBuffer(gGBuffer[index]),
        gMinimumAlbedo, gDemodulateSpecular);
}
