#pragma once

#include "restir/Reservoir.hpp"

#include <cstdint>
#include <span>

namespace RenderingEngine::Restir
{
    struct AnalyticLight
    {
        std::uint32_t stableId = kInvalidStableId;
        std::uint32_t generation = 0u;
        Float3 position{};
        Float3 radiance{};
        float samplingPower = 0.0f;
    };

    struct EmissiveTriangle
    {
        std::uint32_t stableLightId = kInvalidStableId;
        std::uint32_t stablePrimitiveId = kInvalidStableId;
        std::uint32_t generation = 0u;
        Float3 p0{};
        Float3 p1{};
        Float3 p2{};
        Float3 radiance{};
        float selectionProbability = 1.0f;
        bool twoSided = false;
    };

    struct EnvironmentCell
    {
        std::uint32_t stableLightId = kInvalidStableId;
        std::uint32_t cellId = 0u;
        std::uint32_t generation = 0u;
        Float3 direction{ 0.0f, 1.0f, 0.0f };
        Float3 radiance{};
        float solidAngle = 0.0f;
        float samplingPower = 0.0f;
    };

    [[nodiscard]] Candidate GenerateUniformLightCandidate(
        std::span<const AnalyticLight> lights,
        const SurfaceRecord& surface,
        float selectionSample) noexcept;

    [[nodiscard]] Candidate GeneratePowerWeightedLightCandidate(
        std::span<const AnalyticLight> lights,
        const SurfaceRecord& surface,
        float selectionSample) noexcept;

    [[nodiscard]] Candidate GenerateEmissiveTriangleCandidate(
        const EmissiveTriangle& light,
        const SurfaceRecord& surface,
        float sample0,
        float sample1) noexcept;

    [[nodiscard]] Candidate GenerateEnvironmentCandidate(
        std::span<const EnvironmentCell> cells,
        const SurfaceRecord& surface,
        float selectionSample) noexcept;
}
