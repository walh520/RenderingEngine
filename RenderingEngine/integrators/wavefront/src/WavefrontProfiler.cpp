#include "../include/WavefrontProfiler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace RenderingEngine::Wavefront
{
    namespace
    {
        void ValidateHandles(
            const VkCommandBuffer commandBuffer,
            const VkQueryPool queryPool)
        {
            if (commandBuffer == VK_NULL_HANDLE || queryPool == VK_NULL_HANDLE)
            {
                throw std::invalid_argument(
                    "Wavefront timestamp commands require valid Vulkan handles.");
            }
        }
    }

    std::uint32_t TimestampQueryCount(
        const std::span<const ScheduledPass> schedule)
    {
        std::uint64_t expectedBegin = 0u;
        for (const ScheduledPass& pass : schedule)
        {
            if (pass.beginTimestampQuery != expectedBegin ||
                pass.endTimestampQuery != expectedBegin + 1u)
            {
                throw std::invalid_argument(
                    "Wavefront timestamp query slots are not contiguous pairs.");
            }
            expectedBegin += 2u;
        }
        if (expectedBegin > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("Wavefront timestamp query count overflowed.");
        }
        return static_cast<std::uint32_t>(expectedBegin);
    }

    void CmdResetTimestampQueries(
        const VkCommandBuffer commandBuffer,
        const VkQueryPool queryPool,
        const std::span<const ScheduledPass> schedule)
    {
        ValidateHandles(commandBuffer, queryPool);
        const std::uint32_t queryCount = TimestampQueryCount(schedule);
        if (queryCount != 0u)
        {
            vkCmdResetQueryPool(commandBuffer, queryPool, 0u, queryCount);
        }
    }

    void CmdWritePassTimestampBegin(
        const VkCommandBuffer commandBuffer,
        const VkQueryPool queryPool,
        const ScheduledPass& pass)
    {
        ValidateHandles(commandBuffer, queryPool);
        vkCmdWriteTimestamp2(
            commandBuffer,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            queryPool,
            pass.beginTimestampQuery);
    }

    void CmdWritePassTimestampEnd(
        const VkCommandBuffer commandBuffer,
        const VkQueryPool queryPool,
        const ScheduledPass& pass)
    {
        ValidateHandles(commandBuffer, queryPool);
        vkCmdWriteTimestamp2(
            commandBuffer,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            queryPool,
            pass.endTimestampQuery);
    }

    std::vector<PassTiming> DecodePassTimings(
        const std::span<const ScheduledPass> schedule,
        const std::span<const TimestampQueryResult> queryResults,
        const TimestampConfiguration configuration)
    {
        if (configuration.validBits == 0u || configuration.validBits > 64u ||
            !std::isfinite(configuration.periodNanoseconds) ||
            configuration.periodNanoseconds <= 0.0)
        {
            throw std::invalid_argument("Wavefront timestamp configuration is invalid.");
        }

        const std::uint32_t queryCount = TimestampQueryCount(schedule);
        if (queryResults.size() < queryCount)
        {
            throw std::invalid_argument("Wavefront timestamp readback is incomplete.");
        }

        const std::uint64_t mask = configuration.validBits == 64u
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t{ 1u } << configuration.validBits) - 1u;
        std::vector<PassTiming> timings;
        timings.reserve(schedule.size());
        for (const ScheduledPass& pass : schedule)
        {
            const TimestampQueryResult begin = queryResults[pass.beginTimestampQuery];
            const TimestampQueryResult end = queryResults[pass.endTimestampQuery];
            PassTiming timing{ pass.kind, pass.bounce, 0.0, false };
            if (begin.availability != 0u && end.availability != 0u)
            {
                const std::uint64_t beginTicks = begin.ticks & mask;
                const std::uint64_t endTicks = end.ticks & mask;
                const std::uint64_t elapsedTicks = (endTicks - beginTicks) & mask;
                timing.gpuMilliseconds = static_cast<double>(elapsedTicks)
                    * configuration.periodNanoseconds * 1.0e-6;
                if (!std::isfinite(timing.gpuMilliseconds))
                {
                    throw std::overflow_error(
                        "Wavefront timestamp conversion produced a non-finite duration.");
                }
                timing.available = true;
            }
            timings.push_back(timing);
        }
        return timings;
    }
}
