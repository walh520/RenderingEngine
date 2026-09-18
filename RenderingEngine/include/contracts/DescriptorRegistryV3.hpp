#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV3
{
    enum class DescriptorSet : std::uint32_t
    {
        Frame = 0u,
        Scene = 1u,
        TraversalBackend = 2u,
        WavefrontQueues = 3u,
        Reconstruction = 4u,
        Restir = 5u,
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
        // L7 writes a compact GpuPrimarySurfaceV2 array here at bounce zero;
        // L9 reads the same storage buffer without reinterpreting set-4's
        // wider GpuGBufferRecordV2 stride.
        inline constexpr std::uint32_t PrimarySurfaceV2Export = 24u;
    }

    namespace ReconstructionBinding
    {
        inline constexpr std::uint32_t PrimarySurface = 0u;
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
        // Alias of WavefrontBinding::PrimarySurfaceV2Export.  Both bindings
        // must reference one physical buffer so L9 consumes exactly what the
        // selected integrator published through the reconstruction stage.
        inline constexpr std::uint32_t PrimarySurfaceExport = 15u;
        inline constexpr std::uint32_t MotionConstants = 16u;
        inline constexpr std::uint32_t PrepareConstants = 17u;
        inline constexpr std::uint32_t TemporalConstants = 18u;
        inline constexpr std::uint32_t VarianceConstants = 19u;
        inline constexpr std::uint32_t AtrousConstants = 20u;
        inline constexpr std::uint32_t ComposeConstants = 21u;
    }

    namespace DebugProfilerBinding
    {
        inline constexpr std::uint32_t Constants = 0u;
    }

    // Set 5 is a canonical cross-stage registry. A binding has one stable
    // meaning and is never reloaded with a stage-specific private meaning.
    namespace RestirBinding
    {
        inline constexpr std::uint32_t Parameters = 0u;
        inline constexpr std::uint32_t Candidates = 1u;
        inline constexpr std::uint32_t ReservoirRead = 2u;
        inline constexpr std::uint32_t HistoryReservoirRead = 3u;
        inline constexpr std::uint32_t SurfaceCurrent = 4u;
        inline constexpr std::uint32_t SurfaceHistory = 5u;
        inline constexpr std::uint32_t HistoryIdentity = 6u;
        inline constexpr std::uint32_t ReservoirWrite = 7u;
        inline constexpr std::uint32_t DebugRecord = 8u;
        inline constexpr std::uint32_t Statistics = 9u;
        inline constexpr std::uint32_t VisibilityResults = 10u;
        inline constexpr std::uint32_t CandidateAtCenter = 11u;
        inline constexpr std::uint32_t LightTable = 12u;
        inline constexpr std::uint32_t PairwiseTargetSupport = 13u;
        inline constexpr std::uint32_t DirectLighting = 14u;
        inline constexpr std::uint32_t ValidationReasons = 15u;
        inline constexpr std::uint32_t DebugImage = 16u;
        inline constexpr std::uint32_t HistoryAtCurrent = 17u;
        inline constexpr std::uint32_t ReferenceTarget = 18u;
        inline constexpr std::uint32_t ReferenceVisibility = 19u;

        // Wave 4 production-frame graph resources. These are append-only
        // additions; bindings 0-19 retain their published meanings.
        inline constexpr std::uint32_t CurrentToPreviousLightIndex = 20u;
        inline constexpr std::uint32_t PreviousToCurrentLightIndex = 21u;
        inline constexpr std::uint32_t NeighborIndices = 22u;
        inline constexpr std::uint32_t ShadowRayQueue = 23u;
        inline constexpr std::uint32_t DirectDiffuse = 24u;
        inline constexpr std::uint32_t DirectSpecular = 25u;
        inline constexpr std::uint32_t InitialReservoir = 26u;
        inline constexpr std::uint32_t TemporalReservoir = 27u;
        inline constexpr std::uint32_t SpatialReservoir = 28u;
        inline constexpr std::uint32_t PublishedReservoir = 29u;
        // Previous-frame published history is a distinct immutable input.
        // It must never alias the current frame's PublishedReservoir output.
        inline constexpr std::uint32_t PreviousPublishedReservoir = 30u;
    }
}
