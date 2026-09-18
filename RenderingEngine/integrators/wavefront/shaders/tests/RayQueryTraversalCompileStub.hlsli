// Compile-only L5 injection fixture. It proves the L7 adapter is linkable
// without pretending to build or query a real acceleration structure.
bool WfRayQueryTraceClosest(
    WfRayItem ray,
    uint rayIndex,
    out WfMaterialWorkItem hit)
{
    hit = (WfMaterialWorkItem)0;
    hit.metadata = uint4(ray.identity.x, kWfHitMiss, 0u, ray.identity.y);
    return false;
}

bool WfRayQueryTraceAny(WfShadowWorkItem ray)
{
    return ray.directionTMax.w < 0.0f;
}
