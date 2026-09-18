#include "ui/DebugProfilerModel.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace RenderingEngine::Ui
{
    namespace
    {
        [[nodiscard]] bool IsValidStableId(std::string_view value) noexcept
        {
            if (value.empty())
            {
                return false;
            }

            bool hasAlphaNumeric = false;
            for (const unsigned char character : value)
            {
                const bool alphaNumeric = (character >= 'a' && character <= 'z')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= '0' && character <= '9');
                const bool separator = character == '.' || character == '_'
                    || character == '-' || character == '/' || character == ':';
                if (!alphaNumeric && !separator)
                {
                    return false;
                }
                hasAlphaNumeric = hasAlphaNumeric || alphaNumeric;
            }
            return hasAlphaNumeric;
        }

        [[nodiscard]] bool IsValid(TelemetryProvenance value) noexcept
        {
            switch (value)
            {
            case TelemetryProvenance::LiveRuntime:
            case TelemetryProvenance::ImportedArtifact:
            case TelemetryProvenance::SyntheticTest:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsValid(TelemetryAvailability value) noexcept
        {
            switch (value)
            {
            case TelemetryAvailability::Unavailable:
            case TelemetryAvailability::Pending:
            case TelemetryAvailability::Fresh:
            case TelemetryAvailability::Stale:
            case TelemetryAvailability::Invalid:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsValid(MetricDomain value) noexcept
        {
            switch (value)
            {
            case MetricDomain::Cpu:
            case MetricDomain::Gpu:
            case MetricDomain::Traversal:
            case MetricDomain::PathTransport:
            case MetricDomain::Reconstruction:
            case MetricDomain::Memory:
            case MetricDomain::Quality:
            case MetricDomain::Application:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsValid(MetricUnit value) noexcept
        {
            switch (value)
            {
            case MetricUnit::Nanoseconds:
            case MetricUnit::Microseconds:
            case MetricUnit::Milliseconds:
            case MetricUnit::Seconds:
            case MetricUnit::Count:
            case MetricUnit::Bytes:
            case MetricUnit::RaysPerSecond:
            case MetricUnit::Percentage:
            case MetricUnit::Ratio:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsDuration(MetricUnit unit) noexcept
        {
            return unit == MetricUnit::Nanoseconds
                || unit == MetricUnit::Microseconds
                || unit == MetricUnit::Milliseconds
                || unit == MetricUnit::Seconds;
        }

        [[nodiscard]] bool HasMeasurement(TelemetryAvailability availability) noexcept
        {
            return availability == TelemetryAvailability::Fresh
                || availability == TelemetryAvailability::Stale;
        }

        [[nodiscard]] bool RequiresReason(TelemetryAvailability availability) noexcept
        {
            return availability != TelemetryAvailability::Fresh;
        }

        [[nodiscard]] std::string FrameLagReason(
            std::uint64_t snapshotFrame,
            std::uint64_t currentFrame)
        {
            std::ostringstream text;
            text << "provider snapshot frame generation " << snapshotFrame
                << " trails current frame generation " << currentFrame;
            return text.str();
        }

        [[nodiscard]] std::string ConfigGenerationReason(
            std::uint64_t snapshotGeneration,
            std::uint64_t currentGeneration)
        {
            std::ostringstream text;
            text << "provider config generation " << snapshotGeneration
                << " does not match current config generation " << currentGeneration;
            return text.str();
        }

        [[nodiscard]] std::string NamedGenerationReason(
            std::string_view name,
            std::uint64_t snapshotGeneration,
            std::uint64_t currentGeneration)
        {
            std::ostringstream text;
            text << "provider " << name << " generation " << snapshotGeneration
                << " does not match current " << name << " generation "
                << currentGeneration;
            return text.str();
        }

        [[nodiscard]] RollingStatistics ComputeStatistics(
            const std::deque<double>& samples)
        {
            RollingStatistics statistics;
            statistics.sampleCount = samples.size();
            if (samples.empty())
            {
                return statistics;
            }

            statistics.latest = samples.back();
            const auto [minimum, maximum] = std::minmax_element(samples.begin(), samples.end());
            statistics.minimum = *minimum;
            statistics.maximum = *maximum;

            std::vector<double> sorted(samples.begin(), samples.end());
            std::sort(sorted.begin(), sorted.end());
            const std::size_t middle = sorted.size() / 2u;
            statistics.median = sorted.size() % 2u == 0u
                ? std::midpoint(sorted[middle - 1u], sorted[middle])
                : sorted[middle];

            // Nearest-rank percentile: rank = ceil(0.95 * N), one based.
            const std::size_t rank = static_cast<std::size_t>(
                std::ceil(0.95 * static_cast<double>(sorted.size())));
            statistics.percentile95 = sorted[std::max<std::size_t>(1u, rank) - 1u];
            return statistics;
        }

        [[nodiscard]] bool SameMetricContract(
            const MetricDescriptor& left,
            const MetricDescriptor& right) noexcept
        {
            return left.stableId == right.stableId
                && left.domain == right.domain
                && left.unit == right.unit
                && left.source == right.source;
        }

        [[nodiscard]] std::string MissingReason(std::string_view kind)
        {
            std::string reason = "provider omitted ";
            reason.append(kind);
            reason.append(" from its latest accepted snapshot");
            return reason;
        }
    }

    DebugProfilerModel::DebugProfilerModel(std::size_t rollingWindowCapacity)
        : rollingWindowCapacity_(std::max<std::size_t>(1u, rollingWindowCapacity))
    {
    }

    TelemetryUpdateResult DebugProfilerModel::Refresh(
        IDebugProfilerProvider& provider,
        const TelemetryFrameContext& context)
    {
        DebugProfilerSnapshot snapshot;
        try
        {
            snapshot = provider.ReadSnapshot();
        }
        catch (const std::exception& exception)
        {
            return {
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                {},
                std::string("provider threw while reading telemetry: ") + exception.what()
            };
        }
        catch (...)
        {
            return {
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                {},
                "provider threw a non-standard exception while reading telemetry"
            };
        }

        const auto findProvider = [this](std::string_view providerId) -> ProviderView*
        {
            const auto found = std::find_if(
                providers_.begin(),
                providers_.end(),
                [providerId](const ProviderView& candidate)
                {
                    return candidate.providerId == providerId;
                });
            return found == providers_.end() ? nullptr : &*found;
        };

        const auto reject = [&snapshot](
            TelemetryUpdateCode code,
            TelemetryAvailability,
            std::string reason) -> TelemetryUpdateResult
        {
            return { code, snapshot.providerId, std::move(reason) };
        };

        if (!IsValidStableId(snapshot.providerId))
        {
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                "providerId must be a non-empty stable ASCII identifier");
        }
        if (!IsValid(snapshot.provenance) || !IsValid(snapshot.availability))
        {
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                "snapshot contains an invalid provenance or availability enum value");
        }

        ProviderView* previousProvider = findProvider(snapshot.providerId);
        if (previousProvider != nullptr
            && previousProvider->hasAcceptedGeneration
            && previousProvider->provenance != snapshot.provenance)
        {
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                "a provider cannot change provenance after its first accepted snapshot");
        }

        if (snapshot.configGeneration < context.currentConfigGeneration)
        {
            return reject(
                TelemetryUpdateCode::RejectedOldGeneration,
                TelemetryAvailability::Stale,
                ConfigGenerationReason(
                    snapshot.configGeneration,
                    context.currentConfigGeneration));
        }
        if (snapshot.configGeneration > context.currentConfigGeneration)
        {
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                ConfigGenerationReason(
                    snapshot.configGeneration,
                    context.currentConfigGeneration));
        }
        if (snapshot.sceneGeneration < context.currentSceneGeneration
            || snapshot.resourceGeneration < context.currentResourceGeneration)
        {
            const bool sceneIsOld =
                snapshot.sceneGeneration < context.currentSceneGeneration;
            return reject(
                TelemetryUpdateCode::RejectedOldGeneration,
                TelemetryAvailability::Stale,
                NamedGenerationReason(
                    sceneIsOld ? "scene" : "resource",
                    sceneIsOld ? snapshot.sceneGeneration : snapshot.resourceGeneration,
                    sceneIsOld
                        ? context.currentSceneGeneration
                        : context.currentResourceGeneration));
        }
        if (snapshot.sceneGeneration > context.currentSceneGeneration
            || snapshot.resourceGeneration > context.currentResourceGeneration)
        {
            const bool sceneIsFuture =
                snapshot.sceneGeneration > context.currentSceneGeneration;
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                NamedGenerationReason(
                    sceneIsFuture ? "scene" : "resource",
                    sceneIsFuture ? snapshot.sceneGeneration : snapshot.resourceGeneration,
                    sceneIsFuture
                        ? context.currentSceneGeneration
                        : context.currentResourceGeneration));
        }
        if (snapshot.frameGeneration > context.currentFrameGeneration)
        {
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                "provider frame generation is newer than the consuming UI frame");
        }
        if (previousProvider != nullptr && previousProvider->hasAcceptedGeneration)
        {
            const bool olderConfig = snapshot.configGeneration
                < previousProvider->configGeneration;
            const bool olderFrame = snapshot.configGeneration
                    == previousProvider->configGeneration
                && snapshot.frameGeneration < previousProvider->frameGeneration;
            const bool olderScene = snapshot.sceneGeneration
                < previousProvider->sceneGeneration;
            const bool olderResource = snapshot.resourceGeneration
                < previousProvider->resourceGeneration;
            if (olderConfig || olderFrame || olderScene || olderResource)
            {
                return reject(
                    TelemetryUpdateCode::RejectedOldGeneration,
                    TelemetryAvailability::Stale,
                    "provider snapshot generation regressed behind its last accepted snapshot");
            }
        }

        const std::uint64_t frameLag = context.currentFrameGeneration
            - snapshot.frameGeneration;
        TelemetryAvailability providerAvailability = snapshot.availability;
        std::string providerReason = snapshot.reason;
        if (providerAvailability == TelemetryAvailability::Fresh
            && frameLag > context.maximumFreshFrameLag)
        {
            providerAvailability = TelemetryAvailability::Stale;
            providerReason = FrameLagReason(
                snapshot.frameGeneration,
                context.currentFrameGeneration);
        }
        if (RequiresReason(providerAvailability) && providerReason.empty())
        {
            return reject(
                TelemetryUpdateCode::RejectedInvalidSnapshot,
                TelemetryAvailability::Invalid,
                "non-fresh provider availability requires a displayable reason");
        }

        std::unordered_set<std::string> incomingIds;
        incomingIds.reserve(snapshot.metrics.size() + snapshot.debugResources.size());

        const auto idOwnedByAnotherProvider = [this, &snapshot](std::string_view stableId)
        {
            const auto metric = std::find_if(
                metrics_.begin(), metrics_.end(),
                [stableId](const MetricView& view)
                {
                    return view.descriptor.stableId == stableId;
                });
            if (metric != metrics_.end() && metric->providerId != snapshot.providerId)
            {
                return true;
            }
            const auto resource = std::find_if(
                debugResources_.begin(), debugResources_.end(),
                [stableId](const DebugResourceView& view)
                {
                    return view.descriptor.stableId == stableId;
                });
            return resource != debugResources_.end()
                && resource->providerId != snapshot.providerId;
        };

        for (const MetricObservation& observation : snapshot.metrics)
        {
            const MetricDescriptor& descriptor = observation.descriptor;
            if (!IsValidStableId(descriptor.stableId)
                || descriptor.label.empty()
                || descriptor.source.empty()
                || !IsValid(descriptor.domain)
                || !IsValid(descriptor.unit)
                || !IsValid(observation.availability))
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    "metric descriptor or availability is invalid");
            }
            const bool existingDebugCategory = std::any_of(
                debugResources_.begin(), debugResources_.end(),
                [&descriptor](const DebugResourceView& view)
                {
                    return view.descriptor.stableId == descriptor.stableId;
                });
            if (!incomingIds.insert(descriptor.stableId).second
                || idOwnedByAnotherProvider(descriptor.stableId)
                || existingDebugCategory)
            {
                return reject(
                    TelemetryUpdateCode::RejectedDuplicateStableId,
                    TelemetryAvailability::Invalid,
                    std::string("duplicate stable id: ") + descriptor.stableId);
            }

            const auto previous = std::find_if(
                metrics_.begin(), metrics_.end(),
                [&descriptor, &snapshot](const MetricView& view)
                {
                    return view.providerId == snapshot.providerId
                        && view.descriptor.stableId == descriptor.stableId;
                });
            if (previous != metrics_.end()
                && !SameMetricContract(previous->descriptor, descriptor))
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("metric contract changed for stable id: ")
                        + descriptor.stableId);
            }

            const bool providerHasMeasurement = HasMeasurement(providerAvailability);
            const bool observationHasMeasurement = HasMeasurement(observation.availability);
            if (!providerHasMeasurement && observation.value.has_value())
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("non-measurable provider state contains a value for: ")
                        + descriptor.stableId);
            }
            if (providerHasMeasurement
                && observationHasMeasurement != observation.value.has_value())
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("metric availability/value mismatch for: ")
                        + descriptor.stableId);
            }
            if (RequiresReason(observation.availability)
                && observation.reason.empty()
                && HasMeasurement(providerAvailability)
                && !(providerAvailability == TelemetryAvailability::Stale
                    && observation.availability == TelemetryAvailability::Stale))
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("metric state requires a displayable reason: ")
                        + descriptor.stableId);
            }
            if (observation.value.has_value())
            {
                if (!std::isfinite(*observation.value))
                {
                    return reject(
                        TelemetryUpdateCode::RejectedInvalidSnapshot,
                        TelemetryAvailability::Invalid,
                        std::string("non-finite metric value rejected: ")
                            + descriptor.stableId);
                }
                if (IsDuration(descriptor.unit) && *observation.value < 0.0)
                {
                    return reject(
                        TelemetryUpdateCode::RejectedInvalidSnapshot,
                        TelemetryAvailability::Invalid,
                        std::string("negative duration rejected: ")
                            + descriptor.stableId);
                }
            }
        }

        for (const DebugResourceObservation& observation : snapshot.debugResources)
        {
            const DebugResourceDescriptor& descriptor = observation.descriptor;
            if (!IsValidStableId(descriptor.stableId)
                || descriptor.label.empty()
                || descriptor.format.empty()
                || descriptor.owner.empty()
                || descriptor.extent.width == 0u
                || descriptor.extent.height == 0u
                || descriptor.extent.depth == 0u
                || !IsValid(observation.availability))
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    "debug-resource descriptor or availability is invalid");
            }
            const bool existingMetricCategory = std::any_of(
                metrics_.begin(), metrics_.end(),
                [&descriptor](const MetricView& view)
                {
                    return view.descriptor.stableId == descriptor.stableId;
                });
            if (!incomingIds.insert(descriptor.stableId).second
                || idOwnedByAnotherProvider(descriptor.stableId)
                || existingMetricCategory)
            {
                return reject(
                    TelemetryUpdateCode::RejectedDuplicateStableId,
                    TelemetryAvailability::Invalid,
                    std::string("duplicate stable id: ") + descriptor.stableId);
            }

            const bool providerHasResource = HasMeasurement(providerAvailability);
            const bool observationHasResource = HasMeasurement(observation.availability);
            if (providerHasResource && observationHasResource
                && descriptor.opaqueUiToken.empty())
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("available debug resource lacks an opaque UI token: ")
                        + descriptor.stableId);
            }
            if ((!providerHasResource || !observationHasResource)
                && !descriptor.opaqueUiToken.empty())
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("unavailable debug resource exposes a UI token: ")
                        + descriptor.stableId);
            }
            if (RequiresReason(observation.availability)
                && observation.reason.empty()
                && HasMeasurement(providerAvailability)
                && !(providerAvailability == TelemetryAvailability::Stale
                    && observation.availability == TelemetryAvailability::Stale))
            {
                return reject(
                    TelemetryUpdateCode::RejectedInvalidSnapshot,
                    TelemetryAvailability::Invalid,
                    std::string("debug-resource state requires a displayable reason: ")
                        + descriptor.stableId);
            }
        }

        // Validation above is deliberately complete before any metric series
        // or catalog row is changed.  A malformed provider update is atomic.
        previousProvider = findProvider(snapshot.providerId);
        const bool generationChanged = previousProvider != nullptr
            && previousProvider->hasAcceptedGeneration
            && (previousProvider->configGeneration != snapshot.configGeneration
                || previousProvider->sceneGeneration != snapshot.sceneGeneration
                || previousProvider->resourceGeneration != snapshot.resourceGeneration);
        if (previousProvider == nullptr)
        {
            providers_.push_back({});
            previousProvider = &providers_.back();
            previousProvider->providerId = snapshot.providerId;
        }
        previousProvider->provenance = snapshot.provenance;
        previousProvider->availability = providerAvailability;
        previousProvider->frameGeneration = snapshot.frameGeneration;
        previousProvider->configGeneration = snapshot.configGeneration;
        previousProvider->sceneGeneration = snapshot.sceneGeneration;
        previousProvider->resourceGeneration = snapshot.resourceGeneration;
        previousProvider->reason = providerReason;
        previousProvider->hasAcceptedGeneration = true;

        if (generationChanged)
        {
            for (RollingSeries& series : rollingSeries_)
            {
                const auto metric = std::find_if(
                    metrics_.begin(), metrics_.end(),
                    [&series](const MetricView& view)
                    {
                        return view.descriptor.stableId == series.stableId;
                    });
                if (metric != metrics_.end() && metric->providerId == snapshot.providerId)
                {
                    series.samples.clear();
                    series.hasGeneration = false;
                    metric->rolling = {};
                }
            }
        }

        const auto isIncoming = [&incomingIds](std::string_view id)
        {
            return incomingIds.find(std::string(id)) != incomingIds.end();
        };

        for (MetricView& view : metrics_)
        {
            if (view.providerId == snapshot.providerId
                && !isIncoming(view.descriptor.stableId))
            {
                view.provenance = snapshot.provenance;
                view.availability = HasMeasurement(providerAvailability)
                    ? TelemetryAvailability::Unavailable
                    : providerAvailability;
                view.frameGeneration = snapshot.frameGeneration;
                view.configGeneration = snapshot.configGeneration;
                view.sceneGeneration = snapshot.sceneGeneration;
                view.resourceGeneration = snapshot.resourceGeneration;
                view.currentValue.reset();
                view.reason = HasMeasurement(providerAvailability)
                    ? MissingReason("metric")
                    : providerReason;
            }
        }
        for (DebugResourceView& view : debugResources_)
        {
            if (view.providerId == snapshot.providerId
                && !isIncoming(view.descriptor.stableId))
            {
                view.descriptor.opaqueUiToken.clear();
                view.provenance = snapshot.provenance;
                view.availability = HasMeasurement(providerAvailability)
                    ? TelemetryAvailability::Unavailable
                    : providerAvailability;
                view.frameGeneration = snapshot.frameGeneration;
                view.configGeneration = snapshot.configGeneration;
                view.sceneGeneration = snapshot.sceneGeneration;
                view.resourceGeneration = snapshot.resourceGeneration;
                view.reason = HasMeasurement(providerAvailability)
                    ? MissingReason("debug resource")
                    : providerReason;
            }
        }

        for (const MetricObservation& observation : snapshot.metrics)
        {
            auto found = std::find_if(
                metrics_.begin(), metrics_.end(),
                [&observation, &snapshot](const MetricView& view)
                {
                    return view.providerId == snapshot.providerId
                        && view.descriptor.stableId == observation.descriptor.stableId;
                });
            if (found == metrics_.end())
            {
                metrics_.push_back({});
                found = std::prev(metrics_.end());
            }

            found->descriptor = observation.descriptor;
            found->providerId = snapshot.providerId;
            found->provenance = snapshot.provenance;
            found->frameGeneration = snapshot.frameGeneration;
            found->configGeneration = snapshot.configGeneration;
            found->sceneGeneration = snapshot.sceneGeneration;
            found->resourceGeneration = snapshot.resourceGeneration;

            TelemetryAvailability effectiveAvailability = observation.availability;
            std::string effectiveReason = observation.reason;
            if (!HasMeasurement(providerAvailability))
            {
                effectiveAvailability = providerAvailability;
                effectiveReason = providerReason;
            }
            else if (providerAvailability == TelemetryAvailability::Stale
                && HasMeasurement(effectiveAvailability))
            {
                effectiveAvailability = TelemetryAvailability::Stale;
                if (effectiveReason.empty())
                {
                    effectiveReason = providerReason;
                }
            }
            found->availability = effectiveAvailability;
            found->reason = effectiveReason;
            found->currentValue = HasMeasurement(effectiveAvailability)
                ? observation.value
                : std::nullopt;

            auto series = std::find_if(
                rollingSeries_.begin(), rollingSeries_.end(),
                [&observation](const RollingSeries& candidate)
                {
                    return candidate.stableId == observation.descriptor.stableId;
                });
            if (series == rollingSeries_.end())
            {
                rollingSeries_.push_back({ observation.descriptor.stableId, {} });
                series = std::prev(rollingSeries_.end());
            }
            if (found->currentValue.has_value())
            {
                const bool replacesSameGeneration = series->hasGeneration
                    && series->lastFrameGeneration == snapshot.frameGeneration
                    && series->lastConfigGeneration == snapshot.configGeneration;
                if (replacesSameGeneration && !series->samples.empty())
                {
                    series->samples.back() = *found->currentValue;
                }
                else
                {
                    series->samples.push_back(*found->currentValue);
                    while (series->samples.size() > rollingWindowCapacity_)
                    {
                        series->samples.pop_front();
                    }
                }
                series->lastFrameGeneration = snapshot.frameGeneration;
                series->lastConfigGeneration = snapshot.configGeneration;
                series->hasGeneration = true;
            }
            found->rolling = ComputeStatistics(series->samples);
        }

        for (const DebugResourceObservation& observation : snapshot.debugResources)
        {
            auto found = std::find_if(
                debugResources_.begin(), debugResources_.end(),
                [&observation, &snapshot](const DebugResourceView& view)
                {
                    return view.providerId == snapshot.providerId
                        && view.descriptor.stableId == observation.descriptor.stableId;
                });
            if (found == debugResources_.end())
            {
                debugResources_.push_back({});
                found = std::prev(debugResources_.end());
            }

            found->descriptor = observation.descriptor;
            found->providerId = snapshot.providerId;
            found->provenance = snapshot.provenance;
            found->frameGeneration = snapshot.frameGeneration;
            found->configGeneration = snapshot.configGeneration;
            found->sceneGeneration = snapshot.sceneGeneration;
            found->resourceGeneration = snapshot.resourceGeneration;
            found->availability = observation.availability;
            found->reason = observation.reason;
            if (!HasMeasurement(providerAvailability))
            {
                found->availability = providerAvailability;
                found->reason = providerReason;
                found->descriptor.opaqueUiToken.clear();
            }
            else if (providerAvailability == TelemetryAvailability::Stale
                && HasMeasurement(found->availability))
            {
                found->availability = TelemetryAvailability::Stale;
                if (found->reason.empty())
                {
                    found->reason = providerReason;
                }
            }
        }

        return { TelemetryUpdateCode::Accepted, snapshot.providerId, {} };
    }

    void DebugProfilerModel::InvalidateBeforeConfigGeneration(
        std::uint64_t configGeneration)
    {
        InvalidateBeforeGenerationTuple(configGeneration, 0u, 0u);
    }

    void DebugProfilerModel::InvalidateBeforeGenerationTuple(
        std::uint64_t configGeneration,
        std::uint64_t sceneGeneration,
        std::uint64_t resourceGeneration)
    {
        const std::string reason =
            "telemetry belongs to an older config/scene/resource generation; awaiting a compatible snapshot";
        const auto isOlder = [configGeneration, sceneGeneration, resourceGeneration](
            std::uint64_t config,
            std::uint64_t scene,
            std::uint64_t resource) noexcept
        {
            return config < configGeneration
                || scene < sceneGeneration
                || resource < resourceGeneration;
        };

        for (ProviderView& provider : providers_)
        {
            if (provider.hasAcceptedGeneration
                && isOlder(
                    provider.configGeneration,
                    provider.sceneGeneration,
                    provider.resourceGeneration))
            {
                provider.availability = TelemetryAvailability::Stale;
                provider.reason = reason;
            }
        }
        for (MetricView& metric : metrics_)
        {
            if (isOlder(
                metric.configGeneration,
                metric.sceneGeneration,
                metric.resourceGeneration))
            {
                metric.availability = TelemetryAvailability::Stale;
                metric.currentValue.reset();
                metric.reason = reason;
                metric.rolling = {};
            }
        }
        for (DebugResourceView& resource : debugResources_)
        {
            if (isOlder(
                resource.configGeneration,
                resource.sceneGeneration,
                resource.resourceGeneration))
            {
                resource.availability = TelemetryAvailability::Stale;
                resource.descriptor.opaqueUiToken.clear();
                resource.reason = reason;
            }
        }
        for (RollingSeries& series : rollingSeries_)
        {
            const auto metric = std::find_if(
                metrics_.begin(), metrics_.end(),
                [&series](const MetricView& candidate)
                {
                    return candidate.descriptor.stableId == series.stableId;
                });
            if (metric != metrics_.end()
                && isOlder(
                    metric->configGeneration,
                    metric->sceneGeneration,
                    metric->resourceGeneration))
            {
                series.samples.clear();
            }
        }
    }

    void DebugProfilerModel::ApplyReset(ResetMask resetMask)
    {
        if (!HasReset(resetMask, ResetResource::ProfilerStatistics))
        {
            return;
        }

        for (RollingSeries& series : rollingSeries_)
        {
            series.samples.clear();
            series.hasGeneration = false;
        }
        for (MetricView& metric : metrics_)
        {
            metric.availability = TelemetryAvailability::Pending;
            metric.currentValue.reset();
            metric.reason = "profiler statistics reset; awaiting a sample for the current generation";
            metric.rolling = {};
        }
    }

    std::span<const MetricView> DebugProfilerModel::Metrics() const noexcept
    {
        return { metrics_.data(), metrics_.size() };
    }

    std::span<const DebugResourceView> DebugProfilerModel::DebugResources() const noexcept
    {
        return { debugResources_.data(), debugResources_.size() };
    }

    std::span<const ProviderView> DebugProfilerModel::Providers() const noexcept
    {
        return { providers_.data(), providers_.size() };
    }

    const MetricView* DebugProfilerModel::FindMetric(std::string_view stableId) const noexcept
    {
        const auto found = std::find_if(
            metrics_.begin(), metrics_.end(),
            [stableId](const MetricView& view)
            {
                return view.descriptor.stableId == stableId;
            });
        return found == metrics_.end() ? nullptr : &*found;
    }

    const DebugResourceView* DebugProfilerModel::FindDebugResource(
        std::string_view stableId) const noexcept
    {
        const auto found = std::find_if(
            debugResources_.begin(), debugResources_.end(),
            [stableId](const DebugResourceView& view)
            {
                return view.descriptor.stableId == stableId;
            });
        return found == debugResources_.end() ? nullptr : &*found;
    }

    const ProviderView* DebugProfilerModel::FindProvider(std::string_view providerId) const noexcept
    {
        const auto found = std::find_if(
            providers_.begin(), providers_.end(),
            [providerId](const ProviderView& view)
            {
                return view.providerId == providerId;
            });
        return found == providers_.end() ? nullptr : &*found;
    }
}
