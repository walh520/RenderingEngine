#include "renderers/Wave4Acceptance.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "Wave 4 acceptance test failed: "
                    << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failures_ == 0;
        }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] RenderingEngine::Wave4::ComparisonIdentity MakeIdentity(
        const RenderingEngine::ManyLightsTier tier)
    {
        using namespace RenderingEngine::Wave4;
        ComparisonIdentity identity{};
        identity.sceneFingerprint = "many-lights-scene-sha256";
        identity.cameraFingerprint = "camera-sha256";
        identity.assetFingerprint = "asset-sha256";
        identity.configFingerprint = "config-sha256";
        identity.machineFingerprint = "machine-sha256";
        identity.driverFingerprint = "driver-sha256";
        identity.powerProfileToken = "fixed-performance";
        identity.baseSeed = 17u;
        identity.frameIndex = 42u;
        identity.frameGeneration = 43u;
        identity.configGeneration = 3u;
        identity.sceneGeneration = 5u;
        identity.resourceGeneration = 7u;
        identity.lightGeneration = 11u;
        identity.width = 256u;
        identity.height = 256u;
        identity.tier = tier;
        return identity;
    }

    [[nodiscard]] RenderingEngine::Wave4::ComparisonEvidence MakeEvidence(
        const RenderingEngine::ManyLightsTier tier,
        const RenderingEngine::Wave4::ComparisonLeg leg,
        const RenderingEngine::Wave4::ComparisonBiasMode bias)
    {
        using namespace RenderingEngine::Wave4;
        ComparisonEvidence evidence{};
        evidence.providerId = "l9.many-lights.runtime";
        evidence.provenanceProviderId = evidence.providerId;
        evidence.provenanceDetail = "provider-owned immutable readback";
        evidence.provenance = EvidenceProvenance::LiveRuntime;
        evidence.freshness = EvidenceFreshness::Fresh;
        evidence.identity = MakeIdentity(tier);
        evidence.leg = leg;
        evidence.bias = bias;
        evidence.lightCount = LightCount(tier);
        evidence.budget.candidateBudget = leg == ComparisonLeg::HighSppReference
            ? 4'096u : 64u;
        evidence.budget.visibilityBudget = leg == ComparisonLeg::HighSppReference
            ? 1'048'576u : 8u;
        evidence.budget.observedCandidates = evidence.budget.candidateBudget;
        evidence.budget.observedVisibilityRays = evidence.budget.visibilityBudget;
        evidence.budget.identityToken = leg == ComparisonLeg::HighSppReference
            ? "high-spp-reference-budget-v1"
            : "equal-realtime-budget-v1";
        if (leg != ComparisonLeg::HighSppReference)
        {
            evidence.metrics = {true, 0.02, 0.04, 28.0};
        }
        evidence.reservoirM = leg == ComparisonLeg::Restir ? 8u : 0u;
        evidence.winnerVisibilityDispatches = 1u;
        evidence.providerReported = true;
        evidence.runtimeExecuted = true;
        evidence.primaryDirectOwnership = true;
        evidence.splitDirectSignal = true;
        evidence.temporalReuse = leg == ComparisonLeg::Restir;
        evidence.spatialReuse = leg == ComparisonLeg::Restir;
        evidence.debugReadback = true;
        evidence.referenceCaptured = leg == ComparisonLeg::HighSppReference;
        evidence.timingProvenance = leg == ComparisonLeg::HighSppReference
            ? TimingProvenance::CpuWallClock
            : TimingProvenance::VulkanGpuTimestamp;
        evidence.visualAccepted = true;
        return evidence;
    }
}

