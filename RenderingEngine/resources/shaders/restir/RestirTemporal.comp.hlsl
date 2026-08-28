#include "RestirPrivateTypes.hlsli"

// Inputs and output must be distinct allocations. The host must ping-pong
// history/current reservoirs; aliasing binding 7 with bindings 2 or 3 is invalid.
struct TemporalParams
{
    uint reservoirCount;
    uint maxM;
    uint maxHistoryAge;
    uint estimatorMode;
    float normalCosThreshold;
    float relativeDepthThreshold;
    float positionThreshold;
    float thinGeometryThreshold;
    uint baseSeed;
    uint requireSameInstance;
    uint2 reserved;
};

[[vk::binding(0, 5)]] ConstantBuffer<TemporalParams> gParams;
[[vk::binding(2, 5)]] StructuredBuffer<RestirReservoir> gCurrentReservoirs;
[[vk::binding(3, 5)]] StructuredBuffer<RestirReservoir> gHistoryReservoirs;
[[vk::binding(4, 5)]] StructuredBuffer<RestirSurface> gCurrentSurfaces;
[[vk::binding(5, 5)]] StructuredBuffer<RestirSurface> gHistorySurfaces;
// x flags: inside, motion-valid, camera-cut, resize, selected-light-exists.
// y current selected-light generation.
[[vk::binding(6, 5)]] StructuredBuffer<uint4> gTemporalMetadata;
[[vk::binding(7, 5)]] RWStructuredBuffer<RestirReservoir> gOutputReservoirs;
[[vk::binding(8, 5)]] RWStructuredBuffer<RestirDebug> gDebug;
[[vk::binding(9, 5)]] RWStructuredBuffer<uint> gStats;
// History representative re-evaluated at the current surface without visibility.
[[vk::binding(10, 5)]] StructuredBuffer<RestirCandidate> gHistoryAtCurrent;
// Candidate/source target matrix: x current@current, y current@history,
// z history@current, w history@history. Values include support but not visibility.
[[vk::binding(11, 5)]] StructuredBuffer<float4> gReferenceTargetMatrix;
// x/y are visibility support for current/history surface evaluations.
[[vk::binding(12, 5)]] StructuredBuffer<uint2> gReferenceVisibility;

uint ValidateTemporal(
    RestirReservoir history,
    RestirSurface currentSurface,
    RestirSurface historySurface,
    uint4 metadata)
{
    uint reason = 0u;
    if ((history.metadata.z & kRestirReservoirValid) == 0u || history.metadata.x == 0u)
        reason |= kRestirRejectEmpty;
    if ((metadata.x & 1u) == 0u) reason |= kRestirRejectOutside;
    if ((metadata.x & 2u) == 0u) reason |= kRestirRejectMotion;
    if ((metadata.x & 4u) != 0u) reason |= kRestirRejectCameraCut;
    if ((metadata.x & 8u) != 0u) reason |= kRestirRejectResize;
    if ((metadata.x & 16u) == 0u) reason |= kRestirRejectLightDeleted;
    if (history.selected.identity.w != metadata.y) reason |= kRestirRejectLightGeneration;
    if (history.metadata.y >= gParams.maxHistoryAge) reason |= kRestirRejectAge;
    reason |= RestirValidateSurface(
        currentSurface,
        historySurface,
        gParams.normalCosThreshold,
        gParams.relativeDepthThreshold,
        gParams.positionThreshold,
        gParams.thinGeometryThreshold,
        gParams.requireSameInstance != 0u);
    return reason;
}

