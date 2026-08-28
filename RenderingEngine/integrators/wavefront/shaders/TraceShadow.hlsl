#include "include/WavefrontResources.hlsli"
#include "include/TraversalAdapter.hlsli"

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

    const WfShadowWorkItem work = gWfShadowQueue[index];
    if (work.identity.x >= gWfFrame.capacityModeSeed.x)
    {
        WfSetFatal(kWfFatalInvalidCapacity);
        return;
    }
    if (!WfTraceAny(work))
    {
        WfPathState state = gWfPaths[work.identity.x];
        float3 diffuse;
        float3 specular;
        if (work.identity.y == 0u)
        {
            diffuse = state.directDiffuse.xyz + work.diffuseContributionPdf.xyz;
            specular = state.directSpecular.xyz + work.specularContributionLight.xyz;
        }
        else
        {
            diffuse = state.indirectDiffuse.xyz + work.diffuseContributionPdf.xyz;
            specular = state.indirectSpecular.xyz + work.specularContributionLight.xyz;
        }
        if (!WfFiniteNonNegative3(diffuse) || !WfFiniteNonNegative3(specular))
        {
            uint ignored;
            InterlockedAdd(gWfBounceCounters[gWfPass.pass.x].errors.y, 1u, ignored);
            state.identity.w =
                (state.identity.w & ~kWfPathActive) |
                kWfPathTerminated | kWfPathError;
            gWfPaths[work.identity.x] = state;
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
    }
}
