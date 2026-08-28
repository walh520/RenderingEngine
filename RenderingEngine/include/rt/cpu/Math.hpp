#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace RenderingEngine::Rt::Cpu
{
    template <typename T>
    concept FloatingPoint = std::floating_point<T>;

    template <FloatingPoint T>
    struct Vec3
    {
        T x{};
        T y{};
        T z{};

        constexpr Vec3() noexcept = default;

        constexpr explicit Vec3(const T value) noexcept
            : x(value), y(value), z(value)
        {
        }

        constexpr Vec3(const T xValue, const T yValue, const T zValue) noexcept
            : x(xValue), y(yValue), z(zValue)
        {
        }

        [[nodiscard]] constexpr T& operator[](const std::size_t axis) noexcept
        {
            if (axis == 0u)
            {
                return x;
            }
            if (axis == 1u)
            {
                return y;
            }
            return z;
        }

        [[nodiscard]] constexpr const T& operator[](const std::size_t axis) const noexcept
        {
            if (axis == 0u)
            {
                return x;
            }
            if (axis == 1u)
            {
                return y;
            }
            return z;
        }

        constexpr Vec3& operator+=(const Vec3& rhs) noexcept
        {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            return *this;
        }

        constexpr Vec3& operator-=(const Vec3& rhs) noexcept
        {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
            return *this;
        }

        constexpr Vec3& operator*=(const T scale) noexcept
        {
            x *= scale;
            y *= scale;
            z *= scale;
            return *this;
        }

        constexpr Vec3& operator/=(const T scale) noexcept
        {
            x /= scale;
            y /= scale;
            z /= scale;
            return *this;
        }

        [[nodiscard]] friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
    };

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> operator+(Vec3<T> lhs, const Vec3<T>& rhs) noexcept
    {
        lhs += rhs;
        return lhs;
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> operator-(Vec3<T> lhs, const Vec3<T>& rhs) noexcept
    {
        lhs -= rhs;
        return lhs;
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> operator-(const Vec3<T>& value) noexcept
    {
        return Vec3<T>{-value.x, -value.y, -value.z};
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> operator*(Vec3<T> value, const T scale) noexcept
    {
        value *= scale;
        return value;
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> operator*(const T scale, Vec3<T> value) noexcept
    {
        value *= scale;
        return value;
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> operator/(Vec3<T> value, const T scale) noexcept
    {
        value /= scale;
        return value;
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr T Dot(const Vec3<T>& lhs, const Vec3<T>& rhs) noexcept
    {
        return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> Cross(const Vec3<T>& lhs, const Vec3<T>& rhs) noexcept
    {
        return Vec3<T>{
            (lhs.y * rhs.z) - (lhs.z * rhs.y),
            (lhs.z * rhs.x) - (lhs.x * rhs.z),
            (lhs.x * rhs.y) - (lhs.y * rhs.x)};
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr T LengthSquared(const Vec3<T>& value) noexcept
    {
        return Dot(value, value);
    }

    template <FloatingPoint T>
    [[nodiscard]] inline T Length(const Vec3<T>& value) noexcept
    {
        return std::sqrt(LengthSquared(value));
    }

    template <FloatingPoint T>
    [[nodiscard]] inline Vec3<T> NormalizeOrZero(const Vec3<T>& value) noexcept
    {
        const T lengthSquared = LengthSquared(value);
        if (!(lengthSquared > T{0}) || !std::isfinite(lengthSquared))
        {
            return Vec3<T>{};
        }
        return value / std::sqrt(lengthSquared);
    }

    template <FloatingPoint T>
    [[nodiscard]] inline bool IsFinite(const Vec3<T>& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> ComponentMin(const Vec3<T>& lhs, const Vec3<T>& rhs) noexcept
    {
        return Vec3<T>{
            lhs.x < rhs.x ? lhs.x : rhs.x,
            lhs.y < rhs.y ? lhs.y : rhs.y,
            lhs.z < rhs.z ? lhs.z : rhs.z};
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr Vec3<T> ComponentMax(const Vec3<T>& lhs, const Vec3<T>& rhs) noexcept
    {
        return Vec3<T>{
            lhs.x > rhs.x ? lhs.x : rhs.x,
            lhs.y > rhs.y ? lhs.y : rhs.y,
            lhs.z > rhs.z ? lhs.z : rhs.z};
    }

    template <FloatingPoint T>
    [[nodiscard]] constexpr std::size_t LongestAxis(const Vec3<T>& extent) noexcept
    {
        std::size_t axis = 0u;
        if (extent.y > extent.x)
        {
            axis = 1u;
        }
        if (extent.z > extent[axis])
        {
            axis = 2u;
        }
        return axis;
    }

    namespace Detail
    {
        template <FloatingPoint T>
        using PromotedFloat = std::conditional_t<std::is_same_v<T, float>, double, long double>;
    }
}
