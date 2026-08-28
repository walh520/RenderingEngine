#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Demos
{
    enum class EvidenceError
    {
        None = 0,
        InvalidPlan,
        InvalidState,
        InvalidSample,
        InvalidProvenance,
        MissingSample,
        DuplicateSample,
        UnexpectedSample,
        NonFiniteValue,
        GenerationMismatch,
        ConditionsMismatch,
        FrameOrderMismatch,
        SampleIdentityMismatch,
        ExtentMismatch,
        InvalidImage
    };

    struct EvidenceStatus
    {
        EvidenceError error = EvidenceError::None;
        std::string message;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == EvidenceError::None;
        }
    };

    // Source describes provenance, not the measurement mechanism. In
    // particular, ProviderReported alone never implies a GPU timestamp.
    enum class EvidenceSource
    {
        CpuWallClock = 0,
        ProviderReported,
        SyntheticTest
    };

    [[nodiscard]] std::string_view EvidenceSourceToken(EvidenceSource source) noexcept;

    struct EvidenceProvenance
    {
        EvidenceSource source = EvidenceSource::ProviderReported;
        std::string provider;
        std::string detail;
    };

    struct EvidenceCondition
    {
        std::string name;
        std::string value;
    };

    enum class EvidenceSampleKind
    {
        Timing = 0,
        Counter
    };

    enum class EvidenceMeasurementMethod
    {
        Unspecified = 0,
        CpuWallClock,
        GpuTimestampQuery,
        ProviderCounter
    };

    [[nodiscard]] std::string_view EvidenceMeasurementMethodToken(
        EvidenceMeasurementMethod method) noexcept;

    struct EvidenceMetricRequirement
    {
        EvidenceSampleKind kind = EvidenceSampleKind::Timing;
        std::string scope;
        std::string unit;
        EvidenceMeasurementMethod measurementMethod =
            EvidenceMeasurementMethod::Unspecified;
    };

    // Each retained sample is self-describing. repeatIndex is zero-based and
    // conditions are compared as a set (their input order is not significant).
    struct EvidenceSample
    {
        EvidenceSampleKind kind = EvidenceSampleKind::Timing;
        std::string scope;
        std::string unit;
        double value = 0.0;
        EvidenceProvenance provenance;
        std::uint64_t frameIndex = 0;
        std::uint64_t configGeneration = 0;
        std::uint32_t repeatIndex = 0;
        std::vector<EvidenceCondition> conditions;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::uint64_t sampleIndex = 0;
    };

    struct BenchmarkPlan
    {
        std::uint32_t warmupFrameCount = 0;
        std::uint32_t measurementFrameCount = 0;
        std::uint32_t repeatCount = 0;
        std::uint64_t configGeneration = 0;
        std::vector<EvidenceCondition> conditions;
        std::vector<EvidenceMetricRequirement> requiredMetrics;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
    };

    inline constexpr std::uint32_t kCanonicalPortfolioWarmupFrameCount = 120u;
    inline constexpr std::uint32_t kCanonicalPortfolioMeasurementFrameCount = 1'000u;
    inline constexpr std::uint32_t kCanonicalPortfolioRepeatCount = 3u;

    // Applies the canonical portfolio cadence while requiring the caller to
    // name conditions and every measurement method explicitly.
    [[nodiscard]] BenchmarkPlan BuildCanonicalPortfolioBenchmarkPlan(
        std::uint64_t configGeneration,
        std::uint64_t sceneGeneration,
        std::uint64_t resourceGeneration,
        std::vector<EvidenceCondition> conditions,
        std::vector<EvidenceMetricRequirement> requiredMetrics);

    enum class BenchmarkPhase
    {
        Idle = 0,
        Warmup,
        Measurement,
        AwaitingRepeat,
        Complete,
        Failed
    };

    struct BenchmarkFrameInput
    {
        std::uint64_t frameIndex = 0;
        std::uint64_t configGeneration = 0;
        std::uint32_t repeatIndex = 0;
        std::span<const EvidenceSample> samples;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
    };

    // Provider-driven orchestration only: callers decide when a rendered frame
    // is ready and submit its measurements. The state machine owns no renderer,
    // timer, Vulkan query, or sleep loop.
    class BenchmarkSequence final
    {
    public:
        [[nodiscard]] EvidenceStatus Start(const BenchmarkPlan& plan);
        [[nodiscard]] EvidenceStatus SubmitFrame(const BenchmarkFrameInput& frame);
        [[nodiscard]] EvidenceStatus BeginNextRepeat();

        [[nodiscard]] BenchmarkPhase Phase() const noexcept;
        [[nodiscard]] std::uint32_t CurrentRepeatIndex() const noexcept;
        [[nodiscard]] std::uint32_t WarmupFramesAccepted() const noexcept;
        [[nodiscard]] std::uint32_t MeasurementFramesAccepted() const noexcept;
        [[nodiscard]] const BenchmarkPlan& Plan() const noexcept;
        [[nodiscard]] std::span<const EvidenceSample> Samples() const noexcept;
        [[nodiscard]] const EvidenceStatus& LastStatus() const noexcept;

    private:
        [[nodiscard]] EvidenceStatus Fail(EvidenceError error, std::string message);

        BenchmarkPlan m_plan;
        std::vector<EvidenceSample> m_samples;
        EvidenceStatus m_lastStatus;
        BenchmarkPhase m_phase = BenchmarkPhase::Idle;
        std::uint32_t m_currentRepeatIndex = 0;
        std::uint32_t m_warmupFramesAccepted = 0;
        std::uint32_t m_measurementFramesAccepted = 0;
        std::uint64_t m_lastFrameIndex = 0;
        bool m_hasLastFrameIndex = false;
        std::uint64_t m_lastSampleIndex = 0;
        bool m_hasLastSampleIndex = false;
    };

    struct ScopeStatistics
    {
        std::string scope;
        std::string unit;
        std::size_t sampleCount = 0;
        double minimum = 0.0;
        double maximum = 0.0;
        double median = 0.0;
        double p95NearestRank = 0.0;
        EvidenceMeasurementMethod measurementMethod =
            EvidenceMeasurementMethod::Unspecified;
        EvidenceProvenance provenance;
    };

    struct ScopeStatisticsResult : EvidenceStatus
    {
        std::vector<ScopeStatistics> scopes;
    };

    // The plan supplies the expected generation, conditions, scopes, and exact
    // per-scope count (measurementFrameCount * repeatCount). Standalone inputs
    // must contain every required metric on the same globally ordered set of
    // (repeat, frame, sample) identities, matching evidence emitted by
    // BenchmarkSequence. Frame and sample indices advance globally across
    // repeat boundaries.
    [[nodiscard]] ScopeStatisticsResult ComputeScopeStatistics(
        std::span<const EvidenceSample> samples,
        EvidenceSampleKind kind,
        const BenchmarkPlan& plan);

    struct CsvResult : EvidenceStatus
    {
        std::string text;
    };

    // Both functions emit RFC 4180-style CRLF rows, canonical numeric text,
    // canonical condition order, and a stable sample ordering.
    [[nodiscard]] CsvResult BuildTimingsCsv(
        std::span<const EvidenceSample> samples,
        const BenchmarkPlan& plan);
    [[nodiscard]] CsvResult BuildCountersCsv(
        std::span<const EvidenceSample> samples,
        const BenchmarkPlan& plan);

    struct LinearRgbaImageView
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::span<const float> rgba;
    };

    struct ImageComparisonResult : EvidenceStatus
    {
        std::size_t comparedComponentCount = 0;
        double rmse = 0.0;
        double psnr = 0.0;
        bool psnrIsPositiveInfinity = false;
        double maximumAbsoluteError = 0.0;
    };

    // Compares all four linear RGBA components. peakSignalValue is explicit so
    // HDR callers do not silently inherit an LDR range assumption.
    [[nodiscard]] ImageComparisonResult CompareLinearRgba(
        const LinearRgbaImageView& candidate,
        const LinearRgbaImageView& reference,
        double peakSignalValue = 1.0);
}
