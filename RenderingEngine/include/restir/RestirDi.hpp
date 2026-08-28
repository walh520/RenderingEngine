#pragma once

#include "restir/CandidateGenerators.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace RenderingEngine::Restir
{
    // The provisional GPU record uses a 32-bit accepted-source mask. Bit zero
    // is the center reservoir, leaving 30 supported neighbor bits while the
    // top bit remains unused so shifts are always explicit and bounded.
    inline constexpr std::uint32_t kMaxSpatialNeighbors = 30u;

    struct ReuseConfig
    {
        float normalCosThreshold = 0.9063078f;
        float relativeDepthThreshold = 0.05f;
        float positionDistanceThreshold = 0.10f;
        float thinGeometryDistanceThreshold = 0.01f;
        std::uint32_t maxM = 32u;
        std::uint32_t maxHistoryAge = 20u;
        bool requireSameInstance = true;
    };

    struct TemporalValidationInput
    {
        SurfaceRecord currentSurface{};
        SurfaceRecord historySurface{};
        bool reprojectedInside = true;
        bool motionValid = true;
        bool cameraCut = false;
        bool resized = false;
        bool selectedLightExists = true;
        std::uint32_t currentLightGeneration = 0u;
    };

    struct SpatialNeighbor
    {
        Reservoir reservoir{};
        SurfaceRecord surface{};
        bool selectedLightExists = true;
        std::uint32_t currentLightGeneration = 0u;
    };

    using CandidateReevaluation = std::function<Candidate(const Candidate&, const SurfaceRecord&)>;
    using VisibilityQuery = std::function<bool(const Candidate&)>;

    [[nodiscard]] RejectionReason ValidateTemporalHistory(
        const Reservoir& history,
        const TemporalValidationInput& input,
        const ReuseConfig& config) noexcept;

    [[nodiscard]] RejectionReason ValidateSpatialNeighbor(
        const SurfaceRecord& center,
        const SpatialNeighbor& neighbor,
        const ReuseConfig& config) noexcept;

    [[nodiscard]] Reservoir ReuseTemporal(
        const Reservoir& current,
        const Reservoir& history,
        const TemporalValidationInput& input,
        EstimatorMode mode,
        const ReuseConfig& config,
        Pcg32& random,
        const CandidateReevaluation& reevaluate,
        const VisibilityQuery& referenceVisibility,
        RejectionReason& rejectionReason,
        ReservoirStatistics* statistics = nullptr);

    [[nodiscard]] Reservoir ReuseSpatial(
        const Reservoir& center,
        const SurfaceRecord& centerSurface,
        std::span<const SpatialNeighbor> neighbors,
        EstimatorMode mode,
        const ReuseConfig& config,
        Pcg32& random,
        const CandidateReevaluation& reevaluate,
        const VisibilityQuery& referenceVisibility,
        RejectionReason& aggregateRejectionReason,
        ReservoirStatistics* statistics = nullptr);

    enum class FinalVisibilityStatus : std::uint32_t
    {
        Evaluated = 0u,
        EmptyReservoir = 1u,
        AlreadyEvaluated = 2u
    };

    struct FinalLightingResult
    {
        Float3 contribution{};
        FinalVisibilityStatus status = FinalVisibilityStatus::EmptyReservoir;
        bool visible = false;
    };

    // Production visibility is applied once, after all reuse selected one sample.
    [[nodiscard]] FinalLightingResult EvaluateFinalVisibilityOnce(
        Reservoir& reservoir,
        const VisibilityQuery& visibility,
        ReservoirStatistics* statistics = nullptr);

    enum class DirectLightingOwner : std::uint32_t
    {
        None = 0u,
        RestirPrimary = 1u,
        ConventionalNeeMis = 2u,
        EmitterHit = 3u
    };

    struct DirectLightingPolicyInput
    {
        std::uint32_t bounce = 0u;
        bool restirEnabled = true;
        bool hitEmitter = false;
        bool deltaSurface = false;
    };

    struct DirectLightingPolicyDecision
    {
        DirectLightingOwner owner = DirectLightingOwner::None;
        bool useRestir = false;
        bool useNee = false;
        bool useEmitterHit = false;
        bool useMis = false;
    };

    [[nodiscard]] DirectLightingPolicyDecision ResolveDirectLightingPolicy(
        const DirectLightingPolicyInput& input) noexcept;
}
