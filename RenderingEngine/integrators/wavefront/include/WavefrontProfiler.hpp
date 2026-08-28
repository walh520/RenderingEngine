#pragma once

#include "WavefrontSchedule.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Wavefront
{
    // This is the exact layout produced by vkGetQueryPoolResults with
    // VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT.
    struct TimestampQueryResult
    {
        std::uint64_t ticks;
        std::uint64_t availability;
    };

    struct TimestampConfiguration
    {
        std::uint32_t validBits;
        double periodNanoseconds;
    };

    struct PassTiming
    {
        PassKind kind;
        std::uint32_t bounce;
        double gpuMilliseconds;
        bool available;
    };

    [[nodiscard]] std::uint32_t TimestampQueryCount(
        std::span<const ScheduledPass> schedule);

    // Vulkan 1.3 command helpers. The host resets once per frame and writes
    // begin/end around the corresponding scheduled dispatch.
    void CmdResetTimestampQueries(
        VkCommandBuffer commandBuffer,
        VkQueryPool queryPool,
        std::span<const ScheduledPass> schedule);

    void CmdWritePassTimestampBegin(
        VkCommandBuffer commandBuffer,
        VkQueryPool queryPool,
        const ScheduledPass& pass);

    void CmdWritePassTimestampEnd(
        VkCommandBuffer commandBuffer,
        VkQueryPool queryPool,
        const ScheduledPass& pass);

    [[nodiscard]] std::vector<PassTiming> DecodePassTimings(
        std::span<const ScheduledPass> schedule,
        std::span<const TimestampQueryResult> queryResults,
        TimestampConfiguration configuration);

    static_assert(sizeof(TimestampQueryResult) == 16u);
}
