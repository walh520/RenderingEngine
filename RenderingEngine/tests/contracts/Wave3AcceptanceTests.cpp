#include "renderers/Wave3Acceptance.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "Wave 3 acceptance test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept { return failures_ == 0; }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] RenderingEngine::Wave3::ComparisonIdentity MakeIdentity(
        const RenderingEngine::TraversalBackend backend =
            RenderingEngine::TraversalBackend::GpuLbvh)
    {
        using namespace RenderingEngine;
        Wave3::ComparisonIdentity identity{};
        identity.sceneFingerprint = "cornell-scene-sha256";
        identity.cameraFingerprint = "camera-sha256";
        identity.assetFingerprint = "asset-sha256";
        identity.referenceFingerprint = "reference-exr-sha256";
        identity.backend = backend;
        identity.estimator = DirectLightingEstimator::MultipleImportanceSampling;
        identity.lightSelection = LightSelectionStrategy::PowerWeighted;
        identity.environmentSampler = EnvironmentDirectionSampler::UniformSphere;
        identity.baseSeed = 0u;
        identity.configGeneration = 3u;
        identity.sceneGeneration = 5u;
        identity.resourceGeneration = 7u;
        identity.width = 256u;
        identity.height = 256u;
        identity.samplesPerFrame = 1u;
        identity.maximumPathLength = 4u;
        return identity;
    }

    [[nodiscard]] RenderingEngine::Wave3::BackendProfileEvidence MakeProfile(
        const RenderingEngine::TraversalBackend backend)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Demos;
        using namespace RenderingEngine::Wave3;

        BackendProfileEvidence evidence{};
        evidence.identity = MakeIdentity(backend);
        evidence.backend = backend;
        evidence.plan = BuildWave3BackendBenchmarkPlan(evidence.identity, backend);
        evidence.benchmarkSequenceCompleted = true;
        evidence.acceptedWarmupFrames =
            static_cast<std::uint64_t>(evidence.plan.warmupFrameCount)
            * evidence.plan.repeatCount;
        evidence.acceptedMeasurementFrames =
            static_cast<std::uint64_t>(evidence.plan.measurementFrameCount)
            * evidence.plan.repeatCount;
        const std::size_t samplesPerMetric =
            static_cast<std::size_t>(evidence.plan.measurementFrameCount)
            * evidence.plan.repeatCount;
        evidence.samples.reserve(
            samplesPerMetric * evidence.plan.requiredMetrics.size());

        std::uint64_t globalIndex = 0u;
        for (std::uint32_t repeat = 0u; repeat < evidence.plan.repeatCount; ++repeat)
        {
            for (std::uint32_t frame = 0u;
                frame < evidence.plan.measurementFrameCount;
                ++frame)
            {
                ++globalIndex;
                for (const EvidenceMetricRequirement& metric :
                    evidence.plan.requiredMetrics)
                {
                    double value = 0.0;
                    if (metric.kind == EvidenceSampleKind::Timing)
                    {
                        value = metric.scope == "build" ? 0.25 : 0.75;
                        value += static_cast<double>(frame % 5u) * 0.01;
                    }
                    else if (metric.scope == "rays")
                    {
                        value = 65'536.0;
                    }
                    else if (metric.scope == "memory")
                    {
                        value = 8'388'608.0;
                    }
                    else if (metric.scope == "path-length")
                    {
                        value = 4.0;
                    }

                    EvidenceSample sample{};
                    sample.kind = metric.kind;
                    sample.scope = metric.scope;
                    sample.unit = metric.unit;
                    sample.value = value;
                    sample.provenance = {
                        EvidenceSource::ProviderReported,
                        "unit-provider-shape",
                        "CPU test data for gate logic only"};
                    sample.frameIndex = globalIndex;
                    sample.configGeneration = evidence.plan.configGeneration;
                    sample.repeatIndex = repeat;
                    sample.conditions = evidence.plan.conditions;
                    sample.sceneGeneration = evidence.plan.sceneGeneration;
                    sample.resourceGeneration = evidence.plan.resourceGeneration;
                    sample.sampleIndex = globalIndex;
                    evidence.samples.push_back(std::move(sample));
                }
            }
        }
        return evidence;
    }
}

