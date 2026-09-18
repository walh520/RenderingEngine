#include "../include/WavefrontSchedule.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace RenderingEngine::Wavefront
{
    namespace
    {
        [[nodiscard]] constexpr std::uint64_t CeilDivide(
            const std::uint64_t value,
            const std::uint64_t divisor)
        {
            return value == 0 ? 0 : 1 + (value - 1) / divisor;
        }

        [[nodiscard]] std::uint64_t CheckedMultiply(
            const std::uint64_t left,
            const std::uint64_t right,
            const char* const label)
        {
            if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
            {
                throw std::overflow_error(label);
            }
            return left * right;
        }

        [[nodiscard]] std::uint64_t CheckedAdd(
            const std::uint64_t left,
            const std::uint64_t right,
            const char* const label)
        {
            if (right > std::numeric_limits<std::uint64_t>::max() - left)
            {
                throw std::overflow_error(label);
            }
            return left + right;
        }
    }

    DispatchCommandSlot MakeIndirectDispatch(
        const std::uint32_t itemCount,
        const DispatchLimits limits)
    {
        if (limits.maxGroupCountX == 0 || limits.maxGroupCountY == 0)
        {
            throw std::invalid_argument("Wavefront dispatch limits must be non-zero.");
        }

        const std::uint64_t groups = CeilDivide(itemCount, QueueThreadCount);
        if (groups == 0)
        {
            return { 0, 1, 1, 0 };
        }

        const std::uint64_t maxGroups = CheckedMultiply(
            limits.maxGroupCountX,
            limits.maxGroupCountY,
            "Wavefront dispatch limit product overflowed.");
        if (groups > maxGroups)
        {
            throw std::overflow_error("Wavefront dispatch exceeds the device's 2D group limits.");
        }

        const std::uint32_t groupsX = static_cast<std::uint32_t>(
            groups < limits.maxGroupCountX ? groups : limits.maxGroupCountX);
        const std::uint32_t groupsY = static_cast<std::uint32_t>(CeilDivide(groups, groupsX));
        return { groupsX, groupsY, 1, 0 };
    }

    std::uint64_t LinearInvocationIndex(
        const std::uint32_t groupX,
        const std::uint32_t groupY,
        const std::uint32_t localX,
        const std::uint32_t dispatchedGroupCountX)
    {
        if (localX >= QueueThreadCount || dispatchedGroupCountX == 0)
        {
            throw std::invalid_argument("Invalid wavefront workgroup coordinates.");
        }
        return (static_cast<std::uint64_t>(groupY) * dispatchedGroupCountX + groupX)
            * QueueThreadCount + localX;
    }

    QueueAllocation MakeQueueAllocation(
        const std::uint32_t width,
        const std::uint32_t height,
        const std::uint32_t samplesPerDispatch)
    {
        if (width == 0 || height == 0 || samplesPerDispatch == 0)
        {
            throw std::invalid_argument("Wavefront dimensions and sample batch must be non-zero.");
        }

        const std::uint64_t pixels = CheckedMultiply(width, height, "Wavefront pixel count overflowed.");
        const std::uint64_t paths = CheckedMultiply(
            pixels, samplesPerDispatch, "Wavefront path capacity overflowed.");
        if (paths > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("Wavefront path capacity exceeds the 32-bit queue contract.");
        }

        QueueAllocation allocation{};
        allocation.pathCapacity = static_cast<std::uint32_t>(paths);
        allocation.rayBytes = CheckedMultiply(paths, sizeof(WavefrontRayItem), "Ray queue size overflowed.");
        allocation.materialBytes = CheckedMultiply(paths, sizeof(MaterialWorkItem), "Material queue size overflowed.");
        allocation.nextBytes = CheckedMultiply(paths, sizeof(NextBounceCandidate), "Next queue size overflowed.");
        allocation.shadowBytes = CheckedMultiply(paths, sizeof(ShadowWorkItem), "Shadow queue size overflowed.");
        allocation.shadowAovBytes = CheckedMultiply(paths, sizeof(ShadowAovItem), "Shadow AOV sidecar size overflowed.");
        allocation.primarySurfaceV2Bytes = CheckedMultiply(
            pixels,
            sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2),
            "Primary-surface v2 export size overflowed.");
        allocation.sharedPathStateBytes = CheckedMultiply(paths, sizeof(SharedPathState), "Shared path-state size overflowed.");
        allocation.pathStateBytes = CheckedMultiply(paths, sizeof(WavefrontPathState), "Path-state size overflowed.");
        allocation.flagBytes = CheckedMultiply(paths, sizeof(std::uint32_t) * 2u, "Flag buffer size overflowed.");

        std::uint64_t elementCount = paths;
        std::uint64_t scratchOffset = 0;
        for (;;)
        {
            const std::uint64_t groupCount = CeilDivide(elementCount, QueueThreadCount);
            if (elementCount > std::numeric_limits<std::uint32_t>::max()
                || groupCount > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::overflow_error("Wavefront scan hierarchy exceeds 32-bit indexing.");
            }

            ScanLevel level{};
            level.elementCount = static_cast<std::uint32_t>(elementCount);
            level.groupCount = static_cast<std::uint32_t>(groupCount);
            level.prefixOffsetElements = scratchOffset;
            scratchOffset = CheckedAdd(scratchOffset, elementCount, "Scan prefix offset overflowed.");
            level.sumsOffsetElements = scratchOffset;
            scratchOffset = CheckedAdd(scratchOffset, groupCount, "Scan sums offset overflowed.");
            allocation.scanLevels.push_back(level);
            if (groupCount == 1)
            {
                break;
            }
            elementCount = groupCount;
        }
        allocation.scanScratchBytes = CheckedMultiply(
            scratchOffset, sizeof(std::uint32_t) * 2u, "Scan scratch size overflowed.");
        if (scratchOffset > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error(
                "Wavefront scan offsets exceed the shader's 32-bit element addressing.");
        }
        return allocation;
    }

    AtomicAppendResult SimulateAtomicAppend(
        QueueHeader& header,
        std::uint32_t& fatalMask,
        const std::uint32_t queueFatalBit) noexcept
    {
        if (header.attemptedCount == std::numeric_limits<std::uint32_t>::max())
        {
            header.activeCount = header.capacity;
            if (header.overflowCount != std::numeric_limits<std::uint32_t>::max())
            {
                ++header.overflowCount;
            }
            fatalMask |= queueFatalBit;
            return { std::numeric_limits<std::uint32_t>::max(), false, true };
        }

        const std::uint32_t slot = header.attemptedCount++;
        if (slot < header.capacity)
        {
            header.activeCount = header.attemptedCount;
            return { slot, true, fatalMask != 0 };
        }

        ++header.overflowCount;
        header.activeCount = header.capacity;
        fatalMask |= queueFatalBit;
        return { slot, false, true };
    }

    bool HasFatalError(const std::span<const QueueHeader> headers) noexcept
    {
        for (const QueueHeader& header : headers)
        {
            if (header.overflowCount != 0 || header.attemptedCount > header.capacity)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<ScheduledPass> BuildFrameSchedule(
        const std::uint32_t maximumBounceCount,
        const QueueMode queueMode,
        const std::uint32_t scanLevelCount,
        const bool includeCounterVisualization)
    {
        if (maximumBounceCount == 0)
        {
            throw std::invalid_argument("Wavefront schedule requires at least one bounce.");
        }
        if (queueMode != QueueMode::AtomicAppend && queueMode != QueueMode::PrefixScan)
        {
            throw std::invalid_argument("Wavefront schedule queue mode is invalid.");
        }
        if (queueMode == QueueMode::PrefixScan && scanLevelCount == 0)
        {
            throw std::invalid_argument("Prefix compaction requires a non-empty scan hierarchy.");
        }
        if (maximumBounceCount > std::numeric_limits<std::uint32_t>::max() / 3u)
        {
            throw std::overflow_error("Wavefront indirect command count overflowed.");
        }

        const std::uint64_t compactionPasses = queueMode == QueueMode::PrefixScan
            ? 2ull * scanLevelCount
            : 0ull;
        const std::uint64_t passCount = 3ull +
            static_cast<std::uint64_t>(maximumBounceCount) *
                (9ull + compactionPasses) +
            (includeCounterVisualization ? 1ull : 0ull);
        if (passCount > (static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1ull) / 2ull)
        {
            throw std::overflow_error("Wavefront timestamp query count overflowed.");
        }

        std::vector<ScheduledPass> schedule;
        schedule.reserve(static_cast<std::size_t>(passCount));
        schedule.push_back({ PassKind::FrameReset, 0, QueueId::Global, QueueId::Global, 0 });
        schedule.push_back({ PassKind::RayGen, 0, QueueId::Global, QueueId::RayA, 0 });

        for (std::uint32_t bounce = 0; bounce < maximumBounceCount; ++bounce)
        {
            const QueueId currentRay = (bounce & 1u) == 0u ? QueueId::RayA : QueueId::RayB;
            const QueueId nextRay = (bounce & 1u) == 0u ? QueueId::RayB : QueueId::RayA;
            schedule.push_back({ PassKind::PrepareIntersect, bounce, currentRay, QueueId::Material, 0 });
            schedule.push_back({ PassKind::Intersect, bounce, currentRay, QueueId::Material, 0 });
            schedule.push_back({ PassKind::ResetShadeOutputs, bounce, QueueId::Global, QueueId::Next, 0 });
            // Intersect writes a dense MaterialWorkItem at the current ray index;
            // it deliberately does not append to a material queue. Shade must
            // therefore reuse the current ray queue's active count and indirect
            // command while reading material work by the same dense index.
            schedule.push_back({ PassKind::Shade, bounce, currentRay, QueueId::Next, 0 });

            if (queueMode == QueueMode::PrefixScan)
            {
                for (std::uint32_t level = 0; level < scanLevelCount; ++level)
                {
                    schedule.push_back({ PassKind::ScanBlocks, bounce, currentRay, QueueId::Next, level });
                }
                for (std::uint32_t level = scanLevelCount; level-- > 1u;)
                {
                    schedule.push_back({ PassKind::AddScanOffsets, bounce, currentRay, QueueId::Next, level - 1u });
                }
                schedule.push_back({ PassKind::Scatter, bounce, currentRay, QueueId::Next, 0 });
            }

            schedule.push_back({ PassKind::PrepareShadow, bounce, QueueId::Shadow, QueueId::Shadow, 0 });
            schedule.push_back({ PassKind::TraceShadow, bounce, QueueId::Shadow, QueueId::Shadow, 0 });
            schedule.push_back({ PassKind::ResetNextRay, bounce, QueueId::Global, nextRay, 0 });
            schedule.push_back({ PassKind::PrepareNext, bounce, QueueId::Next, nextRay, 0 });
            schedule.push_back({ PassKind::NextBounce, bounce, QueueId::Next, nextRay, 0 });
        }

        schedule.push_back({ PassKind::Resolve, maximumBounceCount, QueueId::Global, QueueId::Global, 0 });
        if (includeCounterVisualization)
        {
            schedule.push_back({ PassKind::VisualizeCounters, maximumBounceCount, QueueId::Global, QueueId::Global, 0 });
        }

        const BarrierRequirement computeToCompute{
            PipelineDomain::Compute,
            MemoryAccess::ShaderWrite,
            PipelineDomain::Compute,
            MemoryAccess::ShaderRead | MemoryAccess::ShaderWrite
        };
        const BarrierRequirement computeToIndirect{
            PipelineDomain::Compute,
            MemoryAccess::ShaderWrite,
            PipelineDomain::IndirectCommand | PipelineDomain::Compute,
            MemoryAccess::IndirectCommandRead | MemoryAccess::ShaderRead
        };

        for (std::size_t index = 0; index < schedule.size(); ++index)
        {
            ScheduledPass& pass = schedule[index];
            pass.beginTimestampQuery = static_cast<std::uint32_t>(index * 2u);
            pass.endTimestampQuery = pass.beginTimestampQuery + 1u;
            if (index == 0)
            {
                pass.barrierBefore = TransferResetToComputeBarrier();
            }
            else if (pass.kind == PassKind::Intersect
                || pass.kind == PassKind::TraceShadow
                || pass.kind == PassKind::NextBounce)
            {
                // The immediately preceding Prepare* pass writes both an indirect
                // command and the sanitized active count consumed by this shader.
                pass.barrierBefore = computeToIndirect;
            }
            else
            {
                pass.barrierBefore = computeToCompute;
            }

            if (pass.kind == PassKind::FrameReset)
            {
                pass.resetQueueMask = (1u << static_cast<std::uint32_t>(QueueId::Count)) - 1u;
                pass.resetFlags = ResetCounters | ResetIndirectArguments | ResetValidateFrame;
                pass.resetIndirectCommandCount = maximumBounceCount * 3u;
            }
            else if (pass.kind == PassKind::ResetShadeOutputs)
            {
                pass.resetQueueMask =
                    (1u << static_cast<std::uint32_t>(QueueId::Next)) |
                    (1u << static_cast<std::uint32_t>(QueueId::Shadow));
            }
            else if (pass.kind == PassKind::ResetNextRay)
            {
                pass.resetQueueMask = 1u << static_cast<std::uint32_t>(pass.writeQueue);
            }
        }
        return schedule;
    }

    std::vector<PreparedPass> BuildPreparedFramePlan(
        const WavefrontFrameConstants& frame,
        const QueueAllocation& allocation,
        const bool includeCounterVisualization)
    {
        const std::uint32_t width = frame.imageSample.x;
        const std::uint32_t height = frame.imageSample.y;
        const std::uint32_t maximumBounceCount = frame.imageSample.w;
        if (frame.capacityModeSeed.y > static_cast<std::uint32_t>(QueueMode::PrefixScan))
        {
            throw std::invalid_argument("Wavefront frame queue mode is invalid.");
        }
        const QueueMode queueMode = static_cast<QueueMode>(frame.capacityModeSeed.y);
        const DispatchLimits limits{
            frame.dispatchLimits.x,
            frame.dispatchLimits.y
        };
        const std::uint64_t pixelCount = CheckedMultiply(
            width, height, "Wavefront prepared-plan pixel count overflowed.");
        if (width == 0u || height == 0u ||
            pixelCount != allocation.pathCapacity ||
            frame.capacityModeSeed.x != allocation.pathCapacity)
        {
            throw std::invalid_argument(
                "Wavefront prepared plan requires one progressive path per pixel.");
        }
        if (limits.maxGroupCountX == 0u || limits.maxGroupCountY == 0u ||
            frame.dispatchLimits.z != QueueThreadCount)
        {
            throw std::invalid_argument("Wavefront prepared-plan dispatch limits are invalid.");
        }

        if (allocation.scanLevels.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            throw std::overflow_error("Wavefront scan level count exceeds uint32.");
        }
        const std::uint32_t scanLevelCount =
            static_cast<std::uint32_t>(allocation.scanLevels.size());
        std::vector<ScheduledPass> schedule = BuildFrameSchedule(
            maximumBounceCount,
            queueMode,
            scanLevelCount,
            includeCounterVisualization);
        std::vector<PreparedPass> prepared;
        prepared.reserve(schedule.size());

        const auto imageDispatch = [&]() -> DispatchCommandSlot
        {
            const std::uint64_t groupsX = CeilDivide(width, 8u);
            const std::uint64_t groupsY = CeilDivide(height, 8u);
            if (groupsX > limits.maxGroupCountX || groupsY > limits.maxGroupCountY)
            {
                throw std::overflow_error("Wavefront image dispatch exceeds device limits.");
            }
            return {
                static_cast<std::uint32_t>(groupsX),
                static_cast<std::uint32_t>(groupsY),
                1u,
                0u
            };
        };

        std::uint32_t nextCommandSlot = 0u;
        std::uint32_t intersectCommandSlot = 0u;
        std::uint32_t shadowCommandSlot = 0u;
        std::uint32_t nextBounceCommandSlot = 0u;
        const auto queueValue = [](const QueueId queue)
        {
            return static_cast<std::uint32_t>(queue);
        };

        for (ScheduledPass& scheduled : schedule)
        {
            PreparedPass pass{};
            pass.scheduled = scheduled;
            pass.constants.pass = {
                scheduled.bounce,
                queueValue(scheduled.readQueue),
                queueValue(scheduled.writeQueue),
                0u
            };

            switch (scheduled.kind)
            {
            case PassKind::FrameReset:
            {
                const std::uint32_t indirectCount = maximumBounceCount * 3u;
                const std::uint32_t resetItemCount = std::max(
                    std::max(static_cast<std::uint32_t>(QueueId::Count), maximumBounceCount),
                    indirectCount);
                const std::uint64_t resetGroups = CeilDivide(resetItemCount, 64u);
                if (resetGroups > limits.maxGroupCountX)
                {
                    throw std::overflow_error("Wavefront reset dispatch exceeds X group limit.");
                }
                pass.constants.params0.x = scheduled.resetQueueMask;
                pass.constants.params2.z = scheduled.resetFlags;
                pass.constants.params2.w = indirectCount;
                pass.directDispatch = {
                    static_cast<std::uint32_t>(resetGroups), 1u, 1u, 0u
                };
                break;
            }
            case PassKind::RayGen:
            case PassKind::Resolve:
            case PassKind::VisualizeCounters:
                pass.directDispatch = imageDispatch();
                break;

            case PassKind::PrepareIntersect:
                intersectCommandSlot = nextCommandSlot++;
                pass.constants.pass.w = intersectCommandSlot;
                pass.directDispatch = { 1u, 1u, 1u, 0u };
                break;
            case PassKind::Intersect:
            case PassKind::Shade:
                pass.constants.pass.w = intersectCommandSlot;
                pass.usesIndirectDispatch = true;
                break;

            case PassKind::ResetShadeOutputs:
            case PassKind::ResetNextRay:
                pass.constants.params0.x = scheduled.resetQueueMask;
                pass.constants.params2.z = scheduled.resetFlags;
                pass.constants.params2.w = scheduled.resetIndirectCommandCount;
                pass.directDispatch = { 1u, 1u, 1u, 0u };
                break;

            case PassKind::ScanBlocks:
            {
                if (scheduled.scanLevel >= allocation.scanLevels.size())
                {
                    throw std::out_of_range("Wavefront scan level is outside allocation.");
                }
                const ScanLevel& level = allocation.scanLevels[scheduled.scanLevel];
                pass.constants.params0.y = level.elementCount;
                pass.constants.params0.z = scheduled.scanLevel == 0u
                    ? 0u
                    : static_cast<std::uint32_t>(
                        allocation.scanLevels[scheduled.scanLevel - 1u].sumsOffsetElements);
                pass.constants.params0.w = static_cast<std::uint32_t>(
                    level.prefixOffsetElements);
                pass.constants.params1.x = static_cast<std::uint32_t>(
                    level.sumsOffsetElements);
                pass.constants.params1.z =
                    (scheduled.scanLevel == 0u
                        ? ScanInputIsFlags | ScanPrefixIsBase
                        : 0u) |
                    (scheduled.scanLevel + 1u == scanLevelCount
                        ? ScanTopLevel
                        : 0u);
                pass.constants.params1.w = queueValue(scheduled.readQueue);
                pass.directDispatch = MakeIndirectDispatch(level.elementCount, limits);
                break;
            }
            case PassKind::AddScanOffsets:
            {
                const std::uint32_t childIndex = scheduled.scanLevel;
                if (childIndex + 1u >= allocation.scanLevels.size())
                {
                    throw std::out_of_range("Wavefront scan parent level is missing.");
                }
                const ScanLevel& child = allocation.scanLevels[childIndex];
                const ScanLevel& parent = allocation.scanLevels[childIndex + 1u];
                pass.constants.params0.y = child.elementCount;
                pass.constants.params0.w = static_cast<std::uint32_t>(
                    child.prefixOffsetElements);
                pass.constants.params1.y = static_cast<std::uint32_t>(
                    parent.prefixOffsetElements);
                pass.constants.params1.z = childIndex == 0u
                    ? ScanChildIsBase
                    : 0u;
                pass.constants.params1.w = queueValue(scheduled.readQueue);
                pass.directDispatch = MakeIndirectDispatch(child.elementCount, limits);
                break;
            }
            case PassKind::Scatter:
                pass.constants.params0.y = allocation.pathCapacity;
                pass.constants.params1.w = queueValue(scheduled.readQueue);
                pass.directDispatch = MakeIndirectDispatch(
                    allocation.pathCapacity, limits);
                break;

            case PassKind::PrepareShadow:
                shadowCommandSlot = nextCommandSlot++;
                pass.constants.pass.w = shadowCommandSlot;
                pass.directDispatch = { 1u, 1u, 1u, 0u };
                break;
            case PassKind::TraceShadow:
                pass.constants.pass.w = shadowCommandSlot;
                pass.usesIndirectDispatch = true;
                break;

            case PassKind::PrepareNext:
                nextBounceCommandSlot = nextCommandSlot++;
                pass.constants.pass.w = nextBounceCommandSlot;
                pass.directDispatch = { 1u, 1u, 1u, 0u };
                break;
            case PassKind::NextBounce:
                pass.constants.pass.w = nextBounceCommandSlot;
                pass.usesIndirectDispatch = true;
                break;
            }
            prepared.push_back(pass);
        }

        if (nextCommandSlot != maximumBounceCount * 3u)
        {
            throw std::logic_error("Wavefront indirect command slot assignment drifted.");
        }
        return prepared;
    }
}
