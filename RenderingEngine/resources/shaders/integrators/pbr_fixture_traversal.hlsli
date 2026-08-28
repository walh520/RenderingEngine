#ifndef RENDERING_ENGINE_PBR_FIXTURE_TRAVERSAL_HLSLI
#define RENDERING_ENGINE_PBR_FIXTURE_TRAVERSAL_HLSLI

// This analytic fixture is a private seam for L6 development. Production
// traversal adapters can replace the two PBR_L6_TRACE_* macros without changing
// the path integrator or its sampling dimensions.
[[vk::binding(7, 0)]] StructuredBuffer<uint> gPbrPrimitiveToLightL6;
[[vk::binding(8, 0)]] StructuredBuffer<PbrFixtureTriangleGpuL6> gPbrFixtureTrianglesL6;
[[vk::binding(9, 0)]] StructuredBuffer<PbrFixtureSphereGpuL6> gPbrFixtureSpheresL6;

uint PbrResolveEmitterLightL6(uint primitiveId, uint embeddedLightIndex)
{
    if (embeddedLightIndex != PBR_L6_INVALID_INDEX)
    {
        return embeddedLightIndex;
    }
    if (primitiveId < gPbrFrameL6.distribution.w)
    {
        return gPbrPrimitiveToLightL6[primitiveId];
    }
    return PBR_L6_INVALID_INDEX;
}

bool PbrIntersectFixtureTriangleL6(
    PbrRayL6 ray,
    PbrFixtureTriangleGpuL6 fixtureTriangle,
    float tMinimum,
    float tMaximum,
    out float t,
    out float3 outwardNormal)
{
    t = 0.0f;
    outwardNormal = 0.0f;
    const float3 edge1 = fixtureTriangle.p1.xyz - fixtureTriangle.p0.xyz;
    const float3 edge2 = fixtureTriangle.p2.xyz - fixtureTriangle.p0.xyz;
    const float3 p = cross(ray.direction, edge2);
    const float determinant = dot(edge1, p);
    if (abs(determinant) <= 1.0e-8f)
    {
        return false;
    }
    const float inverseDeterminant = rcp(determinant);
    const float3 s = ray.origin - fixtureTriangle.p0.xyz;
    const float barycentricU = dot(s, p) * inverseDeterminant;
    if (barycentricU < 0.0f || barycentricU > 1.0f)
    {
        return false;
    }
    const float3 q = cross(s, edge1);
    const float barycentricV = dot(ray.direction, q) * inverseDeterminant;
    if (barycentricV < 0.0f || barycentricU + barycentricV > 1.0f)
    {
        return false;
    }
    const float candidateT = dot(edge2, q) * inverseDeterminant;
    if (!(candidateT > tMinimum && candidateT < tMaximum))
    {
        return false;
    }
    const float3 unnormalizedNormal = cross(edge1, edge2);
    const float normalLengthSquared = dot(unnormalizedNormal, unnormalizedNormal);
    if (!(normalLengthSquared > 0.0f))
    {
        return false;
    }
    t = candidateT;
    outwardNormal = unnormalizedNormal * rsqrt(normalLengthSquared);
    return true;
}

bool PbrIntersectFixtureSphereL6(
    PbrRayL6 ray,
    PbrFixtureSphereGpuL6 sphere,
    float tMinimum,
    float tMaximum,
    out float t,
    out float3 outwardNormal)
{
    t = 0.0f;
    outwardNormal = 0.0f;
    const float radius = sphere.centerRadius.w;
    if (!(radius > 0.0f))
    {
        return false;
    }
    const float3 offset = ray.origin - sphere.centerRadius.xyz;
    const float a = dot(ray.direction, ray.direction);
    const float halfB = dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = halfB * halfB - a * c;
    if (!(discriminant >= 0.0f) || !(a > 0.0f))
    {
        return false;
    }
    const float root = sqrt(discriminant);
    float candidateT = (-halfB - root) / a;
    if (!(candidateT > tMinimum && candidateT < tMaximum))
    {
        candidateT = (-halfB + root) / a;
        if (!(candidateT > tMinimum && candidateT < tMaximum))
        {
            return false;
        }
    }
    t = candidateT;
    outwardNormal = normalize(ray.origin + candidateT * ray.direction - sphere.centerRadius.xyz);
    return true;
}

