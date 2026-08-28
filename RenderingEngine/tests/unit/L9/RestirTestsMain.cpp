#include "restir/Benchmark.hpp"
#include "VulkanSmoke.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using namespace RenderingEngine::Restir;

    class TestContext final
    {
    public:
        void Begin(const std::string_view name)
        {
            ++cases_;
            current_ = name;
        }

        void Check(const bool condition, const std::string_view expression, const int line)
        {
            ++assertions_;
            if (!condition)
            {
                ++failures_;
                std::cerr << "FAIL [" << current_ << "] line " << line << ": " << expression << '\n';
            }
        }

        [[nodiscard]] int Finish() const
        {
            std::cout << "L9 ReSTIR DI tests: " << cases_ << " cases, " << assertions_
                << " assertions, " << failures_ << " failures\n";
            return failures_ == 0u ? 0 : 1;
        }

    private:
        std::string_view current_{};
        std::uint32_t cases_ = 0u;
        std::uint32_t assertions_ = 0u;
        std::uint32_t failures_ = 0u;
    };

#define L9_CHECK(context, expression) (context).Check((expression), #expression, __LINE__)

    [[nodiscard]] Candidate MakeCandidate(
        const std::uint32_t id,
        const float target,
        const float proposal = 1.0f,
        const std::uint32_t generation = 1u)
    {
        Candidate candidate{};
        candidate.identity = { CandidateSource::UniformLight, id, id + 100u, id, generation };
        candidate.directionToLight = { 0.0f, 1.0f, 0.0f };
        candidate.distance = 1.0f;
        candidate.unshadowedContribution = { target, target, target };
        candidate.target = target;
        candidate.proposalPdf = proposal;
        candidate.support = 1.0f;
        candidate.correction = 1.0f;
        return candidate;
    }

    [[nodiscard]] Reservoir MakeReservoir(
        const std::uint32_t id,
        const float target,
        const std::uint32_t count,
        const std::uint32_t generation = 1u)
    {
        Reservoir reservoir{};
        Candidate candidate = MakeCandidate(id, target, 1.0f, generation);
        reservoir.selected = candidate;
        reservoir.weightSum = static_cast<double>(target) * count;
        reservoir.M = count;
        reservoir.flags = ReservoirFlagValid;
        FinalizeReservoir(reservoir);
        return reservoir;
    }

    [[nodiscard]] SurfaceRecord MakeSurface()
    {
        return {
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            2.0f,
            7u,
            11u,
            13u,
            3u,
            false
        };
    }

    [[nodiscard]] bool Near(const double actual, const double expected, const double tolerance)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void TestEqualReservoirStatistics(TestContext& context)
    {
        context.Begin("equal reservoir selection follows uniform theory");
        const std::array candidates{
            MakeCandidate(0u, 1.0f),
            MakeCandidate(1u, 1.0f),
            MakeCandidate(2u, 1.0f),
            MakeCandidate(3u, 1.0f)
        };
        std::array<std::uint32_t, 4u> counts{};
        Pcg32 random(0x12345678u, 17u);
        constexpr std::uint32_t trialCount = 50000u;
        for (std::uint32_t trial = 0u; trial < trialCount; ++trial)
        {
            const Reservoir reservoir = BuildInitialReservoir(candidates, random, 4u);
            ++counts.at(reservoir.selected.identity.lightId);
            L9_CHECK(context, reservoir.M == 4u);
        }
        for (const std::uint32_t count : counts)
        {
            const double frequency = static_cast<double>(count) / trialCount;
            L9_CHECK(context, Near(frequency, 0.25, 0.012));
        }
    }

    void TestWeightedReservoirStatistics(TestContext& context)
    {
        context.Begin("weighted reservoir selection follows normalized weights");
        const std::array candidates{
            MakeCandidate(0u, 1.0f),
            MakeCandidate(1u, 2.0f),
            MakeCandidate(2u, 3.0f),
            MakeCandidate(3u, 4.0f)
        };
        const std::array expected{ 0.1, 0.2, 0.3, 0.4 };
        std::array<std::uint32_t, 4u> counts{};
        Pcg32 random(0xabcdefu, 19u);
        constexpr std::uint32_t trialCount = 75000u;
        for (std::uint32_t trial = 0u; trial < trialCount; ++trial)
        {
            const Reservoir reservoir = BuildInitialReservoir(candidates, random, 4u);
            ++counts.at(reservoir.selected.identity.lightId);
        }
        for (std::size_t index = 0u; index < counts.size(); ++index)
        {
            const double frequency = static_cast<double>(counts[index]) / trialCount;
            L9_CHECK(context, Near(frequency, expected[index], 0.012));
        }
    }

    void TestCandidateContractAndInitialRis(TestContext& context)
    {
        context.Begin("candidate target proposal support and correction are explicit");
        Candidate candidate = MakeCandidate(3u, 2.0f, 0.25f);
        candidate.support = 0.5f;
        candidate.correction = 2.0f;
        L9_CHECK(context, Near(CandidateWeight(candidate), 8.0, 1.0e-6));

        Candidate zeroTarget = MakeCandidate(4u, 0.0f, 0.5f);
        L9_CHECK(context, IsCandidateValid(zeroTarget));
        L9_CHECK(context, CandidateWeight(zeroTarget) == 0.0);

        Candidate invalidProposal = MakeCandidate(5u, 1.0f, 0.0f);
        L9_CHECK(context, !IsCandidateValid(invalidProposal));
        Candidate zeroSupport = MakeCandidate(6u, 1.0f, 0.5f);
        zeroSupport.support = 0.0f;
        const std::array diagnosticCandidates{ invalidProposal, zeroSupport };
        Pcg32 diagnosticRandom(77u, 9u);
        ReservoirStatistics diagnosticStatistics{};
        const Reservoir diagnosticReservoir = BuildInitialReservoir(
            diagnosticCandidates, diagnosticRandom, 8u, &diagnosticStatistics);
        L9_CHECK(context, diagnosticStatistics.zeroProposalCandidates == 1u);
        L9_CHECK(context, diagnosticStatistics.zeroSupportCandidates == 1u);
        L9_CHECK(context, diagnosticStatistics.candidatesRejected == 1u);
        L9_CHECK(context, diagnosticReservoir.M == 1u);

        const std::array candidates{
            MakeCandidate(0u, 1.0f, 0.5f),
            MakeCandidate(1u, 2.0f, 0.25f),
            MakeCandidate(2u, 0.0f, 0.25f)
        };
        Pcg32 random(55u, 3u);
        ReservoirStatistics statistics{};
        const Reservoir reservoir = BuildInitialReservoir(candidates, random, 2u, &statistics);
        L9_CHECK(context, reservoir.M == 2u);
        L9_CHECK(context, (reservoir.flags & ReservoirFlagMClamped) != 0u);
        L9_CHECK(context, statistics.candidatesSeen == 3u);
        L9_CHECK(context, statistics.mClampEvents == 1u);
        L9_CHECK(context, statistics.zeroTargetCandidates == 1u);
        L9_CHECK(context, statistics.zeroProposalCandidates == 0u);
        L9_CHECK(context, reservoir.normalizationWeight > 0.0f);

        const std::array overCap{
            MakeCandidate(10u, 1.0f),
            MakeCandidate(11u, 10.0f),
            MakeCandidate(12u, 100.0f),
            MakeCandidate(13u, 1000.0f)
        };
        const std::array overCapReversed{
            overCap[3], overCap[2], overCap[1], overCap[0]
        };
        Pcg32 forwardRandom(91u, 5u);
        Pcg32 reverseRandom(91u, 5u);
        const Reservoir forward = BuildInitialReservoir(overCap, forwardRandom, 2u);
        const Reservoir reverse = BuildInitialReservoir(overCapReversed, reverseRandom, 2u);
        L9_CHECK(context, forward.M == 2u);
        L9_CHECK(context, reverse.M == 2u);
        L9_CHECK(context, Near(forward.weightSum, 555.5, 1.0e-9));
        L9_CHECK(context, Near(reverse.weightSum, 555.5, 1.0e-9));

        Reservoir overflow = MakeReservoir(99u, 1.0f, 1u);
        overflow.M = std::numeric_limits<std::uint32_t>::max();
        const Reservoir overflowBefore = overflow;
        ReservoirStatistics overflowStatistics{};
        const bool overflowUpdated = UpdateReservoir(
            overflow, MakeCandidate(100u, 1.0f), 1.0, 1u, 0.0f, &overflowStatistics);
        L9_CHECK(context, !overflowUpdated);
        L9_CHECK(context, overflow.M == overflowBefore.M);
        L9_CHECK(context, overflow.weightSum == overflowBefore.weightSum);
        L9_CHECK(context, overflow.selected.identity.lightId == overflowBefore.selected.identity.lightId);
        L9_CHECK(context, overflowStatistics.candidatesRejected == 1u);
    }

    void TestCandidateGenerators(TestContext& context)
    {
        context.Begin("uniform power emissive and environment candidates preserve identity and pdf");
        const SurfaceRecord surface = MakeSurface();
        const std::array lights{
            AnalyticLight{ 10u, 2u, { -1.0f, 2.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 1.0f },
            AnalyticLight{ 20u, 4u, { 1.0f, 2.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, 8.0f }
        };
        const Candidate uniform = GenerateUniformLightCandidate(lights, surface, 0.75f);
        L9_CHECK(context, uniform.identity.source == CandidateSource::UniformLight);
        L9_CHECK(context, uniform.identity.lightId == 20u);
        L9_CHECK(context, Near(uniform.proposalPdf, 0.5, 1.0e-6));
        L9_CHECK(context, uniform.target > 0.0f);

        const Candidate power = GeneratePowerWeightedLightCandidate(lights, surface, 0.50f);
        L9_CHECK(context, power.identity.source == CandidateSource::PowerWeightedLight);
        L9_CHECK(context, power.identity.lightId == 20u);
        L9_CHECK(context, Near(power.proposalPdf, 8.0 / 9.0, 1.0e-6));

        const EmissiveTriangle triangle{
            30u, 300u, 7u,
            { -1.0f, 2.0f, -1.0f },
            { 1.0f, 2.0f, -1.0f },
            { 0.0f, 2.0f, 1.0f },
            { 5.0f, 4.0f, 3.0f },
            0.25f,
            false
        };
        const Candidate emissive = GenerateEmissiveTriangleCandidate(triangle, surface, 0.3f, 0.8f);
        L9_CHECK(context, emissive.identity.source == CandidateSource::EmissiveTriangle);
        L9_CHECK(context, emissive.identity.primitiveId == 300u);
        L9_CHECK(context, emissive.proposalPdf > 0.0f);
        L9_CHECK(context, emissive.target > 0.0f);

        const std::array environment{
            EnvironmentCell{ 40u, 0u, 1u, { 0.0f, 1.0f, 0.0f }, { 1.0f, 2.0f, 3.0f }, 0.5f, 1.0f },
            EnvironmentCell{ 40u, 1u, 1u, { 0.0f, -1.0f, 0.0f }, { 3.0f, 2.0f, 1.0f }, 0.5f, 1.0f }
        };
        const Candidate environmentCandidate = GenerateEnvironmentCandidate(environment, surface, 0.1f);
        L9_CHECK(context, environmentCandidate.identity.source == CandidateSource::Environment);
        L9_CHECK(context, environmentCandidate.identity.sampleId == 0u);
        L9_CHECK(context, Near(environmentCandidate.proposalPdf, 1.0, 1.0e-6));
        L9_CHECK(context, environmentCandidate.target > 0.0f);
    }

    void TestTemporalValidationAndReuse(TestContext& context)
    {
        context.Begin("temporal reuse validates reprojection IDs generations deletion age and M cap");
        const SurfaceRecord surface = MakeSurface();
        Reservoir current = MakeReservoir(1u, 2.0f, 4u, 9u);
        Reservoir history = MakeReservoir(2u, 3.0f, 5u, 9u);
        history.age = 2u;
        TemporalValidationInput input{ surface, surface, true, true, false, false, true, 9u };
        ReuseConfig config{};
        config.maxM = 6u;
        config.maxHistoryAge = 4u;

        L9_CHECK(context, ValidateTemporalHistory(history, input, config) == RejectionReason::None);
        Pcg32 random(88u, 2u);
        RejectionReason rejection = RejectionReason::InvalidCandidate;
        ReservoirStatistics statistics{};
        const CandidateReevaluation reevaluate = [](const Candidate& candidate, const SurfaceRecord&) {
            return candidate;
        };
        const Reservoir reused = ReuseTemporal(
            current, history, input, EstimatorMode::Biased, config, random,
            reevaluate, {}, rejection, &statistics);
        L9_CHECK(context, rejection == RejectionReason::None);
        L9_CHECK(context, reused.M == 6u);
        L9_CHECK(context, (reused.flags & ReservoirFlagMClamped) != 0u);
        L9_CHECK(context, (reused.flags & ReservoirFlagTemporalAccepted) != 0u);
        L9_CHECK(context, reused.normalizationWeight > 0.0f);
        L9_CHECK(context, statistics.temporalAccepted == 1u);
        L9_CHECK(context,
            (reused.selected.sourceSurfaceIndex == 0u && reused.selected.reuseSource == CandidateSource::Invalid) ||
            (reused.selected.sourceSurfaceIndex == 1u && reused.selected.reuseSource == CandidateSource::ReusedTemporal));

        Pcg32 historyOnlyRandom(89u, 2u);
        RejectionReason historyOnlyRejection = RejectionReason::InvalidCandidate;
        const Reservoir historyOnly = ReuseTemporal(
            {}, history, input, EstimatorMode::UnbiasedReference, config, historyOnlyRandom,
            reevaluate, [](const Candidate&) { return true; }, historyOnlyRejection);
        L9_CHECK(context, historyOnlyRejection == RejectionReason::None);
        L9_CHECK(context, historyOnly.selected.sourceSurfaceIndex == 1u);
        L9_CHECK(context, historyOnly.age == 3u);
        L9_CHECK(context, historyOnly.normalizationWeight > 0.0f);

        Pcg32 invalidRemapRandom(90u, 2u);
        RejectionReason invalidRemapReason = RejectionReason::None;
        const Reservoir invalidRemap = ReuseTemporal(
            current, history, input, EstimatorMode::Biased, config, invalidRemapRandom,
            [](const Candidate&, const SurfaceRecord&) { return Candidate{}; },
            {}, invalidRemapReason);
        L9_CHECK(context, HasReason(invalidRemapReason, RejectionReason::InvalidCandidate));
        L9_CHECK(context, invalidRemap.selected.identity.lightId == current.selected.identity.lightId);

        input.selectedLightExists = false;
        RejectionReason deleted = ValidateTemporalHistory(history, input, config);
        L9_CHECK(context, HasReason(deleted, RejectionReason::LightDeleted));
        input.selectedLightExists = true;
        input.currentLightGeneration = 10u;
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::LightGenerationMismatch));
        input.currentLightGeneration = 9u;
        input.cameraCut = true;
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::CameraCut));
        input.cameraCut = false;
        input.historySurface.instanceId = 999u;
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::InstanceMismatch));
        input.historySurface = surface;
        history.age = 4u;
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::HistoryExpired));
        history.age = 0u;
        input.historySurface.position.x = config.positionDistanceThreshold * 2.0f;
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::PositionMismatch));
        input.historySurface = surface;
        input.historySurface.normal = {};
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::NormalMismatch));
        input.historySurface.normal = {
            std::numeric_limits<float>::infinity(), 0.0f, 0.0f };
        L9_CHECK(context, HasReason(
            ValidateTemporalHistory(history, input, config), RejectionReason::NormalMismatch));
    }

    void TestSpatialValidationAndThinGeometry(TestContext& context)
    {
        context.Begin("spatial reuse rejects geometric material and thin-geometry mismatches");
        SurfaceRecord centerSurface = MakeSurface();
        SpatialNeighbor neighbor{};
        neighbor.reservoir = MakeReservoir(4u, 1.5f, 3u, 6u);
        neighbor.surface = centerSurface;
        neighbor.currentLightGeneration = 6u;
        ReuseConfig config{};
        L9_CHECK(context, ValidateSpatialNeighbor(centerSurface, neighbor, config) == RejectionReason::None);

        neighbor.surface.materialId = 99u;
        L9_CHECK(context, HasReason(
            ValidateSpatialNeighbor(centerSurface, neighbor, config), RejectionReason::MaterialMismatch));
        neighbor.surface = centerSurface;
        neighbor.surface.normal = { 0.0f, -1.0f, 0.0f };
        L9_CHECK(context, HasReason(
            ValidateSpatialNeighbor(centerSurface, neighbor, config), RejectionReason::NormalMismatch));
        neighbor.surface = centerSurface;
        centerSurface.thinGeometry = true;
        neighbor.surface.primitiveId = centerSurface.primitiveId + 1u;
        L9_CHECK(context, HasReason(
            ValidateSpatialNeighbor(centerSurface, neighbor, config), RejectionReason::ThinGeometryMismatch));

        centerSurface = MakeSurface();
        neighbor.surface = centerSurface;
        const std::array neighbors{ neighbor };
        Pcg32 random(900u, 5u);
        RejectionReason aggregate = RejectionReason::InvalidCandidate;
        ReservoirStatistics statistics{};
        const Reservoir result = ReuseSpatial(
            MakeReservoir(1u, 1.0f, 2u, 6u),
            centerSurface,
            neighbors,
            EstimatorMode::Biased,
            config,
            random,
            [](const Candidate& candidate, const SurfaceRecord&) { return candidate; },
            {},
            aggregate,
            &statistics);
        L9_CHECK(context, aggregate == RejectionReason::None);
        L9_CHECK(context, (result.flags & ReservoirFlagSpatialAccepted) != 0u);
        L9_CHECK(context, result.M == 5u);
        L9_CHECK(context, statistics.spatialAccepted == 1u);
        L9_CHECK(context,
            (result.selected.sourceSurfaceIndex == 0u && result.selected.reuseSource == CandidateSource::Invalid) ||
            (result.selected.sourceSurfaceIndex == 1u && result.selected.reuseSource == CandidateSource::ReusedSpatial));

        SpatialNeighbor heavyA{};
        heavyA.reservoir = MakeReservoir(20u, 2.0f, 3u, 6u);
        heavyA.surface = centerSurface;
        heavyA.currentLightGeneration = 6u;
        SpatialNeighbor heavyB{};
        heavyB.reservoir = MakeReservoir(30u, 5.0f, 4u, 6u);
        heavyB.surface = centerSurface;
        heavyB.currentLightGeneration = 6u;
        const std::array ordered{ heavyA, heavyB };
        const std::array reversed{ heavyB, heavyA };
        config.maxM = 4u;
        Pcg32 orderedRandom(123u, 7u);
        Pcg32 reversedRandom(123u, 7u);
        RejectionReason orderedReason = RejectionReason::InvalidCandidate;
        RejectionReason reversedReason = RejectionReason::InvalidCandidate;
        const Reservoir orderedResult = ReuseSpatial(
            MakeReservoir(10u, 1.0f, 2u, 6u), centerSurface, ordered,
            EstimatorMode::Biased, config, orderedRandom,
            [](const Candidate& candidate, const SurfaceRecord&) { return candidate; },
            {}, orderedReason);
        const Reservoir reversedResult = ReuseSpatial(
            MakeReservoir(10u, 1.0f, 2u, 6u), centerSurface, reversed,
            EstimatorMode::Biased, config, reversedRandom,
            [](const Candidate& candidate, const SurfaceRecord&) { return candidate; },
            {}, reversedReason);
        const double expectedCappedWeight = 28.0 * 4.0 / 9.0;
        L9_CHECK(context, orderedReason == RejectionReason::None);
        L9_CHECK(context, reversedReason == RejectionReason::None);
        L9_CHECK(context, orderedResult.M == 4u);
        L9_CHECK(context, reversedResult.M == 4u);
        L9_CHECK(context, Near(orderedResult.weightSum, expectedCappedWeight, 1.0e-9));
        L9_CHECK(context, Near(reversedResult.weightSum, expectedCappedWeight, 1.0e-9));

        const Reservoir center = MakeReservoir(40u, 2.0f, 2u, 6u);
        std::vector<SpatialNeighbor> tooMany(kMaxSpatialNeighbors + 1u, heavyA);
        Pcg32 tooManyRandom(321u, 9u);
        RejectionReason tooManyReason = RejectionReason::None;
        ReservoirStatistics tooManyStatistics{};
        const Reservoir passthrough = ReuseSpatial(
            center, centerSurface, tooMany, EstimatorMode::Biased, config, tooManyRandom,
            [](const Candidate& candidate, const SurfaceRecord&) { return candidate; },
            {}, tooManyReason, &tooManyStatistics);
        L9_CHECK(context, tooManyReason == RejectionReason::InvalidCandidate);
        L9_CHECK(context, passthrough.selected.identity.lightId == center.selected.identity.lightId);
        L9_CHECK(context, passthrough.M == center.M);
        L9_CHECK(context, tooManyStatistics.candidatesRejected == 1u);
        L9_CHECK(context, tooManyStatistics.spatialAccepted == 0u);
    }

    void TestReferenceModeAndVisibility(TestContext& context)
    {
        context.Begin("reference correction is labeled and final winner visibility executes once");
        const SurfaceRecord surface = MakeSurface();
        const Reservoir current = MakeReservoir(1u, 2.0f, 2u, 5u);
        const Reservoir history = MakeReservoir(2u, 4.0f, 3u, 5u);
        const TemporalValidationInput input{ surface, surface, true, true, false, false, true, 5u };
        ReuseConfig config{};
        config.maxM = 16u;
        Pcg32 random(42u, 11u);
        RejectionReason rejection = RejectionReason::InvalidCandidate;
        ReservoirStatistics statistics{};
        std::uint32_t referenceCalls = 0u;
        Reservoir result = ReuseTemporal(
            current,
            history,
            input,
            EstimatorMode::UnbiasedReference,
            config,
            random,
            [](const Candidate& candidate, const SurfaceRecord&) { return candidate; },
            [&referenceCalls](const Candidate&) {
                ++referenceCalls;
                return true;
            },
            rejection,
            &statistics);
        L9_CHECK(context, rejection == RejectionReason::None);
        L9_CHECK(context, (result.flags & ReservoirFlagReferenceMode) != 0u);
        L9_CHECK(context, result.normalizationWeight > 0.0f);
        L9_CHECK(context, referenceCalls == 2u);
        L9_CHECK(context, statistics.referenceVisibilityRays == 2u);

        std::uint32_t finalCalls = 0u;
        const FinalLightingResult first = EvaluateFinalVisibilityOnce(
            result,
            [&finalCalls](const Candidate&) {
                ++finalCalls;
                return true;
            },
            &statistics);
        const FinalLightingResult second = EvaluateFinalVisibilityOnce(
            result,
            [&finalCalls](const Candidate&) {
                ++finalCalls;
                return true;
            },
            &statistics);
        L9_CHECK(context, first.status == FinalVisibilityStatus::Evaluated);
        L9_CHECK(context, first.visible);
        L9_CHECK(context, Luminance(first.contribution) > 0.0f);
        L9_CHECK(context, second.status == FinalVisibilityStatus::AlreadyEvaluated);
        L9_CHECK(context, finalCalls == 1u);
        L9_CHECK(context, statistics.finalVisibilityRays == 1u);
        L9_CHECK(context, statistics.duplicateFinalVisibilityRequests == 1u);
    }

    void TestDirectLightingMutualExclusion(TestContext& context)
    {
        context.Begin("primary ReSTIR NEE MIS and emitter-hit ownership are mutually exclusive");
        const DirectLightingPolicyDecision primary = ResolveDirectLightingPolicy({ 0u, true, false, false });
        L9_CHECK(context, primary.owner == DirectLightingOwner::RestirPrimary);
        L9_CHECK(context, primary.useRestir && !primary.useNee && !primary.useEmitterHit && !primary.useMis);

        const DirectLightingPolicyDecision emitter = ResolveDirectLightingPolicy({ 0u, true, true, false });
        L9_CHECK(context, emitter.owner == DirectLightingOwner::EmitterHit);
        L9_CHECK(context, !emitter.useRestir && !emitter.useNee && emitter.useEmitterHit);

        const DirectLightingPolicyDecision secondary = ResolveDirectLightingPolicy({ 1u, true, false, false });
        L9_CHECK(context, secondary.owner == DirectLightingOwner::ConventionalNeeMis);
        L9_CHECK(context, !secondary.useRestir && secondary.useNee && secondary.useMis);

        const DirectLightingPolicyDecision delta = ResolveDirectLightingPolicy({ 0u, true, false, true });
        L9_CHECK(context, delta.owner == DirectLightingOwner::None);
    }

    void TestManyLightsBenchmarkHarness(TestContext& context)
    {
        context.Begin("100 1000 and 10000 light benchmark tiers preserve budgets and counters");
        const std::vector results = RunStandardManyLightsBenchmarks(8u, 4u);
        L9_CHECK(context, results.size() == 3u);
        L9_CHECK(context, results[0].tier.lightCount == 100u);
        L9_CHECK(context, results[1].tier.lightCount == 1000u);
        L9_CHECK(context, results[2].tier.lightCount == 10000u);
        for (const BenchmarkResult& result : results)
        {
            L9_CHECK(context, result.candidateCount == 32u);
            L9_CHECK(context, result.finalVisibilityRays == 8u);
            L9_CHECK(context, result.estimatedGpuBytes > 0u);
            L9_CHECK(context, result.deterministicChecksum != 0u);
            L9_CHECK(context, result.cpuBuildMilliseconds >= 0.0);
            L9_CHECK(context, result.cpuTraceMilliseconds >= 0.0);
            L9_CHECK(context, result.estimatorMode == EstimatorMode::Biased);
            L9_CHECK(context, result.proposal == BenchmarkProposal::RestirMixed);
            L9_CHECK(context, result.candidateBudget == 4u);
            L9_CHECK(context, result.visibilityBudget == 1u);
            L9_CHECK(context, result.referenceErrorStatus == MeasurementStatus::NotMeasured);
            L9_CHECK(context, std::isnan(result.meanAbsoluteError));
            L9_CHECK(context, std::isnan(result.rmse));
            L9_CHECK(context, result.gpuTimingStatus == MeasurementStatus::NotMeasured);
            L9_CHECK(context, std::isnan(result.gpuMilliseconds));
        }
        L9_CHECK(context, results[0].estimatedGpuBytes < results[1].estimatedGpuBytes);
        L9_CHECK(context, results[1].estimatedGpuBytes < results[2].estimatedGpuBytes);
        const std::string csv = FormatBenchmarkCsv(results);
        L9_CHECK(context, csv.find("10000") != std::string::npos);
        L9_CHECK(context, csv.find("visibility_rays") != std::string::npos);
    }
}

