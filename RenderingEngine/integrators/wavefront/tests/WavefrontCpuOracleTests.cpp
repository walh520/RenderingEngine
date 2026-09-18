#include "../include/WavefrontCompactionOracle.hpp"
#include "../include/WavefrontProfiler.hpp"
#include "../include/WavefrontRngDimensions.hpp"
#include "../include/WavefrontSchedule.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace RenderingEngine::Wavefront::Tests
{
    namespace
    {
        void Require(const bool condition, const char* const expression)
        {
            if (!condition)
            {
                throw std::runtime_error(expression);
            }
        }
    }

#define WF_REQUIRE(expression) Require(static_cast<bool>(expression), #expression)

    // Deliberately no main: the lane-local test project owns composition.
    void RunWavefrontCpuOracleSelfTests()
    {
        const auto empty = CompactStable({});
        WF_REQUIRE(empty.nextIndices.empty());

        const std::array flags = {
            FlagPair{ 1, 0 }, FlagPair{ 0, 1 }, FlagPair{ 1, 1 }, FlagPair{ 0, 0 },
            FlagPair{ 1, 0 }
        };
        const CompactionOracleResult compacted = CompactStable(flags);
        WF_REQUIRE(compacted.totals.next == 3);
        WF_REQUIRE(compacted.totals.shadow == 2);
        WF_REQUIRE((compacted.nextIndices == std::vector<std::uint32_t>{ 0, 2, 4 }));
        WF_REQUIRE((compacted.shadowIndices == std::vector<std::uint32_t>{ 1, 2 }));

        // GPU prefix mode allocates for capacity but masks everything beyond
        // the current source queue's active count. This oracle captures the
        // required result even when the inactive tail contains stale ones.
        const std::array staleCapacityFlags = {
            FlagPair{ 1, 0 }, FlagPair{ 0, 1 }, FlagPair{ 1, 0 },
            FlagPair{ 1, 1 }, FlagPair{ 1, 1 }, FlagPair{ 1, 1 }
        };
        const CompactionOracleResult activePrefix = CompactStable(
            std::span<const FlagPair>(staleCapacityFlags.data(), 3u));
        WF_REQUIRE((activePrefix.nextIndices == std::vector<std::uint32_t>{ 0, 2 }));
        WF_REQUIRE((activePrefix.shadowIndices == std::vector<std::uint32_t>{ 1 }));

        const DispatchCommandSlot zero = MakeIndirectDispatch(0, { 65535, 65535 });
        WF_REQUIRE(zero.groupCountX == 0 && zero.groupCountY == 1 && zero.groupCountZ == 1);
        const DispatchCommandSlot split = MakeIndirectDispatch(
            QueueThreadCount * 65535u + 1u, { 65535, 65535 });
        WF_REQUIRE(split.groupCountX == 65535 && split.groupCountY == 2);

        const QueueAllocation allocation = MakeQueueAllocation(1920, 1080);
        WF_REQUIRE(allocation.pathCapacity == 1920u * 1080u);
        WF_REQUIRE(allocation.primarySurfaceV2Bytes
            == 1920ull * 1080ull
                * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2));
        WF_REQUIRE(!allocation.scanLevels.empty());
        WF_REQUIRE(MakeQueueAllocation(1, 1).scanLevels.size() == 1);

        const QueueAllocation batched = MakeQueueAllocation(16u, 8u, 4u);
        WF_REQUIRE(batched.pathCapacity == 16u * 8u * 4u);
        WF_REQUIRE(batched.primarySurfaceV2Bytes
            == 16ull * 8ull
                * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2));

        for (const std::size_t count : { 127u, 128u, 129u, 4097u })
        {
            std::vector<FlagPair> boundaryFlags(count);
            std::vector<std::uint32_t> expectedNext;
            std::vector<std::uint32_t> expectedShadow;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                boundaryFlags[index] = { index % 3u == 0u, index % 5u == 0u };
                if (boundaryFlags[index].next != 0u) expectedNext.push_back(index);
                if (boundaryFlags[index].shadow != 0u) expectedShadow.push_back(index);
            }
            const CompactionOracleResult boundary = CompactStable(boundaryFlags);
            WF_REQUIRE(boundary.nextIndices == expectedNext);
            WF_REQUIRE(boundary.shadowIndices == expectedShadow);
        }

        const std::vector<ScheduledPass> prefixSchedule = BuildFrameSchedule(
            4, QueueMode::PrefixScan,
            static_cast<std::uint32_t>(allocation.scanLevels.size()), true);
        WF_REQUIRE(prefixSchedule.front().kind == PassKind::FrameReset);
        WF_REQUIRE(prefixSchedule.back().kind == PassKind::VisualizeCounters);
        WF_REQUIRE((prefixSchedule.front().resetFlags & ResetCounters) != 0u);
        WF_REQUIRE((prefixSchedule.front().resetFlags & ResetIndirectArguments) != 0u);
        WF_REQUIRE((prefixSchedule.front().resetFlags & ResetValidateFrame) != 0u);
        WF_REQUIRE(prefixSchedule.front().resetIndirectCommandCount == 4u * 3u);
        for (std::size_t index = 0; index < prefixSchedule.size(); ++index)
        {
            WF_REQUIRE(prefixSchedule[index].beginTimestampQuery == index * 2u);
            WF_REQUIRE(prefixSchedule[index].endTimestampQuery == index * 2u + 1u);
            if (prefixSchedule[index].kind == PassKind::Shade ||
                prefixSchedule[index].kind == PassKind::ScanBlocks ||
                prefixSchedule[index].kind == PassKind::Scatter)
            {
                WF_REQUIRE(prefixSchedule[index].readQueue == QueueId::RayA ||
                    prefixSchedule[index].readQueue == QueueId::RayB);
            }
            if (prefixSchedule[index].kind == PassKind::ResetShadeOutputs ||
                prefixSchedule[index].kind == PassKind::ResetNextRay)
            {
                WF_REQUIRE(prefixSchedule[index].resetFlags == ResetNone);
                WF_REQUIRE(prefixSchedule[index].resetIndirectCommandCount == 0u);
            }
        }

        WavefrontFrameConstants frame{};
        frame.imageSample = { 1920u, 1080u, 0u, 4u };
        frame.capacityModeSeed = {
            allocation.pathCapacity,
            static_cast<std::uint32_t>(QueueMode::PrefixScan),
            1u,
            2u
        };
        frame.dispatchLimits = { 65535u, 65535u, QueueThreadCount, 0u };
        const std::vector<PreparedPass> prepared = BuildPreparedFramePlan(
            frame,
            allocation,
            true);
        WF_REQUIRE(prepared.size() == prefixSchedule.size());
        WF_REQUIRE(prepared.front().constants.params2.z ==
            (ResetCounters | ResetIndirectArguments | ResetValidateFrame));
        WF_REQUIRE(prepared.front().constants.params2.w == 12u);
        WF_REQUIRE(prepared.front().directDispatch.groupCountX >= 1u);
        std::uint32_t indirectPassCount = 0u;
        for (const PreparedPass& pass : prepared)
        {
            if (pass.usesIndirectDispatch)
            {
                ++indirectPassCount;
                WF_REQUIRE(pass.constants.pass.w < 12u);
            }
            if (pass.scheduled.kind == PassKind::ScanBlocks &&
                pass.scheduled.scanLevel == 0u)
            {
                WF_REQUIRE((pass.constants.params1.z & ScanInputIsFlags) != 0u);
                WF_REQUIRE((pass.constants.params1.z & ScanPrefixIsBase) != 0u);
                WF_REQUIRE(pass.constants.params1.w ==
                    static_cast<std::uint32_t>(pass.scheduled.readQueue));
            }
        }
        WF_REQUIRE(indirectPassCount == 4u * 4u);

        const std::uint32_t queryCount = TimestampQueryCount(prefixSchedule);
        WF_REQUIRE(queryCount == prefixSchedule.size() * 2u);
        std::vector<TimestampQueryResult> timestampResults(queryCount);
        for (const ScheduledPass& pass : prefixSchedule)
        {
            timestampResults[pass.beginTimestampQuery] = {
                1000u + pass.beginTimestampQuery * 100u, 1u };
            timestampResults[pass.endTimestampQuery] = {
                timestampResults[pass.beginTimestampQuery].ticks + 50u, 1u };
        }
        const std::vector<PassTiming> timings = DecodePassTimings(
            prefixSchedule,
            timestampResults,
            { 64u, 2.0 });
        WF_REQUIRE(timings.size() == prefixSchedule.size());
        for (const PassTiming& timing : timings)
        {
            WF_REQUIRE(timing.available);
            WF_REQUIRE(std::abs(timing.gpuMilliseconds - 0.0001) < 1.0e-12);
        }

        const std::array wrapSchedule{
            ScheduledPass{
                PassKind::Shade, 0u, QueueId::RayA, QueueId::Next, 0u,
                {}, 0u, 1u }
        };
        const std::array wrapResults{
            TimestampQueryResult{ 250u, 1u },
            TimestampQueryResult{ 5u, 1u }
        };
        const std::vector<PassTiming> wrapped = DecodePassTimings(
            wrapSchedule,
            wrapResults,
            { 8u, 10.0 });
        WF_REQUIRE(wrapped.size() == 1u && wrapped[0].available);
        WF_REQUIRE(std::abs(wrapped[0].gpuMilliseconds - 0.00011) < 1.0e-12);

        bool rejectedScheduleMode = false;
        try
        {
            static_cast<void>(BuildFrameSchedule(
                1u, static_cast<QueueMode>(99u), 0u));
        }
        catch (const std::invalid_argument&)
        {
            rejectedScheduleMode = true;
        }
        WF_REQUIRE(rejectedScheduleMode);

        frame.capacityModeSeed.y = 99u;
        bool rejectedFrameDrift = false;
        try
        {
            static_cast<void>(BuildPreparedFramePlan(frame, allocation));
        }
        catch (const std::invalid_argument&)
        {
            rejectedFrameDrift = true;
        }
        WF_REQUIRE(rejectedFrameDrift);

        QueueHeader header{ 0, 0, 1, 0 };
        std::uint32_t fatalMask = 0;
        WF_REQUIRE(SimulateAtomicAppend(header, fatalMask, FatalNextOverflow).committed);
        const AtomicAppendResult overflow = SimulateAtomicAppend(
            header, fatalMask, FatalNextOverflow);
        WF_REQUIRE(!overflow.committed && overflow.fatal);
        WF_REQUIRE((fatalMask & FatalNextOverflow) != 0);

        WF_REQUIRE(RngDimensions::ForBounce(3, RngDimensions::BsdfU)
            == RngDimensions::FirstBounce + 3u * RngDimensions::DimensionsPerBounce + 6u);

        bool rejected = false;
        try
        {
            static_cast<void>(MakeQueueAllocation(
                std::numeric_limits<std::uint32_t>::max(), 2));
        }
        catch (const std::overflow_error&)
        {
            rejected = true;
        }
        WF_REQUIRE(rejected);
    }

#undef WF_REQUIRE
}
