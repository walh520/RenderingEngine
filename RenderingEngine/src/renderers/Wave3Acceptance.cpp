#include "renderers/Wave3Acceptance.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

namespace RenderingEngine::Wave3
{
    namespace
    {
        using Demos::BenchmarkPlan;
        using Demos::EvidenceCondition;
        using Demos::EvidenceMeasurementMethod;
        using Demos::EvidenceMetricRequirement;
        using Demos::EvidenceSampleKind;
        using Demos::ScopeStatistics;

        [[nodiscard]] bool IsQualityNumber(const double value) noexcept
        {
            return std::isfinite(value) || (std::isinf(value) && value > 0.0);
        }

        [[nodiscard]] bool IsWave3ProfileBackend(
            const TraversalBackend backend) noexcept
        {
            return backend == TraversalBackend::GpuLbvh
                || backend == TraversalBackend::VulkanRayTracingPipeline;
        }

        [[nodiscard]] std::string_view BackendToken(
            const TraversalBackend backend) noexcept
        {
            switch (backend)
            {
            case TraversalBackend::GpuLbvh: return "gpu-lbvh";
            case TraversalBackend::VulkanRayTracingPipeline:
                return "vulkan-rt-pipeline";
            default: return "invalid";
            }
        }

        [[nodiscard]] std::vector<EvidenceCondition> BuildConditions(
            const ComparisonIdentity& identity,
            const TraversalBackend backend)
        {
            return {
                {"asset", identity.assetFingerprint},
                {"backend", std::string(BackendToken(backend))},
                {"camera", identity.cameraFingerprint},
                {"environment-sampler", std::to_string(
                    static_cast<std::uint32_t>(identity.environmentSampler))},
                {"estimator", std::to_string(
                    static_cast<std::uint32_t>(identity.estimator))},
                {"height", std::to_string(identity.height)},
                {"max-path-length", std::to_string(identity.maximumPathLength)},
                {"light-selection", std::to_string(
                    static_cast<std::uint32_t>(identity.lightSelection))},
                {"reference", identity.referenceFingerprint},
                {"scene", identity.sceneFingerprint},
                {"seed", std::to_string(identity.baseSeed)},
                {"spp-per-frame", std::to_string(identity.samplesPerFrame)},
                {"transport", std::to_string(
                    static_cast<std::uint32_t>(identity.transportModel))},
                {"width", std::to_string(identity.width)}
            };
        }

        [[nodiscard]] std::vector<EvidenceMetricRequirement> BuildMetrics()
        {
            using enum EvidenceMeasurementMethod;
            using enum EvidenceSampleKind;
            return {
                {Timing, "build", "ms", GpuTimestampQuery},
                {Timing, "trace", "ms", GpuTimestampQuery},
                {Counter, "rays", "count", ProviderCounter},
                {Counter, "memory", "bytes", ProviderCounter},
                {Counter, "path-length", "count", ProviderCounter},
                {Counter, "validation-errors", "count", ProviderCounter},
                {Counter, "queue-overflows", "count", ProviderCounter},
                {Counter, "stack-overflows", "count", ProviderCounter},
                {Counter, "silent-ray-drops", "count", ProviderCounter},
                {Counter, "nan-or-inf", "count", ProviderCounter},
                {Counter, "negative-pdfs", "count", ProviderCounter},
                {Counter, "invalid-hits", "count", ProviderCounter}
            };
        }

        [[nodiscard]] bool SameConditions(
            std::span<const EvidenceCondition> left,
            std::span<const EvidenceCondition> right)
        {
            using Item = std::pair<std::string, std::string>;
            std::vector<Item> leftItems;
            std::vector<Item> rightItems;
            leftItems.reserve(left.size());
            rightItems.reserve(right.size());
            for (const EvidenceCondition& condition : left)
            {
                leftItems.emplace_back(condition.name, condition.value);
            }
            for (const EvidenceCondition& condition : right)
            {
                rightItems.emplace_back(condition.name, condition.value);
            }
            std::sort(leftItems.begin(), leftItems.end());
            std::sort(rightItems.begin(), rightItems.end());
            return leftItems == rightItems;
        }

