#pragma once

#include "ui/DebugProfilerModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Ui
{
    // These inputs are deliberately UI-neutral.  L4/L5/L6 retain ownership of
    // their native counters, query pools and resources; composition copies only
    // resolved values and their availability into this typed boundary.
    struct Wave2CounterObservation final
    {
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::optional<std::uint64_t> value;
        std::string reason;
        std::string source;
    };

    struct Wave2DurationObservation final
    {
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::optional<double> milliseconds;
        std::string reason;
        std::string source;
    };

    struct Wave2L4Counters final
    {
        Wave2CounterObservation rays;
        Wave2CounterObservation hits;
        Wave2CounterObservation nodeTests;
        Wave2CounterObservation triangleTests;
        Wave2CounterObservation stackOverflows;
        Wave2CounterObservation invalidRays;
        Wave2CounterObservation invalidHits;
        Wave2CounterObservation leafVisits;
        Wave2CounterObservation accumulatedLeafPrimitives;
        Wave2CounterObservation maximumStackDepth;
        Wave2CounterObservation maximumLeafOccupancy;
    };

    struct Wave2L4Telemetry final
    {
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::string reason;
        Wave2L4Counters counters;
        Wave2DurationObservation buildMilliseconds;
        Wave2DurationObservation traceMilliseconds;
        std::vector<DebugResourceObservation> debugResources;
    };

    struct Wave2L5Counters final
    {
        // Ray Query does not expose software node/triangle counters.  These
        // fields therefore describe only values actually published by the
        // owning Ray Query integration (for example queue and hit readback).
        Wave2CounterObservation rays;
        Wave2CounterObservation hits;
        Wave2CounterObservation invalidRays;
        Wave2CounterObservation invalidHits;
        Wave2CounterObservation candidateTests;
    };

    struct Wave2L5Telemetry final
    {
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::string reason;
        Wave2L5Counters counters;
        Wave2DurationObservation accelerationStructureBuildMilliseconds;
        Wave2DurationObservation dispatchMilliseconds;
        Wave2DurationObservation traceMilliseconds;
        std::vector<DebugResourceObservation> debugResources;
    };

    // Keep this enum numerically aligned with the L6 megakernel counter ABI,
    // while avoiding a dependency from UI code onto the integrator module.
    enum class Wave2L6Counter : std::uint32_t
    {
        CameraRays = 0u,
        PathRays,
        ShadowRays,
        SurfaceHits,
        Misses,
        ValidLightSamples,
        InvalidLightSamples,
        OccludedLightSamples,
        CameraEmitterHits,
        DeltaEmitterHits,
        MisEmitterHits,
        RussianRouletteTests,
        RussianRouletteTerminations,
        MaximumDepthTerminations,
        ZeroPdf,
        NegativePdf,
        NonFinitePdf,
        NonFiniteBsdf,
        NonFiniteThroughput,
        NonFiniteRadiance,
        NegativeContribution,
        AliasFallbacks,
        InvalidMaterial,
        InvalidBsdfEvaluation,
        InvalidBsdfSample,
        InvalidFrame,
        Count
    };

    inline constexpr std::size_t kWave2L6CounterCount =
        static_cast<std::size_t>(Wave2L6Counter::Count);

    struct Wave2L6Telemetry final
    {
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::string reason;
        std::array<Wave2CounterObservation, kWave2L6CounterCount> counters;
        Wave2DurationObservation traceMilliseconds;
        std::vector<DebugResourceObservation> debugResources;
    };

    struct Wave2GenerationTuple final
    {
        std::uint64_t frameGeneration = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
    };

    struct Wave2TelemetryFrame final
    {
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        Wave2GenerationTuple generation;
        Wave2L4Telemetry flattenedSoftwareGpu;
        Wave2L5Telemetry hardwareRayQuery;
        Wave2L6Telemetry megakernel;
    };

    enum class Wave2PublishCode : std::uint8_t
    {
        Accepted,
        RejectedInvalidFrame,
        RejectedGenerationRegression
    };

    struct Wave2PublishResult final
    {
        Wave2PublishCode code = Wave2PublishCode::RejectedInvalidFrame;
        std::string reason;

        [[nodiscard]] bool Accepted() const noexcept
        {
            return code == Wave2PublishCode::Accepted;
        }
    };

    // Adapter from typed Wave 2 producer reports to the existing immutable
    // DebugProfilerSnapshot contract.  It never samples Vulkan and never
    // invents a value: a missing counter/timestamp remains Pending or
    // Unavailable in the derived snapshot.
    class Wave2TelemetryAdapter final : public IDebugProfilerProvider
    {
    public:
        explicit Wave2TelemetryAdapter(std::string providerId = "wave2.telemetry");

        [[nodiscard]] Wave2PublishResult Publish(Wave2TelemetryFrame frame);

        // A provider with no published frame is intentionally still usable by
        // the model; it reports Unavailable with a reason and generation zero.
        void Clear() noexcept;

        [[nodiscard]] std::string_view ProviderId() const noexcept;
        [[nodiscard]] DebugProfilerSnapshot ReadSnapshot() override;

    private:
        std::string providerId_;
        std::optional<Wave2TelemetryFrame> latestFrame_;
    };
}
