#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace RenderingEngine::Demos
{
    inline constexpr std::size_t kManyLightsWave4PresetCount = 3u;
    inline constexpr std::size_t kManyLightsWave4LegCount = 4u;
    inline constexpr std::uint32_t kManyLightsWave4RealtimeWarmupFrames = 120u;
    inline constexpr std::uint32_t kManyLightsWave4RealtimeMeasurementFrames = 1'000u;
    inline constexpr std::uint32_t kManyLightsWave4RealtimeMinimumRepeats = 3u;

    enum class ManyLightsWave4PresetId : std::uint8_t
    {
        Lights100,
        Lights1000,
        Lights10000
    };

    struct ManyLightsWave4Preset final
    {
        ManyLightsWave4PresetId id = ManyLightsWave4PresetId::Lights100;
        std::string stableToken;
        std::string label;
        std::uint32_t lightCount = 0u;
    };

    [[nodiscard]] std::span<const ManyLightsWave4Preset> GetManyLightsWave4Presets() noexcept;

    enum class ManyLightsWave4Technique : std::uint8_t
    {
        Uniform,
        PowerWeighted,
        RestirDi,
        HighSppReference
    };

    enum class ManyLightsWave4BiasMode : std::uint8_t
    {
        Biased,
        ReferenceCorrection,
        NotApplicable
    };

    struct ManyLightsWave4LegDefinition final
    {
        ManyLightsWave4Technique technique = ManyLightsWave4Technique::Uniform;
        ManyLightsWave4BiasMode bias = ManyLightsWave4BiasMode::NotApplicable;
        std::string stableToken;
        std::string label;
        bool highSppReference = false;
    };

    [[nodiscard]] std::span<const ManyLightsWave4LegDefinition>
        GetManyLightsWave4LegCatalog(ManyLightsWave4BiasMode restirBias) noexcept;

    struct ManyLightsWave4GenerationTuple final
    {
        std::optional<std::uint64_t> frameGeneration;
        std::optional<std::uint64_t> configGeneration;
        std::optional<std::uint64_t> sceneGeneration;
        std::optional<std::uint64_t> resourceGeneration;
        std::optional<std::uint64_t> lightGeneration;
    };

    struct ManyLightsWave4FixedConditions final
    {
        std::string sceneFingerprint;
        std::string assetFingerprint;
        std::string configFingerprint;
        std::string machineFingerprint;
        std::string driverFingerprint;
        std::string powerProfileToken;
        std::string cameraPresetToken;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::optional<std::uint64_t> baseSeed;
        std::optional<std::uint64_t> frameIndex;
        ManyLightsWave4GenerationTuple generation;
    };

    struct ManyLightsWave4Budget final
    {
        std::string identityToken;
        std::optional<std::uint64_t> candidateCount;
        std::optional<std::uint64_t> visibilityRayCount;
    };

    struct ManyLightsWave4ComparisonRequest final
    {
        ManyLightsWave4Preset preset;
        ManyLightsWave4FixedConditions fixed;
        ManyLightsWave4BiasMode restirBias = ManyLightsWave4BiasMode::Biased;
        // Shared by Uniform, Power-weighted, and ReSTIR DI.
        ManyLightsWave4Budget budget;
        // High-SPP reference retains a separate, explicitly named budget.
        ManyLightsWave4Budget referenceBudget;
        std::array<ManyLightsWave4LegDefinition, kManyLightsWave4LegCount> legs{};
    };

    enum class ManyLightsWave4ValidationCode : std::uint8_t
    {
        Accepted,
        MissingField,
        InvalidPreset,
        InvalidLegCatalog,
        InvalidMeasurement,
        InvalidProvenance,
        DuplicateLeg,
        BudgetMismatch,
        GenerationMismatch,
        LightCountMismatch,
        SyntheticProvider,
        UnavailableProvider,
        StaleProvider
    };

    struct ManyLightsWave4ValidationStatus
    {
        ManyLightsWave4ValidationCode code = ManyLightsWave4ValidationCode::Accepted;
        std::string reason;

        [[nodiscard]] bool Accepted() const noexcept
        {
            return code == ManyLightsWave4ValidationCode::Accepted;
        }
    };

    struct ManyLightsWave4RequestResult final : ManyLightsWave4ValidationStatus
    {
        ManyLightsWave4ComparisonRequest request;
    };

    [[nodiscard]] ManyLightsWave4RequestResult BuildManyLightsWave4ComparisonRequest(
        const ManyLightsWave4Preset& preset,
        ManyLightsWave4FixedConditions fixed,
        ManyLightsWave4BiasMode restirBias,
        ManyLightsWave4Budget budget,
        ManyLightsWave4Budget referenceBudget);

    enum class ManyLightsWave4SnapshotState : std::uint8_t
    {
        Unavailable,
        Pending,
        Fresh,
        Stale,
        Invalid
    };

    enum class ManyLightsWave4ProvenanceSource : std::uint8_t
    {
        LiveRuntime,
        ImportedArtifact,
        SyntheticTest
    };

    struct ManyLightsWave4Provenance final
    {
        ManyLightsWave4ProvenanceSource source =
            ManyLightsWave4ProvenanceSource::LiveRuntime;
        std::string providerId;
        std::string detail;
    };

    struct ManyLightsWave4ReservoirObservation final
    {
        std::optional<std::uint64_t> m;
        std::optional<double> weight;
        std::optional<std::uint64_t> lightId;
        std::string source;
        std::optional<std::string> reuseSource;
        std::optional<std::string> rejectionReason;
    };

    enum class ManyLightsWave4TimingSource : std::uint8_t
    {
        VulkanGpuTimestamp,
        CpuWallClock
    };

    struct ManyLightsWave4PerformanceObservation final
    {
        std::optional<double> elapsedMilliseconds;
        std::optional<ManyLightsWave4TimingSource> timingSource;
        std::optional<std::uint64_t> observedCandidates;
        std::optional<std::uint64_t> visibilityRays;
        std::optional<std::uint64_t> memoryBytes;
    };

    struct ManyLightsWave4QualityObservation final
    {
        std::optional<double> mae;
        std::optional<double> rmse;
        std::optional<double> psnr;
    };

    struct ManyLightsWave4BenchmarkObservation final
    {
        std::optional<std::uint32_t> warmupFrameCount;
        std::optional<std::uint32_t> measurementFrameCount;
        std::optional<std::uint32_t> repeatCount;
        std::optional<double> medianMilliseconds;
        std::optional<double> p95Milliseconds;
        std::optional<ManyLightsWave4TimingSource> timingSource;
    };

    struct ManyLightsWave4LegSnapshot final
    {
        ManyLightsWave4LegDefinition leg;
        ManyLightsWave4SnapshotState state = ManyLightsWave4SnapshotState::Unavailable;
        std::string reason;
        std::uint32_t lightCount = 0u;
        ManyLightsWave4FixedConditions fixed;
        ManyLightsWave4Budget budget;
        ManyLightsWave4ReservoirObservation reservoir;
        ManyLightsWave4PerformanceObservation performance;
        ManyLightsWave4QualityObservation quality;
        ManyLightsWave4BenchmarkObservation benchmark;
    };

    struct ManyLightsWave4ProviderSnapshot final
    {
        std::string providerId;
        ManyLightsWave4Provenance provenance;
        ManyLightsWave4SnapshotState state = ManyLightsWave4SnapshotState::Unavailable;
        std::string presetToken;
        std::uint32_t lightCount = 0u;
        ManyLightsWave4FixedConditions fixed;
        ManyLightsWave4BiasMode restirBias = ManyLightsWave4BiasMode::Biased;
        ManyLightsWave4Budget budget;
        ManyLightsWave4Budget referenceBudget;
        std::array<ManyLightsWave4LegSnapshot, kManyLightsWave4LegCount> legs{};
        std::string reason;
    };

    [[nodiscard]] ManyLightsWave4ValidationStatus ValidateManyLightsWave4ProviderSnapshot(
        const ManyLightsWave4ProviderSnapshot& snapshot);

    struct ManyLightsWave4ComparisonResult final : ManyLightsWave4ValidationStatus
    {
        ManyLightsWave4ComparisonRequest request;
        ManyLightsWave4ProviderSnapshot snapshot;
    };

    [[nodiscard]] ManyLightsWave4ComparisonResult BuildManyLightsWave4Comparison(
        const ManyLightsWave4ComparisonRequest& request,
        const ManyLightsWave4ProviderSnapshot& snapshot);

    struct ManyLightsWave4SerializedResult final : ManyLightsWave4ValidationStatus
    {
        std::string text;
    };

    [[nodiscard]] ManyLightsWave4SerializedResult BuildManyLightsWave4Json(
        const ManyLightsWave4ComparisonResult& comparison);
    [[nodiscard]] ManyLightsWave4SerializedResult BuildManyLightsWave4Csv(
        const ManyLightsWave4ComparisonResult& comparison);

    [[nodiscard]] std::string_view ManyLightsWave4TechniqueToken(
        ManyLightsWave4Technique technique) noexcept;
    [[nodiscard]] std::string_view ManyLightsWave4BiasToken(
        ManyLightsWave4BiasMode bias) noexcept;
    [[nodiscard]] std::string_view ManyLightsWave4SnapshotStateToken(
        ManyLightsWave4SnapshotState state) noexcept;
    [[nodiscard]] std::string_view ManyLightsWave4ProvenanceToken(
        ManyLightsWave4ProvenanceSource source) noexcept;
    [[nodiscard]] std::string_view ManyLightsWave4TimingSourceToken(
        ManyLightsWave4TimingSource source) noexcept;
}
