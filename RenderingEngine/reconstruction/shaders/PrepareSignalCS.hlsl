#include "ReconstructionCommon.hlsli"

[[vk::binding(0, 0)]] StructuredBuffer<L8GBufferRecord> gGBuffer : register(t0);
[[vk::binding(1, 0)]] StructuredBuffer<L8SignalRecord> gRawSignal : register(t1);
[[vk::binding(2, 0)]] RWStructuredBuffer<L8SignalRecord> gDemodulatedSignal : register(u0);

[[vk::binding(3, 0)]] cbuffer L8PrepareConstants : register(b0)
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
        gRawSignal[index], gGBuffer[index], gMinimumAlbedo, gDemodulateSpecular);
}
