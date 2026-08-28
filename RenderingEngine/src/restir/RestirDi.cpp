#include "restir/RestirDi.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace RenderingEngine::Restir
{
    namespace
    {
        struct ReferenceSource
        {
            const Reservoir* reservoir = nullptr;
            SurfaceRecord surface{};
            std::uint32_t sourceSurfaceIndex = 0u;
        };

        [[nodiscard]] bool IsReservoirValid(const Reservoir& reservoir) noexcept
        {
            return (reservoir.flags & ReservoirFlagValid) != 0u &&
                reservoir.M > 0u && reservoir.normalizationWeight > 0.0f &&
                IsCandidateValid(reservoir.selected);
        }

        [[nodiscard]] float RelativeDepthDifference(const float a, const float b) noexcept
        {
            const float scale = std::max(1.0f, std::max(std::abs(a), std::abs(b)));
            return std::abs(a - b) / scale;
        }

        [[nodiscard]] bool MergeRepresentative(
            Reservoir& destination,
            const Reservoir& source,
            const SurfaceRecord& destinationSurface,
            const std::uint32_t sourceSurfaceIndex,
            const CandidateSource reuseSource,
            Pcg32& random,
            const CandidateReevaluation& reevaluate,
            ReservoirStatistics* const statistics)
        {
            if (!IsReservoirValid(source))
            {
                return false;
            }

            Candidate representative = reevaluate(source.selected, destinationSurface);
            if (!IsCandidateValid(representative))
            {
                if (statistics != nullptr)
                {
                    ++statistics->candidatesRejected;
                }
                return false;
            }

            representative.reuseSource = reuseSource;
            representative.sourceSurfaceIndex = sourceSurfaceIndex;
            representative.correction = source.normalizationWeight;
            const double mergeWeight = static_cast<double>(representative.target) *
                static_cast<double>(representative.support) *
                static_cast<double>(source.normalizationWeight) * static_cast<double>(source.M);
            if (statistics != nullptr)
            {
                ++statistics->candidatesSeen;
                if (representative.target == 0.0f) ++statistics->zeroTargetCandidates;
                if (!(representative.proposalPdf > 0.0f)) ++statistics->zeroProposalCandidates;
                if (representative.support == 0.0f) ++statistics->zeroSupportCandidates;
            }
            if (source.M > std::numeric_limits<std::uint32_t>::max() - destination.M)
            {
                if (statistics != nullptr)
                {
                    ++statistics->candidatesRejected;
                }
                return false;
            }
            // UpdateReservoir returns whether this representative won, not
            // whether the bounded update was accepted.
            static_cast<void>(UpdateReservoir(
                destination,
                representative,
                mergeWeight,
                source.M,
                random.NextFloat(),
                statistics));
            return true;
        }

        void ApplyBasicReferenceCorrection(
            Reservoir& reservoir,
            const SurfaceRecord& currentSurface,
            const std::span<const ReferenceSource> sources,
            const ReuseConfig& config,
            const CandidateReevaluation& reevaluate,
            const VisibilityQuery& visibility,
            ReservoirStatistics* const statistics)
        {
            if (!IsCandidateValid(reservoir.selected) || sources.empty())
            {
                reservoir.normalizationWeight = 0.0f;
                reservoir.flags &= ~ReservoirFlagValid;
                return;
            }
            if (!visibility)
            {
                reservoir.normalizationWeight = 0.0f;
                reservoir.flags &= ~ReservoirFlagValid;
                return;
            }

            const std::uint32_t selectedSourceSurfaceIndex = reservoir.selected.sourceSurfaceIndex;
            Candidate atCurrent = reevaluate(reservoir.selected, currentSurface);
            if (!IsCandidateValid(atCurrent) || !(atCurrent.target > 0.0f))
            {
                reservoir.normalizationWeight = 0.0f;
                reservoir.flags &= ~ReservoirFlagValid;
                return;
            }

            std::uint64_t rawM = 0u;
            for (const ReferenceSource& source : sources)
            {
                rawM += source.reservoir != nullptr ? source.reservoir->M : 0u;
            }
            const double mScale = rawM > config.maxM
                ? static_cast<double>(config.maxM) / static_cast<double>(rawM)
                : 1.0;

            double denominator = 0.0;
            double selectedSourceTarget = 0.0;
            for (const ReferenceSource& source : sources)
            {
                if (source.reservoir == nullptr || !IsReservoirValid(*source.reservoir))
                {
                    continue;
                }
                Candidate atSource = reevaluate(reservoir.selected, source.surface);
                if (!IsCandidateValid(atSource))
                {
                    continue;
                }

                const bool visible = visibility(atSource);
                if (statistics != nullptr)
                {
                    ++statistics->referenceVisibilityRays;
                }
                const double sourceTarget = visible
                    ? static_cast<double>(atSource.target) * static_cast<double>(atSource.support)
                    : 0.0;
                denominator += static_cast<double>(source.reservoir->M) * mScale * sourceTarget;
                if (source.sourceSurfaceIndex == reservoir.selected.sourceSurfaceIndex)
                {
                    selectedSourceTarget = sourceTarget;
                }
            }

            const double currentTarget = static_cast<double>(atCurrent.target) *
                static_cast<double>(atCurrent.support);
            const double normalizationDenominator = currentTarget * denominator;
            const double normalization = normalizationDenominator > 0.0
                ? reservoir.weightSum * selectedSourceTarget / normalizationDenominator
                : 0.0;
            reservoir.selected = atCurrent;
            reservoir.selected.sourceSurfaceIndex = selectedSourceSurfaceIndex;
            reservoir.normalizationWeight = std::isfinite(normalization)
                ? static_cast<float>(normalization)
                : 0.0f;
            reservoir.flags |= ReservoirFlagReferenceMode;
            if (!(reservoir.normalizationWeight > 0.0f))
            {
                reservoir.flags &= ~ReservoirFlagValid;
            }
        }
    }

    RejectionReason ValidateTemporalHistory(
        const Reservoir& history,
        const TemporalValidationInput& input,
        const ReuseConfig& config) noexcept
    {
        RejectionReason reason = RejectionReason::None;
        if (!IsReservoirValid(history)) reason |= RejectionReason::EmptyReservoir;
        if (!input.reprojectedInside) reason |= RejectionReason::ReprojectionOutside;
        if (!input.motionValid) reason |= RejectionReason::MotionInvalid;
        if (input.cameraCut) reason |= RejectionReason::CameraCut;
        if (input.resized) reason |= RejectionReason::Resize;
        if (RelativeDepthDifference(input.currentSurface.linearDepth, input.historySurface.linearDepth) >
            config.relativeDepthThreshold)
        {
            reason |= RejectionReason::DepthMismatch;
        }
        if (Dot(Normalize(input.currentSurface.normal), Normalize(input.historySurface.normal)) <
            config.normalCosThreshold)
        {
            reason |= RejectionReason::NormalMismatch;
        }
        if (Length(input.currentSurface.position - input.historySurface.position) >
            config.positionDistanceThreshold)
        {
            reason |= RejectionReason::PositionMismatch;
        }
        if (config.requireSameInstance &&
            input.currentSurface.instanceId != input.historySurface.instanceId)
        {
            reason |= RejectionReason::InstanceMismatch;
        }
        if (input.currentSurface.materialId != input.historySurface.materialId)
        {
            reason |= RejectionReason::MaterialMismatch;
        }
        if (input.currentSurface.sceneGeneration != input.historySurface.sceneGeneration)
        {
            reason |= RejectionReason::SceneGenerationMismatch;
        }
        if (!input.selectedLightExists)
        {
            reason |= RejectionReason::LightDeleted;
        }
        else if (history.selected.identity.generation != input.currentLightGeneration)
        {
            reason |= RejectionReason::LightGenerationMismatch;
        }
        if (history.age >= config.maxHistoryAge)
        {
            reason |= RejectionReason::HistoryExpired;
        }
        if ((input.currentSurface.thinGeometry || input.historySurface.thinGeometry) &&
            (input.currentSurface.primitiveId != input.historySurface.primitiveId ||
             Length(input.currentSurface.position - input.historySurface.position) >
                config.thinGeometryDistanceThreshold))
        {
            reason |= RejectionReason::ThinGeometryMismatch;
        }
        return reason;
    }

    RejectionReason ValidateSpatialNeighbor(
        const SurfaceRecord& center,
        const SpatialNeighbor& neighbor,
        const ReuseConfig& config) noexcept
    {
        RejectionReason reason = RejectionReason::None;
        if (!IsReservoirValid(neighbor.reservoir)) reason |= RejectionReason::EmptyReservoir;
        if (RelativeDepthDifference(center.linearDepth, neighbor.surface.linearDepth) >
            config.relativeDepthThreshold)
        {
            reason |= RejectionReason::DepthMismatch;
        }
        if (Dot(Normalize(center.normal), Normalize(neighbor.surface.normal)) < config.normalCosThreshold)
        {
            reason |= RejectionReason::NormalMismatch;
        }
        if (Length(center.position - neighbor.surface.position) > config.positionDistanceThreshold)
        {
            reason |= RejectionReason::PositionMismatch;
        }
        if (config.requireSameInstance && center.instanceId != neighbor.surface.instanceId)
        {
            reason |= RejectionReason::InstanceMismatch;
        }
        if (center.materialId != neighbor.surface.materialId)
        {
            reason |= RejectionReason::MaterialMismatch;
        }
        if (center.sceneGeneration != neighbor.surface.sceneGeneration)
        {
            reason |= RejectionReason::SceneGenerationMismatch;
        }
        if (!neighbor.selectedLightExists)
        {
            reason |= RejectionReason::LightDeleted;
        }
        else if (neighbor.reservoir.selected.identity.generation != neighbor.currentLightGeneration)
        {
            reason |= RejectionReason::LightGenerationMismatch;
        }
        if (neighbor.reservoir.age >= config.maxHistoryAge)
        {
            reason |= RejectionReason::HistoryExpired;
        }
        if ((center.thinGeometry || neighbor.surface.thinGeometry) &&
            (center.primitiveId != neighbor.surface.primitiveId ||
             Length(center.position - neighbor.surface.position) > config.thinGeometryDistanceThreshold))
        {
            reason |= RejectionReason::ThinGeometryMismatch;
        }
        return reason;
    }

    Reservoir ReuseTemporal(
        const Reservoir& current,
        const Reservoir& history,
        const TemporalValidationInput& input,
        const EstimatorMode mode,
        const ReuseConfig& config,
        Pcg32& random,
        const CandidateReevaluation& reevaluate,
        const VisibilityQuery& referenceVisibility,
        RejectionReason& rejectionReason,
        ReservoirStatistics* const statistics)
    {
        rejectionReason = ValidateTemporalHistory(history, input, config);
        if (rejectionReason != RejectionReason::None)
        {
            if (statistics != nullptr) ++statistics->temporalRejected;
            return current;
        }

        Reservoir result{};
        std::vector<ReferenceSource> sources;
        sources.reserve(2u);
        if (MergeRepresentative(
            result, current, input.currentSurface, 0u, CandidateSource::Invalid,
            random, reevaluate, statistics))
        {
            sources.push_back({ &current, input.currentSurface, 0u });
        }
        const bool historyMerged = MergeRepresentative(
            result, history, input.currentSurface, 1u, CandidateSource::ReusedTemporal,
            random, reevaluate, statistics);
        if (historyMerged)
        {
            sources.push_back({ &history, input.historySurface, 1u });
            result.flags |= ReservoirFlagTemporalAccepted;
        }
        else
        {
            rejectionReason |= RejectionReason::InvalidCandidate;
            if (statistics != nullptr) ++statistics->temporalRejected;
            return current;
        }

        ClampReservoirM(result, config.maxM, statistics);
        FinalizeReservoir(result);
        if (mode == EstimatorMode::UnbiasedReference)
        {
            ApplyBasicReferenceCorrection(
                result, input.currentSurface, sources, config, reevaluate, referenceVisibility, statistics);
        }
        result.age = result.selected.sourceSurfaceIndex == 1u
            ? std::min(history.age + 1u, config.maxHistoryAge)
            : 0u;
        if (statistics != nullptr) ++statistics->temporalAccepted;
        return result;
    }

    Reservoir ReuseSpatial(
        const Reservoir& center,
        const SurfaceRecord& centerSurface,
        const std::span<const SpatialNeighbor> neighbors,
        const EstimatorMode mode,
        const ReuseConfig& config,
        Pcg32& random,
        const CandidateReevaluation& reevaluate,
        const VisibilityQuery& referenceVisibility,
        RejectionReason& aggregateRejectionReason,
        ReservoirStatistics* const statistics)
    {
        Reservoir result{};
        aggregateRejectionReason = RejectionReason::None;
        if (neighbors.size() > kMaxSpatialNeighbors)
        {
            aggregateRejectionReason = RejectionReason::InvalidCandidate;
            if (statistics != nullptr)
            {
                ++statistics->candidatesRejected;
            }
            return center;
        }
        std::vector<ReferenceSource> sources;
        sources.reserve(neighbors.size() + 1u);
        if (MergeRepresentative(
            result, center, centerSurface, 0u, CandidateSource::Invalid,
            random, reevaluate, statistics))
        {
            sources.push_back({ &center, centerSurface, 0u });
        }

        for (std::size_t index = 0u; index < neighbors.size(); ++index)
        {
            const SpatialNeighbor& neighbor = neighbors[index];
            const RejectionReason reason = ValidateSpatialNeighbor(centerSurface, neighbor, config);
            if (reason != RejectionReason::None)
            {
                aggregateRejectionReason |= reason;
                if (statistics != nullptr) ++statistics->spatialRejected;
                continue;
            }
            const auto sourceIndex = static_cast<std::uint32_t>(index + 1u);
            if (MergeRepresentative(
                result, neighbor.reservoir, centerSurface, sourceIndex, CandidateSource::ReusedSpatial,
                random, reevaluate, statistics))
            {
                sources.push_back({ &neighbor.reservoir, neighbor.surface, sourceIndex });
                result.flags |= ReservoirFlagSpatialAccepted;
                if (statistics != nullptr) ++statistics->spatialAccepted;
            }
            else
            {
                aggregateRejectionReason |= RejectionReason::InvalidCandidate;
                if (statistics != nullptr) ++statistics->spatialRejected;
            }
        }

        ClampReservoirM(result, config.maxM, statistics);
        FinalizeReservoir(result);
        if (mode == EstimatorMode::UnbiasedReference)
        {
            ApplyBasicReferenceCorrection(
                result, centerSurface, sources, config, reevaluate, referenceVisibility, statistics);
        }
        result.age = 0u;
        return result;
    }

    FinalLightingResult EvaluateFinalVisibilityOnce(
        Reservoir& reservoir,
        const VisibilityQuery& visibility,
        ReservoirStatistics* const statistics)
    {
        FinalLightingResult result{};
        if (!IsReservoirValid(reservoir))
        {
            result.status = FinalVisibilityStatus::EmptyReservoir;
            return result;
        }
        if ((reservoir.flags & ReservoirFlagFinalVisibilityEvaluated) != 0u)
        {
            result.status = FinalVisibilityStatus::AlreadyEvaluated;
            if (statistics != nullptr) ++statistics->duplicateFinalVisibilityRequests;
            return result;
        }

        reservoir.flags |= ReservoirFlagFinalVisibilityEvaluated;
        result.visible = visibility ? visibility(reservoir.selected) : true;
        if (statistics != nullptr)
        {
            ++statistics->finalVisibilityRays;
        }
        result.status = FinalVisibilityStatus::Evaluated;
        if (result.visible)
        {
            result.contribution = reservoir.selected.unshadowedContribution * reservoir.normalizationWeight;
        }
        return result;
    }

    DirectLightingPolicyDecision ResolveDirectLightingPolicy(
        const DirectLightingPolicyInput& input) noexcept
    {
        DirectLightingPolicyDecision decision{};
        if (input.hitEmitter)
        {
            decision.owner = DirectLightingOwner::EmitterHit;
            decision.useEmitterHit = true;
            decision.useMis = input.bounce > 0u;
            return decision;
        }
        if (input.deltaSurface)
        {
            return decision;
        }
        if (input.restirEnabled && input.bounce == 0u)
        {
            decision.owner = DirectLightingOwner::RestirPrimary;
            decision.useRestir = true;
            return decision;
        }

        decision.owner = DirectLightingOwner::ConventionalNeeMis;
        decision.useNee = true;
        decision.useMis = true;
        return decision;
    }
}
