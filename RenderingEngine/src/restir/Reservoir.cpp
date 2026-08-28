#include "restir/Reservoir.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace RenderingEngine::Restir
{
    Pcg32::Pcg32(const std::uint64_t seed, const std::uint64_t sequence) noexcept
        : increment_((sequence << 1u) | 1u)
    {
        static_cast<void>(NextUInt());
        state_ += seed;
        static_cast<void>(NextUInt());
    }

    std::uint32_t Pcg32::NextUInt() noexcept
    {
        const std::uint64_t oldState = state_;
        state_ = oldState * 6364136223846793005ull + increment_;
        const auto xorshifted = static_cast<std::uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
        const auto rotation = static_cast<std::uint32_t>(oldState >> 59u);
        return (xorshifted >> rotation) | (xorshifted << ((32u - rotation) & 31u));
    }

    float Pcg32::NextFloat() noexcept
    {
        constexpr float inverseMantissaRange = 1.0f / 16777216.0f;
        return static_cast<float>(NextUInt() >> 8u) * inverseMantissaRange;
    }

    bool IsFinite(const Float3 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    float Length(const Float3 value) noexcept
    {
        return std::sqrt(std::max(0.0f, Dot(value, value)));
    }

    Float3 Normalize(const Float3 value) noexcept
    {
        const float length = Length(value);
        if (!(length > 0.0f) || !std::isfinite(length))
        {
            return {};
        }
        return value / length;
    }

    float Luminance(const Float3 value) noexcept
    {
        return 0.2126f * value.x + 0.7152f * value.y + 0.0722f * value.z;
    }

    bool IsCandidateValid(const Candidate& candidate) noexcept
    {
        return candidate.identity.source != CandidateSource::Invalid &&
            candidate.identity.lightId != kInvalidStableId &&
            IsFinite(candidate.directionToLight) &&
            IsFinite(candidate.unshadowedContribution) &&
            std::isfinite(candidate.distance) && candidate.distance >= 0.0f &&
            std::isfinite(candidate.target) && candidate.target >= 0.0f &&
            std::isfinite(candidate.proposalPdf) && candidate.proposalPdf > 0.0f &&
            std::isfinite(candidate.support) && candidate.support >= 0.0f &&
            std::isfinite(candidate.correction) && candidate.correction >= 0.0f;
    }

    double CandidateWeight(const Candidate& candidate) noexcept
    {
        if (!IsCandidateValid(candidate))
        {
            return 0.0;
        }

        const double numerator = static_cast<double>(candidate.target) *
            static_cast<double>(candidate.support) * static_cast<double>(candidate.correction);
        const double weight = numerator / static_cast<double>(candidate.proposalPdf);
        return std::isfinite(weight) && weight >= 0.0 ? weight : 0.0;
    }

    bool UpdateReservoir(
        Reservoir& reservoir,
        const Candidate& candidate,
        const double weight,
        const std::uint32_t multiplicity,
        const float randomValue,
        ReservoirStatistics* const statistics) noexcept
    {
        if (!IsCandidateValid(candidate) || !std::isfinite(weight) || weight < 0.0 ||
            multiplicity == 0u)
        {
            if (statistics != nullptr)
            {
                ++statistics->candidatesRejected;
            }
            return false;
        }

        const double previousWeightSum = reservoir.weightSum;
        const double accumulatedWeight = previousWeightSum + weight;
        const std::uint64_t proposedM = static_cast<std::uint64_t>(reservoir.M) + multiplicity;
        if (proposedM > std::numeric_limits<std::uint32_t>::max())
        {
            if (statistics != nullptr) ++statistics->candidatesRejected;
            return false;
        }

        bool selected = false;
        if (weight > 0.0 && accumulatedWeight > 0.0)
        {
            const double threshold = std::clamp(static_cast<double>(randomValue), 0.0, 0.9999999999999999);
            selected = threshold * accumulatedWeight < weight;
            if (selected)
            {
                reservoir.selected = candidate;
            }
        }

        reservoir.weightSum = accumulatedWeight;
        reservoir.M = static_cast<std::uint32_t>(proposedM);

        if (reservoir.selected.identity.source != CandidateSource::Invalid)
        {
            reservoir.flags |= ReservoirFlagValid;
        }
        return selected;
    }

    void FinalizeReservoir(Reservoir& reservoir) noexcept
    {
        const double denominator = static_cast<double>(reservoir.M) *
            static_cast<double>(reservoir.selected.target);
        if ((reservoir.flags & ReservoirFlagValid) != 0u && denominator > 0.0 &&
            std::isfinite(reservoir.weightSum))
        {
            const double normalization = reservoir.weightSum / denominator;
            reservoir.normalizationWeight = std::isfinite(normalization)
                ? static_cast<float>(normalization)
                : 0.0f;
        }
        else
        {
            reservoir.normalizationWeight = 0.0f;
            if (!(reservoir.weightSum > 0.0))
            {
                reservoir.flags &= ~ReservoirFlagValid;
            }
        }
    }

    void ClampReservoirM(
        Reservoir& reservoir,
        const std::uint32_t maxM,
        ReservoirStatistics* const statistics) noexcept
    {
        if (maxM == 0u)
        {
            reservoir = {};
            return;
        }
        if (reservoir.M <= maxM)
        {
            return;
        }
        const double scale = static_cast<double>(maxM) / static_cast<double>(reservoir.M);
        reservoir.weightSum *= scale;
        reservoir.M = maxM;
        reservoir.flags |= ReservoirFlagMClamped;
        if (statistics != nullptr) ++statistics->mClampEvents;
    }

    Reservoir BuildInitialReservoir(
        const std::span<const Candidate> candidates,
        Pcg32& random,
        const std::uint32_t maxM,
        ReservoirStatistics* const statistics) noexcept
    {
        Reservoir reservoir{};
        for (const Candidate& candidate : candidates)
        {
            if (statistics != nullptr)
            {
                ++statistics->candidatesSeen;
                if (candidate.target == 0.0f) ++statistics->zeroTargetCandidates;
                if (!(candidate.proposalPdf > 0.0f)) ++statistics->zeroProposalCandidates;
                if (candidate.support == 0.0f) ++statistics->zeroSupportCandidates;
            }
            static_cast<void>(UpdateReservoir(
                reservoir,
                candidate,
                CandidateWeight(candidate),
                1u,
                random.NextFloat(),
                statistics));
        }
        ClampReservoirM(reservoir, maxM, statistics);
        FinalizeReservoir(reservoir);
        return reservoir;
    }

    ReservoirDebugRecord MakeDebugRecord(
        const Reservoir& reservoir,
        const RejectionReason temporalReason,
        const RejectionReason spatialReason,
        const ReservoirStatistics& statistics) noexcept
    {
        ReservoirDebugRecord record{};
        record.selectedIdentity = reservoir.selected.identity;
        record.reuseSource = reservoir.selected.reuseSource;
        record.M = reservoir.M;
        record.age = reservoir.age;
        record.flags = reservoir.flags;
        record.temporalRejection = temporalReason;
        record.spatialRejection = spatialReason;
        record.weightSum = static_cast<float>(reservoir.weightSum);
        record.target = reservoir.selected.target;
        record.proposalPdf = reservoir.selected.proposalPdf;
        record.correction = reservoir.selected.correction;
        record.normalizationWeight = reservoir.normalizationWeight;
        record.referenceVisibilityRays = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.referenceVisibilityRays, std::numeric_limits<std::uint32_t>::max()));
        record.finalVisibilityRays = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.finalVisibilityRays, std::numeric_limits<std::uint32_t>::max()));
        record.invalidCandidates = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.candidatesRejected, std::numeric_limits<std::uint32_t>::max()));
        record.zeroTargetCandidates = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.zeroTargetCandidates, std::numeric_limits<std::uint32_t>::max()));
        record.zeroProposalCandidates = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.zeroProposalCandidates, std::numeric_limits<std::uint32_t>::max()));
        record.zeroSupportCandidates = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.zeroSupportCandidates, std::numeric_limits<std::uint32_t>::max()));
        record.mClampEvents = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(statistics.mClampEvents, std::numeric_limits<std::uint32_t>::max()));
        return record;
    }
}