bool MergeRepresentative(
    inout RestirReservoir destination,
    RestirReservoir source,
    RestirCandidate atCurrent,
    uint reuseSource,
    uint sourceSurfaceIndex,
    inout uint randomState)
{
    if ((source.metadata.z & kRestirReservoirValid) == 0u || source.metadata.x == 0u) return false;
    atCurrent.source.y = reuseSource;
    atCurrent.source.z = sourceSurfaceIndex;
    atCurrent.proposalSupportCorrection.z = source.weights.y;
    const float mergeWeight = atCurrent.contributionTarget.w *
        atCurrent.proposalSupportCorrection.y * source.weights.y * (float)source.metadata.x;
    return RestirUpdate(destination, atCurrent, mergeWeight, source.metadata.x, RestirRandom(randomState));
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gParams.reservoirCount) return;

    RestirReservoir current = gCurrentReservoirs[index];
    RestirReservoir history = gHistoryReservoirs[index];
    const RestirSurface currentSurface = gCurrentSurfaces[index];
    const RestirSurface historySurface = gHistorySurfaces[index];
    const uint4 metadata = gTemporalMetadata[index];
    const uint rejection = ValidateTemporal(history, currentSurface, historySurface, metadata);
    if (rejection != 0u)
    {
        InterlockedAdd(gStats[kRestirCounterTemporalRejected], 1u);
        gOutputReservoirs[index] = current;
        RestirDebug rejectedDebug = (RestirDebug)0;
        rejectedDebug.selectedIdentity = current.selected.identity;
        rejectedDebug.sourceAndState = uint4(current.selected.source.xy, current.metadata.xy);
        rejectedDebug.weights = float4(
            current.weights.x,
            current.selected.contributionTarget.w,
            current.selected.proposalSupportCorrection.x,
            current.weights.y);
        rejectedDebug.validation = uint4(rejection, 0u, current.metadata.z, 0u);
        gDebug[index] = rejectedDebug;
        return;
    }

    RestirReservoir result = RestirEmptyReservoir();
    uint randomState = RestirHash(gParams.baseSeed ^ index);
    if (!MergeRepresentative(
        result, current, current.selected, kRestirSourceInvalid, 0u, randomState))
        InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);
    const bool historyMerged = MergeRepresentative(
        result, history, gHistoryAtCurrent[index], kRestirSourceTemporal, 1u, randomState);
    if (!historyMerged)
    {
        InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);
        InterlockedAdd(gStats[kRestirCounterTemporalRejected], 1u);
        gOutputReservoirs[index] = current;
        RestirDebug invalidRemapDebug = (RestirDebug)0;
        invalidRemapDebug.selectedIdentity = current.selected.identity;
        invalidRemapDebug.sourceAndState = uint4(current.selected.source.xy, current.metadata.xy);
        invalidRemapDebug.weights = float4(
            current.weights.x,
            current.selected.contributionTarget.w,
            current.selected.proposalSupportCorrection.x,
            current.weights.y);
        invalidRemapDebug.validation = uint4(kRestirRejectCandidate, 0u, current.metadata.z, 0u);
        gDebug[index] = invalidRemapDebug;
        return;
    }
    result.metadata.z |= kRestirReservoirTemporalAccepted;

    const uint unclampedM = result.metadata.x;
    RestirClampM(result, gParams.maxM);
    if (unclampedM != result.metadata.x) InterlockedAdd(gStats[kRestirCounterMClamp], 1u);
    RestirFinalizeNaiveBiased(result);

    if (gParams.estimatorMode == kRestirModeUnbiasedReference &&
        (result.metadata.z & kRestirReservoirValid) != 0u)
    {
        const float4 targets = gReferenceTargetMatrix[index];
        const uint2 visible = gReferenceVisibility[index];
        const float mScale = unclampedM > gParams.maxM
            ? (float)gParams.maxM / (float)unclampedM
            : 1.0f;
        const bool fromHistory = result.selected.source.z == 1u;
        const float targetAtCurrent = fromHistory ? targets.z : targets.x;
        const float targetAtSource = fromHistory ? targets.w : targets.x;
        const float denominator = mScale * (
            (float)current.metadata.x * (fromHistory ? targets.z : targets.x) * (float)visible.x +
            (float)history.metadata.x * (fromHistory ? targets.w : targets.y) * (float)visible.y);
        result.weights.y = targetAtCurrent > 0.0f && denominator > 0.0f
            ? result.weights.x * targetAtSource / (targetAtCurrent * denominator)
            : 0.0f;
        result.metadata.z |= kRestirReservoirReferenceMode;
        if (!(result.weights.y > 0.0f)) result.metadata.z &= ~kRestirReservoirValid;
        InterlockedAdd(gStats[kRestirCounterReferenceVisibility], 2u);
    }

    result.metadata.y = result.selected.source.z == 1u
        ? min(history.metadata.y + 1u, gParams.maxHistoryAge)
        : 0u;
    gOutputReservoirs[index] = result;
    InterlockedAdd(gStats[kRestirCounterTemporalAccepted], 1u);

    RestirDebug debugRecord = (RestirDebug)0;
    debugRecord.selectedIdentity = result.selected.identity;
    debugRecord.sourceAndState = uint4(
        result.selected.source.x,
        result.selected.source.y,
        result.metadata.x,
        result.metadata.y);
    debugRecord.weights = float4(
        result.weights.x,
        result.selected.contributionTarget.w,
        result.selected.proposalSupportCorrection.x,
        result.weights.y);
    debugRecord.validation = uint4(0u, 0u, result.metadata.z,
        gParams.estimatorMode == kRestirModeUnbiasedReference ? 2u : 0u);
    gDebug[index] = debugRecord;
}
