#include "demos/ShowcaseEvidence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

namespace
{
    class TestContext final
    {
    public:
        explicit TestContext(std::ostream& output)
            : m_output(output)
        {
        }

        void Expect(bool condition, const char* description)
        {
            if (condition)
            {
                return;
            }
            m_passed = false;
            m_output << "FAILED: " << description << '\n';
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return m_passed;
        }

    private:
        std::ostream& m_output;
        bool m_passed = true;
    };

    [[nodiscard]] RenderingEngine::Demos::BenchmarkPlan MakePlan()
    {
        using namespace RenderingEngine::Demos;

        BenchmarkPlan plan;
        plan.warmupFrameCount = 1u;
        plan.measurementFrameCount = 2u;
        plan.repeatCount = 2u;
        plan.configGeneration = 42u;
        plan.sceneGeneration = 7u;
        plan.resourceGeneration = 9u;
        plan.conditions = {
            { "seed", "fixed\"seed\nline2" },
            { "scene", "cornell,box" }
        };
        plan.requiredMetrics = {
            { EvidenceSampleKind::Counter, "trace", "rays",
                EvidenceMeasurementMethod::ProviderCounter },
            { EvidenceSampleKind::Timing, "trace", "ms",
                EvidenceMeasurementMethod::GpuTimestampQuery }
        };
        return plan;
    }

    [[nodiscard]] RenderingEngine::Demos::EvidenceSample MakeSample(
        RenderingEngine::Demos::EvidenceSampleKind kind,
        std::string scope,
        std::string unit,
        double value,
        std::uint64_t frameIndex,
        std::uint64_t sampleIndex,
        std::uint32_t repeatIndex,
        const RenderingEngine::Demos::BenchmarkPlan& plan)
    {
        using namespace RenderingEngine::Demos;

        EvidenceSample sample;
        sample.kind = kind;
        sample.scope = std::move(scope);
        sample.unit = std::move(unit);
        sample.value = value;
        sample.provenance = {
            EvidenceSource::SyntheticTest,
            "fixture,\"clock\"",
            "deterministic\nprovider"
        };
        sample.frameIndex = frameIndex;
        sample.configGeneration = plan.configGeneration;
        sample.repeatIndex = repeatIndex;
        sample.conditions = {
            plan.conditions[1],
            plan.conditions[0]
        };
        sample.sceneGeneration = plan.sceneGeneration;
        sample.resourceGeneration = plan.resourceGeneration;
        sample.sampleIndex = sampleIndex;
        return sample;
    }

    [[nodiscard]] std::array<RenderingEngine::Demos::EvidenceSample, 2> MakeFrameSamples(
        double timing,
        double counter,
        std::uint64_t frameIndex,
        std::uint64_t sampleIndex,
        std::uint32_t repeatIndex,
        const RenderingEngine::Demos::BenchmarkPlan& plan)
    {
        using namespace RenderingEngine::Demos;

        return {
            MakeSample(EvidenceSampleKind::Timing, "trace", "ms",
                timing, frameIndex, sampleIndex, repeatIndex, plan),
            MakeSample(EvidenceSampleKind::Counter, "trace", "rays",
                counter, frameIndex, sampleIndex, repeatIndex, plan)
        };
    }

    [[nodiscard]] RenderingEngine::Demos::BenchmarkFrameInput MakeFrameInput(
        std::uint64_t frameIndex,
        std::uint32_t repeatIndex,
        std::span<const RenderingEngine::Demos::EvidenceSample> samples,
        const RenderingEngine::Demos::BenchmarkPlan& plan)
    {
        RenderingEngine::Demos::BenchmarkFrameInput frame;
        frame.frameIndex = frameIndex;
        frame.configGeneration = plan.configGeneration;
        frame.repeatIndex = repeatIndex;
        frame.samples = samples;
        frame.sceneGeneration = plan.sceneGeneration;
        frame.resourceGeneration = plan.resourceGeneration;
        return frame;
    }