bool RunWave4AcceptanceTests()
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Wave4;
    TestContext tests;
    constexpr RenderingEngine::ManyLightsTier tiers[] = {
        RenderingEngine::ManyLightsTier::Lights100,
        RenderingEngine::ManyLightsTier::Lights1000,
        RenderingEngine::ManyLightsTier::Lights10000};
    constexpr ComparisonBiasMode biasModes[] = {
        ComparisonBiasMode::Biased,
        ComparisonBiasMode::ReferenceCorrection};
    for (const ComparisonBiasMode selectedBias : biasModes)
    {
        std::array<ComparisonEvidence, 12u> evidence{};
        std::size_t cursor = 0u;
        for (const RenderingEngine::ManyLightsTier tier : tiers)
        {
            evidence[cursor++] = MakeEvidence(
                tier, ComparisonLeg::Uniform,
                ComparisonBiasMode::NotApplicable);
            evidence[cursor++] = MakeEvidence(
                tier, ComparisonLeg::PowerWeighted,
                ComparisonBiasMode::NotApplicable);
            evidence[cursor++] = MakeEvidence(
                tier, ComparisonLeg::Restir, selectedBias);
            evidence[cursor++] = MakeEvidence(
                tier, ComparisonLeg::HighSppReference,
                ComparisonBiasMode::NotApplicable);
        }

        ComparisonPolicy policy{};
        policy.restirBias = selectedBias;
        policy.requireTimingProvenance = true;
        policy.requireVisualAcceptance = true;
        const AcceptanceResult complete = EvaluateManyLightsComparison(
            std::span<const ComparisonEvidence>(evidence), policy);
        tests.Expect(complete.Passed(),
            "each selected ReSTIR bias must pass as one exact four-leg comparison across all tiers");

        evidence[0].provenance = EvidenceProvenance::SyntheticTest;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::NotRun,
            "synthetic fixtures must not satisfy live acceptance");
        evidence[0].provenance = EvidenceProvenance::LiveRuntime;

        evidence[0].freshness = EvidenceFreshness::Stale;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::NotRun,
            "stale provider evidence must remain not-run");
        evidence[0].freshness = EvidenceFreshness::Fresh;

        evidence[0].provenanceProviderId = "different.provider";
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "provider and provenance identities must match exactly");
        evidence[0].provenanceProviderId = evidence[0].providerId;

        evidence[1].budget.candidateBudget = 65u;
        const AcceptanceResult mismatchedBudget = EvaluateManyLightsComparison(
            std::span<const ComparisonEvidence>(evidence), policy);
        tests.Expect(mismatchedBudget.state == AcceptanceState::Rejected,
            "declared or observed budget mismatch must reject the comparison");
        evidence[1].budget.candidateBudget = 64u;

        evidence[2].metrics.measured = false;
        const AcceptanceResult missingMetric = EvaluateManyLightsComparison(
            std::span<const ComparisonEvidence>(evidence), policy);
        tests.Expect(missingMetric.state == AcceptanceState::NotRun,
            "missing realtime quality metrics must remain explicitly not-run");
        evidence[2].metrics = {true, 0.02, 0.04, 28.0};

        evidence[3].metrics = {true, 0.0, 0.0, 99.0};
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "the high-SPP oracle must keep quality null");
        evidence[3].metrics = {};

        evidence[3].metrics.meanAbsoluteError = 1.0;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "unmeasured High-SPP self-metrics must still remain finite zero fields");
        evidence[3].metrics = {};

        evidence[3].timingProvenance = TimingProvenance::VulkanGpuTimestamp;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "the high-SPP oracle must use CPU wall-clock timing provenance");
        evidence[3].timingProvenance = TimingProvenance::CpuWallClock;

        --evidence[0].budget.observedVisibilityRays;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).Passed(),
            "observed visibility below the declared upper bound must remain valid");
        evidence[0].budget.observedVisibilityRays =
            evidence[0].budget.visibilityBudget + 1u;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "observed visibility above the declared upper bound must reject");
        evidence[0].budget.observedVisibilityRays =
            evidence[0].budget.visibilityBudget;

        ++evidence[4].identity.frameGeneration;
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "mixed frame generations must reject the aggregate comparison");
        --evidence[4].identity.frameGeneration;

        evidence[4].identity.powerProfileToken = "different-power-profile";
        tests.Expect(EvaluateManyLightsComparison(
                std::span<const ComparisonEvidence>(evidence), policy).state
                    == AcceptanceState::Rejected,
            "mixed machine/driver/power performance identity must reject");
    }

    HistoryTransitionEvidence history{};
    history.previous = {
        true, 4u, 3u, 5u, 7u, 11u, RenderingEngine::ManyLightsTier::Lights100,
        256u, 256u, TraversalBackend::GpuFlattenedSahBvh,
        TransportModel::Pbr,
        ExecutionArchitecture::Megakernel,
        ReconstructionMode::TemporalAccumulation};
    history.nextFrameIndex = 5u;
    history.configGeneration = 3u;
    history.sceneGeneration = 5u;
    history.resourceGeneration = 7u;
    history.lightGeneration = 11u;
    history.tier = RenderingEngine::ManyLightsTier::Lights100;
    history.width = 256u;
    history.height = 256u;
    history.backend = TraversalBackend::GpuFlattenedSahBvh;
    history.transportModel = TransportModel::Pbr;
    history.executionArchitecture = ExecutionArchitecture::Megakernel;
    history.reconstruction = ReconstructionMode::TemporalAccumulation;
    history.observed = HistoryDecision::Reuse;
    tests.Expect(EvaluateHistoryTransition(history).Passed(),
        "exact history identity must reuse");
    ++history.lightGeneration;
    history.observed = HistoryDecision::Reset;
    tests.Expect(EvaluateHistoryTransition(history).Passed(),
        "light generation change must reset");

    history.previous.valid = false;
    history.nextFrameIndex = 0u;
    tests.Expect(EvaluateHistoryTransition(history).Passed(),
        "the first frame must explicitly reset history");

    if (tests.Passed())
    {
        std::cout << "Wave 4 exact four-leg, budget, timing, metric, and history acceptance checks passed.\n";
    }
    return tests.Passed();
}
