#pragma once

#include "app/RuntimeConfig.hpp"
#include "demos/ShowcaseEvidence.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Wave3
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
        std::string reason;

        [[nodiscard]] bool Passed() const noexcept
        {
            return state == AcceptanceState::Accepted;
        }
    };

    // Execution architecture is intentionally absent: Megakernel and Wavefront must carry
    // the same immutable comparison identity.
    struct ComparisonIdentity final
    {
        std::string sceneFingerprint;
        std::string cameraFingerprint;
        std::string assetFingerprint;
        std::string referenceFingerprint;
        TraversalBackend backend = TraversalBackend::GpuFlattenedSahBvh;
        TransportModel transportModel = TransportModel::Pbr;
        DirectLightingEstimator estimator =
            DirectLightingEstimator::MultipleImportanceSampling;
        LightSelectionStrategy lightSelection =
            LightSelectionStrategy::PowerWeighted;
        EnvironmentDirectionSampler environmentSampler =
            EnvironmentDirectionSampler::UniformSphere;
        std::uint64_t baseSeed = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint32_t samplesPerFrame = 0u;
        std::uint32_t maximumPathLength = 0u;
    };

    [[nodiscard]] bool IsComplete(const ComparisonIdentity& identity) noexcept;
    [[nodiscard]] bool SameIdentity(
        const ComparisonIdentity& left,
        const ComparisonIdentity& right) noexcept;

    struct RuntimeFaultCounters final
    {
        std::uint64_t validationErrors = 0u;
        std::uint64_t queueOverflows = 0u;
        std::uint64_t stackOverflows = 0u;
        std::uint64_t silentRayDrops = 0u;
        std::uint64_t nanOrInfValues = 0u;
        std::uint64_t negativePdfs = 0u;
        std::uint64_t invalidHits = 0u;

        [[nodiscard]] bool AllZero() const noexcept;
    };

    struct QualityCheckpoint final
    {
        std::uint32_t samplesPerPixel = 0u;
        std::uint64_t rayBudget = 0u;
        double rmse = 0.0;
        double psnr = 0.0;
    };

    struct ExecutionConvergenceEvidence final
    {
        ComparisonIdentity identity{};
        ExecutionArchitecture architecture = ExecutionArchitecture::Megakernel;
        RuntimeFaultCounters faults{};
        std::vector<QualityCheckpoint> checkpoints{};
        std::vector<std::uint64_t> activePathsPerBounce{};
        bool indirectDispatchValidationPassed = false;
        bool megakernelBaselineRetained = false;
    };

    // Thresholds must come from a frozen baseline or an ADR. The gate has no
    // guessed default because the roadmap explicitly forbids invented numbers.
    struct ConvergencePolicy final
    {
        std::uint32_t minimumCheckpointCount = 2u;
        double maximumFinalRmse = 0.0;
        double maximumRelativeFinalRmseDelta = 0.0;
        std::string authority;
    };

    [[nodiscard]] AcceptanceResult EvaluateExecutionConvergence(
        const ExecutionConvergenceEvidence& megakernel,
        const ExecutionConvergenceEvidence& wavefront,
        const ConvergencePolicy& policy);

    struct ReconstructionComparison final
    {
        ComparisonIdentity identity{};
        ReconstructionMode mode = ReconstructionMode::ProgressiveMean;
        std::uint32_t referenceSamplesPerPixel = 0u;
        double rmse = 0.0;
        double psnr = 0.0;
        std::string rawArtifact;
        std::string outputArtifact;
        std::string referenceArtifact;
    };

    // Wave 3 retains all four decomposed outputs. The integration gate names
    // Raw/Temporal/SVGF; A-Trous-only remains mandatory for L8 diagnosis.
    [[nodiscard]] AcceptanceResult EvaluateReconstructionComparisonSet(
        std::span<const ReconstructionComparison> comparisons);

    [[nodiscard]] Demos::BenchmarkPlan BuildWave3BackendBenchmarkPlan(
        const ComparisonIdentity& identity,
        TraversalBackend backend);

    struct BackendProfileEvidence final
    {
        ComparisonIdentity identity{};
        TraversalBackend backend = TraversalBackend::GpuLbvh;
        Demos::BenchmarkPlan plan{};
        bool benchmarkSequenceCompleted = false;
        std::uint64_t acceptedWarmupFrames = 0u;
        std::uint64_t acceptedMeasurementFrames = 0u;
        std::vector<Demos::EvidenceSample> samples{};
    };

    struct BackendProfileResult final
    {
        TraversalBackend backend = TraversalBackend::GpuLbvh;
        AcceptanceResult acceptance{};
        std::vector<Demos::ScopeStatistics> timings{};
        std::vector<Demos::ScopeStatistics> counters{};
    };

    [[nodiscard]] BackendProfileResult EvaluateBackendProfile(
        const BackendProfileEvidence& evidence);

    struct Wave3GateResult final
    {
        AcceptanceResult convergence{};
        AcceptanceResult reconstruction{};
        AcceptanceResult softwareProfile{};
        AcceptanceResult hardwareProfile{};

        [[nodiscard]] bool Passed() const noexcept
        {
            return convergence.Passed()
                && reconstruction.Passed()
                && softwareProfile.Passed()
                && hardwareProfile.Passed();
        }
    };

    [[nodiscard]] Wave3GateResult ComposeWave3Gate(
        const AcceptanceResult& convergence,
        const AcceptanceResult& reconstruction,
        const BackendProfileResult& softwareProfile,
        const BackendProfileResult& hardwareProfile);

    [[nodiscard]] std::string_view ToString(AcceptanceState state) noexcept;
}
