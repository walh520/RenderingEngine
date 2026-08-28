#pragma once

#include "reconstruction/Math.hpp"

namespace rendering::reconstruction {

struct MotionVectorInput final {
    Float3 objectPosition{};
    Matrix4x4 currentObjectToWorld{Matrix4x4::Identity()};
    Matrix4x4 previousObjectToWorld{Matrix4x4::Identity()};
    Matrix4x4 currentViewProjection{Matrix4x4::Identity()};
    Matrix4x4 previousViewProjection{Matrix4x4::Identity()};
    // Right-handed view space looks down -Z; positive linear depth is -view.z.
    Matrix4x4 previousWorldToView{Matrix4x4::Identity()};
    // Jitter is expressed in UV units and is added after NDC-to-UV conversion.
    Float2 currentJitterUv{};
    Float2 previousJitterUv{};
};

struct MotionVectorResult final {
    Float2 currentUv{};
    Float2 previousUv{};
    Float2 motion{};
    float expectedPreviousLinearDepth{};
    bool valid{};
};

// Camera and rigid motion are both captured by projecting objectPosition through
// the current and previous object and camera transforms. The sign convention is:
// motion = previousUv - currentUv, therefore historyUv = currentUv + motion.
[[nodiscard]] MotionVectorResult ComputeMotionVector(const MotionVectorInput& input) noexcept;

} // namespace rendering::reconstruction
