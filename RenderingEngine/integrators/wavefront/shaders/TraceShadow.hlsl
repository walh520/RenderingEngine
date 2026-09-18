#include "include/WavefrontResources.hlsli"
#include "include/TraversalAdapter.hlsli"
#include "include/WavefrontShadowVisibility.hlsli"

[numthreads(128, 1, 1)]
void TraceShadowCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint activeCount = gWfQueueHeaders[kWfQueueShadow].activeCount;
    const uint index = WfLinearQueueIndex(groupId, groupIndex, activeCount);
    if (index >= activeCount || WfGlobalFatalMask() != 0u)
    {
        return;
    }

    const WfShadowWorkItem work = WfUnpackShadowQueue(
        gWfShadowQueue[index],
        gWfShadowAov[index]);
    if (work.identity.x >= gWfFrame.capacityModeSeed.x)
    {
        WfSetFatal(kWfFatalShadowPathIndex);
        return;
    }
    if (work.sampling.x > kWfShadowMethodPhysical
        || work.sampling.x != gPbrFrameL6.output.w)
    {
        // A queue record from a different ShadowMethod/frame must never be
        // reinterpreted under the current frame's policy.
        WfSetFatal(kWfFatalShadowMethod);
        return;
    }

    const float visibility = WfEvaluateShadowVisibility(work, index);
    if (!isfinite(visibility) || visibility < 0.0f || visibility > 1.0f)
    {
        uint ignored;
        InterlockedAdd(
            gWfBounceCounters[gWfPass.pass.x].errors.y, 1u, ignored);
        WfPathState invalidState = gWfPaths[work.identity.x];
        invalidState.identity.w =
            (invalidState.identity.w & ~kWfPathActive) |
            kWfPathTerminated | kWfPathError;
        gWfPaths[work.identity.x] = invalidState;
        WfPublishSharedPath(work.identity.x, invalidState);
        return;
    }

    if (visibility > 0.0f)
    {
        WfPathState state = gWfPaths[work.identity.x];
        float3 diffuse;
        float3 specular;
        if (work.identity.y == 0u)
        {
            diffuse = state.directDiffuse.xyz
                + work.diffuseContributionPdf.xyz * visibility;
            specular = state.directSpecular.xyz
                + work.specularContributionLight.xyz * visibility;
        }
        else
        {
            diffuse = state.indirectDiffuse.xyz
                + work.diffuseContributionPdf.xyz * visibility;
            specular = state.indirectSpecular.xyz
                + work.specularContributionLight.xyz * visibility;
        }
        if (!WfFiniteNonNegative3(diffuse) || !WfFiniteNonNegative3(specular))
        {
            uint ignored;
            InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].errors.y, 1u, ignored);
            state.identity.w =
                (state.identity.w & ~kWfPathActive) |
                kWfPathTerminated | kWfPathError;
            gWfPaths[work.identity.x] = state;
            WfPublishSharedPath(work.identity.x, state);
            return;
        }
        if (work.identity.y == 0u)
        {
            state.directDiffuse.xyz = diffuse;
            state.directSpecular.xyz = specular;
        }
        else
        {
            state.indirectDiffuse.xyz = diffuse;
            state.indirectSpecular.xyz = specular;
        }
        gWfPaths[work.identity.x] = state;
        WfPublishSharedPath(work.identity.x, state);
    }
}
