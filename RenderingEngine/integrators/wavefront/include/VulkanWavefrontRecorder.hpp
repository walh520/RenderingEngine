#pragma once

#include "WavefrontSchedule.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace RenderingEngine::Wavefront
{
    inline constexpr std::size_t VulkanWavefrontPassKindCount =
        static_cast<std::size_t>(PassKind::VisualizeCounters) + 1u;

    struct VulkanWavefrontResourceRequirements final
    {
        QueueAllocation queues{};
        std::uint64_t rayQueueBytesEach = 0u;
        std::uint64_t materialWorkBytes = 0u;
        std::uint64_t denseNextBytes = 0u;
        std::uint64_t denseShadowBytes = 0u;
        std::uint64_t nextQueueBytes = 0u;
        std::uint64_t shadowQueueBytes = 0u;
        std::uint64_t shadowAovBytes = 0u;
        std::uint64_t primarySurfaceV2Bytes = 0u;
        std::uint64_t sharedPathStateBytes = 0u;
        std::uint64_t privatePathStateBytes = 0u;
        std::uint64_t compactionFlagsBytes = 0u;
        std::uint64_t prefixBytes = 0u;
        std::uint64_t scanScratchBytes = 0u;
        std::uint64_t queueHeaderBytes = 0u;
        std::uint64_t indirectArgumentBytes = 0u;
        std::uint64_t bounceCounterBytes = 0u;
        std::uint64_t outputImageBytesEach = 0u;
        std::uint32_t outputImageCount = 7u;
        std::uint32_t indirectCommandCount = 0u;
        VkDeviceSize indirectCommandStride = sizeof(DispatchCommandSlot);
        VkDeviceSize requiredIndirectOffsetAlignment = 4u;
    };

    [[nodiscard]] VulkanWavefrontResourceRequirements
        MakeVulkanWavefrontResourceRequirements(
            std::uint32_t width,
            std::uint32_t height,
            std::uint32_t maximumBounceCount,
            std::uint32_t samplesPerDispatch = 1u);

    struct VulkanWavefrontFunctions final
    {
        PFN_vkCmdPipelineBarrier2 cmdPipelineBarrier2 = nullptr;
        PFN_vkCmdWriteTimestamp2 cmdWriteTimestamp2 = nullptr;
        PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
        PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
        PFN_vkCmdPushConstants cmdPushConstants = nullptr;
        PFN_vkCmdDispatch cmdDispatch = nullptr;
        PFN_vkCmdDispatchIndirect cmdDispatchIndirect = nullptr;

        [[nodiscard]] bool Complete(bool timestampsRequired) const noexcept;
    };

    struct VulkanWavefrontPipelines final
    {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::array<VkPipeline, VulkanWavefrontPassKindCount> passes{};

        [[nodiscard]] VkPipeline For(PassKind kind) const noexcept;
    };

    struct VulkanWavefrontRecordRequest final
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        std::span<const PreparedPass> passes{};
        VulkanWavefrontPipelines pipelines{};
        std::array<VkDescriptorSet, 4u> descriptorSets{};
        VkBuffer indirectArguments = VK_NULL_HANDLE;
        VkDeviceSize indirectBaseOffset = 0u;
        VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
        std::uint32_t timestampQueryBase = 0u;
        bool writeTimestamps = false;
    };

    enum class VulkanWavefrontRecordStatus : std::uint32_t
    {
        Succeeded = 0u,
        InvalidRequest,
        MissingFunction,
        MissingPipeline,
        OffsetOverflow,
        QueryOverflow
    };

    struct VulkanWavefrontRecordResult final
    {
        VulkanWavefrontRecordStatus status =
            VulkanWavefrontRecordStatus::InvalidRequest;
        std::uint32_t recordedPassCount = 0u;
        std::uint32_t directDispatchCount = 0u;
        std::uint32_t indirectDispatchCount = 0u;
        std::uint32_t barrierCount = 0u;
        std::uint32_t timestampWriteCount = 0u;
        std::string reason;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == VulkanWavefrontRecordStatus::Succeeded;
        }
    };

    // Records the exact lane-local prepared plan. The caller owns all Vulkan
    // objects and must keep set 0..3 compatible with the supplied layout. This
    // recorder deliberately does not create scene/traversal resources or submit
    // the command buffer; those are production-runtime responsibilities.
    [[nodiscard]] VulkanWavefrontRecordResult RecordVulkanWavefront(
        const VulkanWavefrontFunctions& functions,
        const VulkanWavefrontRecordRequest& request) noexcept;
}