    void TestBenchmarkStateMachine(TestContext& tests)
    {
        using namespace RenderingEngine::Demos;

        const BenchmarkPlan plan = MakePlan();
        BenchmarkSequence sequence;
        tests.Expect(static_cast<bool>(sequence.Start(plan))
            && sequence.Phase() == BenchmarkPhase::Warmup,
            "a valid plan must start in warmup");

        const BenchmarkFrameInput warmup0 = MakeFrameInput(10u, 0u, {}, plan);
        tests.Expect(static_cast<bool>(sequence.SubmitFrame(warmup0))
            && sequence.Phase() == BenchmarkPhase::Measurement,
            "the configured warmup frame must transition to measurement");

        const auto frame11 = MakeFrameSamples(1.0, 100.0, 11u, 100u, 0u, plan);
        const auto frame12 = MakeFrameSamples(4.0, 400.0, 12u, 101u, 0u, plan);
        tests.Expect(static_cast<bool>(sequence.SubmitFrame(
                MakeFrameInput(11u, 0u, frame11, plan))),
            "the first complete measurement frame must be accepted");
        tests.Expect(static_cast<bool>(sequence.SubmitFrame(
                MakeFrameInput(12u, 0u, frame12, plan)))
            && sequence.Phase() == BenchmarkPhase::AwaitingRepeat,
            "the first measurement window must end at an explicit repeat boundary");
        tests.Expect(static_cast<bool>(sequence.BeginNextRepeat())
            && sequence.CurrentRepeatIndex() == 1u
            && sequence.Phase() == BenchmarkPhase::Warmup,
            "beginning the next repeat must restart its warmup window");

        const BenchmarkFrameInput warmup1 = MakeFrameInput(13u, 1u, {}, plan);
        const auto frame14 = MakeFrameSamples(2.0, 200.0, 14u, 102u, 1u, plan);
        const auto frame15 = MakeFrameSamples(3.0, 300.0, 15u, 103u, 1u, plan);
        tests.Expect(static_cast<bool>(sequence.SubmitFrame(warmup1)),
            "the second repeat warmup must be accepted");
        tests.Expect(static_cast<bool>(sequence.SubmitFrame(
                MakeFrameInput(14u, 1u, frame14, plan))),
            "the second repeat first measurement must be accepted");
        tests.Expect(static_cast<bool>(sequence.SubmitFrame(
                MakeFrameInput(15u, 1u, frame15, plan)))
            && sequence.Phase() == BenchmarkPhase::Complete
            && sequence.Samples().size() == 8u,
            "all repeats must complete with exactly two metrics per measured frame");

        const ScopeStatisticsResult timings = ComputeScopeStatistics(
            sequence.Samples(), EvidenceSampleKind::Timing, plan);
        tests.Expect(static_cast<bool>(timings)
            && timings.scopes.size() == 1u
            && timings.scopes[0].sampleCount == 4u
            && timings.scopes[0].minimum == 1.0
            && timings.scopes[0].maximum == 4.0
            && timings.scopes[0].median == 2.5
            && timings.scopes[0].p95NearestRank == 4.0
            && timings.scopes[0].measurementMethod
                == EvidenceMeasurementMethod::GpuTimestampQuery
            && timings.scopes[0].provenance.source == EvidenceSource::SyntheticTest
            && timings.scopes[0].provenance.provider == "fixture,\"clock\""
            && timings.scopes[0].provenance.detail == "deterministic\nprovider",
            "per-scope statistics must report values and their common provenance");

        const ScopeStatisticsResult counters = ComputeScopeStatistics(
            sequence.Samples(), EvidenceSampleKind::Counter, plan);
        tests.Expect(static_cast<bool>(counters)
            && counters.scopes.size() == 1u
            && counters.scopes[0].median == 250.0
            && counters.scopes[0].p95NearestRank == 400.0,
            "counter scopes must use the same exact aggregation rules");

        std::vector<EvidenceSample> mixedProvenance(
            sequence.Samples().begin(), sequence.Samples().end());
        const auto mixedTiming = std::find_if(
            mixedProvenance.rbegin(),
            mixedProvenance.rend(),
            [](const EvidenceSample& sample)
            {
                return sample.kind == EvidenceSampleKind::Timing;
            });
        mixedTiming->provenance = {
            EvidenceSource::ProviderReported,
            "live.provider",
            "provider timing"
        };
        tests.Expect(ComputeScopeStatistics(
                mixedProvenance,
                EvidenceSampleKind::Timing,
                plan).error == EvidenceError::InvalidProvenance,
            "derived statistics must reject mixed synthetic/live provenance within one scope");

        std::vector<EvidenceSample> staggeredFrames(
            sequence.Samples().begin(), sequence.Samples().end());
        const auto firstCounter = std::find_if(
            staggeredFrames.begin(),
            staggeredFrames.end(),
            [](const EvidenceSample& sample)
            {
                return sample.kind == EvidenceSampleKind::Counter
                    && sample.repeatIndex == 0u
                    && sample.frameIndex == 11u;
            });
        firstCounter->frameIndex = 10u;
        tests.Expect(ComputeScopeStatistics(
                staggeredFrames,
                EvidenceSampleKind::Timing,
                plan).error == EvidenceError::MissingSample
            && BuildTimingsCsv(staggeredFrames, plan).error
                == EvidenceError::MissingSample,
            "statistics and CSV must reject required metrics sampled on staggered frame sets");

        std::vector<EvidenceSample> reusedFrameAcrossRepeats(
            sequence.Samples().begin(), sequence.Samples().end());
        for (EvidenceSample& sample : reusedFrameAcrossRepeats)
        {
            if (sample.repeatIndex == 1u && sample.frameIndex == 14u)
            {
                sample.frameIndex = 11u;
            }
        }
        tests.Expect(ComputeScopeStatistics(
                reusedFrameAcrossRepeats,
                EvidenceSampleKind::Counter,
                plan).error == EvidenceError::FrameOrderMismatch
            && BuildCountersCsv(reusedFrameAcrossRepeats, plan).error
                == EvidenceError::FrameOrderMismatch,
            "statistics and CSV must reject a frame index reused by another repeat");

        std::vector<EvidenceSample> reusedSampleAcrossRepeats(
            sequence.Samples().begin(), sequence.Samples().end());
        for (EvidenceSample& sample : reusedSampleAcrossRepeats)
        {
            if (sample.repeatIndex == 1u && sample.frameIndex == 14u)
            {
                sample.sampleIndex = 100u;
            }
        }
        tests.Expect(ComputeScopeStatistics(
                reusedSampleAcrossRepeats,
                EvidenceSampleKind::Timing,
                plan).error == EvidenceError::SampleIdentityMismatch
            && BuildTimingsCsv(reusedSampleAcrossRepeats, plan).error
                == EvidenceError::SampleIdentityMismatch,
            "statistics and CSV must reject a sample index reused by another repeat");

        const CsvResult timingCsv = BuildTimingsCsv(sequence.Samples(), plan);
        const CsvResult counterCsv = BuildCountersCsv(sequence.Samples(), plan);
        std::vector<EvidenceSample> reversed(
            sequence.Samples().begin(), sequence.Samples().end());
        std::reverse(reversed.begin(), reversed.end());
        const CsvResult reversedTimingCsv = BuildTimingsCsv(reversed, plan);
        tests.Expect(static_cast<bool>(timingCsv)
            && static_cast<bool>(counterCsv)
            && timingCsv.text == reversedTimingCsv.text,
            "CSV row order must be stable regardless of provider submission order");
        tests.Expect(timingCsv.text.find("synthetic-test") != std::string::npos
            && timingCsv.text.find("gpu-timestamp-query") != std::string::npos
            && timingCsv.text.starts_with(
                "scope,unit,measurement_method,value,repeat_index,frame_index,sample_index,config_generation,scene_generation,resource_generation,conditions,source,provider,detail\r\n")
            && timingCsv.text.find(",1,0,11,100,42,7,9,") != std::string::npos
            && counterCsv.text.find("provider-counter") != std::string::npos
            && timingCsv.text.find("\"fixture,\"\"clock\"\"\"") != std::string::npos
            && timingCsv.text.find("\r\n") != std::string::npos,
            "CSV must retain method, full identity, SyntheticTest, and RFC-style escaping");
    }

