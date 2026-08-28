#include "reconstruction/MotionVectors.hpp"

#include <cmath>

namespace rendering::reconstruction {
namespace {

[[nodiscard]] bool IsFinite(const Float4 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool ProjectToUv(
    const Matrix4x4& objectToWorld,
    const Matrix4x4& viewProjection,
    const Float3 objectPosition,
    const Float2 jitterUv,
    Float2& uv) noexcept {
    const Float4 object{objectPosition.x, objectPosition.y, objectPosition.z, 1.0F};
    const Float4 world = Transform(objectToWorld, object);
    const Float4 clip = Transform(viewProjection, world);
    // The private projection convention uses positive clip W for points in
    // front of the camera. A finite negative W must not be reprojected.
    if (!IsFinite(world) || !IsFinite(clip) || clip.w <= 1.0e-8F) {
        return false;
    }

    const float reciprocalW = 1.0F / clip.w;
    const Float2 ndc{clip.x * reciprocalW, clip.y * reciprocalW};
    uv = {ndc.x * 0.5F + 0.5F + jitterUv.x, 0.5F - ndc.y * 0.5F + jitterUv.y};
    return IsFinite(uv);
}

} // namespace

MotionVectorResult ComputeMotionVector(const MotionVectorInput& input) noexcept {
    MotionVectorResult result{};
    const bool currentValid = ProjectToUv(
        input.currentObjectToWorld,
        input.currentViewProjection,
        input.objectPosition,
        input.currentJitterUv,
        result.currentUv);
    const bool previousValid = ProjectToUv(
        input.previousObjectToWorld,
        input.previousViewProjection,
        input.objectPosition,
        input.previousJitterUv,
        result.previousUv);

    const Float4 object{
        input.objectPosition.x,
        input.objectPosition.y,
        input.objectPosition.z,
        1.0F,
    };
    const Float4 previousWorld = Transform(input.previousObjectToWorld, object);
    const Float4 previousView = Transform(input.previousWorldToView, previousWorld);
    const float previousLinearDepth = -previousView.z;
    const bool previousDepthValid = IsFinite(previousWorld) && IsFinite(previousView) &&
        std::isfinite(previousLinearDepth) && previousLinearDepth >= 0.0F;

    result.valid = currentValid && previousValid && previousDepthValid;
    result.motion = result.valid ? result.previousUv - result.currentUv : Float2{};
    result.expectedPreviousLinearDepth = result.valid ? previousLinearDepth : 0.0F;
    return result;
}

} // namespace rendering::reconstruction