        [[nodiscard]] bool SameMetrics(
            std::span<const EvidenceMetricRequirement> left,
            std::span<const EvidenceMetricRequirement> right)
        {
            using Item = std::tuple<EvidenceSampleKind, std::string,
                std::string, EvidenceMeasurementMethod>;
            std::vector<Item> leftItems;
            std::vector<Item> rightItems;
            leftItems.reserve(left.size());
            rightItems.reserve(right.size());
            for (const EvidenceMetricRequirement& metric : left)
            {
                leftItems.emplace_back(
                    metric.kind, metric.scope, metric.unit,
                    metric.measurementMethod);
            }
            for (const EvidenceMetricRequirement& metric : right)
            {
                rightItems.emplace_back(
                    metric.kind, metric.scope, metric.unit,
                    metric.measurementMethod);
            }
            std::sort(leftItems.begin(), leftItems.end());
            std::sort(rightItems.begin(), rightItems.end());
            return leftItems == rightItems;
        }

        [[nodiscard]] bool SamePlan(
            const BenchmarkPlan& actual,
            const BenchmarkPlan& expected)
        {
            return actual.warmupFrameCount == expected.warmupFrameCount
                && actual.measurementFrameCount == expected.measurementFrameCount
                && actual.repeatCount == expected.repeatCount
                && actual.configGeneration == expected.configGeneration
                && actual.sceneGeneration == expected.sceneGeneration
                && actual.resourceGeneration == expected.resourceGeneration
                && SameConditions(actual.conditions, expected.conditions)
                && SameMetrics(actual.requiredMetrics, expected.requiredMetrics);
        }

        [[nodiscard]] const ScopeStatistics* FindScope(
            std::span<const ScopeStatistics> statistics,
            const std::string_view scope)
        {
            const auto iterator = std::ranges::find_if(
                statistics,
                [scope](const ScopeStatistics& statistic)
                {
                    return statistic.scope == scope;
                });
            return iterator == statistics.end() ? nullptr : &*iterator;
        }

        [[nodiscard]] AcceptanceResult Reject(std::string reason)
        {
            return {AcceptanceState::Rejected, std::move(reason)};
        }

        [[nodiscard]] AcceptanceResult NotRun(std::string reason)
        {
            return {AcceptanceState::NotRun, std::move(reason)};
        }
    }

    bool IsComplete(const ComparisonIdentity& identity) noexcept
    {
        return !identity.sceneFingerprint.empty()
            && !identity.cameraFingerprint.empty()
            && !identity.assetFingerprint.empty()
            && !identity.referenceFingerprint.empty()
            && identity.configGeneration != 0u
            && identity.sceneGeneration != 0u
            && identity.resourceGeneration != 0u
            && identity.width != 0u
            && identity.height != 0u
            && identity.samplesPerFrame != 0u
            && identity.maximumPathLength != 0u;
    }

    bool SameIdentity(
        const ComparisonIdentity& left,
        const ComparisonIdentity& right) noexcept
    {
        return left.sceneFingerprint == right.sceneFingerprint
            && left.cameraFingerprint == right.cameraFingerprint
            && left.assetFingerprint == right.assetFingerprint
            && left.referenceFingerprint == right.referenceFingerprint
            && left.backend == right.backend
            && left.transportModel == right.transportModel
            && left.estimator == right.estimator
            && left.lightSelection == right.lightSelection
            && left.environmentSampler == right.environmentSampler
            && left.baseSeed == right.baseSeed
            && left.configGeneration == right.configGeneration
            && left.sceneGeneration == right.sceneGeneration
            && left.resourceGeneration == right.resourceGeneration
            && left.width == right.width
            && left.height == right.height
            && left.samplesPerFrame == right.samplesPerFrame
            && left.maximumPathLength == right.maximumPathLength;
    }

    bool RuntimeFaultCounters::AllZero() const noexcept
    {
        return validationErrors == 0u
            && queueOverflows == 0u
            && stackOverflows == 0u
            && silentRayDrops == 0u
            && nanOrInfValues == 0u
            && negativePdfs == 0u
            && invalidHits == 0u;
    }

