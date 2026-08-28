#pragma once

#include "restir/RestirTypes.hpp"

#include <cstdint>
#include <span>

namespace RenderingEngine::Restir
{
    class Pcg32 final
    {
    public:
        explicit Pcg32(std::uint64_t seed, std::uint64_t sequence = 1u) noexcept;

        [[nodiscard]] std::uint32_t NextUInt() noexcept;
        [[nodiscard]] float NextFloat() noexcept;

    private:
        std::uint64_t state_ = 0u;
        std::uint64_t increment_ = 0u;
    };

    [[nodiscard]] bool IsFinite(const Float3 value) noexcept;
    [[nodiscard]] float Length(const Float3 value) noexcept;
    [[nodiscard]] Float3 Normalize(const Float3 value) noexcept;
    [[nodiscard]] float Luminance(const Float3 value) noexcept;

    // Initial RIS weight: w_i = target_i * support_i * correction_i / proposal_i.
    [[nodiscard]] double CandidateWeight(const Candidate& candidate) noexcept;
    [[nodiscard]] bool IsCandidateValid(const Candidate& candidate) noexcept;

    bool UpdateReservoir(
        Reservoir& reservoir,
        const Candidate& candidate,
        double weight,
        std::uint32_t multiplicity,
        float randomValue,
        ReservoirStatistics* statistics = nullptr) noexcept;

    void FinalizeReservoir(Reservoir& reservoir) noexcept;
    void ClampReservoirM(
        Reservoir& reservoir,
        std::uint32_t maxM,
        ReservoirStatistics* statistics = nullptr) noexcept;

    [[nodiscard]] Reservoir BuildInitialReservoir(
        std::span<const Candidate> candidates,
        Pcg32& random,
        std::uint32_t maxM,
        ReservoirStatistics* statistics = nullptr) noexcept;

    [[nodiscard]] ReservoirDebugRecord MakeDebugRecord(
        const Reservoir& reservoir,
        RejectionReason temporalReason,
        RejectionReason spatialReason,
        const ReservoirStatistics& statistics) noexcept;
}
