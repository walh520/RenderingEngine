#include "renderers/Wave4Acceptance.hpp"

#include <cmath>
#include <iterator>
#include <utility>

namespace RenderingEngine::Wave4
{
    namespace
    {
        [[nodiscard]] AcceptanceResult NotRun(std::string reason)
        {
            return {AcceptanceState::NotRun, std::move(reason)};
        }

        [[nodiscard]] AcceptanceResult Reject(std::string reason)
        {
            return {AcceptanceState::Rejected, std::move(reason)};
        }

        [[nodiscard]] bool IsFiniteMetric(
            const MetricEvidence& metrics) noexcept
        {
            return metrics.measured
                && std::isfinite(metrics.meanAbsoluteError)
                && metrics.meanAbsoluteError >= 0.0
                && std::isfinite(metrics.rootMeanSquareError)
                && metrics.rootMeanSquareError >= 0.0
                && std::isfinite(metrics.peakSignalToNoiseRatio);
        }

        [[nodiscard]] bool IsRealtimeLeg(
            const ComparisonLeg leg) noexcept
        {
            return leg == ComparisonLeg::Uniform
                || leg == ComparisonLeg::PowerWeighted
                || leg == ComparisonLeg::Restir;
        }

        [[nodiscard]] bool IsValidLegBias(
            const ComparisonEvidence& evidence) noexcept
        {
            if (evidence.leg == ComparisonLeg::Restir)
            {
                return evidence.bias == ComparisonBiasMode::Biased
                    || evidence.bias == ComparisonBiasMode::ReferenceCorrection;
            }
            return evidence.bias == ComparisonBiasMode::NotApplicable;
        }

        [[nodiscard]] bool SameIdentityExceptTier(
            const ComparisonIdentity& left,
            const ComparisonIdentity& right) noexcept
        {
            return left.sceneFingerprint == right.sceneFingerprint
                && left.cameraFingerprint == right.cameraFingerprint
                && left.assetFingerprint == right.assetFingerprint
                && left.configFingerprint == right.configFingerprint
                && left.machineFingerprint == right.machineFingerprint
                && left.driverFingerprint == right.driverFingerprint
                && left.powerProfileToken == right.powerProfileToken
                && left.baseSeed == right.baseSeed
                && left.frameIndex == right.frameIndex
                && left.frameGeneration == right.frameGeneration
                && left.configGeneration == right.configGeneration
                && left.sceneGeneration == right.sceneGeneration
                && left.resourceGeneration == right.resourceGeneration
                && left.lightGeneration == right.lightGeneration
                && left.width == right.width
                && left.height == right.height;
        }

        [[nodiscard]] const ComparisonEvidence* Find(
            std::span<const ComparisonEvidence> evidence,
            const ManyLightsTier tier,
            const ComparisonLeg leg,
            const ComparisonBiasMode bias) noexcept
        {
            for (const ComparisonEvidence& item : evidence)
            {
                if (item.identity.tier == tier && item.leg == leg
                    && item.bias == bias)
                {
                    return &item;
                }
            }
            return nullptr;
        }

