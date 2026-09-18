#pragma once

#include "contracts/ReconstructionAbiV2.hpp"
#include "reconstruction/Types.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rendering::reconstruction
{
    struct VulkanHistoryResourceIndices final
    {
        std::uint32_t writePhysicalIndex = 0u;
        std::uint32_t readPhysicalIndex = 0xffffffffu;
        bool hasPrevious = false;
    };

    [[nodiscard]] VulkanHistoryResourceIndices ResolveVulkanHistoryResources(
        std::uint64_t frameIndex,
        std::uint32_t framesInFlight);

    struct VulkanReconstructionResourceRequirements final
    {
        std::uint64_t pixelCount = 0u;
        std::uint32_t framesInFlight = 0u;
        std::uint32_t historyResourceCount = 0u;
        std::uint64_t gBufferBytes = 0u;
        std::uint64_t motionInputBytes = 0u;
        std::uint64_t rawSignalBytes = 0u;
        std::uint64_t workingSignalBytesEach = 0u;
        std::uint64_t historyBytesEach = 0u;
        std::uint64_t temporalDebugBytes = 0u;
        std::uint64_t varianceBytesEach = 0u;
        std::uint64_t outputImageBytes = 0u;
        std::uint32_t atrousSignalPingPongCount = 2u;
        std::uint32_t atrousVariancePingPongCount = 2u;
        std::uint32_t descriptorBindingSpan = 22u;
        std::uint32_t constantBufferCount = 6u;
        VkBufferUsageFlags workingBufferUsage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VkImageUsageFlags outputImageUsage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    };

    [[nodiscard]] VulkanReconstructionResourceRequirements
        MakeVulkanReconstructionResourceRequirements(
            Extent2D extent,
            std::uint32_t framesInFlight);

    enum class VulkanReconstructionPassKind : std::uint32_t
    {
        MotionVectors = 0u,
        PrepareSignal,
        TemporalAccumulation,
        VarianceBootstrap,
        AtrousIteration,
        Compose,
        Count
    };

    struct PreparedVulkanReconstructionPass final
    {
        VulkanReconstructionPassKind kind =
            VulkanReconstructionPassKind::MotionVectors;
        std::uint32_t iteration = 0u;
        std::uint32_t beginTimestampQuery = 0xffffffffu;
        std::uint32_t endTimestampQuery = 0xffffffffu;
    };

    [[nodiscard]] std::vector<PreparedVulkanReconstructionPass>
        BuildVulkanReconstructionPlan(
            ReconstructionOutput output,
            std::uint32_t atrousIterationCount,
            std::uint32_t firstTimestampQuery = 0u);

    [[nodiscard]] bool ValidateVulkanReconstructionPlan(
        std::span<const PreparedVulkanReconstructionPass> passes) noexcept;

    struct VulkanReconstructionFunctions final
    {
        PFN_vkCmdPipelineBarrier2 cmdPipelineBarrier2 = nullptr;
        PFN_vkCmdWriteTimestamp2 cmdWriteTimestamp2 = nullptr;
        PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
        PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
        PFN_vkCmdDispatch cmdDispatch = nullptr;

        [[nodiscard]] bool Complete(bool timestampsRequired) const noexcept;
    };

    inline constexpr std::size_t VulkanReconstructionPassKindCount =
        static_cast<std::size_t>(VulkanReconstructionPassKind::Count);

    struct VulkanReconstructionPipelines final
    {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::array<VkPipeline, VulkanReconstructionPassKindCount> passes{};

        [[nodiscard]] VkPipeline For(
            VulkanReconstructionPassKind kind) const noexcept;
    };

    struct VulkanReconstructionRecordRequest final
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        Extent2D extent{};
        std::span<const PreparedVulkanReconstructionPass> passes{};
        VulkanReconstructionPipelines pipelines{};
        // One set-4 descriptor set per pass. A-Trous iterations must use
        // distinct variants so read/write signal and variance ping-pong never
        // alias within one dispatch.
        std::span<const VkDescriptorSet> descriptorSets{};
        VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
        bool writeTimestamps = false;
        bool insertInitialProducerBarrier = true;
        VkPipelineStageFlags2 initialSourceStage =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        VkAccessFlags2 initialSourceAccess =
            VK_ACCESS_2_SHADER_WRITE_BIT;
    };

    enum class VulkanReconstructionRecordStatus : std::uint32_t
    {
        Succeeded = 0u,
        InvalidRequest,
        MissingFunction,
        MissingPipeline
    };

    struct VulkanReconstructionRecordResult final
    {
        VulkanReconstructionRecordStatus status =
            VulkanReconstructionRecordStatus::InvalidRequest;
        std::uint32_t recordedPassCount = 0u;
        std::uint32_t barrierCount = 0u;
        std::uint32_t timestampWriteCount = 0u;
        bool historyPrepared = false;
        std::string reason;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == VulkanReconstructionRecordStatus::Succeeded;
        }
    };

    // Records compute work only. The caller owns resources, descriptors,
    // pipelines, submission, completion waits, and deferred history publish.
    [[nodiscard]] VulkanReconstructionRecordResult RecordVulkanReconstruction(
        const VulkanReconstructionFunctions& functions,
        const VulkanReconstructionRecordRequest& request) noexcept;
}