    AcceptanceResult EvaluateExecutionConvergence(
        const ExecutionConvergenceEvidence& megakernel,
        const ExecutionConvergenceEvidence& wavefront,
        const ConvergencePolicy& policy)
    {
        if (megakernel.checkpoints.empty() || wavefront.checkpoints.empty())
        {
            return NotRun("Megakernel/Wavefront image checkpoints were not captured.");
        }
        if (!IsComplete(megakernel.identity)
            || !IsComplete(wavefront.identity)
            || !SameIdentity(megakernel.identity, wavefront.identity))
        {
            return Reject("Execution captures do not share one complete comparison identity.");
        }
        if (megakernel.architecture != ExecutionArchitecture::Megakernel
            || wavefront.architecture != ExecutionArchitecture::Wavefront)
        {
            return Reject("Execution evidence is not the required Megakernel/Wavefront pair.");
        }
        if (!megakernel.faults.AllZero() || !wavefront.faults.AllZero())
        {
            return Reject("Validation, overflow, ray-drop, NaN/Inf, PDF, or hit counters are non-zero.");
        }
        if (policy.minimumCheckpointCount < 2u
            || !std::isfinite(policy.maximumFinalRmse)
            || policy.maximumFinalRmse <= 0.0
            || !std::isfinite(policy.maximumRelativeFinalRmseDelta)
            || policy.maximumRelativeFinalRmseDelta < 0.0
            || policy.authority.empty())
        {
            return Reject("Convergence thresholds require an explicit frozen-baseline or ADR authority.");
        }
        if (megakernel.checkpoints.size() != wavefront.checkpoints.size()
            || megakernel.checkpoints.size() < policy.minimumCheckpointCount)
        {
            return Reject("Execution convergence curves have different or insufficient checkpoints.");
        }

        std::uint32_t previousSpp = 0u;
        std::uint64_t previousRayBudget = 0u;
        for (std::size_t index = 0u; index < megakernel.checkpoints.size(); ++index)
        {
            const QualityCheckpoint& mega = megakernel.checkpoints[index];
            const QualityCheckpoint& wave = wavefront.checkpoints[index];
            if (mega.samplesPerPixel == 0u || mega.rayBudget == 0u
                || mega.samplesPerPixel != wave.samplesPerPixel
                || mega.rayBudget != wave.rayBudget
                || mega.samplesPerPixel <= previousSpp
                || mega.rayBudget <= previousRayBudget
                || !std::isfinite(mega.rmse) || mega.rmse < 0.0
                || !std::isfinite(wave.rmse) || wave.rmse < 0.0
                || !IsQualityNumber(mega.psnr)
                || !IsQualityNumber(wave.psnr))
            {
                return Reject("Convergence checkpoints are invalid or do not use identical increasing SPP/ray budgets.");
            }
            previousSpp = mega.samplesPerPixel;
            previousRayBudget = mega.rayBudget;
        }

        const QualityCheckpoint& megaFirst = megakernel.checkpoints.front();
        const QualityCheckpoint& waveFirst = wavefront.checkpoints.front();
        const QualityCheckpoint& megaFinal = megakernel.checkpoints.back();
        const QualityCheckpoint& waveFinal = wavefront.checkpoints.back();
        if (megaFinal.rmse > megaFirst.rmse || waveFinal.rmse > waveFirst.rmse)
        {
            return Reject("One execution architecture does not improve from the first to final checkpoint.");
        }
        if (megaFinal.rmse > policy.maximumFinalRmse
            || waveFinal.rmse > policy.maximumFinalRmse)
        {
            return Reject("One execution architecture exceeds the authority-defined final RMSE threshold.");
        }
        const double denominator = std::max(megaFinal.rmse, waveFinal.rmse);
        const double relativeDelta = denominator == 0.0
            ? 0.0
            : std::abs(megaFinal.rmse - waveFinal.rmse) / denominator;
        if (relativeDelta > policy.maximumRelativeFinalRmseDelta)
        {
            return Reject("Megakernel/Wavefront final RMSE separation exceeds the authority-defined threshold.");
        }
        if (!wavefront.indirectDispatchValidationPassed
            || !wavefront.megakernelBaselineRetained
            || wavefront.activePathsPerBounce.size()
                != wavefront.identity.maximumPathLength
            || wavefront.activePathsPerBounce.empty()
            || wavefront.activePathsPerBounce.front() == 0u
            || !std::ranges::is_sorted(
                wavefront.activePathsPerBounce, std::greater<>{}))
        {
            return Reject("Wavefront validation, baseline retention, or per-bounce active-path telemetry is incomplete.");
        }
        return {AcceptanceState::Accepted,
            "Megakernel and Wavefront convergence evidence satisfies the explicit policy."};
    }

