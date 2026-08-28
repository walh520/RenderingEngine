#include "RestirPrivateTypes.hlsli"

struct DebugParams
{
    uint reservoirCount;
    uint2 outputExtent;
    uint debugMode;
};

[[vk::binding(0, 5)]] ConstantBuffer<DebugParams> gParams;
[[vk::binding(2, 5)]] StructuredBuffer<RestirReservoir> gReservoirs;
[[vk::binding(6, 5)]] StructuredBuffer<uint2> gValidationReasons;
[[vk::binding(8, 5)]] RWStructuredBuffer<RestirDebug> gDebug;
[[vk::binding(10, 5)]] RWTexture2D<float4> gDebugImage;

float3 SourceColor(uint source)
{
    if (source == kRestirSourceUniform) return float3(0.2f, 0.6f, 1.0f);
    if (source == kRestirSourcePower) return float3(1.0f, 0.7f, 0.1f);
    if (source == kRestirSourceEmissiveTriangle) return float3(1.0f, 0.2f, 0.1f);
    if (source == kRestirSourceEnvironment) return float3(0.2f, 1.0f, 0.5f);
    return float3(1.0f, 0.0f, 1.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParams.outputExtent.x || dispatchThreadId.y >= gParams.outputExtent.y)
        return;
    const uint index = dispatchThreadId.y * gParams.outputExtent.x + dispatchThreadId.x;
    if (index >= gParams.reservoirCount) return;

    RestirReservoir reservoir = gReservoirs[index];
    const uint2 rejection = gValidationReasons[index];
    RestirDebug record = (RestirDebug)0;
    record.selectedIdentity = reservoir.selected.identity;
    record.sourceAndState = uint4(
        reservoir.selected.source.x,
        reservoir.selected.source.y,
        reservoir.metadata.x,
        reservoir.metadata.y);
    record.weights = float4(
        reservoir.weights.x,
        reservoir.selected.contributionTarget.w,
        reservoir.selected.proposalSupportCorrection.x,
        reservoir.weights.y);
    record.validation = uint4(rejection, reservoir.metadata.z, 0u);
    gDebug[index] = record;

    float3 color = 0.0f;
    if (gParams.debugMode == 0u) color = SourceColor(reservoir.selected.source.x);
    else if (gParams.debugMode == 1u) color = (float)reservoir.metadata.x / 32.0f;
    else if (gParams.debugMode == 2u) color = (float)reservoir.metadata.y / 20.0f;
    else if (gParams.debugMode == 3u) color = rejection.x != 0u ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
    else if (gParams.debugMode == 4u) color = rejection.y != 0u ? float3(1.0f, 0.2f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
    else if (gParams.debugMode == 5u) color = saturate(reservoir.weights.y).xxx;
    gDebugImage[dispatchThreadId.xy] = float4(color, 1.0f);
}
