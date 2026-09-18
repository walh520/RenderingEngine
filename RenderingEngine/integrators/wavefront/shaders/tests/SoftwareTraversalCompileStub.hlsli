// Compile-only L4 injection fixture. It proves the L7 adapter is linkable
// without pretending to implement software BVH traversal.
bool WfSoftwareTraceClosest(
    WfRayItem ray,
    uint rayIndex,
    out WfMaterialWorkItem hit)
{
    hit = (WfMaterialWorkItem)0;
    hit.metadata = uint4(ray.identity.x, kWfHitMiss, 0u, ray.identity.y);
    return false;
}

bool WfSoftwareTraceAny(WfShadowWorkItem ray)
{
    return ray.directionTMax.w < 0.0f;
}
