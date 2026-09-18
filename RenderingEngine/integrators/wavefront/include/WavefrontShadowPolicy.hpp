#pragma once

#include "WavefrontTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace RenderingEngine::Wavefront
{
    inline constexpr std::uint32_t kShadowPcfFilterTapCount = 8u;
    inline constexpr std::uint32_t kShadowPcssBlockerTapCount = 4u;
    inline constexpr std::uint32_t kShadowPcssFilterTapCount = 12u;

    struct ShadowSamplingPlan final
    {
        ShadowSamplingMethod method = ShadowSamplingMethod::Physical;
        std::uint32_t blockerSearchTapCount = 0u;
        std::uint32_t filterTapCount = 1u;
        float angularRadius = 0.0f;
        bool fullyVisibleWithoutFilter = false;

        [[nodiscard]] constexpr std::uint32_t VisibilityTraceCount() const noexcept
        {
            return blockerSearchTapCount + filterTapCount;
        }
    };

    [[nodiscard]] constexpr bool IsValidShadowSamplingMethod(
        const std::uint32_t value) noexcept
    {
        return value <= static_cast<std::uint32_t>(
            ShadowSamplingMethod::Physical);
    }

    // CPU oracle for the shader policy. A missing PCSS blocker models the
    // shader's early fully-visible result after its four blocker-search taps.
    [[nodiscard]] inline ShadowSamplingPlan MakeShadowSamplingPlan(
        const std::uint32_t methodValue,
        const float lightWorldRadius,
        const float receiverDistance,
        const std::optional<float> averageBlockerDistance = std::nullopt,
        const float rayEpsilon = 1.0e-4f)
    {
        if (!IsValidShadowSamplingMethod(methodValue))
        {
            throw std::invalid_argument("Wavefront shadow method is invalid.");
        }
        if (!std::isfinite(lightWorldRadius) || lightWorldRadius < 0.0f
            || !std::isfinite(receiverDistance) || receiverDistance <= rayEpsilon
            || !std::isfinite(rayEpsilon) || rayEpsilon <= 0.0f)
        {
            throw std::invalid_argument("Wavefront shadow geometry is invalid.");
        }

        const ShadowSamplingMethod method =
            static_cast<ShadowSamplingMethod>(methodValue);
        if (method == ShadowSamplingMethod::Physical)
        {
            return {method, 0u, 1u, 0.0f, false};
        }

        const float finiteAngularRadius = receiverDistance < 1.0e20f
            ? lightWorldRadius / std::max(receiverDistance, 1.0e-4f)
            : 0.0f;
        const float baseRadius = std::clamp(
            std::max(finiteAngularRadius, 0.0025f), 0.0005f, 0.08f);
        if (method == ShadowSamplingMethod::Pcf)
        {
            return {
                method,
                0u,
                kShadowPcfFilterTapCount,
                baseRadius,
                false};
        }

        if (!averageBlockerDistance.has_value())
        {
            return {
                method,
                kShadowPcssBlockerTapCount,
                0u,
                baseRadius,
                true};
        }
        const float blockerDistance = *averageBlockerDistance;
        if (!std::isfinite(blockerDistance)
            || blockerDistance <= rayEpsilon
            || blockerDistance >= receiverDistance)
        {
            throw std::invalid_argument(
                "Wavefront PCSS blocker distance is invalid.");
        }
        const float penumbra = std::max(
            (receiverDistance - blockerDistance)
                / std::max(blockerDistance, rayEpsilon),
            0.0f);
        return {
            method,
            kShadowPcssBlockerTapCount,
            kShadowPcssFilterTapCount,
            baseRadius * std::clamp(penumbra, 0.25f, 4.0f),
            false};
    }
}
