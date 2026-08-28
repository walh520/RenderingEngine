#ifndef RENDERING_ENGINE_L4_SOFTWARE_COMMON_HLSLI
#define RENDERING_ENGINE_L4_SOFTWARE_COMMON_HLSLI

static const uint SOFTWARE_INVALID_INDEX = 0xffffffffu;
static const uint SOFTWARE_QUERY_CLOSEST = 0u;
static const uint SOFTWARE_QUERY_ANY = 1u;

struct SoftwareNodeRecord
{
    float4 boundsMin;
    float4 boundsMax;
    uint4 links;
};

struct SoftwarePrimitiveRecord
{
    float4 v0;
    float4 v1;
    float4 v2;
    uint4 identity;
};

struct SoftwareRayRecord
{
    float4 originTMin;
    float4 directionTMax;
    uint4 query; // ray ID, query mode, visibility mask, reserved
};

struct SoftwareHitRecord
{
    float4 tBary;
    uint4 identity;
};

struct SoftwareMortonPair
{
    uint code;
    uint stablePrimitiveId;
    uint sourceIndex;
    uint reserved;
};

float3 SoftwareMinimum3(const float3 a, const float3 b)
{
    return min(a, b);
}

float3 SoftwareMaximum3(const float3 a, const float3 b)
{
    return max(a, b);
}

void SoftwarePrimitiveBounds(
    const SoftwarePrimitiveRecord primitive,
    out float3 minimumPoint,
    out float3 maximumPoint)
{
    minimumPoint = SoftwareMinimum3(primitive.v0.xyz, SoftwareMinimum3(primitive.v1.xyz, primitive.v2.xyz));
    maximumPoint = SoftwareMaximum3(primitive.v0.xyz, SoftwareMaximum3(primitive.v1.xyz, primitive.v2.xyz));
}

bool SoftwareRayIsValid(const SoftwareRayRecord ray)
{
    const float directionLengthSquared = dot(ray.directionTMax.xyz, ray.directionTMax.xyz);
    return all(isfinite(ray.originTMin.xyz)) && all(isfinite(ray.directionTMax.xyz)) &&
           isfinite(directionLengthSquared) && abs(directionLengthSquared - 1.0f) <= 1.0e-4f &&
           !isnan(ray.originTMin.w) && !isnan(ray.directionTMax.w) &&
           ray.originTMin.w < ray.directionTMax.w && ray.query.y <= SOFTWARE_QUERY_ANY;
}

bool SoftwareIntersectBounds(
    const SoftwareRayRecord ray,
    const float3 boundsMin,
    const float3 boundsMax,
    const float distanceLimit,
    const bool includeDistanceLimit,
    out float nearDistance)
{
    float intervalNear = -1.0f / 0.0f;
    float intervalFar = 1.0f / 0.0f;
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        const float direction = ray.directionTMax[axis];
        const float origin = ray.originTMin[axis];
        if (direction == 0.0f)
        {
            if (origin < boundsMin[axis] || origin > boundsMax[axis])
            {
                nearDistance = 0.0f;
                return false;
            }
            continue;
        }
        const float inverseDirection = 1.0f / direction;
        float axisNear = (boundsMin[axis] - origin) * inverseDirection;
        float axisFar = (boundsMax[axis] - origin) * inverseDirection;
        if (axisNear > axisFar)
        {
            const float temporary = axisNear;
            axisNear = axisFar;
            axisFar = temporary;
        }
        intervalNear = max(intervalNear, axisNear);
        intervalFar = min(intervalFar, axisFar);
        if (intervalFar < intervalNear)
        {
            nearDistance = 0.0f;
            return false;
        }
    }
    const bool withinRay = intervalNear < ray.directionTMax.w;
    const bool withinLimit = includeDistanceLimit ? intervalNear <= distanceLimit
                                                  : intervalNear < distanceLimit;
    if (!(intervalFar > ray.originTMin.w && withinRay && withinLimit))
    {
        nearDistance = 0.0f;
        return false;
    }
    nearDistance = max(intervalNear, ray.originTMin.w);
    return true;
}

