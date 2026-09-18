#pragma once

#include "scene/CanonicalScene.hpp"

#include <cstdint>
#include <vector>

namespace RenderingEngine::Scene
{
    inline constexpr std::uint32_t kManyLightsArenaInvalidIndex = 0xffffffffu;

    enum class ManyLightsArenaTier : std::uint8_t
    {
        Lights100,
        Lights1000,
        Lights10000
    };

    struct ManyLightsArenaOptions final
    {
        ManyLightsArenaTier tier = ManyLightsArenaTier::Lights100;
        std::uint32_t frameIndex = 0u;
        std::uint32_t sceneGeneration = 1u;
        std::uint32_t topologyEpoch = 0u;
        bool animateLights = true;
        bool animateCamera = true;
        bool animateRigidOccluders = true;
    };

    // This identity is deliberately independent from the abi-v0 light-table
    // index. abi-v0 requires GpuLightV0::identity.y to equal the current table
    // slot, while abi-v3 history maps this stable identity across add/delete
    // and table compaction.
    struct ManyLightsArenaLightIdentity final
    {
        std::uint32_t stableLightId = kManyLightsArenaInvalidIndex;
        std::uint32_t primitiveId = kManyLightsArenaInvalidIndex;
        std::uint32_t generation = 0u;
        std::uint32_t sourceTableIndex = kManyLightsArenaInvalidIndex;

        [[nodiscard]] bool operator==(
            const ManyLightsArenaLightIdentity&) const noexcept = default;
    };

    struct ManyLightsArenaFrame final
    {
        CanonicalScene canonical;
        std::vector<ManyLightsArenaLightIdentity> lightIdentities;
        std::uint64_t lightSetGeneration = 0u;
        std::uint32_t firstLightInstanceIndex = kManyLightsArenaInvalidIndex;
        std::uint32_t lightPrimitiveId = kManyLightsArenaInvalidIndex;
        std::uint32_t replacedLightTableIndex = kManyLightsArenaInvalidIndex;
    };

    [[nodiscard]] std::uint32_t ResolveManyLightsArenaCount(
        ManyLightsArenaTier tier) noexcept;
    [[nodiscard]] ManyLightsArenaFrame BuildManyLightsArena(
        const ManyLightsArenaOptions& options);
}
