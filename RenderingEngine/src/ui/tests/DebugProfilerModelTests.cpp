#include "ui/DebugProfilerModel.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    using namespace RenderingEngine::Ui;

    class FixtureProvider final : public IDebugProfilerProvider
    {
    public:
        DebugProfilerSnapshot snapshot;

        [[nodiscard]] DebugProfilerSnapshot ReadSnapshot() override
        {
            return snapshot;
        }
    };

    [[nodiscard]] MetricObservation MakeMetric(
        std::string stableId,
        MetricDomain domain,
        MetricUnit unit,
        std::optional<double> value,
        TelemetryAvailability availability = TelemetryAvailability::Fresh,
        std::string reason = {})
    {
        MetricObservation observation;
        observation.descriptor.stableId = std::move(stableId);
        observation.descriptor.label = "Fixture metric";
        observation.descriptor.domain = domain;
        observation.descriptor.unit = unit;
        observation.descriptor.source = "fixture.timestamp-provider";
        observation.availability = availability;
        observation.value = value;
        observation.reason = std::move(reason);
        return observation;
    }

    [[nodiscard]] DebugResourceObservation MakeDebugResource(
        std::string stableId,
        std::string opaqueToken,
        TelemetryAvailability availability = TelemetryAvailability::Fresh,
        std::string reason = {})
    {
        DebugResourceObservation observation;
        observation.descriptor.stableId = std::move(stableId);
        observation.descriptor.label = "Fixture debug resource";
        observation.descriptor.opaqueUiToken = std::move(opaqueToken);
        observation.descriptor.format = "rgba16f";
        observation.descriptor.extent = { 1280u, 720u, 1u };
        observation.descriptor.legend = "RGB = world-space normal";
        observation.descriptor.owner = "L6";
        observation.availability = availability;
        observation.reason = std::move(reason);
        return observation;
    }

    [[nodiscard]] DebugProfilerSnapshot MakeFreshSnapshot(
        std::string providerId,
        TelemetryProvenance provenance,
        std::uint64_t frameGeneration,
        std::uint64_t configGeneration)
    {
        DebugProfilerSnapshot snapshot;
        snapshot.providerId = std::move(providerId);
        snapshot.provenance = provenance;
        snapshot.availability = TelemetryAvailability::Fresh;
        snapshot.frameGeneration = frameGeneration;
        snapshot.configGeneration = configGeneration;
        return snapshot;
    }

    class Checks final
    {
    public:
        explicit Checks(std::ostream& output)
            : output_(output)
        {
        }

        void Expect(bool condition, std::string_view message)
        {
            if (!condition)
            {
                output_ << "L10 DebugProfilerModel test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failures_ == 0u;
        }

    private:
        std::ostream& output_;
        std::size_t failures_ = 0;
    };

    void TestRollingStatisticsAndReset(Checks& checks)
    {
        DebugProfilerModel model(4u);
        FixtureProvider provider;
        provider.snapshot = MakeFreshSnapshot(
            "live.runtime",
            TelemetryProvenance::LiveRuntime,
            10u,
            2u);
        provider.snapshot.metrics.push_back(MakeMetric(
            "gpu.frame-ms", MetricDomain::Gpu, MetricUnit::Milliseconds, 0.0));
        provider.snapshot.metrics.push_back(MakeMetric(
            "gpu.ray-count", MetricDomain::Traversal, MetricUnit::Count, 42.0));
        provider.snapshot.debugResources.push_back(MakeDebugResource(
            "debug.world-normal", "ui-token/frame-10/normal"));

        const TelemetryUpdateResult initial = model.Refresh(provider, { 10u, 2u, 0u });
        checks.Expect(initial.Accepted(), "fresh live provider snapshot must be accepted");

        const MetricView* zeroMetric = model.FindMetric("gpu.frame-ms");
        checks.Expect(zeroMetric != nullptr
            && zeroMetric->availability == TelemetryAvailability::Fresh
            && zeroMetric->currentValue.has_value()
            && *zeroMetric->currentValue == 0.0
            && zeroMetric->rolling.sampleCount == 1u,
            "a real zero must remain a present fresh sample, not become unavailable");

        const DebugResourceView* resource = model.FindDebugResource("debug.world-normal");
        checks.Expect(resource != nullptr
            && resource->descriptor.opaqueUiToken == "ui-token/frame-10/normal"
            && resource->descriptor.format == "rgba16f"
            && resource->descriptor.extent.width == 1280u
            && resource->descriptor.extent.height == 720u
            && resource->descriptor.extent.depth == 1u
            && resource->descriptor.legend == "RGB = world-space normal"
            && resource->descriptor.owner == "L6",
            "debug catalog must preserve only provider metadata and its opaque UI token");

        for (std::uint64_t frame = 11u; frame <= 14u; ++frame)
        {
            provider.snapshot = MakeFreshSnapshot(
                "live.runtime",
                TelemetryProvenance::LiveRuntime,
                frame,
                2u);
            provider.snapshot.metrics.push_back(MakeMetric(
                "gpu.frame-ms",
                MetricDomain::Gpu,
                MetricUnit::Milliseconds,
                static_cast<double>(frame - 10u)));
            checks.Expect(model.Refresh(provider, { frame, 2u, 0u }).Accepted(),
                "newer rolling sample must be accepted");
        }

        const MetricView* rolling = model.FindMetric("gpu.frame-ms");
        checks.Expect(rolling != nullptr
            && rolling->rolling.sampleCount == 4u
            && rolling->rolling.latest == 4.0
            && rolling->rolling.minimum == 1.0
            && rolling->rolling.maximum == 4.0
            && rolling->rolling.median == 2.5
            && rolling->rolling.percentile95 == 4.0,
            "rolling window must report latest/min/max/median/nearest-rank-p95");

        const MetricView* omittedMetric = model.FindMetric("gpu.ray-count");
        resource = model.FindDebugResource("debug.world-normal");
        checks.Expect(omittedMetric != nullptr
            && omittedMetric->availability == TelemetryAvailability::Unavailable
            && !omittedMetric->currentValue.has_value()
            && !omittedMetric->reason.empty(),
            "an omitted metric must remain visible as unavailable with a reason");
        checks.Expect(resource != nullptr
            && resource->availability == TelemetryAvailability::Unavailable
            && resource->descriptor.opaqueUiToken.empty()
            && !resource->reason.empty(),
            "an omitted debug resource must lose its token and show why it is missing");

        provider.snapshot.metrics.front().value = 40.0;
        checks.Expect(model.Refresh(provider, { 14u, 2u, 0u }).Accepted(),
            "same-generation provider correction must be accepted");
        rolling = model.FindMetric("gpu.frame-ms");
        checks.Expect(rolling != nullptr
            && rolling->rolling.sampleCount == 4u
            && rolling->rolling.latest == 40.0,
            "same-generation correction must replace, not double-count, the latest sample");

        model.ApplyReset(ResetResource::Accumulation);
        rolling = model.FindMetric("gpu.frame-ms");
        checks.Expect(rolling != nullptr && rolling->rolling.sampleCount == 4u,
            "a non-P reset must not clear profiler statistics");

        model.ApplyReset(ResetResource::ProfilerStatistics);
        rolling = model.FindMetric("gpu.frame-ms");
        checks.Expect(rolling != nullptr
            && rolling->rolling.sampleCount == 0u
            && !rolling->currentValue.has_value()
            && rolling->availability == TelemetryAvailability::Pending
            && !rolling->reason.empty(),
            "P reset must clear sample_count and expose a pending reason");

        provider.snapshot.frameGeneration = 15u;
        provider.snapshot.metrics.front().value = 0.0;
        checks.Expect(model.Refresh(provider, { 15u, 2u, 0u }).Accepted(),
            "a fresh sample after P reset must restart the series");
        rolling = model.FindMetric("gpu.frame-ms");
        checks.Expect(rolling != nullptr
            && rolling->rolling.sampleCount == 1u
            && rolling->currentValue == 0.0,
            "post-reset zero sample must restart at sample_count one");

        provider.snapshot.frameGeneration = 1u;
        provider.snapshot.configGeneration = 3u;
        provider.snapshot.metrics.front().value = 7.0;
        checks.Expect(model.Refresh(provider, { 1u, 3u, 0u }).Accepted(),
            "a new current config generation may restart its frame generation");
        rolling = model.FindMetric("gpu.frame-ms");
        checks.Expect(rolling != nullptr
            && rolling->rolling.sampleCount == 1u
            && rolling->rolling.latest == 7.0,
            "a config-generation transition must not mix old rolling samples");
    }

    void TestAvailabilityAndProvenance(Checks& checks)
    {
        DebugProfilerModel model;
        FixtureProvider live;
        live.snapshot = MakeFreshSnapshot(
            "live.status", TelemetryProvenance::LiveRuntime, 5u, 1u);
        live.snapshot.metrics.push_back(MakeMetric(
            "gpu.trace-ms", MetricDomain::Traversal, MetricUnit::Milliseconds, 2.0));
        live.snapshot.debugResources.push_back(MakeDebugResource(
            "debug.hit-distance", "ui-token/hit-distance"));

        checks.Expect(model.Refresh(live, { 10u, 1u, 2u }).Accepted(),
            "lagging but monotonic provider snapshot must be accepted as stale");
        const ProviderView* providerView = model.FindProvider("live.status");
        const MetricView* metric = model.FindMetric("gpu.trace-ms");
        const DebugResourceView* resource = model.FindDebugResource("debug.hit-distance");
        checks.Expect(providerView != nullptr
            && providerView->availability == TelemetryAvailability::Stale
            && providerView->provenance == TelemetryProvenance::LiveRuntime
            && !providerView->reason.empty(),
            "frame-lag policy must turn the provider stale with a reason");
        checks.Expect(metric != nullptr
            && metric->availability == TelemetryAvailability::Stale
            && metric->frameGeneration == 5u
            && metric->configGeneration == 1u
            && !metric->reason.empty(),
            "metric view must carry provenance generations and stale reason");
        checks.Expect(resource != nullptr
            && resource->availability == TelemetryAvailability::Stale
            && !resource->reason.empty(),
            "debug resource must inherit provider staleness and its reason");

        live.snapshot = MakeFreshSnapshot(
            "live.status", TelemetryProvenance::LiveRuntime, 11u, 1u);
        live.snapshot.availability = TelemetryAvailability::Pending;
        live.snapshot.reason = "timestamp query results are not ready";
        checks.Expect(model.Refresh(live, { 11u, 1u, 0u }).Accepted(),
            "provider may explicitly publish pending without fabricated samples");
        metric = model.FindMetric("gpu.trace-ms");
        resource = model.FindDebugResource("debug.hit-distance");
        checks.Expect(metric != nullptr
            && metric->availability == TelemetryAvailability::Pending
            && !metric->currentValue.has_value()
            && metric->reason == "timestamp query results are not ready",
            "pending provider must make an old metric unavailable with the provider reason");
        checks.Expect(resource != nullptr
            && resource->availability == TelemetryAvailability::Pending
            && resource->descriptor.opaqueUiToken.empty(),
            "pending provider must revoke an old debug-resource token");

        live.snapshot = MakeFreshSnapshot(
            "live.status", TelemetryProvenance::LiveRuntime, 12u, 1u);
        live.snapshot.metrics.push_back(MakeMetric(
            "cpu.wait-ms",
            MetricDomain::Cpu,
            MetricUnit::Milliseconds,
            std::nullopt,
            TelemetryAvailability::Unavailable,
            "CPU timing was not instrumented"));
        live.snapshot.debugResources.push_back(MakeDebugResource(
            "debug.motion",
            {},
            TelemetryAvailability::Pending,
            "motion image is awaiting renderer publication"));
        checks.Expect(model.Refresh(live, { 12u, 1u, 0u }).Accepted(),
            "fresh provider may expose unavailable and pending catalog rows");
        metric = model.FindMetric("cpu.wait-ms");
        resource = model.FindDebugResource("debug.motion");
        checks.Expect(metric != nullptr
            && metric->availability == TelemetryAvailability::Unavailable
            && !metric->currentValue.has_value()
            && !metric->reason.empty(),
            "unavailable metric must differ from a numeric zero and explain why");
        checks.Expect(resource != nullptr
            && resource->availability == TelemetryAvailability::Pending
            && resource->descriptor.opaqueUiToken.empty()
            && !resource->reason.empty(),
            "pending debug resource must have no token and must explain why");

        live.snapshot = MakeFreshSnapshot(
            "live.status", TelemetryProvenance::LiveRuntime, 13u, 1u);
        live.snapshot.availability = TelemetryAvailability::Invalid;
        live.snapshot.reason = "provider rejected a malformed query payload";
        checks.Expect(model.Refresh(live, { 13u, 1u, 0u }).Accepted(),
            "a structurally valid provider status may explicitly report invalid data");
        metric = model.FindMetric("cpu.wait-ms");
        checks.Expect(metric != nullptr
            && metric->availability == TelemetryAvailability::Invalid
            && !metric->currentValue.has_value()
            && metric->reason == "provider rejected a malformed query payload",
            "invalid provider data must remain distinct and display its reason");

        FixtureProvider imported;
        imported.snapshot = MakeFreshSnapshot(
            "artifact.run-42", TelemetryProvenance::ImportedArtifact, 100u, 7u);
        imported.snapshot.metrics.push_back(MakeMetric(
            "artifact.gpu-ms", MetricDomain::Gpu, MetricUnit::Milliseconds, 3.5));
        checks.Expect(model.Refresh(imported, { 100u, 7u, 0u }).Accepted(),
            "imported artifact provider must be accepted with explicit provenance");

        FixtureProvider synthetic;
        synthetic.snapshot = MakeFreshSnapshot(
            "test.fixture", TelemetryProvenance::SyntheticTest, 0u, 0u);
        synthetic.snapshot.metrics.push_back(MakeMetric(
            "test.metric", MetricDomain::Application, MetricUnit::Ratio, 1.0));
        checks.Expect(model.Refresh(synthetic, { 0u, 0u, 0u }).Accepted(),
            "synthetic fixture provider must be accepted without being relabeled live");
        const MetricView* importedMetric = model.FindMetric("artifact.gpu-ms");
        const MetricView* syntheticMetric = model.FindMetric("test.metric");
        checks.Expect(importedMetric != nullptr
            && importedMetric->provenance == TelemetryProvenance::ImportedArtifact,
            "imported metric provenance must survive into the view model");
        checks.Expect(syntheticMetric != nullptr
            && syntheticMetric->provenance == TelemetryProvenance::SyntheticTest,
            "synthetic metric provenance must survive into the view model");
    }

    void TestConfigGenerationInvalidation(Checks& checks)
    {
        DebugProfilerModel model;
        FixtureProvider provider;
        provider.snapshot = MakeFreshSnapshot(
            "generation.fixture", TelemetryProvenance::SyntheticTest, 4u, 7u);
        provider.snapshot.metrics.push_back(MakeMetric(
            "cpu.generation-ms", MetricDomain::Cpu, MetricUnit::Milliseconds, 0.5));
        provider.snapshot.debugResources.push_back(MakeDebugResource(
            "debug.generation", "ui-token/generation-7"));
        checks.Expect(model.Refresh(provider, { 4u, 7u, 0u }).Accepted(),
            "config invalidation fixture must first accept a fresh snapshot");

        model.InvalidateBeforeConfigGeneration(8u);
        const ProviderView* providerView = model.FindProvider("generation.fixture");
        const MetricView* metric = model.FindMetric("cpu.generation-ms");
        const DebugResourceView* resource = model.FindDebugResource("debug.generation");
        checks.Expect(providerView != nullptr
            && providerView->availability == TelemetryAvailability::Stale
            && !providerView->reason.empty(),
            "a config commit must immediately mark the older provider stale");
        checks.Expect(metric != nullptr
            && metric->availability == TelemetryAvailability::Stale
            && !metric->currentValue.has_value()
            && metric->rolling.sampleCount == 0u,
            "a config commit must clear current values and rolling samples from older metrics");
        checks.Expect(resource != nullptr
            && resource->availability == TelemetryAvailability::Stale
            && resource->descriptor.opaqueUiToken.empty()
            && !resource->reason.empty(),
            "a config commit must revoke older opaque debug tokens before provider refresh");

        provider.snapshot.frameGeneration = 1u;
        provider.snapshot.configGeneration = 8u;
        provider.snapshot.sceneGeneration = 2u;
        provider.snapshot.resourceGeneration = 9u;
        provider.snapshot.debugResources.front().descriptor.opaqueUiToken =
            "ui-token/resource-9";
        checks.Expect(model.Refresh(provider, { 1u, 8u, 0u, 2u, 9u }).Accepted(),
            "a compatible scene/resource generation tuple must refresh the catalog");
        model.InvalidateBeforeGenerationTuple(8u, 3u, 9u);
        resource = model.FindDebugResource("debug.generation");
        checks.Expect(resource != nullptr
            && resource->sceneGeneration == 2u
            && resource->resourceGeneration == 9u
            && resource->availability == TelemetryAvailability::Stale
            && resource->descriptor.opaqueUiToken.empty(),
            "a scene reload must revoke an older opaque token even when config is unchanged");
    }

    void TestRejectedSnapshotsAreAtomic(Checks& checks)
    {
        DebugProfilerModel model;
        FixtureProvider provider;
        provider.snapshot = MakeFreshSnapshot(
            "reject.fixture", TelemetryProvenance::SyntheticTest, 20u, 3u);
        provider.snapshot.metrics.push_back(MakeMetric(
            "gpu.duration-ms", MetricDomain::Gpu, MetricUnit::Milliseconds, 5.0));
        checks.Expect(model.Refresh(provider, { 20u, 3u, 0u }).Accepted(),
            "atomic-rejection baseline must be accepted");

        provider.snapshot.frameGeneration = 21u;
        provider.snapshot.metrics.front().value = std::numeric_limits<double>::quiet_NaN();
        TelemetryUpdateResult result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedInvalidSnapshot,
            "non-finite metric must be rejected");
        const MetricView* metric = model.FindMetric("gpu.duration-ms");
        const ProviderView* acceptedProvider = model.FindProvider("reject.fixture");
        checks.Expect(metric != nullptr
            && metric->currentValue == 5.0
            && metric->rolling.sampleCount == 1u,
            "non-finite rejection must leave the last accepted metric series unchanged");
        checks.Expect(acceptedProvider != nullptr
            && acceptedProvider->availability == TelemetryAvailability::Fresh
            && acceptedProvider->frameGeneration == 20u
            && acceptedProvider->reason.empty(),
            "a rejected update must preserve the complete last accepted provider view");

        provider.snapshot.metrics.front().value = -0.01;
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedInvalidSnapshot,
            "negative duration must be rejected");

        provider.snapshot.metrics.front().value = 6.0;
        provider.snapshot.metrics.push_back(provider.snapshot.metrics.front());
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedDuplicateStableId,
            "duplicate metric stable IDs must be rejected");
        provider.snapshot.metrics.resize(1u);

        DebugResourceObservation duplicateResource = MakeDebugResource(
            "gpu.duration-ms", "ui-token/duplicate");
        provider.snapshot.debugResources.push_back(std::move(duplicateResource));
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedDuplicateStableId,
            "metric/debug-resource stable ID collision must be rejected");
        provider.snapshot.debugResources.clear();

        provider.snapshot.frameGeneration = 19u;
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedOldGeneration,
            "regressive frame generation must be rejected");

        provider.snapshot.frameGeneration = 1u;
        provider.snapshot.configGeneration = 2u;
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedOldGeneration,
            "snapshot for an older current-config generation must be rejected");

        provider.snapshot.configGeneration = 4u;
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedInvalidSnapshot,
            "snapshot from a future config generation must be rejected");

        provider.snapshot.frameGeneration = 21u;
        provider.snapshot.configGeneration = 3u;
        provider.snapshot.metrics.front().descriptor.source = "changed.source";
        result = model.Refresh(provider, { 21u, 3u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedInvalidSnapshot,
            "stable metric ID must reject source/domain/unit contract drift");

        FixtureProvider collision;
        collision.snapshot = MakeFreshSnapshot(
            "other.provider", TelemetryProvenance::ImportedArtifact, 30u, 8u);
        collision.snapshot.metrics.push_back(MakeMetric(
            "gpu.duration-ms", MetricDomain::Gpu, MetricUnit::Milliseconds, 1.0));
        result = model.Refresh(collision, { 30u, 8u, 0u });
        checks.Expect(result.code == TelemetryUpdateCode::RejectedDuplicateStableId,
            "a different provider cannot claim an existing global stable ID");

        metric = model.FindMetric("gpu.duration-ms");
        checks.Expect(metric != nullptr
            && metric->currentValue == 5.0
            && metric->rolling.sampleCount == 1u,
            "all rejected snapshots must preserve the last accepted metric payload");
    }
}

bool RunDebugProfilerModelTests(std::ostream& output)
{
    Checks checks(output);
    TestRollingStatisticsAndReset(checks);
    TestAvailabilityAndProvenance(checks);
    TestConfigGenerationInvalidation(checks);
    TestRejectedSnapshotsAreAtomic(checks);
    if (checks.Passed())
    {
        output << "L10 provider-driven debug/profiler model checks passed.\n";
    }
    return checks.Passed();
}
