#include "../include/WavefrontResources.hlsli"
#include "../include/TraversalAdapter.hlsli"

[numthreads(1, 1, 1)]
void TraversalAdapterProbeCS()
{
    WfRayItem ray = (WfRayItem)0;
    ray.directionTMax.w = 1.0f;
    WfMaterialWorkItem hit;
    const bool closest = WfTraceClosest(ray, 0u, hit);

    WfShadowWorkItem shadow = (WfShadowWorkItem)0;
    shadow.directionTMax.w = 1.0f;
    const bool anyHit = WfTraceAny(shadow);
    gWfFlags[0u] = uint2(closest ? 1u : 0u, anyHit ? 1u : 0u);
}