int main(const int argc, const char* const argv[])
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark")
        {
            const std::vector results = RenderingEngine::Restir::RunStandardManyLightsBenchmarks(256u, 8u);
            std::cout << RenderingEngine::Restir::FormatBenchmarkCsv(results);
            return 0;
        }

        TestContext context;
        TestEqualReservoirStatistics(context);
        TestWeightedReservoirStatistics(context);
        TestCandidateContractAndInitialRis(context);
        TestCandidateGenerators(context);
        TestTemporalValidationAndReuse(context);
        TestSpatialValidationAndThinGeometry(context);
        TestReferenceModeAndVisibility(context);
        TestDirectLightingMutualExclusion(context);
        TestManyLightsBenchmarkHarness(context);
        const RenderingEngine::Restir::Tests::VulkanSmokeReport vulkan =
            RenderingEngine::Restir::Tests::RunVulkanSmoke();
        if (vulkan.status == RenderingEngine::Restir::Tests::VulkanSmokeStatus::SkippedUnavailable)
        {
            std::cout << "L9 Vulkan smoke: SKIPPED (" << vulkan.message << ")\n";
        }
        else
        {
            context.Begin("actual Vulkan Initial Temporal Spatial Visibility Debug dispatch and readback");
            L9_CHECK(context, vulkan.validationErrorCount == 0u);
            L9_CHECK(context, vulkan.reservoirCount == 4u);
            L9_CHECK(context, vulkan.nonemptyReservoirCount > 0u);
            L9_CHECK(context, vulkan.finalVisibilityRays > 0u);
            L9_CHECK(context, vulkan.debugImagePixelCount == 4u);
            L9_CHECK(context, vulkan.readbackChecksum != 0u);
            std::cout << "L9 Vulkan smoke: PASS device=\"" << vulkan.deviceName
                << "\" validation=" << (vulkan.validationLayerEnabled ? "enabled" : "unavailable")
                << " nonempty=" << vulkan.nonemptyReservoirCount
                << " final_visibility=" << vulkan.finalVisibilityRays
                << " checksum=" << vulkan.readbackChecksum << '\n';
        }
        return context.Finish();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Unhandled L9 test failure: " << exception.what() << '\n';
        return 2;
    }
}
