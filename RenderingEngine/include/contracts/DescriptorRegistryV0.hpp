#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV0
{
    enum class DescriptorSet : std::uint32_t
    {
        Frame = 0u,
        Scene = 1u,
        TraversalBackend = 2u,
        WavefrontReserved = 3u,
        ReconstructionReserved = 4u,
        RestirReserved = 5u,
        DebugProfiler = 6u
    };

    namespace FrameBinding
    {
        inline constexpr std::uint32_t Constants = 0u;
    }

    namespace SceneBinding
    {
        inline constexpr std::uint32_t Constants = 0u;
        inline constexpr std::uint32_t Vertices = 1u;
        inline constexpr std::uint32_t Indices = 2u;
        inline constexpr std::uint32_t Geometries = 3u;
        inline constexpr std::uint32_t Instances = 4u;
        inline constexpr std::uint32_t Materials = 5u;
        inline constexpr std::uint32_t Lights = 6u;
        inline constexpr std::uint32_t Textures = 16u;
        inline constexpr std::uint32_t Samplers = 17u;
    }

    // Set 2 belongs to the selected traversal backend but has no abi-v0
    // bindings. Sets 3-5 are deliberately empty until their own ABI waves.
    namespace DebugProfilerBinding
    {
        inline constexpr std::uint32_t Constants = 0u;
    }
}