bool SoftwareIntersectTriangle(
    const SoftwareRayRecord ray,
    const SoftwarePrimitiveRecord primitive,
    const float distanceLimit,
    const bool includeDistanceLimit,
    out float hitDistance,
    out float baryU,
    out float baryV,
    out bool frontFace)
{
    const float3 edge1 = primitive.v1.xyz - primitive.v0.xyz;
    const float3 edge2 = primitive.v2.xyz - primitive.v0.xyz;
    const float3 p = cross(ray.directionTMax.xyz, edge2);
    const float determinant = dot(edge1, p);
    if (determinant == 0.0f || !isfinite(determinant))
    {
        hitDistance = 0.0f;
        baryU = 0.0f;
        baryV = 0.0f;
        frontFace = false;
        return false;
    }
    const float inverseDeterminant = 1.0f / determinant;
    const float3 translated = ray.originTMin.xyz - primitive.v0.xyz;
    baryU = dot(translated, p) * inverseDeterminant;
    if (!(baryU >= 0.0f && baryU <= 1.0f))
    {
        hitDistance = 0.0f;
        baryV = 0.0f;
        frontFace = false;
        return false;
    }
    const float3 q = cross(translated, edge1);
    baryV = dot(ray.directionTMax.xyz, q) * inverseDeterminant;
    if (!(baryV >= 0.0f && baryU + baryV <= 1.0f))
    {
        hitDistance = 0.0f;
        frontFace = false;
        return false;
    }
    hitDistance = dot(edge2, q) * inverseDeterminant;
    frontFace = determinant > 0.0f;
    const bool withinRay = hitDistance < ray.directionTMax.w;
    const bool withinLimit = includeDistanceLimit ? hitDistance <= distanceLimit
                                                  : hitDistance < distanceLimit;
    return isfinite(hitDistance) && hitDistance > ray.originTMin.w &&
           withinRay && withinLimit;
}

uint SoftwareExpandMortonBits(uint value)
{
    value &= 0x000003ffu;
    value = (value | (value << 16u)) & 0x030000ffu;
    value = (value | (value << 8u)) & 0x0300f00fu;
    value = (value | (value << 4u)) & 0x030c30c3u;
    value = (value | (value << 2u)) & 0x09249249u;
    return value;
}

float SoftwareFiniteMidpoint(const float minimumValue, const float maximumValue)
{
    return minimumValue < 0.0f && maximumValue > 0.0f
               ? minimumValue * 0.5f + maximumValue * 0.5f
               : minimumValue + (maximumValue - minimumValue) * 0.5f;
}

uint SoftwareQuantizeMortonAxis(
    const float value,
    const float minimumValue,
    const float maximumValue)
{
    // Opposite-sign extremes need half scaling to avoid overflow. Same-sign
    // bounds use direct subtraction so adjacent large floats retain an extent.
    const bool spansZero = minimumValue < 0.0f && maximumValue > 0.0f;
    const float extent = spansZero
                             ? maximumValue * 0.5f - minimumValue * 0.5f
                             : maximumValue - minimumValue;
    const float numerator = spansZero
                                ? value * 0.5f - minimumValue * 0.5f
                                : value - minimumValue;
    const float normalized = extent > 0.0f ? numerator / extent : 0.5f;
    return min((uint)(saturate(normalized) * 1024.0f), 1023u);
}

uint SoftwareMortonCode(const float3 centerPoint, const float3 sceneMin, const float3 sceneMax)
{
    const uint x = SoftwareQuantizeMortonAxis(centerPoint.x, sceneMin.x, sceneMax.x);
    const uint y = SoftwareQuantizeMortonAxis(centerPoint.y, sceneMin.y, sceneMax.y);
    const uint z = SoftwareQuantizeMortonAxis(centerPoint.z, sceneMin.z, sceneMax.z);
    return (SoftwareExpandMortonBits(x) << 2u) |
           (SoftwareExpandMortonBits(y) << 1u) |
           SoftwareExpandMortonBits(z);
}

#endif
