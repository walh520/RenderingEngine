#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Wavefront
{
    struct FlagPair
    {
        std::uint32_t next;
        std::uint32_t shadow;

        [[nodiscard]] friend constexpr FlagPair operator+(
            const FlagPair left,
            const FlagPair right) noexcept
        {
            return { left.next + right.next, left.shadow + right.shadow };
        }
    };

    struct CompactionOracleResult
    {
        std::vector<FlagPair> exclusivePrefix;
        std::vector<std::uint32_t> nextIndices;
        std::vector<std::uint32_t> shadowIndices;
        FlagPair totals{};
    };

    [[nodiscard]] std::vector<FlagPair> BlellochExclusiveScan(
        std::span<const FlagPair> input);

    [[nodiscard]] CompactionOracleResult CompactStable(
        std::span<const FlagPair> flags);
}