    void TestCanonicalPlanAndMeasurementMethods(TestContext& tests)
    {
        using namespace RenderingEngine::Demos;

        const std::vector<EvidenceCondition> conditions = {
            { "preset", "portfolio" }
        };
        const std::vector<EvidenceMetricRequirement> metrics = {
            { EvidenceSampleKind::Timing, "cpu.frame", "ms",
                EvidenceMeasurementMethod::CpuWallClock },
            { EvidenceSampleKind::Timing, "gpu.frame", "ms",
                EvidenceMeasurementMethod::GpuTimestampQuery },
            { EvidenceSampleKind::Counter, "trace.rays", "count",
                EvidenceMeasurementMethod::ProviderCounter }
        };
        const BenchmarkPlan canonical = BuildCanonicalPortfolioBenchmarkPlan(
            11u, 12u, 13u, conditions, metrics);
        BenchmarkSequence canonicalSequence;
        tests.Expect(canonical.warmupFrameCount
                == kCanonicalPortfolioWarmupFrameCount
            && canonical.measurementFrameCount
                == kCanonicalPortfolioMeasurementFrameCount
            && canonical.repeatCount == kCanonicalPortfolioRepeatCount
            && canonical.configGeneration == 11u
            && canonical.sceneGeneration == 12u
            && canonical.resourceGeneration == 13u
            && static_cast<bool>(canonicalSequence.Start(canonical))
            && canonicalSequence.Phase() == BenchmarkPhase::Warmup,
            "canonical portfolio builder must publish the 120/1000/3 cadence and generations");
        tests.Expect(EvidenceMeasurementMethodToken(
                EvidenceMeasurementMethod::CpuWallClock) == "cpu-wall-clock"
            && EvidenceMeasurementMethodToken(
                EvidenceMeasurementMethod::GpuTimestampQuery) == "gpu-timestamp-query"
            && EvidenceMeasurementMethodToken(
                EvidenceMeasurementMethod::ProviderCounter) == "provider-counter",
            "measurement methods must have explicit stable evidence tokens");

        BenchmarkPlan unspecifiedMethod = canonical;
        unspecifiedMethod.requiredMetrics[0].measurementMethod =
            EvidenceMeasurementMethod::Unspecified;
        BenchmarkSequence unspecifiedSequence;
        tests.Expect(unspecifiedSequence.Start(unspecifiedMethod).error
                == EvidenceError::InvalidPlan,
            "a timing requirement without a measurement method must be rejected");

        BenchmarkPlan counterUsingClock = canonical;
        counterUsingClock.requiredMetrics[2].measurementMethod =
            EvidenceMeasurementMethod::CpuWallClock;
        BenchmarkSequence counterUsingClockSequence;
        tests.Expect(counterUsingClockSequence.Start(counterUsingClock).error
                == EvidenceError::InvalidPlan,
            "provider counters must not be declared as CPU wall-clock timings");

        BenchmarkPlan gpuPlan = MakePlan();
        gpuPlan.warmupFrameCount = 0u;
        gpuPlan.measurementFrameCount = 1u;
        gpuPlan.repeatCount = 1u;
        auto gpuSamples = MakeFrameSamples(1.0, 10.0, 1u, 5u, 0u, gpuPlan);
        gpuSamples[0].provenance = {
            EvidenceSource::CpuWallClock,
            "std::chrono::steady_clock",
            "CPU substitution fixture"
        };
        BenchmarkSequence gpuSequence;
        tests.Expect(static_cast<bool>(gpuSequence.Start(gpuPlan))
            && gpuSequence.SubmitFrame(MakeFrameInput(
                1u, 0u, gpuSamples, gpuPlan)).error
                == EvidenceError::InvalidProvenance,
            "gpu-timestamp-query evidence must reject CPU wall-clock substitution");
    }

