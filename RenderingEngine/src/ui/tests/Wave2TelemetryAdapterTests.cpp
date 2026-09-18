#include "ui/Wave2TelemetryAdapter.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>
#include <utility>

namespace
{
    using namespace RenderingEngine::Ui;

    class Checks final
    {
    public:
        explicit Checks(std::ostream& output)
            : output_(output)
        {
        }

        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                output_ << "L10 Wave2TelemetryAdapter test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failures_ == 0u;
        }

    private:
        std::ostream& output_;
        std::size_t failures_ = 0u;
    };

    [[nodiscard]] Wave2CounterObservation FreshCounter(
        const std::uint64_t value,
        const char* source)
    {
        Wave2CounterObservation result;
        result.availability = TelemetryAvailability::Fresh;
        result.value = value;
        result.source = source;
        return result;
    }

    [[nodiscard]] Wave2DurationObservation FreshDuration(
        const double milliseconds,
        const char* source)
    {
        Wave2DurationObservation result;
        result.availability = TelemetryAvailability::Fresh;
        result.milliseconds = milliseconds;
        result.source = source;
        return result;
    }

    void TestUnavailableAndPending(Checks& checks)
    {
        Wave2TelemetryAdapter adapter;
        const DebugProfilerSnapshot empty = adapter.ReadSnapshot();
        checks.Expect(empty.providerId == "wave2.telemetry"
                && empty.availability == TelemetryAvailability::Unavailable
                && !empty.reason.empty()
                && empty.metrics.empty(),
            "an adapter without a published frame must be unavailable, not zero-valued");

        Wave2TelemetryFrame pending;
        pending.generation = { 7u, 3u, 11u, 13u };
        pending.flattenedSoftwareGpu.availability = TelemetryAvailability::Pending;
        pending.flattenedSoftwareGpu.reason = "L4 query fence has not signaled";
        const Wave2PublishResult published = adapter.Publish(std::move(pending));
        checks.Expect(published.Accepted(), "pending producer frame must publish");

        const DebugProfilerSnapshot snapshot = adapter.ReadSnapshot();
        checks.Expect(snapshot.availability == TelemetryAvailability::Pending
                && snapshot.frameGeneration == 7u
                && snapshot.configGeneration == 3u
                && snapshot.sceneGeneration == 11u
                && snapshot.resourceGeneration == 13u,
            "pending snapshot must preserve the complete generation tuple");
        checks.Expect(!snapshot.metrics.empty(),
            "typed lane catalog must retain rows while readback is pending");
        checks.Expect(snapshot.metrics.front().availability == TelemetryAvailability::Pending
                && !snapshot.metrics.front().value.has_value()
                && !snapshot.metrics.front().reason.empty(),
            "pending counter must not be represented as numeric zero");
    }

    void TestFreshCountersAndProvenance(Checks& checks)
    {
        Wave2TelemetryAdapter adapter;
        Wave2TelemetryFrame frame;
        frame.provenance = TelemetryProvenance::ImportedArtifact;
        frame.generation = { 21u, 5u, 8u, 9u };
        frame.flattenedSoftwareGpu.counters.rays = FreshCounter(
            0u, "runtime.l4.readback.rays");
        frame.flattenedSoftwareGpu.counters.nodeTests = FreshCounter(
            17u, "runtime.l4.readback.nodeTests");
        frame.flattenedSoftwareGpu.traceMilliseconds = FreshDuration(
            1.25, "runtime.l4.timestamp.trace");
        frame.megakernel.counters[
            static_cast<std::size_t>(Wave2L6Counter::InvalidFrame)] = FreshCounter(
            2u, "runtime.l6.readback.invalidFrame");
        const Wave2PublishResult published = adapter.Publish(std::move(frame));
        checks.Expect(published.Accepted(), "fresh typed producer frame must publish");

        DebugProfilerModel model;
        TelemetryFrameContext context;
        context.currentFrameGeneration = 21u;
        context.currentConfigGeneration = 5u;
        context.currentSceneGeneration = 8u;
        context.currentResourceGeneration = 9u;
        checks.Expect(model.Refresh(adapter, context).Accepted(),
            "fresh typed producer frame must be accepted by the model");

        const MetricView* rays = model.FindMetric("wave2.l4.flattened-sah.rays");
        const MetricView* nodeTests = model.FindMetric(
            "wave2.l4.flattened-sah.node-tests");
        const MetricView* trace = model.FindMetric(
            "wave2.l4.flattened-sah.trace-ms");
        const MetricView* invalidFrame = model.FindMetric(
            "wave2.l6.megakernel.counter.25");
        const MetricView* missingRayQuery = model.FindMetric(
            "wave2.l5.ray-query.trace-ms");
        checks.Expect(rays != nullptr
                && rays->availability == TelemetryAvailability::Fresh
                && rays->currentValue.has_value()
                && *rays->currentValue == 0.0
                && rays->provenance == TelemetryProvenance::ImportedArtifact
                && rays->frameGeneration == 21u,
            "measured zero must remain a Fresh measured zero with provenance");
        checks.Expect(nodeTests != nullptr
                && nodeTests->currentValue.has_value()
                && *nodeTests->currentValue == 17.0
                && nodeTests->descriptor.source
                    == "L4.FlattenedSAH.counters.nodeTests",
            "L4 typed counter must retain its canonical descriptor source");
        checks.Expect(trace != nullptr
                && trace->currentValue.has_value()
                && *trace->currentValue == 1.25
                && trace->descriptor.unit == MetricUnit::Milliseconds
                && trace->descriptor.source
                    == "L4.FlattenedSAH.timestamps.trace",
            "L4 timestamp must map to a GPU millisecond metric");
        checks.Expect(invalidFrame != nullptr
                && invalidFrame->currentValue.has_value()
                && *invalidFrame->currentValue == 2.0
                && invalidFrame->provenance == TelemetryProvenance::ImportedArtifact,
            "L6 counter slot must preserve its typed ABI index and provenance");
        checks.Expect(missingRayQuery != nullptr
                && missingRayQuery->availability == TelemetryAvailability::Unavailable
                && !missingRayQuery->currentValue.has_value()
                && !missingRayQuery->reason.empty(),
            "missing L5 timing must remain unavailable even when another lane is Fresh");
    }

    void TestCanonicalSourceAcrossLaneTransitions(Checks& checks)
    {
        Wave2TelemetryAdapter adapter;
        DebugProfilerModel model;

        Wave2TelemetryFrame inactive;
        inactive.generation = { 1u, 1u, 1u, 1u };
        inactive.megakernel.availability = TelemetryAvailability::Fresh;
        inactive.megakernel.traceMilliseconds = FreshDuration(
            2.0, "runtime.l6.timestamp.trace");
        checks.Expect(adapter.Publish(std::move(inactive)).Accepted(),
            "inactive-L4 baseline must publish");
        checks.Expect(model.Refresh(adapter, { 1u, 1u, 0u, 1u, 1u }).Accepted(),
            "inactive-L4 baseline must enter the profiler model");
        const MetricView* trace = model.FindMetric(
            "wave2.l4.flattened-sah.trace-ms");
        checks.Expect(trace != nullptr
                && trace->availability == TelemetryAvailability::Unavailable
                && !trace->currentValue.has_value()
                && trace->descriptor.source
                    == "L4.FlattenedSAH.timestamps.trace",
            "inactive L4 must publish the canonical trace source");

        Wave2TelemetryFrame active;
        active.generation = { 2u, 2u, 1u, 1u };
        active.flattenedSoftwareGpu.availability = TelemetryAvailability::Fresh;
        active.flattenedSoftwareGpu.traceMilliseconds = FreshDuration(
            1.25, "L4 traversal inside the production L6 dispatch");
        active.megakernel.availability = TelemetryAvailability::Fresh;
        active.megakernel.traceMilliseconds = FreshDuration(
            2.1, "runtime.l6.timestamp.trace");
        checks.Expect(adapter.Publish(std::move(active)).Accepted(),
            "active-L4 frame must publish");
        model.InvalidateBeforeConfigGeneration(2u);
        checks.Expect(model.Refresh(adapter, { 2u, 2u, 0u, 1u, 1u }).Accepted(),
            "inactive-to-active L4 must not mutate the metric contract");
        trace = model.FindMetric("wave2.l4.flattened-sah.trace-ms");
        checks.Expect(trace != nullptr
                && trace->availability == TelemetryAvailability::Fresh
                && trace->currentValue == 1.25
                && trace->descriptor.source
                    == "L4.FlattenedSAH.timestamps.trace",
            "active L4 must keep the canonical source while accepting its value");

        Wave2TelemetryFrame inactiveAgain;
        inactiveAgain.generation = { 3u, 3u, 1u, 1u };
        inactiveAgain.megakernel.availability = TelemetryAvailability::Fresh;
        inactiveAgain.megakernel.traceMilliseconds = FreshDuration(
            2.2, "runtime.l6.timestamp.trace");
        checks.Expect(adapter.Publish(std::move(inactiveAgain)).Accepted(),
            "second inactive-L4 frame must publish");
        model.InvalidateBeforeConfigGeneration(3u);
        checks.Expect(model.Refresh(adapter, { 3u, 3u, 0u, 1u, 1u }).Accepted(),
            "active-to-inactive L4 must not mutate the metric contract");
        trace = model.FindMetric("wave2.l4.flattened-sah.trace-ms");
        checks.Expect(trace != nullptr
                && trace->availability == TelemetryAvailability::Unavailable
                && !trace->currentValue.has_value()
                && trace->descriptor.source
                    == "L4.FlattenedSAH.timestamps.trace",
            "returning to inactive L4 must preserve the canonical source");
    }

    void TestCanonicalCounterSourceAcrossProfilerTransitions(Checks& checks)
    {
        Wave2TelemetryAdapter adapter;
        DebugProfilerModel model;
        constexpr std::size_t counterIndex =
            static_cast<std::size_t>(Wave2L6Counter::CameraRays);

        Wave2TelemetryFrame unavailable;
        unavailable.generation = { 10u, 5u, 2u, 3u };
        unavailable.megakernel.availability = TelemetryAvailability::Fresh;
        unavailable.megakernel.counters[counterIndex].reason =
            "profiler counters are disabled";
        unavailable.megakernel.traceMilliseconds = FreshDuration(
            3.0, "runtime.l6.timestamp.trace");
        checks.Expect(adapter.Publish(std::move(unavailable)).Accepted(),
            "counter-unavailable baseline must publish");
        checks.Expect(model.Refresh(adapter, { 10u, 5u, 0u, 2u, 3u }).Accepted(),
            "counter-unavailable baseline must enter the profiler model");
        const MetricView* counter = model.FindMetric(
            "wave2.l6.megakernel.counter.0");
        checks.Expect(counter != nullptr
                && counter->availability == TelemetryAvailability::Unavailable
                && !counter->currentValue.has_value()
                && counter->descriptor.source == "L6.Megakernel.counters.0",
            "unavailable counter must publish its canonical source");

        Wave2TelemetryFrame fresh;
        fresh.generation = { 11u, 5u, 2u, 3u };
        fresh.megakernel.availability = TelemetryAvailability::Fresh;
        fresh.megakernel.counters[counterIndex] = FreshCounter(
            42u, "L6 production counter readback");
        fresh.megakernel.traceMilliseconds = FreshDuration(
            3.1, "runtime.l6.timestamp.trace");
        checks.Expect(adapter.Publish(std::move(fresh)).Accepted(),
            "fresh counter frame must publish");
        checks.Expect(model.Refresh(adapter, { 11u, 5u, 0u, 2u, 3u }).Accepted(),
            "unavailable-to-fresh counter must not mutate the metric contract");
        counter = model.FindMetric("wave2.l6.megakernel.counter.0");
        checks.Expect(counter != nullptr
                && counter->availability == TelemetryAvailability::Fresh
                && counter->currentValue == 42.0
                && counter->descriptor.source == "L6.Megakernel.counters.0",
            "fresh counter must retain its canonical descriptor source");

        Wave2TelemetryFrame unavailableAgain;
        unavailableAgain.generation = { 12u, 5u, 2u, 3u };
        unavailableAgain.megakernel.availability = TelemetryAvailability::Fresh;
        unavailableAgain.megakernel.counters[counterIndex].reason =
            "profiler counters are disabled";
        unavailableAgain.megakernel.traceMilliseconds = FreshDuration(
            3.2, "runtime.l6.timestamp.trace");
        checks.Expect(adapter.Publish(std::move(unavailableAgain)).Accepted(),
            "second counter-unavailable frame must publish");
        checks.Expect(model.Refresh(adapter, { 12u, 5u, 0u, 2u, 3u }).Accepted(),
            "fresh-to-unavailable counter must not mutate the metric contract");
        counter = model.FindMetric("wave2.l6.megakernel.counter.0");
        checks.Expect(counter != nullptr
                && counter->availability == TelemetryAvailability::Unavailable
                && !counter->currentValue.has_value()
                && counter->descriptor.source == "L6.Megakernel.counters.0"
                && counter->rolling.sampleCount == 1u,
            "counter disable must keep its canonical source and accepted history");
    }

    void TestInvalidMeasurementAndGeneration(Checks& checks)
    {
        Wave2TelemetryAdapter adapter;
        Wave2TelemetryFrame frame;
        frame.generation = { 30u, 4u, 2u, 2u };
        frame.flattenedSoftwareGpu.counters.hits.availability = TelemetryAvailability::Fresh;
        // Fresh without a value is malformed producer state; the adapter must
        // expose Pending rather than inventing a count.
        const Wave2PublishResult first = adapter.Publish(std::move(frame));
        checks.Expect(first.Accepted(), "malformed-but-typed frame must publish for fail-closed mapping");
        const DebugProfilerSnapshot malformed = adapter.ReadSnapshot();
        checks.Expect(malformed.availability == TelemetryAvailability::Pending
                && malformed.metrics[1].availability == TelemetryAvailability::Pending
                && !malformed.metrics[1].value.has_value(),
            "Fresh-without-value must become Pending with no fabricated count");

        Wave2TelemetryFrame regression;
        regression.generation = { 29u, 4u, 2u, 2u };
        const Wave2PublishResult rejected = adapter.Publish(std::move(regression));
        checks.Expect(rejected.code == Wave2PublishCode::RejectedGenerationRegression,
            "generation regression must be rejected before replacing the latest frame");

        Wave2TelemetryFrame invalid;
        invalid.generation = { 31u, 4u, 2u, 2u };
        invalid.flattenedSoftwareGpu.counters.rays.availability =
            static_cast<TelemetryAvailability>(255u);
        const Wave2PublishResult accepted = adapter.Publish(std::move(invalid));
        checks.Expect(accepted.Accepted(), "invalid upstream enum must be retained for fail-closed reporting");
        const DebugProfilerSnapshot invalidSnapshot = adapter.ReadSnapshot();
        checks.Expect(invalidSnapshot.availability == TelemetryAvailability::Invalid
                && invalidSnapshot.metrics.front().availability == TelemetryAvailability::Invalid
                && !invalidSnapshot.metrics.front().value.has_value(),
            "invalid upstream enum must map to Invalid without a numeric value");

        Wave2TelemetryAdapter mixedAdapter;
        Wave2TelemetryFrame mixed;
        mixed.generation = { 40u, 5u, 3u, 3u };
        mixed.flattenedSoftwareGpu.counters.rays = FreshCounter(
            64u, "runtime.l4.readback.rays");
        mixed.hardwareRayQuery.counters.invalidHits.availability =
            static_cast<TelemetryAvailability>(255u);
        checks.Expect(mixedAdapter.Publish(std::move(mixed)).Accepted(),
            "mixed fresh/invalid producer frame must publish for fail-closed mapping");
        const DebugProfilerSnapshot mixedSnapshot = mixedAdapter.ReadSnapshot();
        checks.Expect(mixedSnapshot.availability == TelemetryAvailability::Invalid
                && mixedSnapshot.metrics.front().availability == TelemetryAvailability::Fresh,
            "an invalid producer row must dominate the provider status without hiding fresh rows");
    }
}

bool RunWave2TelemetryAdapterTests(std::ostream& output)
{
    Checks checks(output);
    TestUnavailableAndPending(checks);
    TestFreshCountersAndProvenance(checks);
    TestCanonicalSourceAcrossLaneTransitions(checks);
    TestCanonicalCounterSourceAcrossProfilerTransitions(checks);
    TestInvalidMeasurementAndGeneration(checks);
    if (checks.Passed())
    {
        output << "L10 Wave 2 typed telemetry adapter checks passed.\n";
    }
    return checks.Passed();
}
