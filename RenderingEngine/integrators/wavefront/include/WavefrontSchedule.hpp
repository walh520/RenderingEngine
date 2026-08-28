#pragma once

#include "WavefrontTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Wavefront
{
    inline constexpr std::uint32_t QueueThreadCount = 128;

    struct DispatchLimits
    {
        std::uint32_t maxGroupCountX;
        std::uint32_t maxGroupCountY;
    };

    struct ScanLevel
    {
        std::uint32_t elementCount;
        std::uint32_t groupCount;
        std::uint64_t prefixOffsetElements;
        std::uint64_t sumsOffsetElements;
    };

    struct QueueAllocation
    {
        std::uint32_t pathCapacity;
        std::uint64_t rayBytes;
        std::uint64_t materialBytes;
        std::uint64_t nextBytes;
        std::uint64_t shadowBytes;
        std::uint64_t pathStateBytes;
        std::uint64_t flagBytes;
        std::uint64_t scanScratchBytes;
        std::vector<ScanLevel> scanLevels;
    };

    struct AtomicAppendResult
    {
        std::uint32_t slot;
        bool committed;
        bool fatal;
    };

    enum class PassKind : std::uint32_t
    {
        FrameReset,
        RayGen,
        PrepareIntersect,
        Intersect,
        ResetShadeOutputs,
        Shade,
        ScanBlocks,
        AddScanOffsets,
        Scatter,
        PrepareShadow,
        TraceShadow,
        ResetNextRay,
        PrepareNext,
        NextBounce,
        Resolve,
        VisualizeCounters
    };

    enum class PipelineDomain : std::uint32_t
    {
        None = 0,
        Transfer = 1u << 0u,
        Compute = 1u << 1u,
        IndirectCommand = 1u << 2u,
        Fragment = 1u << 3u,
        AccelerationStructureBuild = 1u << 4u
    };

    enum class MemoryAccess : std::uint32_t
    {
        None = 0,
        TransferWrite = 1u << 0u,
        ShaderRead = 1u << 1u,
        ShaderWrite = 1u << 2u,
        IndirectCommandRead = 1u << 3u,
        SampledImageRead = 1u << 4u,
        AccelerationStructureRead = 1u << 5u,
        AccelerationStructureWrite = 1u << 6u
    };

    [[nodiscard]] constexpr PipelineDomain operator|(
        const PipelineDomain left,
        const PipelineDomain right) noexcept
    {
        return static_cast<PipelineDomain>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr MemoryAccess operator|(
        const MemoryAccess left,
        const MemoryAccess right) noexcept
    {
        return static_cast<MemoryAccess>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    struct BarrierRequirement
    {
        PipelineDomain sourceStage;
        MemoryAccess sourceAccess;
        PipelineDomain destinationStage;
        MemoryAccess destinationAccess;
    };

    struct ScheduledPass
    {
        PassKind kind;
        std::uint32_t bounce;
        QueueId readQueue;
        QueueId writeQueue;
        std::uint32_t scanLevel;
        BarrierRequirement barrierBefore{};
        std::uint32_t beginTimestampQuery = 0;
        std::uint32_t endTimestampQuery = 0;
        std::uint32_t resetQueueMask = 0;
        std::uint32_t resetFlags = ResetNone;
        std::uint32_t resetIndirectCommandCount = 0;
    };

    struct PreparedPass
    {
        ScheduledPass scheduled;
        WavefrontPassConstants constants{};
        DispatchCommandSlot directDispatch{};
        bool usesIndirectDispatch = false;
    };

    [[nodiscard]] DispatchCommandSlot MakeIndirectDispatch(
        std::uint32_t itemCount,
        DispatchLimits limits);

    [[nodiscard]] std::uint64_t LinearInvocationIndex(
        std::uint32_t groupX,
        std::uint32_t groupY,
        std::uint32_t localX,
        std::uint32_t dispatchedGroupCountX);

    [[nodiscard]] QueueAllocation MakeQueueAllocation(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t samplesPerDispatch = 1);

    [[nodiscard]] AtomicAppendResult SimulateAtomicAppend(
        QueueHeader& header,
        std::uint32_t& fatalMask,
        std::uint32_t queueFatalBit) noexcept;

    [[nodiscard]] bool HasFatalError(std::span<const QueueHeader> headers) noexcept;

    [[nodiscard]] std::vector<ScheduledPass> BuildFrameSchedule(
        std::uint32_t maximumBounceCount,
        QueueMode queueMode,
        std::uint32_t scanLevelCount,
        bool includeCounterVisualization = false);

    // Produces the exact private push constants, direct group counts, indirect
    // command slots, scan offsets and timestamp slots needed by a Vulkan host.
    // Capacity, queue mode, dispatch limits and bounce count are derived from
    // the same frame record that the host binds, preventing plan/frame drift.
    // The caller records the barriers described by scheduled.barrierBefore.
    [[nodiscard]] std::vector<PreparedPass> BuildPreparedFramePlan(
        const WavefrontFrameConstants& frame,
        const QueueAllocation& allocation,
        bool includeCounterVisualization = false);

    [[nodiscard]] constexpr BarrierRequirement TransferResetToComputeBarrier() noexcept
    {
        return {
            PipelineDomain::Transfer,
            MemoryAccess::TransferWrite,
            PipelineDomain::Compute,
            MemoryAccess::ShaderRead | MemoryAccess::ShaderWrite
        };
    }

    [[nodiscard]] constexpr BarrierRequirement AccelerationStructureToComputeBarrier() noexcept
    {
        return {
            PipelineDomain::AccelerationStructureBuild,
            MemoryAccess::AccelerationStructureWrite,
            PipelineDomain::Compute,
            MemoryAccess::AccelerationStructureRead
        };
    }

    [[nodiscard]] constexpr BarrierRequirement ResolveToFragmentBarrier() noexcept
    {
        return {
            PipelineDomain::Compute,
            MemoryAccess::ShaderWrite,
            PipelineDomain::Fragment,
            MemoryAccess::SampledImageRead
        };
    }
}
