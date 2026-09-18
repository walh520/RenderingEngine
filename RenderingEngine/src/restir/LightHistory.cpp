#include "restir/LightHistory.hpp"

#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>

namespace RenderingEngine::Restir
{
    namespace
    {
        struct StableKey final
        {
            std::uint32_t light = 0u;
            std::uint32_t primitive = 0u;

            [[nodiscard]] bool operator==(const StableKey&) const noexcept = default;
        };

        struct StableKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const StableKey key) const noexcept
            {
                const std::uint64_t combined =
                    (static_cast<std::uint64_t>(key.light) << 32u) | key.primitive;
                return static_cast<std::size_t>(
                    combined ^ (combined >> 33u) ^ (combined << 11u));
            }
        };

        [[nodiscard]] bool ValidateIdentities(
            const std::span<const ProductionLightIdentity> identities,
            std::unordered_map<StableKey, std::uint32_t, StableKeyHash>& indices,
            std::string& reason)
        {
            if (identities.size() > std::numeric_limits<std::uint32_t>::max())
            {
                reason = "light table exceeds the 32-bit abi-v3 index contract";
                return false;
            }
            indices.reserve(identities.size());
            for (std::size_t index = 0u; index < identities.size(); ++index)
            {
                const ProductionLightIdentity& identity = identities[index];
                if (identity.stableLightId == kInvalidLightIndex
                    || identity.generation == 0u)
                {
                    reason = "light table contains an invalid stable ID or generation";
                    return false;
                }
                if (!indices.emplace(
                        StableKey{ identity.stableLightId, identity.primitiveId },
                        static_cast<std::uint32_t>(index)).second)
                {
                    reason = "light table contains a duplicate stable light/primitive identity";
                    return false;
                }
            }
            return true;
        }
    }

    LightHistoryMapping BuildLightHistoryMapping(
        const std::span<const ProductionLightIdentity> previous,
        const std::span<const ProductionLightIdentity> current,
        const std::uint64_t previousLightGeneration,
        const std::uint64_t currentLightGeneration)
    {
        LightHistoryMapping mapping{};
        mapping.previousLightGeneration = previousLightGeneration;
        mapping.currentLightGeneration = currentLightGeneration;
        if (previousLightGeneration == 0u || currentLightGeneration == 0u)
        {
            mapping.reason = "light-set generations must be non-zero";
            return mapping;
        }

        std::unordered_map<StableKey, std::uint32_t, StableKeyHash> previousIndices;
        std::unordered_map<StableKey, std::uint32_t, StableKeyHash> currentIndices;
        if (!ValidateIdentities(previous, previousIndices, mapping.reason)
            || !ValidateIdentities(current, currentIndices, mapping.reason))
        {
            return mapping;
        }

        mapping.currentToPrevious.assign(current.size(), kInvalidLightIndex);
        mapping.previousToCurrent.assign(previous.size(), kInvalidLightIndex);
        mapping.currentToPreviousGpu.resize(current.size());
        mapping.previousToCurrentGpu.resize(previous.size());
        for (std::size_t currentIndex = 0u; currentIndex < current.size(); ++currentIndex)
        {
            const ProductionLightIdentity& currentIdentity = current[currentIndex];
            const auto found = previousIndices.find({
                currentIdentity.stableLightId, currentIdentity.primitiveId });
            if (found == previousIndices.end())
            {
                mapping.currentToPreviousGpu[currentIndex] = {
                    kInvalidLightIndex, currentIdentity.generation };
                ++mapping.added;
                continue;
            }
            const std::uint32_t previousIndex = found->second;
            mapping.currentToPrevious[currentIndex] = previousIndex;
            mapping.previousToCurrent[previousIndex] =
                static_cast<std::uint32_t>(currentIndex);
            mapping.currentToPreviousGpu[currentIndex] = {
                previousIndex, currentIdentity.generation };
            mapping.previousToCurrentGpu[previousIndex] = {
                static_cast<std::uint32_t>(currentIndex), currentIdentity.generation };
            if (previous[previousIndex].generation == currentIdentity.generation)
            {
                ++mapping.retained;
            }
            else
            {
                ++mapping.generationChanged;
            }
        }
        for (const std::uint32_t currentIndex : mapping.previousToCurrent)
        {
            if (currentIndex == kInvalidLightIndex)
            {
                ++mapping.deleted;
            }
        }
        mapping.valid = true;
        mapping.reason.clear();
        return mapping;
    }

    bool IsHistoryLightReusable(
        const LightHistoryMapping& mapping,
        const std::span<const ProductionLightIdentity> previous,
        const std::span<const ProductionLightIdentity> current,
        const std::uint32_t previousIndex) noexcept
    {
        if (!mapping.valid
            || previousIndex >= previous.size()
            || previousIndex >= mapping.previousToCurrent.size())
        {
            return false;
        }
        const std::uint32_t currentIndex = mapping.previousToCurrent[previousIndex];
        if (currentIndex == kInvalidLightIndex || currentIndex >= current.size())
        {
            return false;
        }
        const ProductionLightIdentity& before = previous[previousIndex];
        const ProductionLightIdentity& after = current[currentIndex];
        return before.stableLightId == after.stableLightId
            && before.primitiveId == after.primitiveId
            && before.generation == after.generation;
    }
}
