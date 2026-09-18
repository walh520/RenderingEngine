#include "ui/Wave4TelemetryAdapter.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace RenderingEngine::Ui
{
    namespace
    {
        [[nodiscard]] TelemetryAvailability ToTelemetryAvailability(
            const Demos::ManyLightsWave4SnapshotState state) noexcept
        {
            switch (state)
            {
            case Demos::ManyLightsWave4SnapshotState::Unavailable:
                return TelemetryAvailability::Unavailable;
            case Demos::ManyLightsWave4SnapshotState::Pending:
                return TelemetryAvailability::Pending;
            case Demos::ManyLightsWave4SnapshotState::Fresh:
                return TelemetryAvailability::Fresh;
            case Demos::ManyLightsWave4SnapshotState::Stale:
                return TelemetryAvailability::Stale;
            case Demos::ManyLightsWave4SnapshotState::Invalid:
                return TelemetryAvailability::Invalid;
            }
            return TelemetryAvailability::Invalid;
        }

        [[nodiscard]] TelemetryProvenance ToTelemetryProvenance(
            const Demos::ManyLightsWave4ProvenanceSource source) noexcept
        {
            switch (source)
            {
            case Demos::ManyLightsWave4ProvenanceSource::LiveRuntime:
                return TelemetryProvenance::LiveRuntime;
            case Demos::ManyLightsWave4ProvenanceSource::ImportedArtifact:
                return TelemetryProvenance::ImportedArtifact;
            case Demos::ManyLightsWave4ProvenanceSource::SyntheticTest:
                return TelemetryProvenance::SyntheticTest;
            }
            return TelemetryProvenance::SyntheticTest;
        }

        [[nodiscard]] bool IsGenerationRegression(
            const Demos::ManyLightsWave4GenerationTuple& previous,
            const Demos::ManyLightsWave4GenerationTuple& current) noexcept
        {
            return *current.frameGeneration < *previous.frameGeneration
                || *current.configGeneration < *previous.configGeneration
                || *current.sceneGeneration < *previous.sceneGeneration
                || *current.resourceGeneration < *previous.resourceGeneration
                || *current.lightGeneration < *previous.lightGeneration;
        }

        [[nodiscard]] std::string MetricSource(
            const std::string_view providerId,
            const std::string_view source)
        {
            std::string result(providerId);
            result.push_back(':');
            result.append(source);
            return result;
        }

        [[nodiscard]] MetricObservation MakeMetric(
            const std::string_view stableId,
            const std::string_view label,
            const MetricDomain domain,
            const MetricUnit unit,
            const std::string_view providerId,
            const std::string_view source,
            const TelemetryAvailability availability,
            const std::optional<double> value,
            const std::string_view reason)
        {
            MetricObservation result;
            result.descriptor.stableId = std::string(stableId);
            result.descriptor.label = std::string(label);
            result.descriptor.domain = domain;
            result.descriptor.unit = unit;
            result.descriptor.source = MetricSource(providerId, source);
            result.availability = availability;
            result.value = value;
            result.reason = std::string(reason);
            return result;
        }

        [[nodiscard]] std::string LegMetricId(
            const Demos::ManyLightsWave4ProviderSnapshot& snapshot,
            const Demos::ManyLightsWave4LegSnapshot& leg,
            const std::string_view suffix)
        {
            std::string result = "wave4.";
            result.append(snapshot.presetToken);
            result.push_back('.');
            result.append(leg.leg.stableToken);
            result.push_back('.');
            result.append(suffix);
            return result;
        }

        [[nodiscard]] std::string LegMetricLabel(
            const Demos::ManyLightsWave4LegSnapshot& leg,
            const std::string_view suffix)
        {
            std::string result = "Many Lights ";
            result.append(leg.leg.label);
            result.push_back(' ');
            result.append(suffix);
            return result;
        }

        void AppendLegMetrics(
            DebugProfilerSnapshot& result,
            const Demos::ManyLightsWave4ProviderSnapshot& snapshot,
            const Demos::ManyLightsWave4LegSnapshot& leg)
        {
            const TelemetryAvailability availability = ToTelemetryAvailability(leg.state);
            const std::string& providerId = snapshot.providerId;
            const std::string reason = leg.reason.empty()
                ? snapshot.reason
                : leg.reason;
            const auto id = [&snapshot, &leg](const std::string_view suffix)
            {
                return LegMetricId(snapshot, leg, suffix);
            };
            const auto label = [&leg](const std::string_view suffix)
            {
                return LegMetricLabel(leg, suffix);
            };
            const auto source = [&leg](const std::string_view suffix)
            {
                std::string value(leg.leg.stableToken);
                value.push_back('.');
                value.append(suffix);
                return value;
            };

            const std::optional<double> visibilityRays = leg.performance.visibilityRays.has_value()
                ? std::optional<double>(static_cast<double>(*leg.performance.visibilityRays))
                : std::nullopt;
            const std::optional<double> observedCandidates =
                leg.performance.observedCandidates.has_value()
                    ? std::optional<double>(static_cast<double>(
                        *leg.performance.observedCandidates))
                    : std::nullopt;
            const std::optional<double> memoryBytes = leg.performance.memoryBytes.has_value()
                ? std::optional<double>(static_cast<double>(*leg.performance.memoryBytes))
                : std::nullopt;
            const std::optional<double> warmup = leg.benchmark.warmupFrameCount.has_value()
                ? std::optional<double>(static_cast<double>(*leg.benchmark.warmupFrameCount))
                : std::nullopt;
            const std::optional<double> measurement = leg.benchmark.measurementFrameCount.has_value()
                ? std::optional<double>(static_cast<double>(*leg.benchmark.measurementFrameCount))
                : std::nullopt;
            const std::optional<double> repeats = leg.benchmark.repeatCount.has_value()
                ? std::optional<double>(static_cast<double>(*leg.benchmark.repeatCount))
                : std::nullopt;

            if (leg.leg.technique == Demos::ManyLightsWave4Technique::RestirDi)
            {
                const std::optional<double> m = leg.reservoir.m.has_value()
                    ? std::optional<double>(static_cast<double>(*leg.reservoir.m))
                    : std::nullopt;
                const std::optional<double> lightId = leg.reservoir.lightId.has_value()
                    ? std::optional<double>(static_cast<double>(*leg.reservoir.lightId))
                    : std::nullopt;
                result.metrics.push_back(MakeMetric(
                    id("reservoir-m"), label("reservoir M"), MetricDomain::PathTransport,
                    MetricUnit::Count, providerId, source("reservoir.m"), availability, m, reason));
                result.metrics.push_back(MakeMetric(
                    id("reservoir-weight"), label("reservoir weight"), MetricDomain::PathTransport,
                    MetricUnit::Ratio, providerId, source("reservoir.weight"), availability,
                    leg.reservoir.weight, reason));
                result.metrics.push_back(MakeMetric(
                    id("reservoir-light-id"), label("reservoir light ID"), MetricDomain::PathTransport,
                    MetricUnit::Count, providerId, source("reservoir.lightId"), availability,
                    lightId, reason));
            }
            const bool gpuTiming = leg.performance.timingSource
                == Demos::ManyLightsWave4TimingSource::VulkanGpuTimestamp;
            result.metrics.push_back(MakeMetric(
                id("elapsed-ms"), label(gpuTiming ? "GPU ms" : "CPU ms"),
                gpuTiming ? MetricDomain::Gpu : MetricDomain::Application,
                MetricUnit::Milliseconds, providerId,
                source(gpuTiming ? "performance.vulkanGpuTimestamp"
                                 : "performance.cpuWallClock"),
                availability, leg.performance.elapsedMilliseconds, reason));
            result.metrics.push_back(MakeMetric(
                id("observed-candidates"), label("observed candidates"),
                MetricDomain::PathTransport, MetricUnit::Count, providerId,
                source("performance.observedCandidates"), availability,
                observedCandidates, reason));
            result.metrics.push_back(MakeMetric(
                id("visibility-rays"), label("visibility rays"), MetricDomain::Traversal,
                MetricUnit::Count, providerId, source("performance.visibilityRays"),
                availability, visibilityRays, reason));
            result.metrics.push_back(MakeMetric(
                id("memory-bytes"), label("memory bytes"), MetricDomain::Memory,
                MetricUnit::Bytes, providerId, source("performance.memoryBytes"),
                availability, memoryBytes, reason));
            if (leg.leg.technique
                != Demos::ManyLightsWave4Technique::HighSppReference)
            {
                result.metrics.push_back(MakeMetric(
                    id("mae"), label("MAE"), MetricDomain::Quality, MetricUnit::Ratio,
                    providerId, source("quality.mae"), availability, leg.quality.mae, reason));
                result.metrics.push_back(MakeMetric(
                    id("rmse"), label("RMSE"), MetricDomain::Quality, MetricUnit::Ratio,
                    providerId, source("quality.rmse"), availability, leg.quality.rmse, reason));
                result.metrics.push_back(MakeMetric(
                    id("psnr"), label("PSNR"), MetricDomain::Quality, MetricUnit::Ratio,
                    providerId, source("quality.psnr"), availability, leg.quality.psnr, reason));
            }
            result.metrics.push_back(MakeMetric(
                id("warmup-frames"), label("warm-up frames"), MetricDomain::Application,
                MetricUnit::Count, providerId, source("benchmark.warmupFrameCount"),
                availability, warmup, reason));
            result.metrics.push_back(MakeMetric(
                id("measurement-frames"), label("measurement frames"), MetricDomain::Application,
                MetricUnit::Count, providerId, source("benchmark.measurementFrameCount"),
                availability, measurement, reason));
            result.metrics.push_back(MakeMetric(
                id("repeat-count"), label("repeat count"), MetricDomain::Application,
                MetricUnit::Count, providerId, source("benchmark.repeatCount"),
                availability, repeats, reason));
            result.metrics.push_back(MakeMetric(
                id("median-ms"), label(gpuTiming ? "median GPU ms" : "median CPU ms"),
                gpuTiming ? MetricDomain::Gpu : MetricDomain::Application,
                MetricUnit::Milliseconds, providerId, source("benchmark.medianMilliseconds"),
                availability, leg.benchmark.medianMilliseconds, reason));
            result.metrics.push_back(MakeMetric(
                id("p95-ms"), label(gpuTiming ? "P95 GPU ms" : "P95 CPU ms"),
                gpuTiming ? MetricDomain::Gpu : MetricDomain::Application,
                MetricUnit::Milliseconds, providerId, source("benchmark.p95Milliseconds"),
                availability, leg.benchmark.p95Milliseconds, reason));
        }
    }

    Wave4TelemetryAdapter::Wave4TelemetryAdapter(std::string providerId)
        : providerId_(std::move(providerId))
    {
    }

    Wave4PublishResult Wave4TelemetryAdapter::Publish(
        Demos::ManyLightsWave4ProviderSnapshot snapshot)
    {
        if (providerId_.empty())
        {
            return {
                Wave4PublishCode::RejectedInvalidSnapshot,
                "Wave 4 adapter provider ID must not be empty"
            };
        }
        if (snapshot.providerId != providerId_
            || snapshot.provenance.providerId != providerId_)
        {
            return {
                Wave4PublishCode::RejectedInvalidSnapshot,
                "Wave 4 adapter, snapshot, and provenance provider IDs must match"
            };
        }
        const Demos::ManyLightsWave4ValidationStatus validation =
            Demos::ValidateManyLightsWave4ProviderSnapshot(snapshot);
        if (!validation.Accepted())
        {
            return {
                validation.code == Demos::ManyLightsWave4ValidationCode::SyntheticProvider
                    ? Wave4PublishCode::RejectedSyntheticProvider
                    : Wave4PublishCode::RejectedInvalidSnapshot,
                validation.reason
            };
        }
        if (latestSnapshot_.has_value()
            && IsGenerationRegression(
                latestSnapshot_->fixed.generation,
                snapshot.fixed.generation))
        {
            return {
                Wave4PublishCode::RejectedGenerationRegression,
                "Wave 4 provider generation regressed behind the last published snapshot"
            };
        }
        latestSnapshot_ = std::move(snapshot);
        return { Wave4PublishCode::Accepted, {} };
    }

    void Wave4TelemetryAdapter::Clear() noexcept
    {
        latestSnapshot_.reset();
    }

    std::string_view Wave4TelemetryAdapter::ProviderId() const noexcept
    {
        return providerId_;
    }

    const Demos::ManyLightsWave4ProviderSnapshot*
        Wave4TelemetryAdapter::LatestSnapshot() const noexcept
    {
        return latestSnapshot_.has_value() ? &*latestSnapshot_ : nullptr;
    }

    DebugProfilerSnapshot Wave4TelemetryAdapter::ReadSnapshot()
    {
        DebugProfilerSnapshot result;
        result.providerId = providerId_;
        if (!latestSnapshot_.has_value())
        {
            result.provenance = TelemetryProvenance::LiveRuntime;
            result.availability = TelemetryAvailability::Unavailable;
            result.reason = "no Many Lights Wave 4 provider snapshot has been published";
            return result;
        }

        const Demos::ManyLightsWave4ProviderSnapshot& snapshot = *latestSnapshot_;
        result.provenance = ToTelemetryProvenance(snapshot.provenance.source);
        result.availability = ToTelemetryAvailability(snapshot.state);
        result.reason = snapshot.reason;
        result.frameGeneration = *snapshot.fixed.generation.frameGeneration;
        result.configGeneration = *snapshot.fixed.generation.configGeneration;
        result.sceneGeneration = *snapshot.fixed.generation.sceneGeneration;
        result.resourceGeneration = *snapshot.fixed.generation.resourceGeneration;
        if (snapshot.state == Demos::ManyLightsWave4SnapshotState::Fresh)
        {
            for (const Demos::ManyLightsWave4LegSnapshot& leg : snapshot.legs)
            {
                AppendLegMetrics(result, snapshot, leg);
            }
        }
        else if (result.reason.empty())
        {
            result.reason = snapshot.state == Demos::ManyLightsWave4SnapshotState::Stale
                ? "Many Lights Wave 4 telemetry is stale after a generation change"
                : "Many Lights Wave 4 telemetry is not currently fresh";
        }
        return result;
    }
}