        [[nodiscard]] AcceptanceResult ValidateCommonEvidence(
            const ComparisonEvidence& evidence,
            const ComparisonPolicy& policy)
        {
            if (evidence.providerId.empty()
                || evidence.provenanceProviderId.empty()
                || evidence.provenanceDetail.empty()
                || evidence.providerId != evidence.provenanceProviderId)
            {
                return Reject("Wave 4 provider and provenance identities are incomplete or inconsistent.");
            }
            if (evidence.provenance == EvidenceProvenance::SyntheticTest)
            {
                return NotRun("Synthetic Wave 4 evidence cannot satisfy live acceptance.");
            }
            if (evidence.provenance != EvidenceProvenance::LiveRuntime
                && evidence.provenance != EvidenceProvenance::ImportedArtifact)
            {
                return Reject("Wave 4 evidence provenance is invalid.");
            }
            if (evidence.freshness == EvidenceFreshness::Unavailable
                || evidence.freshness == EvidenceFreshness::Stale)
            {
                return NotRun("Wave 4 provider evidence is unavailable or stale.");
            }
            if (evidence.freshness != EvidenceFreshness::Fresh)
            {
                return Reject("Wave 4 evidence freshness is invalid.");
            }
            if (!IsComplete(evidence.identity))
            {
                return Reject("Wave 4 evidence identity is incomplete.");
            }
            if (evidence.lightCount
                != LightCount(evidence.identity.tier))
            {
                return Reject("Many Lights evidence does not identify the declared 100/1,000/10,000-light tier.");
            }
            if (!IsValidLegBias(evidence))
            {
                return Reject("Wave 4 leg and ReSTIR bias mode are inconsistent.");
            }
            if (evidence.budget.candidateBudget == 0u
                || evidence.budget.visibilityBudget == 0u
                || evidence.budget.identityToken.empty()
                || evidence.budget.observedCandidates
                    != evidence.budget.candidateBudget
                || evidence.budget.observedVisibilityRays
                    > evidence.budget.visibilityBudget)
            {
                return Reject("Candidate or visibility evidence exceeds or does not consume its declared budget.");
            }
            if (evidence.providerReported && !evidence.runtimeExecuted)
            {
                return Reject("Provider-reported Wave 4 evidence lacks a runtime execution.");
            }
            if (!evidence.runtimeExecuted || !evidence.providerReported)
            {
                return NotRun("Live Wave 4 provider/runtime evidence was not captured.");
            }
            if (IsRealtimeLeg(evidence.leg)
                && (!evidence.primaryDirectOwnership
                    || !evidence.splitDirectSignal
                    || evidence.winnerVisibilityDispatches != 1u))
            {
                return Reject("Primary direct ownership, split direct signal, or exactly-one winner visibility is missing.");
            }
            if (policy.requireDebugReadback
                && evidence.leg == ComparisonLeg::Restir
                && !evidence.debugReadback)
            {
                return NotRun("Wave 4 reservoir/debug readback was not captured.");
            }
            if (evidence.leg == ComparisonLeg::HighSppReference
                && (evidence.metrics.measured
                    || !std::isfinite(evidence.metrics.meanAbsoluteError)
                    || !std::isfinite(evidence.metrics.rootMeanSquareError)
                    || !std::isfinite(evidence.metrics.peakSignalToNoiseRatio)
                    || evidence.metrics.meanAbsoluteError != 0.0
                    || evidence.metrics.rootMeanSquareError != 0.0
                    || evidence.metrics.peakSignalToNoiseRatio != 0.0))
            {
                return Reject("The high-SPP oracle must not report quality metrics against itself.");
            }
            if (policy.requireFiniteMetrics && IsRealtimeLeg(evidence.leg)
                && !IsFiniteMetric(evidence.metrics))
            {
                return NotRun("Finite MAE/RMSE/PSNR evidence was not captured.");
            }
            if (policy.requireTimingProvenance)
            {
                const TimingProvenance expected =
                    evidence.leg == ComparisonLeg::HighSppReference
                        ? TimingProvenance::CpuWallClock
                        : TimingProvenance::VulkanGpuTimestamp;
                if (evidence.timingProvenance == TimingProvenance::NotMeasured)
                {
                    return NotRun("Required GPU/CPU timing provenance was not captured.");
                }
                if (evidence.timingProvenance != expected)
                {
                    return Reject("Realtime legs require Vulkan GPU timestamps and the high-SPP oracle requires CPU wall-clock timing.");
                }
            }
            if (policy.requireVisualAcceptance && !evidence.visualAccepted)
            {
                return NotRun("Human visual acceptance was not captured.");
            }
            if (evidence.leg == ComparisonLeg::Restir
                && (!evidence.temporalReuse || !evidence.spatialReuse
                    || evidence.reservoirM == 0u))
            {
                return Reject("ReSTIR evidence lacks temporal/spatial reuse or a non-empty reservoir.");
            }
            if (evidence.leg == ComparisonLeg::HighSppReference
                && !evidence.referenceCaptured)
            {
                return NotRun("The high-SPP CPU reference artifact was not captured.");
            }
            return {AcceptanceState::Accepted, ""};
        }
    }

    bool IsComplete(const ComparisonIdentity& identity) noexcept
    {
        return !identity.sceneFingerprint.empty()
            && !identity.cameraFingerprint.empty()
            && !identity.assetFingerprint.empty()
            && !identity.configFingerprint.empty()
            && !identity.machineFingerprint.empty()
            && !identity.driverFingerprint.empty()
            && !identity.powerProfileToken.empty()
            && identity.frameGeneration != 0u
            && identity.configGeneration != 0u
            && identity.sceneGeneration != 0u
            && identity.resourceGeneration != 0u
            && identity.lightGeneration != 0u
            && identity.width != 0u
            && identity.height != 0u
            && (identity.tier == ManyLightsTier::Lights100
                || identity.tier == ManyLightsTier::Lights1000
                || identity.tier == ManyLightsTier::Lights10000);
    }

    bool SameRunIdentity(
        const ComparisonIdentity& left,
        const ComparisonIdentity& right) noexcept
    {
        return SameIdentityExceptTier(left, right)
            && left.tier == right.tier;
    }

