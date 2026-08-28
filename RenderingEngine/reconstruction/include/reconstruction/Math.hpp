#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace rendering::reconstruction {

struct Float2 final {
    float x{};
    float y{};
};

struct Float3 final {
    float x{};
    float y{};
    float z{};
};

struct Float4 final {
    float x{};
    float y{};
    float z{};
    float w{};
};

[[nodiscard]] constexpr Float2 operator+(const Float2 a, const Float2 b) noexcept {
    return {a.x + b.x, a.y + b.y};
}

[[nodiscard]] constexpr Float2 operator-(const Float2 a, const Float2 b) noexcept {
    return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] constexpr Float2 operator*(const Float2 a, const float s) noexcept {
    return {a.x * s, a.y * s};
}

[[nodiscard]] constexpr Float3 operator+(const Float3 a, const Float3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Float3 operator-(const Float3 a, const Float3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] constexpr Float3 operator*(const Float3 a, const float s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}

[[nodiscard]] constexpr Float3 operator*(const Float3 a, const Float3 b) noexcept {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

[[nodiscard]] constexpr Float3 operator/(const Float3 a, const float s) noexcept {
    return {a.x / s, a.y / s, a.z / s};
}

[[nodiscard]] constexpr Float3 operator/(const Float3 a, const Float3 b) noexcept {
    return {a.x / b.x, a.y / b.y, a.z / b.z};
}

constexpr Float3& operator+=(Float3& a, const Float3 b) noexcept {
    a = a + b;
    return a;
}

[[nodiscard]] constexpr float Dot(const Float3 a, const Float3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline float Length(const Float3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

[[nodiscard]] inline Float3 Normalize(const Float3 value) noexcept {
    const float length = Length(value);
    return length > 1.0e-20F ? value / length : Float3{0.0F, 0.0F, 1.0F};
}

[[nodiscard]] constexpr Float3 Lerp(const Float3 a, const Float3 b, const float t) noexcept {
    return a * (1.0F - t) + b * t;
}

[[nodiscard]] constexpr float Lerp(const float a, const float b, const float t) noexcept {
    return a * (1.0F - t) + b * t;
}

[[nodiscard]] constexpr float Luminance(const Float3 value) noexcept {
    return 0.2126F * value.x + 0.7152F * value.y + 0.0722F * value.z;
}

[[nodiscard]] inline bool IsFinite(const Float2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] inline bool IsFinite(const Float3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] constexpr Float3 Max(const Float3 value, const float minimum) noexcept {
    return {
        std::max(value.x, minimum),
        std::max(value.y, minimum),
        std::max(value.z, minimum),
    };
}

struct Matrix4x4 final {
    // Row-major matrix; vectors are multiplied as M * column-vector.
    std::array<float, 16> values{};

    [[nodiscard]] static constexpr Matrix4x4 Identity() noexcept {
        return {{1.0F, 0.0F, 0.0F, 0.0F,
                 0.0F, 1.0F, 0.0F, 0.0F,
                 0.0F, 0.0F, 1.0F, 0.0F,
                 0.0F, 0.0F, 0.0F, 1.0F}};
    }
};

[[nodiscard]] constexpr Float4 Transform(const Matrix4x4& matrix, const Float4 value) noexcept {
    return {
        matrix.values[0] * value.x + matrix.values[1] * value.y + matrix.values[2] * value.z + matrix.values[3] * value.w,
        matrix.values[4] * value.x + matrix.values[5] * value.y + matrix.values[6] * value.z + matrix.values[7] * value.w,
        matrix.values[8] * value.x + matrix.values[9] * value.y + matrix.values[10] * value.z + matrix.values[11] * value.w,
        matrix.values[12] * value.x + matrix.values[13] * value.y + matrix.values[14] * value.z + matrix.values[15] * value.w,
    };
}

[[nodiscard]] constexpr Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b) noexcept {
    Matrix4x4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float value = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                value += a.values[row * 4 + k] * b.values[k * 4 + column];
            }
            result.values[row * 4 + column] = value;
        }
    }
    return result;
}

} // namespace rendering::reconstruction
