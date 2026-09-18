#include "include/WavefrontResources.hlsli"
#include "include/WavefrontRng.hlsli"
#include "include/PbrWavefrontShading.hlsli"
#include "include/WavefrontReconstructionExport.hlsli"

[numthreads(128, 1, 1)]
void ShadeCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint bounce = gWfPass.pass.x;
    const uint sourceQueue = gWfPass.pass.y;
    const uint activeCount = gWfQueueHeaders[sourceQueue].activeCount;
    const uint index = WfLinearQueueIndex(groupId, groupIndex, activeCount);
    if (index >= activeCount || WfGlobalFatalMask() != 0u)
    {
        return;
    }
    if (index == 0u)
    {
        uint ignored;
        // PrepareDispatch counts Intersect, TraceShadow and NextBounce. Shade
        // reuses Intersect's indirect command, so it records its own fourth
        // non-empty path-stage dispatch here.
        InterlockedAdd(gWfBounceCounters[bounce].errors.w, 1u, ignored);
    }

    WfRayItem ray;
    if (sourceQueue == kWfQueueRayA)
    {
        ray = gWfRayA[index];
    }
    else if (sourceQueue == kWfQueueRayB)
    {
        ray = gWfRayB[index];
    }
    else
    {
        WfSetFatal(kWfFatalShadeSourceQueue);
        return;
    }

    const WfMaterialWorkItem work = gWfMaterialWork[index];
    const uint pathIndex = ray.identity.y;
    // One progressive path is allocated per pixel. identity.x is a unique ray
    // ID and intentionally includes a bounce * capacity offset after the first
    // bounce; identity.y remains the stable path/pixel index.
    const uint pixelIndex = pathIndex;
    // Host allocation rejects pixel products above the 32-bit queue contract.
    const uint pixelCount =
        gWfFrame.imageSample.x * gWfFrame.imageSample.y;
    const uint expectedRayId =
        pathIndex + bounce * gWfFrame.capacityModeSeed.x;
    if (pathIndex >= gWfFrame.capacityModeSeed.x
        || pixelIndex >= pixelCount
        || ray.identity.x != expectedRayId)
    {
        WfSetFatal(kWfFatalShadePathIndex);
        return;
    }

    if (bounce == 0u)
    {
        // Every primary path overwrites its ABI-v2 slot. Miss/error paths stay
        // explicitly invalid instead of leaking an earlier frame's surface.
        WfClearPrimarySurfaceV2(pixelIndex);
    }

    WfPathState state = gWfPaths[pathIndex];
    uint2 flags = uint2(0u, 0u);
    WfNextBounceCandidate nextCandidate = (WfNextBounceCandidate)0;
    WfShadowWorkItem shadowCandidate = (WfShadowWorkItem)0;

    if (work.metadata.y == kWfHitMiss)
    {
        const float3 environment = PbrEnvironmentRadianceL6(ray.directionTMax.xyz);
        float misWeight = 1.0f;
        if (bounce != 0u &&
            (state.identity.w & kWfPathPreviousDelta) == 0u &&
            gPbrFrameL6.sampling.w != 0u)
        {
            const bool restirOwnsPrimaryEnvironment =
                gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_RESTIR_PRIMARY &&
                bounce == 1u &&
                (state.identity.w & kWfPathPreviousRestirOwned) != 0u;
            if (restirOwnsPrimaryEnvironment)
            {
                // A non-delta primary BSDF miss is the same direct-light path
                // already owned by ReSTIR. Delta paths remain visible, and
                // secondary misses continue through the ordinary MIS branch.
                misWeight = 0.0f;
            }
            else
            {
                const float lightPdf = PbrSelectedLightPdfL6(
                    gPbrFrameL6.environment.x,
                    WfPreviousLightContext(state),
                    ray.directionTMax.xyz,
                    0.0f,
                    0.0f);
                if (lightPdf > 0.0f)
                {
                    misWeight = PbrPowerHeuristicL6(
                        state.previousPositionPdf.w, lightPdf);
                }
            }
        }
        if (PbrIsFinite3L6(environment) && PbrIsFiniteFloatL6(misWeight) &&
            WfPbrAccumulateTerminal(state, bounce, environment, misWeight))
        {
        }
        else
        {
            WfPbrRecordNonFinite(bounce);
            state.identity.w |= kWfPathError;
        }
        state.identity.w = (state.identity.w & ~kWfPathActive) | kWfPathTerminated;
        gWfPaths[pathIndex] = state;
        WfPublishSharedPath(pathIndex, state);
        if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
        {
            gWfFlags[index] = flags;
        }
        return;
    }

    const PbrHitL6 hit = WfToPbrHit(work);
    if (!WfPbrHitIsValid(hit, ray) || hit.materialIndex >= gPbrFrameL6.trace.z)
    {
        WfPbrRecordNonFinite(bounce);
        state.identity.w =
            (state.identity.w & ~kWfPathActive) |
            kWfPathTerminated | kWfPathError;
        gWfPaths[pathIndex] = state;
        WfPublishSharedPath(pathIndex, state);
        if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
        {
            gWfFlags[index] = flags;
        }
        return;
    }

    const PbrMaterialGpuL6 material = gPbrMaterialsL6[hit.materialIndex];
    if (!PbrMaterialAuxiliaryIsValidL6(material))
    {
        WfPbrRecordNonFinite(bounce);
        state.identity.w =
            (state.identity.w & ~kWfPathActive) |
            kWfPathTerminated | kWfPathError;
        gWfPaths[pathIndex] = state;
        WfPublishSharedPath(pathIndex, state);
        if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
        {
            gWfFlags[index] = flags;
        }
        return;
    }
    const BsdfParamsL6 bsdfParameters = PbrMaterialToBsdfParamsL6(material);
    const bool currentRestirOwned = bounce == 0u
        && BsdfSupportsRestirPrimaryDirectL6(bsdfParameters);

    if (bounce == 0u)
    {
        WfPublishPrimarySurfaceV2(
            pixelIndex,
            hit.position,
            dot(
                hit.position - gPbrFrameL6.cameraPositionTanHalfFov.xyz,
                normalize(gPbrFrameL6.cameraForwardAspect.xyz)),
            hit.geometricNormal,
            hit.shadingNormal,
            hit.materialIndex,
            hit.instanceId,
            hit.primitiveId,
            hit.frontFace,
            material.baseColorMetallic.xyz,
            material.baseColorMetallic.w,
            material.emissiveRoughness.w,
            material.transmissionIor.x,
            material.f0.xyz,
            currentRestirOwned);
    }

    float3 beta = state.throughputEta.xyz;
    float3 betaDiffuse = state.diffuseThroughput.xyz;
    float3 betaSpecular = state.specularThroughput.xyz;
    if (hit.frontFace == 0u && material.transmissionIor.x > 0.0f &&
        !PbrApplyInteriorAttenuationL6(
            material, hit.t, beta, betaDiffuse, betaSpecular))
    {
        WfPbrRecordNonFinite(bounce);
        state.throughputEta.xyz = 0.0f;
        state.diffuseThroughput.xyz = 0.0f;
        state.specularThroughput.xyz = 0.0f;
        state.identity.w =
            (state.identity.w & ~kWfPathActive) |
            kWfPathTerminated | kWfPathError;
        gWfPaths[pathIndex] = state;
        WfPublishSharedPath(pathIndex, state);
        if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
        {
            gWfFlags[index] = flags;
        }
        return;
    }
    state.throughputEta.xyz = beta;
    state.diffuseThroughput.xyz = betaDiffuse;
    state.specularThroughput.xyz = betaSpecular;

    const float3 emission = material.emissiveRoughness.xyz;
    if (WfPbrEmitterVisible(hit) && PbrMaxComponentL6(emission) > 0.0f)
    {
        float misWeight = 1.0f;
        if (bounce != 0u &&
            (state.identity.w & kWfPathPreviousDelta) == 0u &&
            gPbrFrameL6.sampling.w != 0u)
        {
            const bool restirOwnsPrimaryEmitter =
                gPbrFrameL6.sampling.w == PBR_L6_ESTIMATOR_RESTIR_PRIMARY &&
                bounce == 1u &&
                (state.identity.w & kWfPathPreviousRestirOwned) != 0u;
            if (restirOwnsPrimaryEmitter)
            {
                // Suppress only the non-delta primary emitter path. ReSTIR
                // cannot replace a delta chain, and deeper emitter hits remain
                // part of the Wavefront indirect MIS estimate.
                misWeight = 0.0f;
            }
            else
            {
                const float lightPdf = PbrSelectedLightPdfL6(
                    hit.emitterLightIndex,
                    WfPreviousLightContext(state),
                    ray.directionTMax.xyz,
                    hit.t,
                    hit.geometricNormal);
                if (lightPdf > 0.0f)
                {
                    misWeight = PbrPowerHeuristicL6(
                        state.previousPositionPdf.w, lightPdf);
                }
            }
        }
        if (!WfPbrAccumulateTerminal(state, bounce, emission, misWeight))
        {
            WfPbrRecordNonFinite(bounce);
            state.identity.w =
                (state.identity.w & ~kWfPathActive) |
                kWfPathTerminated | kWfPathError;
            gWfPaths[pathIndex] = state;
            WfPublishSharedPath(pathIndex, state);
            if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
            {
                gWfFlags[index] = flags;
            }
            return;
        }
    }

    if ((material.metadata.w & PBR_L6_MATERIAL_PURE_EMITTER) != 0u ||
        !PbrIsFinite3L6(beta) || any(beta < 0.0f))
    {
        state.identity.w = (state.identity.w & ~kWfPathActive) | kWfPathTerminated;
        gWfPaths[pathIndex] = state;
        WfPublishSharedPath(pathIndex, state);
        if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
        {
            gWfFlags[index] = flags;
        }
        return;
    }

    const BsdfContextL6 bsdfContext = PbrBuildBsdfContextL6(hit, material);
    if (!ValidateBsdfParamsL6(bsdfContext, bsdfParameters))
    {
        WfPbrRecordNonFinite(bounce);
        state.identity.w =
            (state.identity.w & ~kWfPathActive) |
            kWfPathTerminated | kWfPathError;
        gWfPaths[pathIndex] = state;
        WfPublishSharedPath(pathIndex, state);
        if (gWfFrame.capacityModeSeed.y == kWfQueueModePrefixScan)
        {
            gWfFlags[index] = flags;
        }
        return;
    }

    const float3 wo = -ray.directionTMax.xyz;
    if (WfPbrBuildDirectShadow(
        pathIndex,
        state.identity.y,
        bounce,
        hit,
        bsdfContext,
        bsdfParameters,
        wo,
        beta,
        betaDiffuse,
        betaSpecular,
        currentRestirOwned,
        shadowCandidate))
    {
        flags.y = 1u;
    }

    if (bounce + 1u < gWfFrame.imageSample.w)
    {
        const BsdfSampleL6 bsdfSample = SampleBsdfL6(
            bsdfContext,
            bsdfParameters,
            wo,
            WfPbrBsdfRandom(pathIndex, state.identity.y, bounce));
        bool validSample = bsdfSample.isValid != 0u &&
            PbrIsFiniteFloatL6(bsdfSample.pdf) &&
            bsdfSample.pdf > 0.0f &&
            PbrIsFinite3L6(bsdfSample.value) &&
            PbrIsFinite3L6(bsdfSample.direction) &&
            !any(bsdfSample.value < 0.0f);
        if (bsdfSample.pdf < 0.0f)
        {
            WfPbrRecordNegativePdf(bounce);
        }

        float3 nextBeta = 0.0f;
        float3 nextDiffuse = 0.0f;
        float3 nextSpecular = 0.0f;
        float etaScale = state.throughputEta.w;
        if (validSample)
        {
            const float cosine = abs(dot(hit.geometricNormal, bsdfSample.direction));
            const float3 factor = bsdfSample.value * (cosine / bsdfSample.pdf);
            if (bounce == 0u)
            {
                nextDiffuse = beta * bsdfSample.diffuseValue *
                    (cosine / bsdfSample.pdf);
                nextSpecular = beta * bsdfSample.specularValue *
                    (cosine / bsdfSample.pdf);
            }
            else
            {
                nextDiffuse = betaDiffuse * factor;
                nextSpecular = betaSpecular * factor;
            }
            nextBeta = nextDiffuse + nextSpecular;
            validSample = PbrIsFinite3L6(nextBeta) &&
                PbrIsFinite3L6(nextDiffuse) &&
                PbrIsFinite3L6(nextSpecular) &&
                !any(nextBeta < 0.0f);
            if ((bsdfSample.lobeFlags & kBsdfLobeTransmissionL6) != 0u)
            {
                etaScale *= bsdfSample.eta * bsdfSample.eta;
                validSample = validSample &&
                    PbrIsFiniteFloatL6(etaScale) && etaScale > 0.0f;
            }
        }

        const uint nextBounce = bounce + 1u;
        const float rrStart = gPbrFrameL6.russianRoulette.x;
        const float rrMinimum = gPbrFrameL6.russianRoulette.y;
        const float rrMaximum = gPbrFrameL6.russianRoulette.z;
        const bool validRoulette = PbrIsFiniteFloatL6(rrStart) &&
            PbrIsFiniteFloatL6(rrMinimum) &&
            PbrIsFiniteFloatL6(rrMaximum) &&
            rrStart >= 0.0f && floor(rrStart) == rrStart &&
            rrMinimum > 0.0f && rrMinimum <= rrMaximum && rrMaximum <= 1.0f;
        if (!validRoulette)
        {
            validSample = false;
        }
        else if (validSample && float(nextBounce) >= rrStart)
        {
            const float rrMagnitude = PbrMaxComponentL6(nextBeta * etaScale);
            if (!(rrMagnitude > 0.0f) || !PbrIsFiniteFloatL6(rrMagnitude))
            {
                validSample = false;
            }
            else
            {
                const float continuation = clamp(
                    rrMagnitude, rrMinimum, rrMaximum);
                const float rouletteSample = WfSampleDimension(
                    pathIndex,
                    state.identity.y,
                    WfBounceDimension(bounce, kWfRussianRoulette),
                    gPbrFrameL6.output.x,
                    gPbrFrameL6.sampling.x,
                    gPbrFrameL6.sampling.y);
                if (rouletteSample >= continuation)
                {
                    validSample = false;
                }
                else
                {
                    nextBeta /= continuation;
                    nextDiffuse /= continuation;
                    nextSpecular /= continuation;
                }
            }
        }

        if (validSample)
        {
            uint eventFlags = 0u;
            if (bsdfSample.isDelta != 0u)
            {
                eventFlags |= kWfPathPreviousDelta;
            }
            if ((bsdfSample.lobeFlags & kBsdfLobeDiffuseL6) == 0u)
            {
                eventFlags |= kWfPathPreviousSpecular;
            }
            if (currentRestirOwned)
            {
                eventFlags |= kWfPathPreviousRestirOwned;
            }
            nextCandidate.originTMin = float4(
                PbrOffsetRayOriginL6(
                    hit.position, hit.geometricNormal, bsdfSample.direction),
                gPbrFrameL6.russianRoulette.w);
            nextCandidate.directionTMax = float4(
                normalize(bsdfSample.direction), 1.0e30f);
            nextCandidate.throughputEta = float4(nextBeta, etaScale);
            nextCandidate.diffuseThroughput = float4(nextDiffuse, 0.0f);
            nextCandidate.specularThroughput = float4(nextSpecular, 0.0f);
            nextCandidate.previousPositionPdf = float4(hit.position, bsdfSample.pdf);
            nextCandidate.previousGeometricNormal = float4(hit.geometricNormal, 0.0f);
            nextCandidate.previousShadingNormal = float4(hit.shadingNormal, 0.0f);
            nextCandidate.identity = uint4(pathIndex, nextBounce, eventFlags, 0u);
            flags.x = 1u;
        }
        else if (!PbrIsFinite3L6(nextBeta))
        {
            WfPbrRecordNonFinite(bounce);
            state.identity.w |= kWfPathError;
        }
    }

    if (gWfFrame.capacityModeSeed.y == kWfQueueModeAtomicAppend)
    {
        if (flags.x != 0u)
        {
            uint slot;
            if (WfTryReserve(kWfQueueNext, slot))
            {
                gWfNextQueue[slot] = nextCandidate;
                uint ignored;
                InterlockedAdd(gWfBounceCounters[bounce].work.w, 1u, ignored);
            }
        }
        if (flags.y != 0u)
        {
            uint slot;
            if (WfTryReserve(kWfQueueShadow, slot))
            {
                WfShadowQueueItem sharedShadow;
                WfShadowAovItem shadowAov;
                WfPackShadowQueue(slot, shadowCandidate, sharedShadow, shadowAov);
                gWfShadowQueue[slot] = sharedShadow;
                gWfShadowAov[slot] = shadowAov;
                uint ignored;
                InterlockedAdd(gWfBounceCounters[bounce].work.z, 1u, ignored);
            }
        }
    }
    else
    {
        gWfDenseNext[index] = nextCandidate;
        gWfDenseShadow[index] = shadowCandidate;
        gWfFlags[index] = flags;
    }

    if (flags.x == 0u)
    {
        state.identity.w = (state.identity.w & ~kWfPathActive) | kWfPathTerminated;
    }
    gWfPaths[pathIndex] = state;
    WfPublishSharedPath(pathIndex, state);
}