    AcceptanceResult EvaluateReconstructionComparisonSet(
        const std::span<const ReconstructionComparison> comparisons)
    {
        if (comparisons.empty())
        {
            return NotRun("No 1-SPP reconstruction comparison artifacts were supplied.");
        }
        if (comparisons.size() != 4u)
        {
            return Reject("Raw, Temporal, A-Trous-only, and SVGF require exactly four comparison entries.");
        }

        const ReconstructionComparison& first = comparisons.front();
        if (!IsComplete(first.identity) || first.identity.samplesPerFrame != 1u
            || first.referenceSamplesPerPixel <= 1u)
        {
            return Reject("Reconstruction comparison identity or high-SPP reference is incomplete.");
        }
        std::set<ReconstructionMode> modes;
        std::set<std::string> outputArtifacts;
        for (const ReconstructionComparison& comparison : comparisons)
        {
            if (!SameIdentity(first.identity, comparison.identity)
                || comparison.referenceSamplesPerPixel
                    != first.referenceSamplesPerPixel
                || comparison.rawArtifact.empty()
                || comparison.outputArtifact.empty()
                || comparison.referenceArtifact.empty()
                || comparison.rawArtifact != first.rawArtifact
                || comparison.referenceArtifact != first.referenceArtifact
                || (comparison.mode == ReconstructionMode::ProgressiveMean
                    && comparison.outputArtifact != comparison.rawArtifact)
                || !outputArtifacts.insert(comparison.outputArtifact).second
                || !std::isfinite(comparison.rmse) || comparison.rmse < 0.0
                || !IsQualityNumber(comparison.psnr)
                || !modes.insert(comparison.mode).second)
            {
                return Reject("Reconstruction entries have stale identities, non-unique artifacts, invalid metrics, or duplicate modes.");
            }
        }
        if (!modes.contains(ReconstructionMode::ProgressiveMean)
            || !modes.contains(ReconstructionMode::TemporalAccumulation)
            || !modes.contains(ReconstructionMode::SpatialFixedAtrous)
            || !modes.contains(ReconstructionMode::Svgf))
        {
            return Reject("The decomposed reconstruction comparison set is incomplete.");
        }
        return {AcceptanceState::Accepted,
            "The 1-SPP Raw/Temporal/A-Trous/SVGF comparison set is complete."};
    }

    BenchmarkPlan BuildWave3BackendBenchmarkPlan(
        const ComparisonIdentity& identity,
        const TraversalBackend backend)
    {
        return Demos::BuildCanonicalPortfolioBenchmarkPlan(
            identity.configGeneration,
            identity.sceneGeneration,
            identity.resourceGeneration,
            BuildConditions(identity, backend),
            BuildMetrics());
    }

