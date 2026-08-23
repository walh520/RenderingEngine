#pragma once

#include <algorithm>
#include <cmath>

namespace RenderingEngine
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3() = default;
        constexpr Vec3(float xValue, float yValue, float zValue)
            : x(xValue), y(yValue), z(zValue)
        {
        }
    };

    [[nodiscard]] inline constexpr Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
    {
        return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
    }

    [[nodiscard]] inline constexpr Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
    {
        return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
    }

    [[nodiscard]] inline constexpr Vec3 operator*(const Vec3& value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    [[nodiscard]] inline constexpr float Dot(const Vec3& lhs, const Vec3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    [[nodiscard]] inline constexpr Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
    {
        return {
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x
        };
    }

    [[nodiscard]] inline Vec3 Normalize(const Vec3& value)
    {
        const float lengthSquared = Dot(value, value);
        if (lengthSquared <= 1.0e-12f)
        {
            return {};
        }

        return value * (1.0f / std::sqrt(lengthSquared));
    }

    class Camera final
    {
    public:
        [[nodiscard]] const Vec3& Position() const noexcept { return position_; }

        [[nodiscard]] Vec3 Forward() const
        {
            constexpr float degreesToRadians = 0.01745329251994329577f;
            const float yaw = yawDegrees_ * degreesToRadians;
            const float pitch = pitchDegrees_ * degreesToRadians;
            return Normalize({
                std::cos(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::sin(yaw) * std::cos(pitch)
            });
        }

        [[nodiscard]] Vec3 Right() const
        {
            return Normalize(Cross(Forward(), { 0.0f, 1.0f, 0.0f }));
        }

        [[nodiscard]] Vec3 Up() const
        {
            return Normalize(Cross(Right(), Forward()));
        }

        [[nodiscard]] float VerticalFovDegrees() const noexcept { return verticalFovDegrees_; }

        void Move(float forward, float right, float vertical, float deltaSeconds, bool sprint)
        {
            const float speed = movementSpeed_ * (sprint ? 3.0f : 1.0f) * deltaSeconds;
            position_ = position_ + Forward() * (forward * speed);
            position_ = position_ + Right() * (right * speed);
            position_.y += vertical * speed;
        }

        void Rotate(float xOffset, float yOffset)
        {
            yawDegrees_ += xOffset * mouseSensitivity_;
            pitchDegrees_ = std::clamp(
                pitchDegrees_ + yOffset * mouseSensitivity_,
                -89.0f,
                89.0f);
        }

        void Zoom(float wheelOffset)
        {
            verticalFovDegrees_ = std::clamp(verticalFovDegrees_ - wheelOffset * 2.0f, 25.0f, 80.0f);
        }

    private:
        Vec3 position_{ 0.0f, 0.25f, 2.5f };
        float yawDegrees_ = -90.0f;
        float pitchDegrees_ = -3.0f;
        float verticalFovDegrees_ = 52.0f;
        float movementSpeed_ = 3.0f;
        float mouseSensitivity_ = 0.1f;
    };
}
