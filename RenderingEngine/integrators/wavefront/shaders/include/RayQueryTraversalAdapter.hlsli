#ifndef RENDERING_ENGINE_WAVEFRONT_RAY_QUERY_TRAVERSAL_ADAPTER_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_RAY_QUERY_TRAVERSAL_ADAPTER_HLSLI

// L5 supplies AS descriptors, alpha semantics and these functions. This keeps
// L7 independent of BLAS/TLAS construction and instance metadata ownership.
bool WfRayQueryTraceClosest(WfRayItem ray, uint rayIndex, out WfMaterialWorkItem hit);
bool WfRayQueryTraceAny(WfShadowWorkItem ray);

#ifndef WF_RAY_QUERY_TRAVERSAL_IMPLEMENTATION
#error "WF_RAY_QUERY_TRAVERSAL_IMPLEMENTATION must name the L5 implementation include"
#endif
#include WF_RAY_QUERY_TRAVERSAL_IMPLEMENTATION

bool WfTraceClosest(WfRayItem ray, uint rayIndex, out WfMaterialWorkItem hit)
{
    return WfRayQueryTraceClosest(ray, rayIndex, hit);
}

bool WfTraceAny(WfShadowWorkItem ray)
{
    return WfRayQueryTraceAny(ray);
}

#endif
