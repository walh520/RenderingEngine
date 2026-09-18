#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace RenderingEngine::Restir
{
    inline constexpr std::uint32_t kInvalidLightIndex = 0xffffffffu;

    struct ProductionLightIdentity final
    {
        std::uint32_t stableLightId = kInvalidLightIndex;
        std::uint32_t primitiveId = kInvalidLightIndex;
        std::uint32_t generation = 0u;
        std::uint32_t flags = 0u;
    };

    // Matches HLSL uint2 used by abi-v3 set-5 bindings 20/21.
    struct LightIndexGeneration final
    {
        std::uint32_t index = kInvalidLightIndex;
        std::uint32_t generation = 0u;
    };

    static_assert(sizeof(LightIndexGeneration) == 8u);

    struct LightHistoryMapping final
    {
        bool valid = false;
        std::uint64_t previousLightGeneration = 0u;
        std::uint64_t currentLightGeneration = 0u;
        std::vector<std::uint32_t> currentToPrevious;
        std::vector<std::uint32_t> previousToCurrent;
        std::vector<LightIndexGeneration> currentToPreviousGpu;
        std::vector<LightIndexGeneration> previousToCurrentGpu;
        std::uint32_t retained = 0u;
        std::uint32_t added = 0u;
        std::uint32_t deleted = 0u;
        std::uint32_t generationChanged = 0u;
        std::string reason;
    };

    [[nodiscard]] LightHistoryMapping BuildLightHistoryMapping(
        std::span<const ProductionLightIdentity> previous,
        std::span<const ProductionLightIdentity> current,
        std::uint64_t previousLightGeneration,
        std::uint64_t currentLightGeneration);

    [[nodiscard]] bool IsHistoryLightReusable(
        const LightHistoryMapping& mapping,
        std::span<const ProductionLightIdentity> previous,
        std::span<const ProductionLightIdentity> current,
        std::uint32_t previousIndex) noexcept;
}
