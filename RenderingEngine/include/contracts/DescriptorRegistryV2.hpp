#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV2
{
    enum class DescriptorSet : std::uint32_t
    {
        Frame = 0u,
        Scene = 1u,
        TraversalBackend = 2u,
        WavefrontQueues = 3u,
        Reconstruction = 4u,
        RestirReserved = 5u,
        DebugProfiler = 6u
    };

    namespace TraversalBinding
    {
        inline constexpr std::uint32_t BackendScene = 0u;
        inline constexpr std::uint32_t Rays = 1u;
        inline constexpr std::uint32_t Hits = 2u;
        inline constexpr std::uint32_t AlphaAtlas = 8u;
        inline constexpr std::uint32_t AlphaSampler = 9u;
    }

    // Bindings 0-4 preserve the published abi-v1 meanings and binary records.
    // Every Wave 3-private resource is append-only after those bindings.
    namespace WavefrontBinding
    {
        inline constexpr std::uint32_t Constants = 0u;
        inline constexpr std::uint32_t PathStates = 1u;
        inline constexpr std::uint32_t RayQueueA = 2u;
        inline constexpr std::uint32_t HitQueue = 3u;
        inline constexpr std::uint32_t ShadowQueue = 4u;
        inline constexpr std::uint32_t RayQueueB = 5u;
        inline constexpr std::uint32_t PrivatePathStates = 6u;
        inline constexpr std::uint32_t DenseNext = 7u;
        inline constexpr std::uint32_t DenseShadow = 8u;
        inline constexpr std::uint32_t NextQueue = 9u;
        inline constexpr std::uint32_t CompactionFlags = 10u;
        inline constexpr std::uint32_t Prefix = 11u;
        inline constexpr std::uint32_t ScanScratch = 12u;
        inline constexpr std::uint32_t QueueHeaders = 13u;
        inline constexpr std::uint32_t IndirectArguments = 14u;
        inline constexpr std::uint32_t BounceCounters = 15u;
        inline constexpr std::uint32_t Output = 16u;
        inline constexpr std::uint32_t DirectDiffuse = 17u;
        inline constexpr std::uint32_t DirectSpecular = 18u;
        inline constexpr std::uint32_t IndirectDiffuse = 19u;
        inline constexpr std::uint32_t IndirectSpecular = 20u;
        inline constexpr std::uint32_t DebugOutput = 21u;
        inline constexpr std::uint32_t CameraEmission = 22u;
        inline constexpr std::uint32_t ShadowAov = 23u;
        // Compact one-record-per-pixel export consumed directly by Wave 4.
        // Binding 15 remains the published BounceCounters resource.
        inline constexpr std::uint32_t PrimarySurfaceV2Export = 24u;

        // L7's material-work record is the published HitQueue record. Keep an
        // explicit alias so host code can use the lane name without allocating
        // a second, ABI-incompatible descriptor.
        inline constexpr std::uint32_t MaterialWork = HitQueue;
    }

    namespace ReconstructionBinding
    {
        inline constexpr std::uint32_t PrimarySurface = 0u;
        inline constexpr std::uint32_t GBuffer = PrimarySurface;
        inline constexpr std::uint32_t MotionInputs = 1u;
        inline constexpr std::uint32_t CurrentTransforms = 2u;
        inline constexpr std::uint32_t PreviousTransforms = 3u;
        inline constexpr std::uint32_t RawSignal = 4u;
        inline constexpr std::uint32_t DemodulatedSignal = 5u;
        inline constexpr std::uint32_t HistoryRead = 6u;
        inline constexpr std::uint32_t HistoryWrite = 7u;
        inline constexpr std::uint32_t TemporalSignal = 8u;
        inline constexpr std::uint32_t TemporalDebug = 9u;
        inline constexpr std::uint32_t Variance = 10u;
        inline constexpr std::uint32_t AtrousInput = 11u;
        inline constexpr std::uint32_t AtrousOutput = 12u;
        inline constexpr std::uint32_t ComposedOutput = 13u;
        inline constexpr std::uint32_t AtrousVarianceOutput = 14u;
        // Shared compact primary-surface export.  Set 3 binding 24 aliases
        // the same VkBuffer for Wavefront/ReSTIR consumers; set 4 binding 15
        // lets every production integrator publish it without owning the
        // Wavefront queue layout.
        inline constexpr std::uint32_t PrimarySurfaceExport = 15u;
        inline constexpr std::uint32_t MotionConstants = 16u;
        inline constexpr std::uint32_t PrepareConstants = 17u;
        inline constexpr std::uint32_t TemporalConstants = 18u;
        inline constexpr std::uint32_t VarianceConstants = 19u;
        inline constexpr std::uint32_t AtrousConstants = 20u;
        inline constexpr std::uint32_t ComposeConstants = 21u;
    }
}
