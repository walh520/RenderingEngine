#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV1
{
    enum class DescriptorSet : std::uint32_t
    {
        Frame = 0u,
        Scene = 1u,
        TraversalBackend = 2u,
        WavefrontQueues = 3u,
        ReconstructionReserved = 4u,
        RestirReserved = 5u,
        DebugProfiler = 6u
    };

    // Set 0 and set 1 retain every abi-v0 binding. Set 2 binding zero remains
    // backend-specific (TLAS for hardware, node storage for software); common
    // batch inputs and outputs are fixed at bindings one and two.
    namespace TraversalBinding
    {
        inline constexpr std::uint32_t BackendScene = 0u;
        inline constexpr std::uint32_t Rays = 1u;
        inline constexpr std::uint32_t Hits = 2u;
        inline constexpr std::uint32_t AlphaAtlas = 8u;
        inline constexpr std::uint32_t AlphaSampler = 9u;
    }

    namespace WavefrontBinding
    {
        inline constexpr std::uint32_t Constants = 0u;
        inline constexpr std::uint32_t PathStates = 1u;
        inline constexpr std::uint32_t RayQueue = 2u;
        inline constexpr std::uint32_t HitQueue = 3u;
        inline constexpr std::uint32_t ShadowQueue = 4u;
    }
}