    AcceptanceResult EvaluateFrameEvidence(
        const FramePlan& plan,
        const ComparisonEvidence& evidence,
        const ComparisonPolicy& policy)
    {
        if (!plan.IsReady() || !ValidateFramePlan(plan))
        {
            return NotRun("Wave 4 frame plan is not ready or failed its dependency audit.");
        }
        if (IsRealtimeLeg(evidence.leg)
            && evidence.winnerVisibilityDispatches
                != plan.winnerVisibilityPassCount)
        {
            return Reject("Runtime winner-visibility dispatch count does not match the declarative frame graph.");
        }
        return ValidateCommonEvidence(evidence, policy);
    }

    AcceptanceResult EvaluateManyLightsComparison(
        const std::span<const ComparisonEvidence> evidence,
        const ComparisonPolicy& policy)
    {
        if (evidence.empty())
        {
            return NotRun("No Many Lights comparison evidence was supplied.");
        }
        if (policy.restirBias != ComparisonBiasMode::Biased
            && policy.restirBias != ComparisonBiasMode::ReferenceCorrection)
        {
            return Reject("The four-leg comparison requires one explicit ReSTIR bias mode.");
        }

        constexpr ManyLightsTier tiers[] = {
            ManyLightsTier::Lights100,
            ManyLightsTier::Lights1000,
            ManyLightsTier::Lights10000};
        constexpr std::size_t requiredEvidenceCount =
            std::size(tiers) * 4u;
        if (evidence.size() != requiredEvidenceCount)
        {
            return NotRun("Each tier requires exactly Uniform, Power, one selected ReSTIR bias, and High-SPP CPU reference evidence.");
        }
        const ComparisonIdentity* firstIdentity = nullptr;
        for (const ComparisonEvidence& item : evidence)
        {
            if (!IsComplete(item.identity))
            {
                return Reject("Many Lights evidence has an invalid tier or identity.");
            }
            const AcceptanceResult common =
                ValidateCommonEvidence(item, policy);
            if (common.state != AcceptanceState::Accepted)
            {
                return common;
            }
            for (const ComparisonEvidence& prior : evidence)
            {
                if (&prior == &item)
                {
                    break;
                }
                if (prior.identity.tier == item.identity.tier
                    && prior.leg == item.leg && prior.bias == item.bias)
                {
                    return Reject("Many Lights evidence contains a duplicate tier/leg/bias record.");
                }
            }
            if (firstIdentity == nullptr)
            {
                firstIdentity = &item.identity;
            }
            else if (!SameIdentityExceptTier(*firstIdentity, item.identity))
            {
                return Reject("Many Lights legs do not share one scene/camera/seed/generation identity.");
            }
        }

        bool haveCommonBudget = false;
        std::uint64_t commonCandidateBudget = 0u;
        std::uint64_t commonVisibilityBudget = 0u;
        std::string_view commonBudgetIdentity{};
        for (const ManyLightsTier tier : tiers)
        {
            const ComparisonEvidence* uniform = Find(
                evidence, tier, ComparisonLeg::Uniform,
                ComparisonBiasMode::NotApplicable);
            const ComparisonEvidence* power = Find(
                evidence, tier, ComparisonLeg::PowerWeighted,
                ComparisonBiasMode::NotApplicable);
            const ComparisonEvidence* restir = Find(
                evidence, tier, ComparisonLeg::Restir,
                policy.restirBias);
            const ComparisonEvidence* reference = Find(
                evidence, tier, ComparisonLeg::HighSppReference,
                ComparisonBiasMode::NotApplicable);
            if (uniform == nullptr || power == nullptr || restir == nullptr
                || reference == nullptr)
            {
                return NotRun("Each 100/1,000/10,000 tier requires Uniform, Power, the selected ReSTIR bias, and a High-SPP CPU reference.");
            }
            if (uniform->budget.candidateBudget
                    != power->budget.candidateBudget
                || uniform->budget.candidateBudget
                    != restir->budget.candidateBudget
                || uniform->budget.visibilityBudget
                    != power->budget.visibilityBudget
                || uniform->budget.visibilityBudget
                    != restir->budget.visibilityBudget
                || uniform->budget.identityToken
                    != power->budget.identityToken
                || uniform->budget.identityToken
                    != restir->budget.identityToken)
            {
                return Reject("Uniform, Power, and the selected ReSTIR mode do not use one identical realtime budget identity and count.");
            }
            if (reference->budget.identityToken
                    == uniform->budget.identityToken)
            {
                return Reject("The High-SPP CPU reference requires an independent budget identity.");
            }
            if (!haveCommonBudget)
            {
                commonCandidateBudget = uniform->budget.candidateBudget;
                commonVisibilityBudget = uniform->budget.visibilityBudget;
                commonBudgetIdentity = uniform->budget.identityToken;
                haveCommonBudget = true;
            }
            else if (uniform->budget.candidateBudget != commonCandidateBudget
                || uniform->budget.visibilityBudget != commonVisibilityBudget
                || uniform->budget.identityToken != commonBudgetIdentity)
            {
                return Reject("The 100/1,000/10,000 tiers do not use one common realtime budget.");
            }
        }

        return {AcceptanceState::Accepted,
            "Many Lights 100/1,000/10,000 four-leg evidence uses one selected ReSTIR bias, an independent High-SPP CPU oracle, equal realtime budgets, and explicit timing provenance."};
    }

