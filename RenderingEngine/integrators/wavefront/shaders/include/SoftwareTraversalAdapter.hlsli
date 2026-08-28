#ifndef RENDERING_ENGINE_WAVEFRONT_SOFTWARE_TRAVERSAL_ADAPTER_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_SOFTWARE_TRAVERSAL_ADAPTER_HLSLI

// L4 supplies these two functions before selecting this adapter. L7 owns only
// the seam and intentionally contains no flattened-BVH traversal algorithm.
bool WfSoftwareTraceClosest(WfRayItem ray, uint rayIndex, out WfMaterialWorkItem hit);
bool WfSoftwareTraceAny(WfShadowWorkItem ray);

#ifndef WF_SOFTWARE_TRAVERSAL_IMPLEMENTATION
#error "WF_SOFTWARE_TRAVERSAL_IMPLEMENTATION must name the L4 implementation include"
#endif
#include WF_SOFTWARE_TRAVERSAL_IMPLEMENTATION

bool WfTraceClosest(WfRayItem ray, uint rayIndex, out WfMaterialWorkItem hit)
{
    return WfSoftwareTraceClosest(ray, rayIndex, hit);
}

bool WfTraceAny(WfShadowWorkItem ray)
{
    return WfSoftwareTraceAny(ray);
}

#endif