bool PbrTraceClosestFixtureIgnoringL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    uint ignoredPrimitiveId,
    out PbrHitL6 hit)
{
    hit.t = tMaximum;
    hit.position = 0.0f;
    hit.geometricNormal = 0.0f;
    hit.shadingNormal = 0.0f;
    hit.materialIndex = PBR_L6_INVALID_INDEX;
    hit.primitiveId = PBR_L6_INVALID_INDEX;
    hit.emitterLightIndex = PBR_L6_INVALID_INDEX;
    hit.frontFace = 0u;
    bool found = false;

    [loop]
    for (uint triangleIndex = 0u; triangleIndex < gPbrFrameL6.trace.x; ++triangleIndex)
    {
        const PbrFixtureTriangleGpuL6 fixtureTriangle = gPbrFixtureTrianglesL6[triangleIndex];
        const uint primitiveId = fixtureTriangle.metadata.y;
        if (primitiveId == ignoredPrimitiveId)
        {
            continue;
        }
        float candidateT;
        float3 outwardNormal;
        if (PbrIntersectFixtureTriangleL6(
            ray,
            fixtureTriangle,
            tMinimum,
            tMaximum,
            candidateT,
            outwardNormal))
        {
            const bool equalDistanceLowerId = candidateT == hit.t && primitiveId < hit.primitiveId;
            if (!found || candidateT < hit.t || equalDistanceLowerId)
            {
                found = true;
                hit.t = candidateT;
                hit.geometricNormal = outwardNormal;
                hit.materialIndex = fixtureTriangle.metadata.x;
                hit.primitiveId = primitiveId;
                hit.emitterLightIndex = PbrResolveEmitterLightL6(primitiveId, fixtureTriangle.metadata.z);
            }
        }
    }

    [loop]
    for (uint sphereIndex = 0u; sphereIndex < gPbrFrameL6.trace.y; ++sphereIndex)
    {
        const PbrFixtureSphereGpuL6 sphere = gPbrFixtureSpheresL6[sphereIndex];
        const uint primitiveId = sphere.metadata.y;
        if (primitiveId == ignoredPrimitiveId)
        {
            continue;
        }
        float candidateT;
        float3 outwardNormal;
        if (PbrIntersectFixtureSphereL6(
            ray,
            sphere,
            tMinimum,
            tMaximum,
            candidateT,
            outwardNormal))
        {
            const bool equalDistanceLowerId = candidateT == hit.t && primitiveId < hit.primitiveId;
            if (!found || candidateT < hit.t || equalDistanceLowerId)
            {
                found = true;
                hit.t = candidateT;
                hit.geometricNormal = outwardNormal;
                hit.materialIndex = sphere.metadata.x;
                hit.primitiveId = primitiveId;
                hit.emitterLightIndex = PbrResolveEmitterLightL6(primitiveId, sphere.metadata.z);
            }
        }
    }

    if (found)
    {
        hit.position = ray.origin + hit.t * ray.direction;
        hit.frontFace = dot(ray.direction, hit.geometricNormal) < 0.0f ? 1u : 0u;
        hit.shadingNormal = hit.frontFace != 0u ? hit.geometricNormal : -hit.geometricNormal;
    }
    return found;
}

bool PbrTraceClosestFixtureL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    out PbrHitL6 hit)
{
    return PbrTraceClosestFixtureIgnoringL6(
        ray,
        tMinimum,
        tMaximum,
        PBR_L6_INVALID_INDEX,
        hit);
}

bool PbrTraceAnyFixtureL6(
    PbrRayL6 ray,
    float tMinimum,
    float tMaximum,
    uint ignoredPrimitiveId)
{
    PbrHitL6 ignoredHit;
    return PbrTraceClosestFixtureIgnoringL6(
        ray,
        tMinimum,
        tMaximum,
        ignoredPrimitiveId,
        ignoredHit);
}

#ifndef PBR_L6_TRACE_CLOSEST
#define PBR_L6_TRACE_CLOSEST PbrTraceClosestFixtureL6
#endif

#ifndef PBR_L6_TRACE_ANY
#define PBR_L6_TRACE_ANY PbrTraceAnyFixtureL6
#endif

#endif
