#include "RestirPrivateTypes.hlsli"

// Spatial reuse is strictly ping-pong: gInputReservoirs/gInputSurfaces and
// gOutputReservoirs must never alias within one dispatch. This avoids order-
// dependent neighbor feedback.
struct SpatialParams
{
    uint reservoirCount;
    uint neighborCount;
    uint maxM;
    uint maxHistoryAge;
    uint estimatorMode;
    uint baseSeed;
    uint requireSameInstance;
    uint reserved0;
    float normalCosThreshold;
    float relativeDepthThreshold;
    float positionThreshold;
    float thinGeometryThreshold;
};

[[vk::binding(0, 5)]] ConstantBuffer<SpatialParams> gParams;
[[vk::binding(2, 5)]] StructuredBuffer<RestirReservoir> gInputReservoirs;
[[vk::binding(4, 5)]] StructuredBuffer<RestirSurface> gInputSurfaces;
// Per center pixel, neighborCount global reservoir indices.
[[vk::binding(6, 5)]] StructuredBuffer<uint> gNeighborIndices;
[[vk::binding(7, 5)]] RWStructuredBuffer<RestirReservoir> gOutputReservoirs;
[[vk::binding(8, 5)]] RWStructuredBuffer<RestirDebug> gDebug;
[[vk::binding(9, 5)]] RWStructuredBuffer<uint> gStats;
// Per center, source 0 is center and sources 1..N are neighbors. Every entry is
// that source reservoir's representative re-evaluated at the center surface.
[[vk::binding(10, 5)]] StructuredBuffer<RestirCandidate> gCandidateAtCenter;
// Per center/source: x light exists, y current light generation.
[[vk::binding(11, 5)]] StructuredBuffer<uint2> gSourceLightMetadata;
// Square matrix per center. Row is the selected source candidate, column is
// the evaluation surface. x = target*support, y = visibility support (0/1).
// This explicit matrix is validation-only and records reference visibility cost.
[[vk::binding(12, 5)]] StructuredBuffer<float2> gPairwiseTargetSupport;

uint SourceStride()
{
    return gParams.neighborCount + 1u;
}

uint SourceReservoirIndex(uint centerIndex, uint sourceIndex)
{
    return sourceIndex == 0u
        ? centerIndex
        : gNeighborIndices[centerIndex * gParams.neighborCount + sourceIndex - 1u];
}

RestirReservoir LoadSourceReservoir(uint centerIndex, uint sourceIndex)
{
    return gInputReservoirs[SourceReservoirIndex(centerIndex, sourceIndex)];
}

RestirSurface LoadSourceSurface(uint centerIndex, uint sourceIndex)
{
    return gInputSurfaces[SourceReservoirIndex(centerIndex, sourceIndex)];
}

float2 LoadPairwise(uint centerIndex, uint selectedSource, uint surfaceSource)
{
    const uint stride = SourceStride();
    return gPairwiseTargetSupport[centerIndex * stride * stride + selectedSource * stride + surfaceSource];
}