    void TestExplicitBenchmarkFailures(TestContext& tests)
    {
        using namespace RenderingEngine::Demos;

        BenchmarkPlan plan = MakePlan();
        plan.warmupFrameCount = 0u;
        plan.measurementFrameCount = 1u;
        plan.repeatCount = 1u;

        BenchmarkSequence missing;
        tests.Expect(static_cast<bool>(missing.Start(plan)),
            "missing-sample fixture plan must start");
        const auto complete = MakeFrameSamples(1.0, 10.0, 1u, 5u, 0u, plan);
        const std::span<const EvidenceSample> timingOnly(complete.data(), 1u);
        const EvidenceStatus missingStatus =
            missing.SubmitFrame(MakeFrameInput(1u, 0u, timingOnly, plan));
        tests.Expect(missingStatus.error == EvidenceError::MissingSample
            && missing.Phase() == BenchmarkPhase::Failed,
            "a missing required scope must enter an explicit failed state");

        BenchmarkSequence nonFinite;
        tests.Expect(static_cast<bool>(nonFinite.Start(plan)),
            "non-finite fixture plan must start");
        auto nonFiniteSamples = MakeFrameSamples(1.0, 10.0, 1u, 5u, 0u, plan);
        nonFiniteSamples[0].value = std::numeric_limits<double>::infinity();
        const EvidenceStatus nonFiniteStatus =
            nonFinite.SubmitFrame(MakeFrameInput(1u, 0u, nonFiniteSamples, plan));
        tests.Expect(nonFiniteStatus.error == EvidenceError::NonFiniteValue
            && nonFinite.Phase() == BenchmarkPhase::Failed,
            "a non-finite sample must enter an explicit failed state");

        BenchmarkSequence wrongGeneration;
        tests.Expect(static_cast<bool>(wrongGeneration.Start(plan)),
            "generation-mismatch fixture plan must start");
        BenchmarkFrameInput wrongGenerationFrame =
            MakeFrameInput(1u, 0u, complete, plan);
        ++wrongGenerationFrame.configGeneration;
        const EvidenceStatus generationStatus =
            wrongGeneration.SubmitFrame(wrongGenerationFrame);
        tests.Expect(generationStatus.error == EvidenceError::GenerationMismatch
            && wrongGeneration.Phase() == BenchmarkPhase::Failed,
            "a frame/config generation mismatch must fail explicitly");

        BenchmarkSequence wrongSceneGeneration;
        tests.Expect(static_cast<bool>(wrongSceneGeneration.Start(plan)),
            "scene-generation-mismatch fixture plan must start");
        BenchmarkFrameInput wrongSceneFrame = MakeFrameInput(1u, 0u, complete, plan);
        ++wrongSceneFrame.sceneGeneration;
        tests.Expect(wrongSceneGeneration.SubmitFrame(wrongSceneFrame).error
                == EvidenceError::GenerationMismatch,
            "a frame/scene generation mismatch must fail explicitly");

        BenchmarkSequence wrongSampleResourceGeneration;
        tests.Expect(static_cast<bool>(wrongSampleResourceGeneration.Start(plan)),
            "sample-resource-generation-mismatch fixture plan must start");
        auto wrongResourceSamples = complete;
        ++wrongResourceSamples[0].resourceGeneration;
        tests.Expect(wrongSampleResourceGeneration.SubmitFrame(MakeFrameInput(
                1u, 0u, wrongResourceSamples, plan)).error
                == EvidenceError::GenerationMismatch,
            "a sample/frame resource generation mismatch must fail explicitly");

        std::vector<EvidenceSample> wrongSampleGeneration(complete.begin(), complete.end());
        wrongSampleGeneration[0].configGeneration += 1u;
        const ScopeStatisticsResult badStatistics = ComputeScopeStatistics(
            wrongSampleGeneration, EvidenceSampleKind::Timing, plan);
        tests.Expect(badStatistics.error == EvidenceError::GenerationMismatch,
            "statistics must reject a sample from another config generation");

        std::vector<EvidenceSample> wrongResourceGeneration(
            complete.begin(), complete.end());
        ++wrongResourceGeneration[1].resourceGeneration;
        tests.Expect(ComputeScopeStatistics(
                wrongResourceGeneration,
                EvidenceSampleKind::Counter,
                plan).error == EvidenceError::GenerationMismatch,
            "statistics must reject a sample from another resource generation");

        BenchmarkSequence mismatchedSampleIdentity;
        tests.Expect(static_cast<bool>(mismatchedSampleIdentity.Start(plan)),
            "sample-identity-mismatch fixture plan must start");
        auto mismatchedSamples = complete;
        ++mismatchedSamples[1].sampleIndex;
        tests.Expect(mismatchedSampleIdentity.SubmitFrame(
                MakeFrameInput(1u, 0u, mismatchedSamples, plan)).error
                == EvidenceError::SampleIdentityMismatch,
            "metrics in one frame must share one explicit sample identity");

        BenchmarkPlan repeatedPlan = plan;
        repeatedPlan.repeatCount = 2u;
        BenchmarkSequence reusedSampleSequence;
        tests.Expect(static_cast<bool>(reusedSampleSequence.Start(repeatedPlan)),
            "cross-repeat sample-identity fixture plan must start");
        const auto firstRepeat = MakeFrameSamples(
            1.0, 10.0, 1u, 5u, 0u, repeatedPlan);
        tests.Expect(static_cast<bool>(reusedSampleSequence.SubmitFrame(
                MakeFrameInput(1u, 0u, firstRepeat, repeatedPlan)))
            && static_cast<bool>(reusedSampleSequence.BeginNextRepeat()),
            "cross-repeat sample-identity fixture must reach its repeat boundary");
        const auto secondRepeat = MakeFrameSamples(
            2.0, 20.0, 2u, 5u, 1u, repeatedPlan);
        tests.Expect(reusedSampleSequence.SubmitFrame(MakeFrameInput(
                2u, 1u, secondRepeat, repeatedPlan)).error
                == EvidenceError::SampleIdentityMismatch,
            "BenchmarkSequence must reject a sample index reused across repeats");

        std::vector<EvidenceSample> missingStatistics(complete.begin(), complete.end());
        missingStatistics.erase(missingStatistics.begin());
        const ScopeStatisticsResult incompleteStatistics = ComputeScopeStatistics(
            missingStatistics, EvidenceSampleKind::Timing, plan);
        tests.Expect(incompleteStatistics.error == EvidenceError::MissingSample,
            "statistics must reject a required scope with no samples");
    }

