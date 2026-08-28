#include "../include/WavefrontCompactionOracle.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace RenderingEngine::Wavefront
{
    std::vector<FlagPair> BlellochExclusiveScan(const std::span<const FlagPair> input)
    {
        if (input.empty())
        {
            return {};
        }
        if (input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("Wavefront compaction input exceeds 32-bit indexing.");
        }

        const std::size_t paddedSize = std::bit_ceil(input.size());
        std::vector<FlagPair> scratch(paddedSize, FlagPair{});
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            if (input[index].next > 1 || input[index].shadow > 1)
            {
                throw std::invalid_argument("Wavefront compaction flags must be binary.");
            }
            scratch[index] = input[index];
        }

        for (std::size_t stride = 1; stride < paddedSize; stride <<= 1u)
        {
            for (std::size_t index = stride * 2 - 1; index < paddedSize; index += stride * 2)
            {
                scratch[index] = scratch[index] + scratch[index - stride];
            }
        }

        scratch[paddedSize - 1] = {};
        for (std::size_t stride = paddedSize >> 1u; stride != 0; stride >>= 1u)
        {
            for (std::size_t index = stride * 2 - 1; index < paddedSize; index += stride * 2)
            {
                const FlagPair left = scratch[index - stride];
                scratch[index - stride] = scratch[index];
                scratch[index] = scratch[index] + left;
            }
        }

        scratch.resize(input.size());
        return scratch;
    }

    CompactionOracleResult CompactStable(const std::span<const FlagPair> flags)
    {
        CompactionOracleResult result{};
        result.exclusivePrefix = BlellochExclusiveScan(flags);
        if (flags.empty())
        {
            return result;
        }

        const FlagPair last = result.exclusivePrefix.back() + flags.back();
        result.totals = last;
        result.nextIndices.resize(last.next);
        result.shadowIndices.resize(last.shadow);

        const std::uint32_t flagCount = static_cast<std::uint32_t>(flags.size());
        for (std::uint32_t index = 0; index < flagCount; ++index)
        {
            if (flags[index].next != 0)
            {
                result.nextIndices[result.exclusivePrefix[index].next] = index;
            }
            if (flags[index].shadow != 0)
            {
                result.shadowIndices[result.exclusivePrefix[index].shadow] = index;
            }
        }
        return result;
    }
}