bool MergeSpatialSource(
    inout RestirReservoir destination,
    RestirReservoir source,
    RestirCandidate atCenter,
    uint sourceIndex,
    inout uint randomState)
{
    if ((source.metadata.z & kRestirReservoirValid) == 0u || source.metadata.x == 0u) return false;
    atCenter.source.y = sourceIndex == 0u ? kRestirSourceInvalid : kRestirSourceSpatial;
    atCenter.source.z = sourceIndex;
    atCenter.proposalSupportCorrection.z = source.weights.y;
    const float mergeWeight = atCenter.contributionTarget.w *
        atCenter.proposalSupportCorrection.y * source.weights.y * (float)source.metadata.x;
    return RestirUpdate(destination, atCenter, mergeWeight, source.metadata.x, RestirRandom(randomState));
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint centerIndex = dispatchThreadId.x;
    if (centerIndex >= gParams.reservoirCount) return;

    const uint stride = SourceStride();
    const RestirSurface centerSurface = gInputSurfaces[centerIndex];
    if (gParams.neighborCount > 30u)
    {
        // The accepted-source mask is deliberately 32-bit (center + 30
        // neighbors). Reject an unsupported dispatch instead of allowing a
        // wrapped shift to silently reuse a different source.
        RestirReservoir passthrough = gInputReservoirs[centerIndex];
        gOutputReservoirs[centerIndex] = passthrough;
        RestirDebug invalidConfig = (RestirDebug)0;
        invalidConfig.selectedIdentity = passthrough.selected.identity;
        invalidConfig.sourceAndState = uint4(
            passthrough.selected.source.x,
            passthrough.selected.source.y,
            passthrough.metadata.x,
            passthrough.metadata.y);
        invalidConfig.validation = uint4(0u, kRestirRejectCandidate, passthrough.metadata.z, 0u);
        gDebug[centerIndex] = invalidConfig;
        InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);
        return;
    }
    RestirReservoir result = RestirEmptyReservoir();
    uint randomState = RestirHash(gParams.baseSeed ^ centerIndex);
    uint acceptedMask = 1u;
    uint aggregateRejection = 0u;

    RestirReservoir center = gInputReservoirs[centerIndex];
    if (!MergeSpatialSource(
        result,
        center,
        gCandidateAtCenter[centerIndex * stride],
        0u,
        randomState))
        InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);

    const uint boundedNeighborCount = min(gParams.neighborCount, 30u);
    for (uint neighborOffset = 0u; neighborOffset < boundedNeighborCount; ++neighborOffset)
    {
        const uint sourceIndex = neighborOffset + 1u;
        const uint neighborIndex = SourceReservoirIndex(centerIndex, sourceIndex);
        if (neighborIndex >= gParams.reservoirCount)
        {
            aggregateRejection |= kRestirRejectOutside;
            InterlockedAdd(gStats[kRestirCounterSpatialRejected], 1u);
            continue;
        }

        RestirReservoir neighbor = gInputReservoirs[neighborIndex];
        const RestirSurface neighborSurface = gInputSurfaces[neighborIndex];
        uint reason = RestirValidateSurface(
            centerSurface,
            neighborSurface,
            gParams.normalCosThreshold,
            gParams.relativeDepthThreshold,
            gParams.positionThreshold,
            gParams.thinGeometryThreshold,
            gParams.requireSameInstance != 0u);
        const uint2 lightMetadata = gSourceLightMetadata[centerIndex * stride + sourceIndex];
        if ((neighbor.metadata.z & kRestirReservoirValid) == 0u || neighbor.metadata.x == 0u)
            reason |= kRestirRejectEmpty;
        if (lightMetadata.x == 0u) reason |= kRestirRejectLightDeleted;
        if (neighbor.selected.identity.w != lightMetadata.y) reason |= kRestirRejectLightGeneration;
        if (neighbor.metadata.y >= gParams.maxHistoryAge) reason |= kRestirRejectAge;
        if (reason != 0u)
        {
            aggregateRejection |= reason;
            InterlockedAdd(gStats[kRestirCounterSpatialRejected], 1u);
            continue;
        }

        if (!MergeSpatialSource(
            result,
            neighbor,
            gCandidateAtCenter[centerIndex * stride + sourceIndex],
            sourceIndex,
            randomState))
        {
            aggregateRejection |= kRestirRejectCandidate;
            InterlockedAdd(gStats[kRestirCounterInvalidCandidate], 1u);
            continue;
        }
        acceptedMask |= 1u << sourceIndex;
        result.metadata.z |= kRestirReservoirSpatialAccepted;
        InterlockedAdd(gStats[kRestirCounterSpatialAccepted], 1u);
    }

    const uint unclampedM = result.metadata.x;
    RestirClampM(result, gParams.maxM);
    if (unclampedM != result.metadata.x) InterlockedAdd(gStats[kRestirCounterMClamp], 1u);
    RestirFinalizeNaiveBiased(result);

    if (gParams.estimatorMode == kRestirModeUnbiasedReference &&
        (result.metadata.z & kRestirReservoirValid) != 0u)
    {
        const uint selectedSource = min(result.selected.source.z, stride - 1u);
        const float2 currentEvaluation = LoadPairwise(centerIndex, selectedSource, 0u);
        const float2 selectedSourceEvaluation = LoadPairwise(centerIndex, selectedSource, selectedSource);
        const float mScale = unclampedM > gParams.maxM
            ? (float)gParams.maxM / (float)unclampedM
            : 1.0f;
        float denominator = 0.0f;
        uint visibilityCount = 0u;
        const uint boundedSourceCount = boundedNeighborCount + 1u;
        for (uint sourceIndex = 0u; sourceIndex < boundedSourceCount; ++sourceIndex)
        {
            if ((acceptedMask & (1u << sourceIndex)) == 0u) continue;
            const RestirReservoir source = LoadSourceReservoir(centerIndex, sourceIndex);
            const float2 evaluation = LoadPairwise(centerIndex, selectedSource, sourceIndex);
            denominator += (float)source.metadata.x * mScale * evaluation.x * evaluation.y;
            ++visibilityCount;
        }
        result.weights.y = currentEvaluation.x > 0.0f && denominator > 0.0f
            ? result.weights.x * selectedSourceEvaluation.x /
                (currentEvaluation.x * denominator)
            : 0.0f;
        result.metadata.z |= kRestirReservoirReferenceMode;
        if (!(result.weights.y > 0.0f)) result.metadata.z &= ~kRestirReservoirValid;
        InterlockedAdd(gStats[kRestirCounterReferenceVisibility], visibilityCount);
    }

    result.metadata.y = 0u;
    gOutputReservoirs[centerIndex] = result;

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
    debugRecord.validation = uint4(0u, aggregateRejection, result.metadata.z, 0u);
    gDebug[centerIndex] = debugRecord;
}
