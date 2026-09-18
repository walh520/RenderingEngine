#pragma once

#include "renderers/Wave4FrameGraph.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace RenderingEngine::Wave4
{
    enum class AcceptanceState : std::uint32_t
    {
        NotRun = 0u,
        Accepted,
        Rejected
    };

    struct AcceptanceResult final
    {
        AcceptanceState state = AcceptanceState::NotRun;
        std::string reason{};

        [[nodiscard]] bool Passed() const noexcept
        {
            return state == AcceptanceState::Accepted;
        }
    };

    enum class ComparisonLeg : std::uint32_t
    {
        Uniform = 0u,
        PowerWeighted,
        Restir,
        HighSppReference
    };

    enum class ComparisonBiasMode : std::uint32_t
    {
        NotApplicable = 0u,
        Biased,
        ReferenceCorrection
    };

    enum class TimingProvenance : std::uint32_t
    {
        NotMeasured = 0u,
        VulkanGpuTimestamp,
        CpuWallClock
    };

    enum class EvidenceProvenance : std::uint32_t
    {
        LiveRuntime = 0u,
        ImportedArtifact,
        SyntheticTest
    };

    enum class EvidenceFreshness : std::uint32_t
    {
        Unavailable = 0u,
        Fresh,
        Stale
    };

    struct ComparisonIdentity final
    {
        std::string sceneFingerprint{};
        std::string cameraFingerprint{};
        std::string assetFingerprint{};
        std::string configFingerprint{};
        std::string machineFingerprint{};
        std::string driverFingerprint{};
        std::string powerProfileToken{};
        std::uint64_t baseSeed = 0u;
        std::uint64_t frameIndex = 0u;
        std::uint64_t frameGeneration = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        std::uint64_t lightGeneration = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        ManyLightsTier tier = ManyLightsTier::Lights100;
    };

    [[nodiscard]] bool IsComplete(
        const ComparisonIdentity& identity) noexcept;
    [[nodiscard]] bool SameRunIdentity(
        const ComparisonIdentity& left,
        const ComparisonIdentity& right) noexcept;

    struct BudgetEvidence final
    {
        std::uint64_t candidateBudget = 0u;
        std::uint64_t visibilityBudget = 0u;
        std::uint64_t observedCandidates = 0u;
        std::uint64_t observedVisibilityRays = 0u;
        std::string identityToken{};
    };

    struct MetricEvidence final
    {
        bool measured = false;
        double meanAbsoluteError = 0.0;
        double rootMeanSquareError = 0.0;
        double peakSignalToNoiseRatio = 0.0;
    };

    struct ComparisonEvidence final
    {
        std::string providerId{};
        std::string provenanceProviderId{};
        std::string provenanceDetail{};
        EvidenceProvenance provenance = EvidenceProvenance::LiveRuntime;
        EvidenceFreshness freshness = EvidenceFreshness::Unavailable;
        ComparisonIdentity identity{};
        ComparisonLeg leg = ComparisonLeg::Uniform;
        ComparisonBiasMode bias = ComparisonBiasMode::NotApplicable;
        std::uint32_t lightCount = 0u;
        BudgetEvidence budget{};
        MetricEvidence metrics{};
        std::uint32_t reservoirM = 0u;
        std::uint32_t winnerVisibilityDispatches = 0u;
        bool providerReported = false;
        bool runtimeExecuted = false;
        bool primaryDirectOwnership = false;
        bool splitDirectSignal = false;
        bool temporalReuse = false;
        bool spatialReuse = false;
        bool debugReadback = false;
        bool referenceCaptured = false;
        TimingProvenance timingProvenance = TimingProvenance::NotMeasured;
        bool visualAccepted = false;
    };

    struct ComparisonPolicy final
    {
        bool requireFiniteMetrics = true;
        ComparisonBiasMode restirBias = ComparisonBiasMode::Biased;
        bool requireTimingProvenance = false;
        bool requireVisualAcceptance = false;
        bool requireDebugReadback = true;
    };

    struct HistoryTransitionEvidence final
    {
        HistoryIdentity previous{};
        std::uint64_t nextFrameIndex = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        std::uint64_t lightGeneration = 0u;
        ManyLightsTier tier = ManyLightsTier::Lights100;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        TraversalBackend backend = TraversalBackend::GpuFlattenedSahBvh;
        TransportModel transportModel = TransportModel::Pbr;
        ExecutionArchitecture executionArchitecture =
            ExecutionArchitecture::Megakernel;
        ReconstructionMode reconstruction = ReconstructionMode::ProgressiveMean;
        HistoryDecision observed = HistoryDecision::Reset;
    };

    [[nodiscard]] AcceptanceResult EvaluateFrameEvidence(
        const FramePlan& plan,
        const ComparisonEvidence& evidence,
        const ComparisonPolicy& policy);

    [[nodiscard]] AcceptanceResult EvaluateManyLightsComparison(
        std::span<const ComparisonEvidence> evidence,
        const ComparisonPolicy& policy);

    [[nodiscard]] AcceptanceResult EvaluateHistoryTransition(
        const HistoryTransitionEvidence& evidence);

    [[nodiscard]] std::string_view ToString(
        AcceptanceState state) noexcept;
    [[nodiscard]] std::string_view ToString(ComparisonLeg leg) noexcept;
    [[nodiscard]] std::string_view ToString(ComparisonBiasMode mode) noexcept;
    [[nodiscard]] std::string_view ToString(TimingProvenance provenance) noexcept;
    [[nodiscard]] std::string_view ToString(EvidenceProvenance provenance) noexcept;
    [[nodiscard]] std::string_view ToString(EvidenceFreshness freshness) noexcept;
}
