#include "RestirCandidateGeneration.hlsli"

struct InitialParams
{
    uint reservoirCount;
    uint candidatesPerReservoir;
    uint maxM;
    uint baseSeed;
};

[[vk::binding(0, 5)]] ConstantBuffer<InitialParams> gParams;
[[vk::binding(1, 5)]] StructuredBuffer<RestirCandidate> gCandidates;
[[vk::binding(7, 5)]] RWStructuredBuffer<RestirReservoir> gOutputReservoirs;
[[vk::binding(8, 5)]] RWStructuredBuffer<RestirDebug> gDebug;
[[vk::binding(9, 5)]] RWStructuredBuffer<uint> gStats;

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint reservoirIndex = dispatchThreadId.x;
    if (reservoirIndex >= gParams.reservoirCount) return;

    RestirReservoir reservoir = RestirEmptyReservoir();
    uint randomState = RestirHash(gParams.baseSeed ^ reservoirIndex);
    const uint candidateOffset = reservoirIndex * gParams.candidatesPerReservoir;
    for (uint candidateIndex = 0u; candidateIndex < gParams.candidatesPerReservoir; ++candidateIndex)
    {
        RestirCandidate candidate = gCandidates[candidateOffset + candidateIndex];
        InterlockedAdd(gStats[kRestirCounterCandidate], 1u);
        if (candidate.contributionTarget.w == 0.0f) InterlockedAdd(gStats[kRestirCounterZeroTarget], 1u);
        if (!(candidate.proposalSupportCorrection.x > 0.0f)) InterlockedAdd(gStats[kRestirCounterZeroPdf], 1u);
        if (candidate.proposalSupportCorrection.y == 0.0f) InterlockedAdd(gStats[kRestirCounterZeroSupport], 1u);
        if (!RestirCandidateValid(candidate))
        {
            InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);
            continue;
        }
        if (!RestirUpdate(reservoir, candidate, RestirCandidateWeight(candidate), 1u, RestirRandom(randomState)))
            InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);
    }

    const uint unclampedM = reservoir.metadata.x;
    RestirClampM(reservoir, gParams.maxM);
    if (reservoir.metadata.x != unclampedM) InterlockedAdd(gStats[kRestirCounterMClamp], 1u);
    RestirFinalizeNaiveBiased(reservoir);
    gOutputReservoirs[reservoirIndex] = reservoir;

    RestirDebug debugRecord = (RestirDebug)0;
    debugRecord.selectedIdentity = reservoir.selected.identity;
    debugRecord.sourceAndState = uint4(
        reservoir.selected.source.x,
        reservoir.selected.source.y,
        reservoir.metadata.x,
        reservoir.metadata.y);
    debugRecord.weights = float4(
        reservoir.weights.x,
        reservoir.selected.contributionTarget.w,
        reservoir.selected.proposalSupportCorrection.x,
        reservoir.weights.y);
    debugRecord.validation.z = reservoir.metadata.z;
    gDebug[reservoirIndex] = debugRecord;
}
