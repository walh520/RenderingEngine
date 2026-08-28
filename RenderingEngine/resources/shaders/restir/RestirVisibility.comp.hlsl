#include "RestirPrivateTypes.hlsli"

struct VisibilityParams
{
    uint reservoirCount;
    uint3 reserved;
};

[[vk::binding(0, 5)]] ConstantBuffer<VisibilityParams> gParams;
[[vk::binding(2, 5)]] StructuredBuffer<RestirReservoir> gInputReservoirs;
// Exactly one traversal result per valid winner, produced after reuse.
[[vk::binding(6, 5)]] StructuredBuffer<uint> gWinnerVisibility;
[[vk::binding(7, 5)]] RWStructuredBuffer<RestirReservoir> gOutputReservoirs;
[[vk::binding(8, 5)]] RWStructuredBuffer<RestirDebug> gDebug;
[[vk::binding(9, 5)]] RWStructuredBuffer<uint> gStats;
[[vk::binding(10, 5)]] RWStructuredBuffer<float4> gDirectLighting;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gParams.reservoirCount) return;

    RestirReservoir reservoir = gInputReservoirs[index];
    float3 contribution = 0.0f;
    uint visibilityCount = 0u;
    if ((reservoir.metadata.z & kRestirReservoirValid) != 0u)
    {
        if ((reservoir.metadata.z & kRestirReservoirFinalVisibility) != 0u)
        {
            InterlockedAdd(gStats[kRestirCounterDuplicateFinalVisibility], 1u);
        }
        else
        {
            const bool visible = gWinnerVisibility[index] != 0u;
            reservoir.metadata.z |= kRestirReservoirFinalVisibility;
            contribution = visible
                ? reservoir.selected.contributionTarget.xyz * reservoir.weights.y
                : 0.0f;
            visibilityCount = 1u;
            InterlockedAdd(gStats[kRestirCounterFinalVisibility], 1u);
        }
    }

    gDirectLighting[index] = float4(contribution, 1.0f);
    gOutputReservoirs[index] = reservoir;
    RestirDebug debugRecord = gDebug[index];
    debugRecord.validation.z = reservoir.metadata.z;
    debugRecord.validation.w += visibilityCount;
    gDebug[index] = debugRecord;
}
