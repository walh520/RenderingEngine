#pragma once

#include "ui/RuntimeConfigHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Ui
{
    // Provenance is data supplied by a provider, not an inference made by the
    // UI.  SyntheticTest must never be presented as live renderer evidence.
    enum class TelemetryProvenance : std::uint8_t
    {
        LiveRuntime,
        ImportedArtifact,
        SyntheticTest
    };

    enum class TelemetryAvailability : std::uint8_t
    {
        Unavailable,
        Pending,
        Fresh,
        Stale,
        Invalid
    };

    enum class MetricDomain : std::uint8_t
    {
        Cpu,
        Gpu,
        Traversal,
        Integrator,
        Reconstruction,
        Memory,
        Quality,
        Application
    };

    enum class MetricUnit : std::uint8_t
    {
        Nanoseconds,
        Microseconds,
        Milliseconds,
        Seconds,
        Count,
        Bytes,
        RaysPerSecond,
        Percentage,
        Ratio
    };

    struct MetricDescriptor
    {
        std::string stableId;
        std::string label;
        MetricDomain domain = MetricDomain::Application;
        MetricUnit unit = MetricUnit::Count;
        std::string source;
    };

    struct MetricObservation
    {
        MetricDescriptor descriptor;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::optional<double> value;
        std::string reason;
    };

    struct DebugResourceExtent
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;
    };

    // The token is an opaque lookup key owned by the provider.  This catalog
    // deliberately contains no native graphics resource or lifetime handle.
    struct DebugResourceDescriptor
    {
        std::string stableId;
        std::string label;
        std::string opaqueUiToken;
        std::string format;
        DebugResourceExtent extent;
        std::string legend;
        std::string owner;
    };

    struct DebugResourceObservation
    {
        DebugResourceDescriptor descriptor;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::string reason;
    };

    struct DebugProfilerSnapshot
    {
        std::string providerId;
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::uint64_t frameGeneration = 0;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string reason;
        std::vector<MetricObservation> metrics;
        std::vector<DebugResourceObservation> debugResources;
    };

    class IDebugProfilerProvider
    {
    public:
        virtual ~IDebugProfilerProvider() = default;

        // Implementations may read live counters, deserialize an artifact, or
        // supply a deterministic test fixture.  The model never fabricates a
        // metric or debug resource when this provider has no data.
        [[nodiscard]] virtual DebugProfilerSnapshot ReadSnapshot() = 0;
    };

    struct TelemetryFrameContext
    {
        std::uint64_t currentFrameGeneration = 0;
        std::uint64_t currentConfigGeneration = 0;
        std::uint64_t maximumFreshFrameLag = 0;
        std::uint64_t currentSceneGeneration = 0;
        std::uint64_t currentResourceGeneration = 0;
    };

    struct RollingStatistics
    {
        std::size_t sampleCount = 0;
        std::optional<double> latest;
        std::optional<double> minimum;
        std::optional<double> maximum;
        std::optional<double> median;
        std::optional<double> percentile95;
    };

    struct MetricView
    {
        MetricDescriptor descriptor;
        std::string providerId;
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::uint64_t frameGeneration = 0;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::optional<double> currentValue;
        std::string reason;
        RollingStatistics rolling;
    };

    struct DebugResourceView
    {
        DebugResourceDescriptor descriptor;
        std::string providerId;
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::uint64_t frameGeneration = 0;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string reason;
    };

    struct ProviderView
    {
        std::string providerId;
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::uint64_t frameGeneration = 0;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string reason;
        bool hasAcceptedGeneration = false;
    };

    enum class TelemetryUpdateCode : std::uint8_t
    {
        Accepted,
        RejectedInvalidSnapshot,
        RejectedDuplicateStableId,
        RejectedOldGeneration
    };

    struct TelemetryUpdateResult
    {
        TelemetryUpdateCode code = TelemetryUpdateCode::RejectedInvalidSnapshot;
        std::string providerId;
        std::string reason;

        [[nodiscard]] bool Accepted() const noexcept
        {
            return code == TelemetryUpdateCode::Accepted;
        }
    };

    class DebugProfilerModel final
    {
    public:
        explicit DebugProfilerModel(std::size_t rollingWindowCapacity = 120);

        [[nodiscard]] TelemetryUpdateResult Refresh(
            IDebugProfilerProvider& provider,
            const TelemetryFrameContext& context);

        // Called at the same fixed frame point that commits RuntimeConfig.
        // Rows from older generations lose current values/UI tokens immediately
        // so delayed providers cannot display old data as part of a new tuple.
        void InvalidateBeforeConfigGeneration(std::uint64_t configGeneration);
        void InvalidateBeforeGenerationTuple(
            std::uint64_t configGeneration,
            std::uint64_t sceneGeneration,
            std::uint64_t resourceGeneration);

        // ResetResource::ProfilerStatistics (P) invalidates all current metric
        // values and empties every rolling series.  Debug-resource catalog
        // entries are unaffected because the UI does not own those resources.
        void ApplyReset(ResetMask resetMask);

        [[nodiscard]] std::span<const MetricView> Metrics() const noexcept;
        [[nodiscard]] std::span<const DebugResourceView> DebugResources() const noexcept;
        [[nodiscard]] std::span<const ProviderView> Providers() const noexcept;

        [[nodiscard]] const MetricView* FindMetric(std::string_view stableId) const noexcept;
        [[nodiscard]] const DebugResourceView* FindDebugResource(
            std::string_view stableId) const noexcept;
        [[nodiscard]] const ProviderView* FindProvider(
            std::string_view providerId) const noexcept;

    private:
        struct RollingSeries
        {
            std::string stableId;
            std::deque<double> samples;
            std::uint64_t lastFrameGeneration = 0;
            std::uint64_t lastConfigGeneration = 0;
            bool hasGeneration = false;
        };

        std::size_t rollingWindowCapacity_ = 120;
        std::vector<MetricView> metrics_;
        std::vector<DebugResourceView> debugResources_;
        std::vector<ProviderView> providers_;
        std::vector<RollingSeries> rollingSeries_;
    };
}
