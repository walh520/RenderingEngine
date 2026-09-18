#include "ui/Wave4TelemetryAdapter.hpp"

#include <cstddef>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace
{
    using namespace RenderingEngine::Demos;
    using namespace RenderingEngine::Ui;

    class Checks final
    {
    public:
        explicit Checks(std::ostream& output) noexcept
            : output_(output)
        {
        }

        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                output_ << "L10 Wave4TelemetryAdapter test failed: " << message << '\n';
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

    [[nodiscard]] ManyLightsWave4ProviderSnapshot MakeFreshSnapshot(
        const std::uint64_t lightGeneration = 9u)
    {
        ManyLightsWave4FixedConditions fixed;
        fixed.sceneFingerprint = "scene-sha256-many-lights";
        fixed.assetFingerprint = "asset-sha256-many-lights";
        fixed.configFingerprint = "config-sha256-wave4";
        fixed.machineFingerprint = "machine-sha256-test-host";
        fixed.driverFingerprint = "driver-sha256-test-driver";
        fixed.powerProfileToken = "fixed-performance-test";
        fixed.cameraPresetToken = "many-lights-hero";
        fixed.width = 1'920u;
        fixed.height = 1'080u;
        fixed.baseSeed = 1'337u;
        fixed.frameIndex = 42u;
        fixed.generation = { 42u, 7u, 3u, 5u, lightGeneration };
        ManyLightsWave4Budget budget;
        budget.identityToken = "equal-candidates-64-visibility-32";
        budget.candidateCount = 64u;
        budget.visibilityRayCount = 32u;
        ManyLightsWave4Budget referenceBudget;
        referenceBudget.identityToken = "high-spp-4096-reference";
        referenceBudget.candidateCount = 4'096u;
        referenceBudget.visibilityRayCount = 1'048'576u;
        const auto request = BuildManyLightsWave4ComparisonRequest(
            GetManyLightsWave4Presets()[0], fixed,
            ManyLightsWave4BiasMode::Biased, budget, referenceBudget);

        ManyLightsWave4ProviderSnapshot snapshot;
        snapshot.providerId = "l9.many-lights.runtime";
        snapshot.provenance = {
            ManyLightsWave4ProvenanceSource::LiveRuntime,
            "l9.many-lights.runtime",
            "provider-owned snapshot"
        };
        snapshot.state = ManyLightsWave4SnapshotState::Fresh;
        snapshot.presetToken = request.request.preset.stableToken;
        snapshot.lightCount = request.request.preset.lightCount;
        snapshot.fixed = request.request.fixed;
        snapshot.restirBias = request.request.restirBias;
        snapshot.budget = request.request.budget;
        snapshot.referenceBudget = request.request.referenceBudget;
        for (std::size_t index = 0u; index < snapshot.legs.size(); ++index)
        {
            auto& leg = snapshot.legs[index];
            leg.leg = request.request.legs[index];
            leg.state = ManyLightsWave4SnapshotState::Fresh;
            leg.lightCount = snapshot.lightCount;
            leg.fixed = snapshot.fixed;
            leg.budget = leg.leg.highSppReference
                ? snapshot.referenceBudget : snapshot.budget;
            if (leg.leg.technique == ManyLightsWave4Technique::RestirDi)
            {
                leg.reservoir.m = 1u;
                leg.reservoir.weight = 1.0;
                leg.reservoir.lightId = 17u + static_cast<std::uint64_t>(index);
                leg.reservoir.source = "l9.reservoir.readback";
                leg.reservoir.reuseSource = std::string("temporal-spatial");
                leg.reservoir.rejectionReason = std::string();
            }
            leg.performance.elapsedMilliseconds = 0.75;
            leg.performance.timingSource = leg.leg.highSppReference
                ? ManyLightsWave4TimingSource::CpuWallClock
                : ManyLightsWave4TimingSource::VulkanGpuTimestamp;
            leg.performance.observedCandidates = *leg.budget.candidateCount;
            leg.performance.visibilityRays = *leg.budget.visibilityRayCount;
            leg.performance.memoryBytes = 8192u;
            if (!leg.leg.highSppReference)
            {
                leg.quality.mae = 0.01;
                leg.quality.rmse = 0.02;
                leg.quality.psnr = 30.0;
            }
            leg.benchmark.warmupFrameCount = 120u;
            leg.benchmark.measurementFrameCount = 1'000u;
            leg.benchmark.repeatCount = 3u;
            leg.benchmark.medianMilliseconds = 0.75;
            leg.benchmark.p95Milliseconds = 1.0;
            leg.benchmark.timingSource = *leg.performance.timingSource;
        }
        return snapshot;
    }

    [[nodiscard]] ManyLightsWave4ProviderSnapshot MakePendingSnapshot()
    {
        ManyLightsWave4ProviderSnapshot snapshot;
        snapshot.providerId = "l9.many-lights.runtime";
        snapshot.provenance = {
            ManyLightsWave4ProvenanceSource::LiveRuntime,
            "l9.many-lights.runtime",
            "awaiting query readback"
        };
        snapshot.state = ManyLightsWave4SnapshotState::Pending;
        snapshot.presetToken = "many-lights-100";
        snapshot.lightCount = 100u;
        snapshot.fixed.sceneFingerprint = "scene-sha256-many-lights";
        snapshot.fixed.assetFingerprint = "asset-sha256-many-lights";
        snapshot.fixed.configFingerprint = "config-sha256-wave4";
        snapshot.fixed.machineFingerprint = "machine-sha256-test-host";
        snapshot.fixed.driverFingerprint = "driver-sha256-test-driver";
        snapshot.fixed.powerProfileToken = "fixed-performance-test";
        snapshot.fixed.cameraPresetToken = "many-lights-hero";
        snapshot.fixed.width = 1'920u;
        snapshot.fixed.height = 1'080u;
        snapshot.fixed.baseSeed = 1'337u;
        snapshot.fixed.frameIndex = 42u;
        snapshot.fixed.generation = { 42u, 7u, 3u, 5u, 9u };
        snapshot.budget.identityToken = "equal-candidates-64-visibility-32";
        snapshot.budget.candidateCount = 64u;
        snapshot.budget.visibilityRayCount = 32u;
        snapshot.referenceBudget.identityToken = "high-spp-4096-reference";
        snapshot.referenceBudget.candidateCount = 4'096u;
        snapshot.referenceBudget.visibilityRayCount = 1'048'576u;
        return snapshot;
    }

    void TestUnavailableAndFreshMapping(Checks& checks)
    {
        Wave4TelemetryAdapter adapter;
        const DebugProfilerSnapshot empty = adapter.ReadSnapshot();
        checks.Expect(empty.availability == TelemetryAvailability::Unavailable
                && empty.metrics.empty()
                && !empty.reason.empty(),
            "adapter without a provider frame must be unavailable with no fabricated metrics");

        const Wave4PublishResult published = adapter.Publish(MakeFreshSnapshot());
        checks.Expect(published.Accepted(), "fresh live provider snapshot must publish");
        const DebugProfilerSnapshot snapshot = adapter.ReadSnapshot();
        checks.Expect(snapshot.availability == TelemetryAvailability::Fresh
                && snapshot.configGeneration == 7u
                && snapshot.sceneGeneration == 3u
                && snapshot.resourceGeneration == 5u
                && snapshot.metrics.size() == 48u,
            "fresh snapshot must preserve generations without fabricating reservoir rows for non-ReSTIR legs");
        checks.Expect(snapshot.metrics.front().value.has_value()
                && *snapshot.metrics.front().value == 0.75
                && snapshot.metrics.front().descriptor.stableId
                    == "wave4.many-lights-100.uniform-one-light.elapsed-ms",
            "typed elapsed time must map without changing its provider value");
    }

    void TestPendingAndGenerationInvalidation(Checks& checks)
    {
        Wave4TelemetryAdapter adapter;
        checks.Expect(adapter.Publish(MakeFreshSnapshot()).Accepted(),
            "baseline fresh frame must publish before invalidation checks");
        const Wave4PublishResult regression = adapter.Publish(MakeFreshSnapshot(8u));
        checks.Expect(regression.code == Wave4PublishCode::RejectedGenerationRegression,
            "a light-generation regression must be rejected");
        checks.Expect(adapter.LatestSnapshot() != nullptr
                && *adapter.LatestSnapshot()->fixed.generation.lightGeneration == 9u,
            "rejected light generation must not replace the last good snapshot");

        const Wave4PublishResult pending = adapter.Publish(MakePendingSnapshot());
        checks.Expect(pending.Accepted(),
            "pending provider state may publish without fabricating measurements");
        const DebugProfilerSnapshot pendingSnapshot = adapter.ReadSnapshot();
        checks.Expect(pendingSnapshot.availability == TelemetryAvailability::Pending
                && pendingSnapshot.metrics.empty()
                && pendingSnapshot.configGeneration == 7u,
            "pending state must be visible while numeric rows remain absent");
    }

    void TestInvalidAndSyntheticFailClosed(Checks& checks)
    {
        Wave4TelemetryAdapter adapter;
        ManyLightsWave4ProviderSnapshot invalid = MakeFreshSnapshot();
        invalid.legs[0].performance.elapsedMilliseconds =
            std::numeric_limits<double>::quiet_NaN();
        const Wave4PublishResult invalidResult = adapter.Publish(std::move(invalid));
        checks.Expect(invalidResult.code == Wave4PublishCode::RejectedInvalidSnapshot,
            "NaN GPU timing must be rejected before it reaches the UI model");
        checks.Expect(adapter.LatestSnapshot() == nullptr,
            "invalid first snapshot must not create a partial latest frame");

        ManyLightsWave4ProviderSnapshot synthetic = MakeFreshSnapshot();
        synthetic.provenance.source = ManyLightsWave4ProvenanceSource::SyntheticTest;
        const Wave4PublishResult syntheticResult = adapter.Publish(std::move(synthetic));
        checks.Expect(syntheticResult.code == Wave4PublishCode::RejectedSyntheticProvider,
            "synthetic data must never be promoted as live Wave 4 telemetry");

        Wave4TelemetryAdapter mismatchedAdapter("different.provider");
        const Wave4PublishResult mismatched =
            mismatchedAdapter.Publish(MakeFreshSnapshot());
        checks.Expect(mismatched.code == Wave4PublishCode::RejectedInvalidSnapshot,
            "adapter and upstream provider identities must match exactly");
    }
}

int RunWave4TelemetryAdapterTests(std::ostream& output)
{
    Checks checks(output);
    TestUnavailableAndFreshMapping(checks);
    TestPendingAndGenerationInvalidation(checks);
    TestInvalidAndSyntheticFailClosed(checks);
    if (checks.Passed())
    {
        output << "L10 Wave 4 telemetry adapter checks passed.\n";
    }
    return checks.Passed() ? 0 : 1;
}