bool RunWave3AcceptanceTests()
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Wave3;

    TestContext tests;
    ExecutionConvergenceEvidence megakernel{};
    megakernel.identity = MakeIdentity();
    megakernel.architecture = ExecutionArchitecture::Megakernel;
    megakernel.checkpoints = {
        {1u, 262'144u, 0.08, 21.9},
        {4u, 1'048'576u, 0.03, 30.4},
        {16u, 4'194'304u, 0.0100, 40.0}};

    ExecutionConvergenceEvidence wavefront{};
    wavefront.identity = megakernel.identity;
    wavefront.architecture = ExecutionArchitecture::Wavefront;
    wavefront.checkpoints = {
        {1u, 262'144u, 0.081, 21.8},
        {4u, 1'048'576u, 0.031, 30.2},
        {16u, 4'194'304u, 0.0105, 39.6}};
    wavefront.activePathsPerBounce = {65'536u, 42'000u, 12'000u, 1'000u};
    wavefront.indirectDispatchValidationPassed = true;
    wavefront.megakernelBaselineRetained = true;

    const ConvergencePolicy policy{
        3u, 0.02, 0.10, "ADR-fixture-threshold"};
    const AcceptanceResult convergence = EvaluateExecutionConvergence(
        megakernel, wavefront, policy);
    tests.Expect(convergence.Passed(),
        "same-identity curves and explicit policy must pass the gate logic");

    ++wavefront.identity.sceneGeneration;
    tests.Expect(!EvaluateExecutionConvergence(
            megakernel, wavefront, policy).Passed(),
        "stale comparison identity must fail closed");
    wavefront.identity = megakernel.identity;

    const std::array reconstruction = {
        ReconstructionComparison{MakeIdentity(), ReconstructionMode::ProgressiveMean,
            4096u, 0.50, 6.0, "raw.exr", "raw.exr", "reference.exr"},
        ReconstructionComparison{MakeIdentity(),
            ReconstructionMode::TemporalAccumulation,
            4096u, 0.20, 14.0, "raw.exr", "temporal.exr", "reference.exr"},
        ReconstructionComparison{MakeIdentity(),
            ReconstructionMode::SpatialFixedAtrous,
            4096u, 0.05, 26.0, "raw.exr", "atrous.exr", "reference.exr"},
        ReconstructionComparison{MakeIdentity(), ReconstructionMode::Svgf,
            4096u, 0.03, 30.5, "raw.exr", "svgf.exr", "reference.exr"}};
    const AcceptanceResult reconstructionResult =
        EvaluateReconstructionComparisonSet(reconstruction);
    tests.Expect(reconstructionResult.Passed(),
        "decomposed 1-SPP comparison artifacts must pass the gate logic");

    std::array duplicateOutput = reconstruction;
    duplicateOutput.back().outputArtifact =
        duplicateOutput[duplicateOutput.size() - 2u].outputArtifact;
    tests.Expect(!EvaluateReconstructionComparisonSet(duplicateOutput).Passed(),
        "four labels must not reuse one reconstruction output artifact");

    BackendProfileEvidence missingProfile{};
    missingProfile.identity = MakeIdentity();
    missingProfile.backend = TraversalBackend::GpuLbvh;
    missingProfile.plan = BuildWave3BackendBenchmarkPlan(
        missingProfile.identity, missingProfile.backend);
    tests.Expect(EvaluateBackendProfile(missingProfile).acceptance.state
            == AcceptanceState::NotRun,
        "missing GPU samples must remain explicitly not-run");

    const BackendProfileResult software =
        EvaluateBackendProfile(MakeProfile(TraversalBackend::GpuLbvh));
    const BackendProfileResult hardware = EvaluateBackendProfile(
        MakeProfile(TraversalBackend::VulkanRayTracingPipeline));
    tests.Expect(software.acceptance.Passed()
            && hardware.acceptance.Passed(),
        "canonical provider-shaped samples must exercise both profile accept paths");

    BackendProfileEvidence incompleteCadence =
        MakeProfile(TraversalBackend::GpuLbvh);
    --incompleteCadence.acceptedWarmupFrames;
    tests.Expect(!EvaluateBackendProfile(incompleteCadence).acceptance.Passed(),
        "measurement samples must not hide an incomplete warmup cadence");

    const Wave3GateResult fullGate = ComposeWave3Gate(
        convergence, reconstructionResult, software, hardware);
    tests.Expect(fullGate.Passed(),
        "four accepted evidence groups must compose the Wave 3 gate");
    tests.Expect(ToString(AcceptanceState::NotRun) == "not-run"
            && ToString(AcceptanceState::Accepted) == "accepted"
            && ToString(AcceptanceState::Rejected) == "rejected",
        "evidence states must serialize without ambiguity");

    if (tests.Passed())
    {
        std::cout << "Wave 3 fail-closed acceptance-contract checks passed.\n";
    }
    return tests.Passed();
}
