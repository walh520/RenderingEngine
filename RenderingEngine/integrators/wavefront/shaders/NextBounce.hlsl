#include "include/WavefrontResources.hlsli"

[numthreads(128, 1, 1)]
void NextBounceCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint activeCount = gWfQueueHeaders[kWfQueueNext].activeCount;
    const uint index = WfLinearQueueIndex(groupId, groupIndex, activeCount);
    if (index >= activeCount || WfGlobalFatalMask() != 0u)
    {
        return;
    }

    const WfNextBounceCandidate candidate = gWfNextQueue[index];
    const uint pathIndex = candidate.identity.x;
    if (pathIndex >= gWfFrame.capacityModeSeed.x)
    {
        WfSetFatal(kWfFatalInvalidCapacity);
        return;
    }

    const float directionLengthSquared = dot(
        candidate.directionTMax.xyz, candidate.directionTMax.xyz);
    const float geometricLengthSquared = dot(
        candidate.previousGeometricNormal.xyz,
        candidate.previousGeometricNormal.xyz);
    const float shadingLengthSquared = dot(
        candidate.previousShadingNormal.xyz,
        candidate.previousShadingNormal.xyz);
    const bool candidateIsValid =
        WfFinite4(candidate.originTMin) && candidate.originTMin.w > 0.0f &&
        WfFinite4(candidate.directionTMax) &&
        candidate.directionTMax.w > candidate.originTMin.w &&
        isfinite(directionLengthSquared) && directionLengthSquared > 0.0f &&
        WfFinite4(candidate.throughputEta) &&
        all(candidate.throughputEta.xyz >= 0.0f) && candidate.throughputEta.w > 0.0f &&
        WfFinite4(candidate.diffuseThroughput) &&
        all(candidate.diffuseThroughput.xyz >= 0.0f) &&
        WfFinite4(candidate.specularThroughput) &&
        all(candidate.specularThroughput.xyz >= 0.0f) &&
        WfFinite4(candidate.previousPositionPdf) &&
        candidate.previousPositionPdf.w > 0.0f &&
        WfFinite4(candidate.previousGeometricNormal) &&
        isfinite(geometricLengthSquared) && geometricLengthSquared > 0.0f &&
        WfFinite4(candidate.previousShadingNormal) &&
        isfinite(shadingLengthSquared) && shadingLengthSquared > 0.0f &&
        candidate.identity.y < gWfFrame.imageSample.w;
    if (!candidateIsValid)
    {
        uint ignored;
        InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].errors.y, 1u, ignored);
        WfPathState invalidState = gWfPaths[pathIndex];
        invalidState.identity.w =
            (invalidState.identity.w & ~kWfPathActive) |
            kWfPathTerminated | kWfPathError;
        gWfPaths[pathIndex] = invalidState;
        return;
    }

    WfPathState state = gWfPaths[pathIndex];
    if ((state.identity.w & kWfPathError) != 0u)
    {
        // TraceShadow may have detected a non-finite AOV accumulation after
        // Shade produced this candidate. Do not reactivate that failed path.
        return;
    }

    WfRayItem ray;
    ray.originTMin = candidate.originTMin;
    ray.directionTMax = candidate.directionTMax;
    ray.path = uint4(pathIndex, gWfPaths[pathIndex].identity.x, candidate.identity.y, candidate.identity.z);

    uint slot;
    const uint destinationQueue = gWfPass.pass.z;
    if (!WfTryReserve(destinationQueue, slot))
    {
        return;
    }
    if (destinationQueue == kWfQueueRayA)
    {
        gWfRayA[slot] = ray;
    }
    else
    {
        gWfRayB[slot] = ray;
    }

    state.throughputEta = candidate.throughputEta;
    state.diffuseThroughput = candidate.diffuseThroughput;
    state.specularThroughput = candidate.specularThroughput;
    state.previousPositionPdf = candidate.previousPositionPdf;
    state.previousGeometricNormal = candidate.previousGeometricNormal;
    state.previousShadingNormal = candidate.previousShadingNormal;
    state.identity.z = candidate.identity.y;
    state.identity.w =
        (state.identity.w & kWfPathError) |
        kWfPathActive |
        (candidate.identity.z & (kWfPathPreviousDelta | kWfPathPreviousSpecular));
    gWfPaths[pathIndex] = state;

    uint ignored;
    InterlockedAdd(gWfBounceCounters[candidate.identity.y].work.x, 1u, ignored);
}