    BackendProfileResult EvaluateBackendProfile(
        const BackendProfileEvidence& evidence)
    {
        BackendProfileResult result{};
        result.backend = evidence.backend;
        if (evidence.samples.empty())
        {
            result.acceptance = NotRun(
                "No live GPU timestamp/counter samples were supplied.");
            return result;
        }
        if (!IsComplete(evidence.identity)
            || evidence.identity.samplesPerFrame != 1u
            || evidence.identity.backend != evidence.backend
            || !IsWave3ProfileBackend(evidence.backend))
        {
            result.acceptance = Reject(
                "The backend profile identity is incomplete or is not the Wave 3 LBVH/RT Pipeline pair.");
            return result;
        }
        const BenchmarkPlan expected =
            BuildWave3BackendBenchmarkPlan(evidence.identity, evidence.backend);
        if (!SamePlan(evidence.plan, expected))
        {
            result.acceptance = Reject(
                "The backend profile does not use the canonical 120/1000/3 protocol and exact metric contract.");
            return result;
        }
        const std::uint64_t expectedWarmupFrames =
            static_cast<std::uint64_t>(evidence.plan.warmupFrameCount)
            * evidence.plan.repeatCount;
        const std::uint64_t expectedMeasurementFrames =
            static_cast<std::uint64_t>(evidence.plan.measurementFrameCount)
            * evidence.plan.repeatCount;
        if (!evidence.benchmarkSequenceCompleted
            || evidence.acceptedWarmupFrames != expectedWarmupFrames
            || evidence.acceptedMeasurementFrames != expectedMeasurementFrames)
        {
            result.acceptance = Reject(
                "The provider did not complete every canonical warmup, measurement, and repeat frame.");
            return result;
        }
        for (const Demos::EvidenceSample& sample : evidence.samples)
        {
            if (sample.provenance.source != Demos::EvidenceSource::ProviderReported
                || sample.provenance.provider.empty()
                || sample.provenance.detail.empty())
            {
                result.acceptance = Reject(
                    "GPU acceptance rejects CPU-wall-clock, synthetic, or anonymous provenance.");
                return result;
            }
        }

        Demos::ScopeStatisticsResult timingResult =
            Demos::ComputeScopeStatistics(
                evidence.samples, EvidenceSampleKind::Timing, evidence.plan);
        if (!timingResult)
        {
            result.acceptance = Reject(timingResult.message);
            return result;
        }
        Demos::ScopeStatisticsResult counterResult =
            Demos::ComputeScopeStatistics(
                evidence.samples, EvidenceSampleKind::Counter, evidence.plan);
        if (!counterResult)
        {
            result.acceptance = Reject(counterResult.message);
            return result;
        }
        result.timings = std::move(timingResult.scopes);
        result.counters = std::move(counterResult.scopes);

        const ScopeStatistics* const build = FindScope(result.timings, "build");
        const ScopeStatistics* const trace = FindScope(result.timings, "trace");
        constexpr std::size_t expectedSampleCount =
            static_cast<std::size_t>(Demos::kCanonicalPortfolioMeasurementFrameCount)
            * Demos::kCanonicalPortfolioRepeatCount;
        if (build == nullptr || trace == nullptr
            || build->measurementMethod
                != EvidenceMeasurementMethod::GpuTimestampQuery
            || trace->measurementMethod
                != EvidenceMeasurementMethod::GpuTimestampQuery
            || build->sampleCount != expectedSampleCount
            || trace->sampleCount != expectedSampleCount
            || !std::isfinite(build->median) || build->median < 0.0
            || !std::isfinite(build->p95NearestRank)
            || build->p95NearestRank < build->median
            || !std::isfinite(trace->median) || trace->median < 0.0
            || !std::isfinite(trace->p95NearestRank)
            || trace->p95NearestRank < trace->median)
        {
            result.acceptance = Reject(
                "Build/trace GPU timestamp median and p95 statistics are incomplete.");
            return result;
        }

        for (const std::string_view workload :
            {"rays", "memory", "path-length"})
        {
            const ScopeStatistics* const statistic =
                FindScope(result.counters, workload);
            if (statistic == nullptr || statistic->sampleCount != expectedSampleCount
                || !std::isfinite(statistic->minimum)
                || statistic->minimum <= 0.0)
            {
                result.acceptance = Reject(
                    "Ray, memory, or path-length workload counters are incomplete.");
                return result;
            }
        }
        for (const std::string_view fault : {
            "validation-errors", "queue-overflows", "stack-overflows",
            "silent-ray-drops", "nan-or-inf", "negative-pdfs", "invalid-hits"})
        {
            const ScopeStatistics* const statistic =
                FindScope(result.counters, fault);
            if (statistic == nullptr || statistic->sampleCount != expectedSampleCount
                || statistic->maximum != 0.0)
            {
                result.acceptance = Reject(
                    "At least one required validation/fault counter is missing or non-zero.");
                return result;
            }
        }
        result.acceptance = {AcceptanceState::Accepted,
            "Canonical live GPU build/trace profile and zero-fault counters are complete."};
        return result;
    }

    Wave3GateResult ComposeWave3Gate(
        const AcceptanceResult& convergence,
        const AcceptanceResult& reconstruction,
        const BackendProfileResult& softwareProfile,
        const BackendProfileResult& hardwareProfile)
    {
        Wave3GateResult result{
            convergence,
            reconstruction,
            softwareProfile.acceptance,
            hardwareProfile.acceptance};
        if (softwareProfile.backend != TraversalBackend::GpuLbvh)
        {
            result.softwareProfile = Reject(
                "Wave 3 software profile must be the GPU LBVH provider.");
        }
        if (hardwareProfile.backend
            != TraversalBackend::VulkanRayTracingPipeline)
        {
            result.hardwareProfile = Reject(
                "Wave 3 hardware profile must be the RT Pipeline/SBT provider.");
        }
        return result;
    }

    std::string_view ToString(const AcceptanceState state) noexcept
    {
        switch (state)
        {
        case AcceptanceState::NotRun: return "not-run";
        case AcceptanceState::Accepted: return "accepted";
        case AcceptanceState::Rejected: return "rejected";
        default: return "invalid";
        }
    }
}
