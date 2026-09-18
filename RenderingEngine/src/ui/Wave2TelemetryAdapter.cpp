#include "ui/Wave2TelemetryAdapter.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace RenderingEngine::Ui
{
    namespace
    {
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

        [[nodiscard]] bool HasMeasurement(TelemetryAvailability value) noexcept
        {
            return value == TelemetryAvailability::Fresh
                || value == TelemetryAvailability::Stale;
        }

        struct AvailabilityAccumulator final
        {
            bool freshValue = false;
            bool staleValue = false;
            bool pending = false;
            bool invalid = false;

            void Observe(
                const TelemetryAvailability availability,
                const bool hasValue) noexcept
            {
                if (hasValue && availability == TelemetryAvailability::Fresh)
                {
                    freshValue = true;
                }
                else if (hasValue && availability == TelemetryAvailability::Stale)
                {
                    staleValue = true;
                }
                else if (availability == TelemetryAvailability::Pending)
                {
                    pending = true;
                }
                else if (availability == TelemetryAvailability::Invalid)
                {
                    invalid = true;
                }
            }

            [[nodiscard]] TelemetryAvailability Result() const noexcept
            {
                // Invalid producer state must dominate otherwise-fresh partial
                // telemetry.  Individual usable rows remain visible, but the
                // provider banner may not advertise the mixed frame as Fresh.
                if (invalid)
                {
                    return TelemetryAvailability::Invalid;
                }
                if (freshValue)
                {
                    return TelemetryAvailability::Fresh;
                }
                if (staleValue)
                {
                    return TelemetryAvailability::Stale;
                }
                if (pending)
                {
                    return TelemetryAvailability::Pending;
                }
                return TelemetryAvailability::Unavailable;
            }
        };

        [[nodiscard]] std::string FallbackReason(
            const std::string_view producer,
            const std::string_view valueName,
            const TelemetryAvailability availability)
        {
            std::string reason;
            if (availability == TelemetryAvailability::Pending)
            {
                reason = "upstream ";
                reason.append(producer);
                reason.append(" ");
                reason.append(valueName);
                reason.append(" is awaiting resolved GPU readback");
            }
            else if (availability == TelemetryAvailability::Invalid)
            {
                reason = "upstream ";
                reason.append(producer);
                reason.append(" ");
                reason.append(valueName);
                reason.append(" was marked invalid");
            }
            else if (availability == TelemetryAvailability::Stale)
            {
                reason = "upstream ";
                reason.append(producer);
                reason.append(" ");
                reason.append(valueName);
                reason.append(" is from an older resolved frame");
            }
            else
            {
                reason = "upstream ";
                reason.append(producer);
                reason.append(" has not published ");
                reason.append(valueName);
            }
            return reason;
        }

        [[nodiscard]] Wave2CounterObservation InheritLaneState(
            const Wave2CounterObservation& observation,
            const TelemetryAvailability laneAvailability,
            const std::string_view laneReason)
        {
            Wave2CounterObservation result = observation;
            if (result.availability == TelemetryAvailability::Unavailable
                && !result.value.has_value()
                && result.reason.empty()
                && laneAvailability != TelemetryAvailability::Unavailable)
            {
                result.availability = laneAvailability;
                result.reason = laneReason;
            }
            return result;
        }

        [[nodiscard]] Wave2DurationObservation InheritLaneState(
            const Wave2DurationObservation& observation,
            const TelemetryAvailability laneAvailability,
            const std::string_view laneReason)
        {
            Wave2DurationObservation result = observation;
            if (result.availability == TelemetryAvailability::Unavailable
                && !result.milliseconds.has_value()
                && result.reason.empty()
                && laneAvailability != TelemetryAvailability::Unavailable)
            {
                result.availability = laneAvailability;
                result.reason = laneReason;
            }
            return result;
        }

        [[nodiscard]] MetricObservation MakeCounterMetric(
            const std::string_view stableId,
            const std::string_view label,
            const MetricDomain domain,
            const std::string_view fallbackSource,
            const std::string_view producer,
            const std::string_view valueName,
            const Wave2CounterObservation& input,
            AvailabilityAccumulator& availability)
        {
            Wave2CounterObservation observation = input;
            if (!IsValid(observation.availability))
            {
                observation.availability = TelemetryAvailability::Invalid;
                observation.value.reset();
                observation.reason = FallbackReason(
                    producer, valueName, TelemetryAvailability::Invalid);
            }
            else if (observation.value.has_value()
                && !HasMeasurement(observation.availability))
            {
                observation.availability = TelemetryAvailability::Invalid;
                observation.value.reset();
                observation.reason = "upstream supplied a counter value without Fresh/Stale availability";
            }
            else if (!observation.value.has_value()
                && HasMeasurement(observation.availability))
            {
                observation.availability = TelemetryAvailability::Pending;
                observation.reason = FallbackReason(
                    producer, valueName, TelemetryAvailability::Pending);
            }

            if (observation.reason.empty())
            {
                observation.reason = FallbackReason(
                    producer, valueName, observation.availability);
            }
            availability.Observe(
                observation.availability,
                observation.value.has_value());

            MetricObservation result;
            result.descriptor.stableId = std::string(stableId);
            result.descriptor.label = std::string(label);
            result.descriptor.domain = domain;
            result.descriptor.unit = MetricUnit::Count;
            // MetricDescriptor::source is part of the stable metric contract.
            // A producer observation may name a more specific live readback
            // path, but that path can change as a lane becomes active or
            // inactive and must not redefine the descriptor.
            result.descriptor.source = std::string(fallbackSource);
            result.availability = observation.availability;
            if (observation.value.has_value())
            {
                result.value = static_cast<double>(*observation.value);
            }
            result.reason = std::move(observation.reason);
            return result;
        }

        [[nodiscard]] MetricObservation MakeDurationMetric(
            const std::string_view stableId,
            const std::string_view label,
            const std::string_view fallbackSource,
            const std::string_view producer,
            const std::string_view valueName,
            const Wave2DurationObservation& input,
            AvailabilityAccumulator& availability)
        {
            Wave2DurationObservation observation = input;
            if (!IsValid(observation.availability))
            {
                observation.availability = TelemetryAvailability::Invalid;
                observation.milliseconds.reset();
                observation.reason = FallbackReason(
                    producer, valueName, TelemetryAvailability::Invalid);
            }
            else if (observation.milliseconds.has_value()
                && !HasMeasurement(observation.availability))
            {
                observation.availability = TelemetryAvailability::Invalid;
                observation.milliseconds.reset();
                observation.reason = "upstream supplied a duration without Fresh/Stale availability";
            }
            else if (!observation.milliseconds.has_value()
                && HasMeasurement(observation.availability))
            {
                observation.availability = TelemetryAvailability::Pending;
                observation.reason = FallbackReason(
                    producer, valueName, TelemetryAvailability::Pending);
            }
            else if (observation.milliseconds.has_value()
                && (!std::isfinite(*observation.milliseconds)
                    || *observation.milliseconds < 0.0))
            {
                observation.availability = TelemetryAvailability::Invalid;
                observation.milliseconds.reset();
                observation.reason = "upstream supplied a negative or non-finite GPU duration";
            }

            if (observation.reason.empty())
            {
                observation.reason = FallbackReason(
                    producer, valueName, observation.availability);
            }
            availability.Observe(
                observation.availability,
                observation.milliseconds.has_value());

            MetricObservation result;
            result.descriptor.stableId = std::string(stableId);
            result.descriptor.label = std::string(label);
            result.descriptor.domain = MetricDomain::Gpu;
            result.descriptor.unit = MetricUnit::Milliseconds;
            // Keep the descriptor source canonical across active/inactive
            // producer states; observation.source is per-observation metadata,
            // not permission to mutate the stable metric contract.
            result.descriptor.source = std::string(fallbackSource);
            result.availability = observation.availability;
            result.value = observation.milliseconds;
            result.reason = std::move(observation.reason);
            return result;
        }

        void AppendResourceObservations(
            std::vector<DebugResourceObservation>& destination,
            const std::vector<DebugResourceObservation>& input,
            AvailabilityAccumulator& availability)
        {
            for (const DebugResourceObservation& source : input)
            {
                DebugResourceObservation resource = source;
                if (!IsValid(resource.availability))
                {
                    resource.availability = TelemetryAvailability::Invalid;
                    resource.descriptor.opaqueUiToken.clear();
                    resource.reason = "upstream supplied an invalid debug-resource availability";
                }
                const bool hasToken = !resource.descriptor.opaqueUiToken.empty();
                if (hasToken && !HasMeasurement(resource.availability))
                {
                    resource.descriptor.opaqueUiToken.clear();
                    resource.reason = "debug-resource token was withheld until its resource became Fresh";
                }
                else if (!hasToken && HasMeasurement(resource.availability))
                {
                    resource.availability = TelemetryAvailability::Pending;
                    resource.reason = "upstream debug resource is awaiting opaque UI registration";
                }
                if (resource.reason.empty()
                    && resource.availability != TelemetryAvailability::Fresh)
                {
                    resource.reason = "upstream debug resource is not currently Fresh";
                }
                availability.Observe(resource.availability, hasToken);
                destination.push_back(std::move(resource));
            }
        }

        void ObserveLane(
            const TelemetryAvailability laneAvailability,
            const std::string_view laneName,
            const std::string& laneReason,
            AvailabilityAccumulator& availability)
        {
            if (!IsValid(laneAvailability))
            {
                availability.invalid = true;
                return;
            }
            if (laneAvailability == TelemetryAvailability::Invalid)
            {
                availability.invalid = true;
            }
            else if (laneAvailability == TelemetryAvailability::Pending)
            {
                availability.pending = true;
            }
            else if (laneAvailability == TelemetryAvailability::Stale
                || laneAvailability == TelemetryAvailability::Fresh)
            {
                // A lane status alone is not a measurement.  Only a counter,
                // timestamp or resource token may promote the provider to a
                // measurable state; this prevents an empty producer report
                // from becoming a fabricated zero-valued sample.  A stale
                // lane with no value is normalized to Pending by the typed
                // observation helpers below.
                static_cast<void>(laneName);
            }
            static_cast<void>(laneReason);
        }

        void AppendL4Metrics(
            DebugProfilerSnapshot& snapshot,
            const Wave2L4Telemetry& lane,
            AvailabilityAccumulator& availability)
        {
            ObserveLane(lane.availability, "L4", lane.reason, availability);
            const auto counter = [&lane](const Wave2CounterObservation& value)
            {
                return InheritLaneState(value, lane.availability, lane.reason);
            };
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.rays", "L4 rays",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.rays",
                "L4 Flattened SAH", "rays", counter(lane.counters.rays), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.hits", "L4 hits",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.hits",
                "L4 Flattened SAH", "hits", counter(lane.counters.hits), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.node-tests", "L4 node tests",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.nodeTests",
                "L4 Flattened SAH", "node tests", counter(lane.counters.nodeTests), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.triangle-tests", "L4 triangle tests",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.triangleTests",
                "L4 Flattened SAH", "triangle tests", counter(lane.counters.triangleTests), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.stack-overflows", "L4 stack overflows",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.stackOverflows",
                "L4 Flattened SAH", "stack overflows", counter(lane.counters.stackOverflows), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.invalid-rays", "L4 invalid rays",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.invalidRays",
                "L4 Flattened SAH", "invalid rays", counter(lane.counters.invalidRays), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.invalid-hits", "L4 invalid hits",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.invalidHits",
                "L4 Flattened SAH", "invalid hits", counter(lane.counters.invalidHits), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.leaf-visits", "L4 leaf visits",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.leafVisits",
                "L4 Flattened SAH", "leaf visits", counter(lane.counters.leafVisits), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.leaf-primitives", "L4 leaf primitives",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.accumulatedLeafPrimitives",
                "L4 Flattened SAH", "accumulated leaf primitives",
                counter(lane.counters.accumulatedLeafPrimitives), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.maximum-stack-depth", "L4 maximum stack depth",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.maximumStackDepth",
                "L4 Flattened SAH", "maximum stack depth",
                counter(lane.counters.maximumStackDepth), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l4.flattened-sah.maximum-leaf-occupancy", "L4 maximum leaf occupancy",
                MetricDomain::Traversal, "L4.FlattenedSAH.counters.maximumLeafOccupancy",
                "L4 Flattened SAH", "maximum leaf occupancy",
                counter(lane.counters.maximumLeafOccupancy), availability));
            snapshot.metrics.push_back(MakeDurationMetric(
                "wave2.l4.flattened-sah.build-ms", "L4 build GPU ms",
                "L4.FlattenedSAH.timestamps.build", "L4 Flattened SAH", "build timestamp",
                InheritLaneState(lane.buildMilliseconds, lane.availability, lane.reason), availability));
            snapshot.metrics.push_back(MakeDurationMetric(
                "wave2.l4.flattened-sah.trace-ms", "L4 trace GPU ms",
                "L4.FlattenedSAH.timestamps.trace", "L4 Flattened SAH", "trace timestamp",
                InheritLaneState(lane.traceMilliseconds, lane.availability, lane.reason), availability));
            AppendResourceObservations(snapshot.debugResources, lane.debugResources, availability);
        }

        void AppendL5Metrics(
            DebugProfilerSnapshot& snapshot,
            const Wave2L5Telemetry& lane,
            AvailabilityAccumulator& availability)
        {
            ObserveLane(lane.availability, "L5", lane.reason, availability);
            const auto counter = [&lane](const Wave2CounterObservation& value)
            {
                return InheritLaneState(value, lane.availability, lane.reason);
            };
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l5.ray-query.rays", "L5 Ray Query rays",
                MetricDomain::Traversal, "L5.RayQuery.counters.rays",
                "L5 Ray Query", "rays", counter(lane.counters.rays), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l5.ray-query.hits", "L5 Ray Query hits",
                MetricDomain::Traversal, "L5.RayQuery.counters.hits",
                "L5 Ray Query", "hits", counter(lane.counters.hits), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l5.ray-query.invalid-rays", "L5 Ray Query invalid rays",
                MetricDomain::Traversal, "L5.RayQuery.counters.invalidRays",
                "L5 Ray Query", "invalid rays", counter(lane.counters.invalidRays), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l5.ray-query.invalid-hits", "L5 Ray Query invalid hits",
                MetricDomain::Traversal, "L5.RayQuery.counters.invalidHits",
                "L5 Ray Query", "invalid hits", counter(lane.counters.invalidHits), availability));
            snapshot.metrics.push_back(MakeCounterMetric(
                "wave2.l5.ray-query.candidate-tests", "L5 Ray Query candidate tests",
                MetricDomain::Traversal, "L5.RayQuery.counters.candidateTests",
                "L5 Ray Query", "candidate tests", counter(lane.counters.candidateTests), availability));
            snapshot.metrics.push_back(MakeDurationMetric(
                "wave2.l5.ray-query.as-build-ms", "L5 AS build GPU ms",
                "L5.RayQuery.timestamps.asBuild", "L5 Ray Query", "AS build timestamp",
                InheritLaneState(lane.accelerationStructureBuildMilliseconds,
                    lane.availability, lane.reason), availability));
            snapshot.metrics.push_back(MakeDurationMetric(
                "wave2.l5.ray-query.dispatch-ms", "L5 dispatch GPU ms",
                "L5.RayQuery.timestamps.dispatch", "L5 Ray Query", "dispatch timestamp",
                InheritLaneState(lane.dispatchMilliseconds, lane.availability, lane.reason), availability));
            snapshot.metrics.push_back(MakeDurationMetric(
                "wave2.l5.ray-query.trace-ms", "L5 trace GPU ms",
                "L5.RayQuery.timestamps.trace", "L5 Ray Query", "trace timestamp",
                InheritLaneState(lane.traceMilliseconds, lane.availability, lane.reason), availability));
            AppendResourceObservations(snapshot.debugResources, lane.debugResources, availability);
        }

        constexpr std::array<std::string_view, kWave2L6CounterCount> kL6CounterNames = {
            "camera rays", "path rays", "shadow rays", "surface hits", "misses",
            "valid light samples", "invalid light samples", "occluded light samples",
            "camera emitter hits", "delta emitter hits", "MIS emitter hits",
            "Russian roulette tests", "Russian roulette terminations",
            "maximum depth terminations", "zero PDF", "negative PDF", "non-finite PDF",
            "non-finite BSDF", "non-finite throughput", "non-finite radiance",
            "negative contribution", "alias fallbacks", "invalid material",
            "invalid BSDF evaluation", "invalid BSDF sample", "invalid frame"};

        void AppendL6Metrics(
            DebugProfilerSnapshot& snapshot,
            const Wave2L6Telemetry& lane,
            AvailabilityAccumulator& availability)
        {
            ObserveLane(lane.availability, "L6", lane.reason, availability);
            for (std::size_t index = 0u; index < lane.counters.size(); ++index)
            {
                const auto observation = InheritLaneState(
                    lane.counters[index], lane.availability, lane.reason);
                const std::string suffix = std::to_string(index);
                const std::string stableId = "wave2.l6.megakernel.counter." + suffix;
                const std::string label = "L6 " + std::string(kL6CounterNames[index]);
                const std::string source = "L6.Megakernel.counters." + suffix;
                snapshot.metrics.push_back(MakeCounterMetric(
                    stableId, label, MetricDomain::PathTransport, source,
                    "L6 Megakernel", kL6CounterNames[index], observation, availability));
            }
            snapshot.metrics.push_back(MakeDurationMetric(
                "wave2.l6.megakernel.trace-ms", "L6 megakernel GPU ms",
                "L6.Megakernel.timestamps.trace", "L6 Megakernel", "trace timestamp",
                InheritLaneState(lane.traceMilliseconds, lane.availability, lane.reason), availability));
            AppendResourceObservations(snapshot.debugResources, lane.debugResources, availability);
        }
    }

    Wave2TelemetryAdapter::Wave2TelemetryAdapter(std::string providerId)
        : providerId_(std::move(providerId))
    {
    }

    Wave2PublishResult Wave2TelemetryAdapter::Publish(Wave2TelemetryFrame frame)
    {
        if (providerId_.empty() || !IsValid(frame.provenance))
        {
            return {
                Wave2PublishCode::RejectedInvalidFrame,
                "Wave 2 provider ID or provenance is invalid"};
        }

        if (latestFrame_.has_value())
        {
            const Wave2GenerationTuple& previous = latestFrame_->generation;
            const Wave2GenerationTuple& current = frame.generation;
            const bool regressed = current.configGeneration < previous.configGeneration
                || (current.configGeneration == previous.configGeneration
                    && current.frameGeneration < previous.frameGeneration)
                || current.sceneGeneration < previous.sceneGeneration
                || current.resourceGeneration < previous.resourceGeneration;
            if (regressed)
            {
                return {
                    Wave2PublishCode::RejectedGenerationRegression,
                    "Wave 2 telemetry generation regressed behind the last published frame"};
            }
        }

        latestFrame_ = std::move(frame);
        return { Wave2PublishCode::Accepted, {} };
    }

    void Wave2TelemetryAdapter::Clear() noexcept
    {
        latestFrame_.reset();
    }

    std::string_view Wave2TelemetryAdapter::ProviderId() const noexcept
    {
        return providerId_;
    }

    DebugProfilerSnapshot Wave2TelemetryAdapter::ReadSnapshot()
    {
        if (!latestFrame_.has_value())
        {
            DebugProfilerSnapshot snapshot;
            snapshot.providerId = providerId_;
            snapshot.provenance = TelemetryProvenance::LiveRuntime;
            snapshot.availability = TelemetryAvailability::Unavailable;
            snapshot.reason = "no Wave 2 producer frame has been published";
            return snapshot;
        }

        const Wave2TelemetryFrame& frame = *latestFrame_;
        DebugProfilerSnapshot snapshot;
        snapshot.providerId = providerId_;
        snapshot.provenance = frame.provenance;
        snapshot.frameGeneration = frame.generation.frameGeneration;
        snapshot.configGeneration = frame.generation.configGeneration;
        snapshot.sceneGeneration = frame.generation.sceneGeneration;
        snapshot.resourceGeneration = frame.generation.resourceGeneration;

        AvailabilityAccumulator availability;
        AppendL4Metrics(snapshot, frame.flattenedSoftwareGpu, availability);
        AppendL5Metrics(snapshot, frame.hardwareRayQuery, availability);
        AppendL6Metrics(snapshot, frame.megakernel, availability);
        snapshot.availability = availability.Result();
        if (snapshot.availability != TelemetryAvailability::Fresh)
        {
            snapshot.reason = snapshot.availability == TelemetryAvailability::Invalid
                ? "one or more Wave 2 producer measurements are invalid"
                : snapshot.availability == TelemetryAvailability::Pending
                    ? "Wave 2 GPU query results are pending"
                    : snapshot.availability == TelemetryAvailability::Stale
                        ? "Wave 2 telemetry is from an older resolved frame"
                        : "Wave 2 producers have not published resolved telemetry";
        }
        return snapshot;
    }
}
