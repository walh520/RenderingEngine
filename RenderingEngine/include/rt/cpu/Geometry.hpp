#pragma once

#include "Math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace RenderingEngine::Rt::Cpu
{
    inline constexpr std::uint32_t kInvalidPrimitiveId =
        (std::numeric_limits<std::uint32_t>::max)();

    template <FloatingPoint T>
    struct Ray
    {
        Vec3<T> origin{};
        Vec3<T> direction{};
        T tMin{T{0}};
        T tMax{(std::numeric_limits<T>::infinity)()};

        constexpr Ray() noexcept = default;

        constexpr Ray(
            const Vec3<T>& rayOrigin,
            const Vec3<T>& rayDirection,
            const T minimumDistance = T{0},
            const T maximumDistance = (std::numeric_limits<T>::infinity)()) noexcept
            : origin(rayOrigin),
              direction(rayDirection),
              tMin(minimumDistance),
              tMax(maximumDistance)
        {
        }

        [[nodiscard]] constexpr Vec3<T> At(const T distance) const noexcept
        {
            return origin + (direction * distance);
        }

        [[nodiscard]] inline bool IsValid() const noexcept
        {
            const T directionLengthSquared = LengthSquared(direction);
            const T unitDirectionTolerance =
                (std::numeric_limits<T>::epsilon)() * T{64};
            return IsFinite(origin) && IsFinite(direction) &&
                   std::isfinite(directionLengthSquared) &&
                   std::abs(directionLengthSquared - T{1}) <= unitDirectionTolerance &&
                   !std::isnan(tMin) && !std::isnan(tMax) && tMin < tMax;
        }
    };

    template <FloatingPoint T>
    struct Aabb
    {
        Vec3<T> minimum{
            (std::numeric_limits<T>::infinity)(),
            (std::numeric_limits<T>::infinity)(),
            (std::numeric_limits<T>::infinity)()};
        Vec3<T> maximum{
            -(std::numeric_limits<T>::infinity)(),
            -(std::numeric_limits<T>::infinity)(),
            -(std::numeric_limits<T>::infinity)()};

        constexpr Aabb() noexcept = default;

        constexpr Aabb(const Vec3<T>& minimumPoint, const Vec3<T>& maximumPoint) noexcept
            : minimum(minimumPoint), maximum(maximumPoint)
        {
        }

        [[nodiscard]] inline bool IsValid() const noexcept
        {
            return IsFinite(minimum) && IsFinite(maximum) && minimum.x <= maximum.x &&
                   minimum.y <= maximum.y && minimum.z <= maximum.z;
        }

        constexpr void Expand(const Vec3<T>& point) noexcept
        {
            minimum = ComponentMin(minimum, point);
            maximum = ComponentMax(maximum, point);
        }

        void Expand(const Aabb& bounds) noexcept
        {
            if (!bounds.IsValid())
            {
                return;
            }
            if (!IsValid())
            {
                *this = bounds;
                return;
            }
            minimum = ComponentMin(minimum, bounds.minimum);
            maximum = ComponentMax(maximum, bounds.maximum);
        }

        [[nodiscard]] constexpr Vec3<T> Extent() const noexcept
        {
            return maximum - minimum;
        }

        [[nodiscard]] constexpr Vec3<T> Centroid() const noexcept
        {
            return (minimum * T{0.5}) + (maximum * T{0.5});
        }

        [[nodiscard]] constexpr T SurfaceArea() const noexcept
        {
            if (minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z)
            {
                return T{0};
            }
            const Vec3<T> extent = Extent();
            return T{2} * ((extent.x * extent.y) + (extent.y * extent.z) +
                           (extent.z * extent.x));
        }
    };

    template <FloatingPoint T>
    struct Triangle
    {
        Vec3<T> v0{};
        Vec3<T> v1{};
        Vec3<T> v2{};
        std::uint32_t primitiveId{kInvalidPrimitiveId};

        constexpr Triangle() noexcept = default;

        constexpr Triangle(
            const Vec3<T>& firstVertex,
            const Vec3<T>& secondVertex,
            const Vec3<T>& thirdVertex,
            const std::uint32_t stablePrimitiveId) noexcept
            : v0(firstVertex),
              v1(secondVertex),
              v2(thirdVertex),
              primitiveId(stablePrimitiveId)
        {
        }

        [[nodiscard]] constexpr const Vec3<T>& Vertex(const std::size_t index) const noexcept
        {
            if (index == 0u)
            {
                return v0;
            }
            if (index == 1u)
            {
                return v1;
            }
            return v2;
        }

        [[nodiscard]] constexpr Aabb<T> Bounds() const noexcept
        {
            Aabb<T> result{};
            result.Expand(v0);
            result.Expand(v1);
            result.Expand(v2);
            return result;
        }

        [[nodiscard]] constexpr Vec3<T> Centroid() const noexcept
        {
            using P = Detail::PromotedFloat<T>;
            return Vec3<T>{
                static_cast<T>(
                    (static_cast<P>(v0.x) + static_cast<P>(v1.x) +
                     static_cast<P>(v2.x)) /
                    P{3}),
                static_cast<T>(
                    (static_cast<P>(v0.y) + static_cast<P>(v1.y) +
                     static_cast<P>(v2.y)) /
                    P{3}),
                static_cast<T>(
                    (static_cast<P>(v0.z) + static_cast<P>(v1.z) +
                     static_cast<P>(v2.z)) /
                    P{3})};
        }
    };

    template <FloatingPoint T>
    struct Hit
    {
        bool isHit{false};
        T t{(std::numeric_limits<T>::infinity)()};
        Vec3<T> barycentric{};
        std::uint32_t primitiveId{kInvalidPrimitiveId};
        Vec3<T> geometricNormal{};
        bool frontFace{false};

        [[nodiscard]] constexpr bool IsHit() const noexcept
        {
            return isHit;
        }
    };

    template <FloatingPoint T>
    [[nodiscard]] inline bool IsBetterHit(const Hit<T>& candidate, const Hit<T>& current) noexcept
    {
        if (!candidate.IsHit())
        {
            return false;
        }
        if (!current.IsHit())
        {
            return true;
        }
        if (candidate.t < current.t)
        {
            return true;
        }
        if (candidate.t > current.t)
        {
            return false;
        }
        return candidate.primitiveId < current.primitiveId;
    }

    // The returned interval is the closed box interval clipped by the ray's open
    // (tMin, tMax) domain. A one-point tangent inside that open domain is a hit.
    // Signed zero directions take this explicit parallel path rather than relying
    // on infinities produced by reciprocal multiplication.
    template <FloatingPoint T>
    [[nodiscard]] inline bool IntersectAabb(
        const Ray<T>& ray,
        const Aabb<T>& bounds,
        T& nearDistance,
        T& farDistance) noexcept
    {
        nearDistance = (std::numeric_limits<T>::infinity)();
        farDistance = -(std::numeric_limits<T>::infinity)();

        if (!ray.IsValid() || !bounds.IsValid())
        {
            return false;
        }

        using P = Detail::PromotedFloat<T>;
        P intervalNear = -(std::numeric_limits<P>::infinity)();
        P intervalFar = (std::numeric_limits<P>::infinity)();

        for (std::size_t axis = 0u; axis < 3u; ++axis)
        {
            const P origin = static_cast<P>(ray.origin[axis]);
            const P direction = static_cast<P>(ray.direction[axis]);
            const P slabMinimum = static_cast<P>(bounds.minimum[axis]);
            const P slabMaximum = static_cast<P>(bounds.maximum[axis]);

            if (direction == P{0})
            {
                if (origin < slabMinimum || origin > slabMaximum)
                {
                    return false;
                }
                continue;
            }

            P axisNear = (slabMinimum - origin) / direction;
            P axisFar = (slabMaximum - origin) / direction;
            if (axisNear > axisFar)
            {
                std::swap(axisNear, axisFar);
            }
            if (axisNear > intervalNear)
            {
                intervalNear = axisNear;
            }
            if (axisFar < intervalFar)
            {
                intervalFar = axisFar;
            }
            if (intervalFar < intervalNear)
            {
                return false;
            }
        }

        const P openMinimum = static_cast<P>(ray.tMin);
        const P openMaximum = static_cast<P>(ray.tMax);
        if (!(intervalFar > openMinimum && intervalNear < openMaximum))
        {
            return false;
        }

        const P clippedNear = intervalNear > openMinimum ? intervalNear : openMinimum;
        const P clippedFar = intervalFar < openMaximum ? intervalFar : openMaximum;
        nearDistance = static_cast<T>(clippedNear);
        farDistance = static_cast<T>(clippedFar);
        return true;
    }

    template <FloatingPoint T>
    [[nodiscard]] inline bool IntersectAabb(const Ray<T>& ray, const Aabb<T>& bounds) noexcept
    {
        T nearDistance{};
        T farDistance{};
        return IntersectAabb(ray, bounds, nearDistance, farDistance);
    }

    // Watertight ray/triangle test using the Woop/Benthin/Wald axis permutation
    // and shear formulation. Adjacent triangles evaluate a shared edge with the
    // same products in reverse order, avoiding Moller-Trumbore edge cracks.
    template <FloatingPoint T>
    [[nodiscard]] inline bool IntersectTriangle(
        const Ray<T>& ray,
        const Triangle<T>& triangle,
        Hit<T>& outHit) noexcept
    {
        outHit = Hit<T>{};
        if (!ray.IsValid() || !IsFinite(triangle.v0) || !IsFinite(triangle.v1) ||
            !IsFinite(triangle.v2))
        {
            return false;
        }

        using P = Detail::PromotedFloat<T>;
        const Vec3<P> promotedDirection{
            static_cast<P>(ray.direction.x),
            static_cast<P>(ray.direction.y),
            static_cast<P>(ray.direction.z)};

        const Vec3<P> absoluteDirection{
            std::fabs(promotedDirection.x),
            std::fabs(promotedDirection.y),
            std::fabs(promotedDirection.z)};
        const std::size_t kz = LongestAxis(absoluteDirection);
        if (promotedDirection[kz] == P{0})
        {
            return false;
        }

        std::size_t kx = (kz + 1u) % 3u;
        std::size_t ky = (kx + 1u) % 3u;
        if (promotedDirection[kz] < P{0})
        {
            std::swap(kx, ky);
        }

        const auto translateVertex = [&ray](const Vec3<T>& vertex) noexcept
        {
            return Vec3<P>{
                static_cast<P>(vertex.x) - static_cast<P>(ray.origin.x),
                static_cast<P>(vertex.y) - static_cast<P>(ray.origin.y),
                static_cast<P>(vertex.z) - static_cast<P>(ray.origin.z)};
        };

        const Vec3<P> a = translateVertex(triangle.v0);
        const Vec3<P> b = translateVertex(triangle.v1);
        const Vec3<P> c = translateVertex(triangle.v2);
        const P shearX = promotedDirection[kx] / promotedDirection[kz];
        const P shearY = promotedDirection[ky] / promotedDirection[kz];
        const P shearZ = P{1} / promotedDirection[kz];

        const P ax = a[kx] - (shearX * a[kz]);
        const P ay = a[ky] - (shearY * a[kz]);
        const P bx = b[kx] - (shearX * b[kz]);
        const P by = b[ky] - (shearY * b[kz]);
        const P cx = c[kx] - (shearX * c[kz]);
        const P cy = c[ky] - (shearY * c[kz]);

        const P edge0 = (bx * cy) - (by * cx);
        const P edge1 = (cx * ay) - (cy * ax);
        const P edge2 = (ax * by) - (ay * bx);

        const bool hasNegativeEdge = edge0 < P{0} || edge1 < P{0} || edge2 < P{0};
        const bool hasPositiveEdge = edge0 > P{0} || edge1 > P{0} || edge2 > P{0};
        if (hasNegativeEdge && hasPositiveEdge)
        {
            return false;
        }

        const P determinant = edge0 + edge1 + edge2;
        if (determinant == P{0} || !std::isfinite(determinant))
        {
            return false;
        }

        const P az = shearZ * a[kz];
        const P bz = shearZ * b[kz];
        const P cz = shearZ * c[kz];
        const P scaledDistance = (edge0 * az) + (edge1 * bz) + (edge2 * cz);
        const P distance = scaledDistance / determinant;
        if (!std::isfinite(distance) || !(distance > static_cast<P>(ray.tMin) &&
                                          distance < static_cast<P>(ray.tMax)))
        {
            return false;
        }

        const T hitDistance = static_cast<T>(distance);
        if (!(hitDistance > ray.tMin && hitDistance < ray.tMax))
        {
            return false;
        }

        const P inverseDeterminant = P{1} / determinant;
        const P barycentric0 = edge0 * inverseDeterminant;
        const P barycentric1 = edge1 * inverseDeterminant;
        const P barycentric2 = edge2 * inverseDeterminant;

        const Vec3<P> edgeA{
            static_cast<P>(triangle.v1.x) - static_cast<P>(triangle.v0.x),
            static_cast<P>(triangle.v1.y) - static_cast<P>(triangle.v0.y),
            static_cast<P>(triangle.v1.z) - static_cast<P>(triangle.v0.z)};
        const Vec3<P> edgeB{
            static_cast<P>(triangle.v2.x) - static_cast<P>(triangle.v0.x),
            static_cast<P>(triangle.v2.y) - static_cast<P>(triangle.v0.y),
            static_cast<P>(triangle.v2.z) - static_cast<P>(triangle.v0.z)};
        const Vec3<P> promotedNormal = Cross(edgeA, edgeB);
        const P normalLengthSquared = LengthSquared(promotedNormal);
        if (!(normalLengthSquared > P{0}) || !std::isfinite(normalLengthSquared))
        {
            return false;
        }

        const P inverseNormalLength = P{1} / std::sqrt(normalLengthSquared);
        const Vec3<T> geometricNormal{
            static_cast<T>(promotedNormal.x * inverseNormalLength),
            static_cast<T>(promotedNormal.y * inverseNormalLength),
            static_cast<T>(promotedNormal.z * inverseNormalLength)};

        const auto canonicalZero = [](const P value) noexcept
        {
            return static_cast<T>(value == P{0} ? P{0} : value);
        };

        outHit.isHit = true;
        outHit.t = hitDistance;
        outHit.barycentric = Vec3<T>{
            canonicalZero(barycentric0),
            canonicalZero(barycentric1),
            canonicalZero(barycentric2)};
        outHit.primitiveId = triangle.primitiveId;
        outHit.geometricNormal = geometricNormal;
        outHit.frontFace = Dot(ray.direction, geometricNormal) < T{0};
        return true;
    }

    template <FloatingPoint T>
    [[nodiscard]] inline Hit<T> IntersectTriangle(
        const Ray<T>& ray,
        const Triangle<T>& triangle) noexcept
    {
        Hit<T> hit{};
        static_cast<void>(IntersectTriangle(ray, triangle, hit));
        return hit;
    }
}
