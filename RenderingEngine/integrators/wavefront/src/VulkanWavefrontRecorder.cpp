#include "../include/VulkanWavefrontRecorder.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace RenderingEngine::Wavefront
{
    namespace
    {
        [[nodiscard]] std::uint64_t CheckedMultiply(
            const std::uint64_t left,
            const std::uint64_t right,
            const char* const label)
        {
            if (left != 0u
                && right > std::numeric_limits<std::uint64_t>::max() / left)
            {
                throw std::overflow_error(label);
            }
            return left * right;
        }

        [[nodiscard]] bool HasFlag(
            const PipelineDomain value,
            const PipelineDomain flag) noexcept
        {
            return (static_cast<std::uint32_t>(value)
                & static_cast<std::uint32_t>(flag)) != 0u;
        }

        [[nodiscard]] bool HasFlag(
            const MemoryAccess value,
            const MemoryAccess flag) noexcept
        {
            return (static_cast<std::uint32_t>(value)
                & static_cast<std::uint32_t>(flag)) != 0u;
        }

        [[nodiscard]] VkPipelineStageFlags2 ToVulkanStages(
            const PipelineDomain value) noexcept
        {
            VkPipelineStageFlags2 result = VK_PIPELINE_STAGE_2_NONE;
            if (HasFlag(value, PipelineDomain::Transfer))
                result |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            if (HasFlag(value, PipelineDomain::Compute))
                result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            if (HasFlag(value, PipelineDomain::IndirectCommand))
                result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            if (HasFlag(value, PipelineDomain::Fragment))
                result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            if (HasFlag(value, PipelineDomain::AccelerationStructureBuild))
                result |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            return result;
        }

        [[nodiscard]] VkAccessFlags2 ToVulkanAccess(
            const MemoryAccess value) noexcept
        {
            VkAccessFlags2 result = VK_ACCESS_2_NONE;
            if (HasFlag(value, MemoryAccess::TransferWrite))
                result |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
            if (HasFlag(value, MemoryAccess::ShaderRead))
                result |= VK_ACCESS_2_SHADER_READ_BIT;
            if (HasFlag(value, MemoryAccess::ShaderWrite))
                result |= VK_ACCESS_2_SHADER_WRITE_BIT;
            if (HasFlag(value, MemoryAccess::IndirectCommandRead))
                result |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            if (HasFlag(value, MemoryAccess::SampledImageRead))
                result |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            if (HasFlag(value, MemoryAccess::AccelerationStructureRead))
                result |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            if (HasFlag(value, MemoryAccess::AccelerationStructureWrite))
                result |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            return result;
        }

        [[nodiscard]] bool NeedsBarrier(
            const BarrierRequirement& barrier) noexcept
        {
            return barrier.sourceStage != PipelineDomain::None
                || barrier.sourceAccess != MemoryAccess::None
                || barrier.destinationStage != PipelineDomain::None
                || barrier.destinationAccess != MemoryAccess::None;
        }

        void Fail(
            VulkanWavefrontRecordResult& result,
            const VulkanWavefrontRecordStatus status,
            std::string reason) noexcept
        {
            result.status = status;
            result.reason = std::move(reason);
        }
    }

    VulkanWavefrontResourceRequirements MakeVulkanWavefrontResourceRequirements(
        const std::uint32_t width,
        const std::uint32_t height,
        const std::uint32_t maximumBounceCount,
        const std::uint32_t samplesPerDispatch)
    {
        if (maximumBounceCount == 0u)
        {
            throw std::invalid_argument(
                "Wavefront Vulkan resources require at least one bounce.");
        }
        VulkanWavefrontResourceRequirements result{};
        result.queues = MakeQueueAllocation(width, height, samplesPerDispatch);
        result.rayQueueBytesEach = result.queues.rayBytes;
        result.materialWorkBytes = result.queues.materialBytes;
        result.denseNextBytes = result.queues.nextBytes;
        result.denseShadowBytes = result.queues.shadowBytes;
        result.nextQueueBytes = result.queues.nextBytes;
        result.shadowQueueBytes = result.queues.shadowBytes;
        result.shadowAovBytes = result.queues.shadowAovBytes;
        result.primarySurfaceV2Bytes = result.queues.primarySurfaceV2Bytes;
        result.sharedPathStateBytes = result.queues.sharedPathStateBytes;
        result.privatePathStateBytes = result.queues.pathStateBytes;
        result.compactionFlagsBytes = result.queues.flagBytes;
        result.prefixBytes = result.queues.flagBytes;
        result.scanScratchBytes = result.queues.scanScratchBytes;
        result.queueHeaderBytes = CheckedMultiply(
            static_cast<std::uint64_t>(QueueId::Count),
            sizeof(QueueHeader),
            "Wavefront queue-header bytes overflowed.");
        if (maximumBounceCount
            > std::numeric_limits<std::uint32_t>::max() / 3u)
        {
            throw std::overflow_error(
                "Wavefront indirect-command count overflowed.");
        }
        result.indirectCommandCount = maximumBounceCount * 3u;
        result.indirectArgumentBytes = CheckedMultiply(
            result.indirectCommandCount,
            sizeof(DispatchCommandSlot),
            "Wavefront indirect-argument bytes overflowed.");
        result.bounceCounterBytes = CheckedMultiply(
            maximumBounceCount,
            sizeof(BounceCounters),
            "Wavefront bounce-counter bytes overflowed.");
        result.outputImageBytesEach = CheckedMultiply(
            result.queues.pathCapacity,
            sizeof(Float4),
            "Wavefront output image bytes overflowed.");
        return result;
    }

    bool VulkanWavefrontFunctions::Complete(
        const bool timestampsRequired) const noexcept
    {
        return cmdPipelineBarrier2 != nullptr
            && (!timestampsRequired || cmdWriteTimestamp2 != nullptr)
            && cmdBindPipeline != nullptr
            && cmdBindDescriptorSets != nullptr
            && cmdPushConstants != nullptr
            && cmdDispatch != nullptr
            && cmdDispatchIndirect != nullptr;
    }

    VkPipeline VulkanWavefrontPipelines::For(const PassKind kind) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        return index < passes.size() ? passes[index] : VK_NULL_HANDLE;
    }

    VulkanWavefrontRecordResult RecordVulkanWavefront(
        const VulkanWavefrontFunctions& functions,
        const VulkanWavefrontRecordRequest& request) noexcept
    {
        VulkanWavefrontRecordResult result{};
        if (request.commandBuffer == VK_NULL_HANDLE || request.passes.empty()
            || request.pipelines.layout == VK_NULL_HANDLE)
        {
            Fail(result, VulkanWavefrontRecordStatus::InvalidRequest,
                "Wavefront Vulkan record request is incomplete.");
            return result;
        }
        for (const VkDescriptorSet set : request.descriptorSets)
        {
            if (set == VK_NULL_HANDLE)
            {
                Fail(result, VulkanWavefrontRecordStatus::InvalidRequest,
                    "Wavefront Vulkan record requires compatible set 0..3 descriptors.");
                return result;
            }
        }
        if (request.writeTimestamps
            && request.timestampQueryPool == VK_NULL_HANDLE)
        {
            Fail(result, VulkanWavefrontRecordStatus::InvalidRequest,
                "Wavefront timestamp recording requires a query pool.");
            return result;
        }
        if (!functions.Complete(request.writeTimestamps))
        {
            Fail(result, VulkanWavefrontRecordStatus::MissingFunction,
                "Wavefront Vulkan function table is incomplete.");
            return result;
        }

        try
        {
            for (const PreparedPass& pass : request.passes)
            {
                const VkPipeline pipeline = request.pipelines.For(pass.scheduled.kind);
                if (pipeline == VK_NULL_HANDLE)
                {
                    Fail(result, VulkanWavefrontRecordStatus::MissingPipeline,
                        "Wavefront prepared pass has no compute pipeline.");
                    return result;
                }

                if (NeedsBarrier(pass.scheduled.barrierBefore))
                {
                    const VkMemoryBarrier2 memoryBarrier{
                        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                        nullptr,
                        ToVulkanStages(pass.scheduled.barrierBefore.sourceStage),
                        ToVulkanAccess(pass.scheduled.barrierBefore.sourceAccess),
                        ToVulkanStages(pass.scheduled.barrierBefore.destinationStage),
                        ToVulkanAccess(pass.scheduled.barrierBefore.destinationAccess)};
                    const VkDependencyInfo dependency{
                        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        nullptr,
                        0u,
                        1u,
                        &memoryBarrier,
                        0u,
                        nullptr,
                        0u,
                        nullptr};
                    functions.cmdPipelineBarrier2(
                        request.commandBuffer, &dependency);
                    ++result.barrierCount;
                }

                if (request.writeTimestamps)
                {
                    if (pass.scheduled.beginTimestampQuery
                        > std::numeric_limits<std::uint32_t>::max()
                            - request.timestampQueryBase
                        || pass.scheduled.endTimestampQuery
                        > std::numeric_limits<std::uint32_t>::max()
                            - request.timestampQueryBase)
                    {
                        Fail(result, VulkanWavefrontRecordStatus::QueryOverflow,
                            "Wavefront timestamp query range overflowed.");
                        return result;
                    }
                    functions.cmdWriteTimestamp2(
                        request.commandBuffer,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        request.timestampQueryPool,
                        request.timestampQueryBase
                            + pass.scheduled.beginTimestampQuery);
                    ++result.timestampWriteCount;
                }

                functions.cmdBindPipeline(
                    request.commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline);
                functions.cmdBindDescriptorSets(
                    request.commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    request.pipelines.layout,
                    0u,
                    static_cast<std::uint32_t>(request.descriptorSets.size()),
                    request.descriptorSets.data(),
                    0u,
                    nullptr);
                functions.cmdPushConstants(
                    request.commandBuffer,
                    request.pipelines.layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0u,
                    static_cast<std::uint32_t>(sizeof(pass.constants)),
                    &pass.constants);

                if (pass.usesIndirectDispatch)
                {
                    if (request.indirectArguments == VK_NULL_HANDLE)
                    {
                        Fail(result, VulkanWavefrontRecordStatus::InvalidRequest,
                            "Wavefront indirect pass requires an indirect buffer.");
                        return result;
                    }
                    constexpr VkDeviceSize stride = sizeof(DispatchCommandSlot);
                    const VkDeviceSize slot = pass.constants.pass.w;
                    if (slot > (std::numeric_limits<VkDeviceSize>::max()
                            - request.indirectBaseOffset) / stride)
                    {
                        Fail(result, VulkanWavefrontRecordStatus::OffsetOverflow,
                            "Wavefront indirect command offset overflowed.");
                        return result;
                    }
                    const VkDeviceSize offset = request.indirectBaseOffset
                        + slot * stride;
                    if ((offset & 3u) != 0u)
                    {
                        Fail(result, VulkanWavefrontRecordStatus::InvalidRequest,
                            "Wavefront indirect command offset is not four-byte aligned.");
                        return result;
                    }
                    functions.cmdDispatchIndirect(
                        request.commandBuffer,
                        request.indirectArguments,
                        offset);
                    ++result.indirectDispatchCount;
                }
                else
                {
                    functions.cmdDispatch(
                        request.commandBuffer,
                        pass.directDispatch.groupCountX,
                        pass.directDispatch.groupCountY,
                        pass.directDispatch.groupCountZ);
                    ++result.directDispatchCount;
                }

                if (request.writeTimestamps)
                {
                    functions.cmdWriteTimestamp2(
                        request.commandBuffer,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        request.timestampQueryPool,
                        request.timestampQueryBase
                            + pass.scheduled.endTimestampQuery);
                    ++result.timestampWriteCount;
                }
                ++result.recordedPassCount;
            }
        }
        catch (const std::exception& error)
        {
            Fail(result, VulkanWavefrontRecordStatus::InvalidRequest, error.what());
            return result;
        }
        catch (...)
        {
            Fail(result, VulkanWavefrontRecordStatus::InvalidRequest,
                "Wavefront Vulkan recorder caught an unknown exception.");
            return result;
        }

        result.status = VulkanWavefrontRecordStatus::Succeeded;
        result.reason.clear();
        return result;
    }
}