    void TestLinearImageComparison(TestContext& tests)
    {
        using namespace RenderingEngine::Demos;

        const std::array<float, 4> reference = { 0.0f, 0.25f, 0.5f, 1.0f };
        const LinearRgbaImageView referenceView = { 1u, 1u, reference };
        const ImageComparisonResult identical =
            CompareLinearRgba(referenceView, referenceView);
        tests.Expect(static_cast<bool>(identical)
            && identical.comparedComponentCount == 4u
            && identical.rmse == 0.0
            && identical.maximumAbsoluteError == 0.0
            && identical.psnrIsPositiveInfinity
            && std::isinf(identical.psnr)
            && identical.psnr > 0.0,
            "identical linear RGBA images must report zero error and flagged +infinite PSNR");

        const std::array<float, 4> candidate = { 1.0f, 0.25f, 0.5f, 1.0f };
        const ImageComparisonResult knownError = CompareLinearRgba(
            { 1u, 1u, candidate }, referenceView);
        constexpr double expectedPsnr = 6.020599913279624;
        tests.Expect(static_cast<bool>(knownError)
            && knownError.rmse == 0.5
            && knownError.maximumAbsoluteError == 1.0
            && !knownError.psnrIsPositiveInfinity
            && std::abs(knownError.psnr - expectedPsnr) < 1.0e-12,
            "linear RGBA RMSE, PSNR, and maximum absolute error must match a known fixture");

        const std::array<float, 8> larger = {
            0.0f, 0.25f, 0.5f, 1.0f,
            0.0f, 0.25f, 0.5f, 1.0f
        };
        const ImageComparisonResult extentMismatch = CompareLinearRgba(
            { 2u, 1u, larger }, referenceView);
        tests.Expect(extentMismatch.error == EvidenceError::ExtentMismatch,
            "image comparison must reject mismatched extents explicitly");

        std::array<float, 4> nonFinite = reference;
        nonFinite[2] = std::numeric_limits<float>::quiet_NaN();
        const ImageComparisonResult nonFiniteResult = CompareLinearRgba(
            { 1u, 1u, nonFinite }, referenceView);
        tests.Expect(nonFiniteResult.error == EvidenceError::NonFiniteValue,
            "image comparison must reject non-finite linear components explicitly");

        const ImageComparisonResult badSpan = CompareLinearRgba(
            { 1u, 1u, std::span<const float>(candidate).first(3u) }, referenceView);
        tests.Expect(badSpan.error == EvidenceError::InvalidImage,
            "image comparison must reject RGBA spans that do not match their extent");
    }
}

bool RunShowcaseEvidenceTests(std::ostream& output)
{
    TestContext tests(output);
    TestBenchmarkStateMachine(tests);
    TestCanonicalPlanAndMeasurementMethods(tests);
    TestExplicitBenchmarkFailures(tests);
    TestLinearImageComparison(tests);
    if (tests.Passed())
    {
        output << "L10 showcase evidence tests passed.\n";
    }
    return tests.Passed();
}