    AcceptanceResult EvaluateHistoryTransition(
        const HistoryTransitionEvidence& evidence)
    {
        if (evidence.configGeneration == 0u
            || evidence.sceneGeneration == 0u
            || evidence.resourceGeneration == 0u
            || evidence.lightGeneration == 0u
            || evidence.width == 0u || evidence.height == 0u)
        {
            return Reject("History transition identity is incomplete.");
        }

        const bool exactReuse = evidence.previous.valid
            && evidence.nextFrameIndex != 0u
            && evidence.previous.publishedFrame + 1u
                == evidence.nextFrameIndex
            && evidence.previous.configGeneration
                == evidence.configGeneration
            && evidence.previous.sceneGeneration
                == evidence.sceneGeneration
            && evidence.previous.resourceGeneration
                == evidence.resourceGeneration
            && evidence.previous.lightGeneration
                == evidence.lightGeneration
            && evidence.previous.tier == evidence.tier
            && evidence.previous.width == evidence.width
            && evidence.previous.height == evidence.height
            && evidence.previous.backend == evidence.backend
            && evidence.previous.transportModel == evidence.transportModel
            && evidence.previous.executionArchitecture
                == evidence.executionArchitecture
            && evidence.previous.reconstruction == evidence.reconstruction;
        if (exactReuse && evidence.observed != HistoryDecision::Reuse)
        {
            return Reject("An exact previous frame, including light generation, must reuse history.");
        }
        if (!exactReuse && evidence.observed != HistoryDecision::Reset)
        {
            return Reject("A changed scene/config/resource/light generation must reset history.");
        }
        return {AcceptanceState::Accepted,
            exactReuse ? "History reuse identity is exact."
                       : "History invalidation is fail-closed, including light generation."};
    }

    std::string_view ToString(const AcceptanceState state) noexcept
    {
        switch (state)
        {
        case AcceptanceState::NotRun: return "not-run";
        case AcceptanceState::Accepted: return "accepted";
        case AcceptanceState::Rejected: return "rejected";
        default: return "invalid";
        }
    }

    std::string_view ToString(const ComparisonLeg leg) noexcept
    {
        switch (leg)
        {
        case ComparisonLeg::Uniform: return "uniform";
        case ComparisonLeg::PowerWeighted: return "power-weighted";
        case ComparisonLeg::Restir: return "restir";
        case ComparisonLeg::HighSppReference: return "high-spp-reference";
        default: return "invalid";
        }
    }

    std::string_view ToString(const ComparisonBiasMode mode) noexcept
    {
        switch (mode)
        {
        case ComparisonBiasMode::NotApplicable: return "not-applicable";
        case ComparisonBiasMode::Biased: return "biased";
        case ComparisonBiasMode::ReferenceCorrection: return "reference-correction";
        default: return "invalid";
        }
    }

    std::string_view ToString(const TimingProvenance provenance) noexcept
    {
        switch (provenance)
        {
        case TimingProvenance::NotMeasured: return "not-measured";
        case TimingProvenance::VulkanGpuTimestamp:
            return "vulkan-gpu-timestamp";
        case TimingProvenance::CpuWallClock: return "cpu-wall-clock";
        default: return "invalid";
        }
    }

    std::string_view ToString(const EvidenceProvenance provenance) noexcept
    {
        switch (provenance)
        {
        case EvidenceProvenance::LiveRuntime: return "live-runtime";
        case EvidenceProvenance::ImportedArtifact: return "imported-artifact";
        case EvidenceProvenance::SyntheticTest: return "synthetic-test";
        default: return "invalid";
        }
    }

    std::string_view ToString(const EvidenceFreshness freshness) noexcept
    {
        switch (freshness)
        {
        case EvidenceFreshness::Unavailable: return "unavailable";
        case EvidenceFreshness::Fresh: return "fresh";
        case EvidenceFreshness::Stale: return "stale";
        default: return "invalid";
        }
    }
}
