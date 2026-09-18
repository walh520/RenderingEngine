#include "../include/VulkanWavefrontRecorder.hpp"

#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace RenderingEngine::Wavefront::Tests
{
    namespace
    {
        struct CommandAudit final
        {
            std::uint32_t barriers = 0u;
            std::uint32_t timestampWrites = 0u;
            std::uint32_t pipelineBinds = 0u;
            std::uint32_t descriptorBinds = 0u;
            std::uint32_t pushConstants = 0u;
            std::uint32_t directDispatches = 0u;
            std::uint32_t indirectDispatches = 0u;
            std::vector<std::uint32_t> queries;
            std::vector<VkDeviceSize> indirectOffsets;
        };

        CommandAudit gAudit;

        void Require(const bool condition, const char* const expression)
        {
            if (!condition) throw std::runtime_error(expression);
        }

#define WF_VK_REQUIRE(expression) Require(static_cast<bool>(expression), #expression)

        template <typename Handle>
        [[nodiscard]] Handle FakeHandle(const std::uintptr_t value) noexcept
        {
            if constexpr (std::is_pointer_v<Handle>)
                return reinterpret_cast<Handle>(value);
            else
                return static_cast<Handle>(value);
        }

        VKAPI_ATTR void VKAPI_CALL AuditBarrier(
            VkCommandBuffer,
            const VkDependencyInfo* const dependency)
        {
            WF_VK_REQUIRE(dependency != nullptr);
            WF_VK_REQUIRE(dependency->memoryBarrierCount == 1u);
            WF_VK_REQUIRE(dependency->pMemoryBarriers != nullptr);
            ++gAudit.barriers;
        }

        VKAPI_ATTR void VKAPI_CALL AuditTimestamp(
            VkCommandBuffer,
            VkPipelineStageFlags2,
            VkQueryPool,
            const std::uint32_t query)
        {
            ++gAudit.timestampWrites;
            gAudit.queries.push_back(query);
        }

        VKAPI_ATTR void VKAPI_CALL AuditBindPipeline(
            VkCommandBuffer,
            VkPipelineBindPoint bindPoint,
            VkPipeline)
        {
            WF_VK_REQUIRE(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);
            ++gAudit.pipelineBinds;
        }

        VKAPI_ATTR void VKAPI_CALL AuditBindDescriptorSets(
            VkCommandBuffer,
            VkPipelineBindPoint bindPoint,
            VkPipelineLayout,
            std::uint32_t firstSet,
            std::uint32_t descriptorSetCount,
            const VkDescriptorSet*,
            std::uint32_t,
            const std::uint32_t*)
        {
            WF_VK_REQUIRE(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);
            WF_VK_REQUIRE(firstSet == 0u);
            WF_VK_REQUIRE(descriptorSetCount == 4u);
            ++gAudit.descriptorBinds;
        }

        VKAPI_ATTR void VKAPI_CALL AuditPushConstants(
            VkCommandBuffer,
            VkPipelineLayout,
            VkShaderStageFlags stages,
            std::uint32_t offset,
            std::uint32_t size,
            const void*)
        {
            WF_VK_REQUIRE(stages == VK_SHADER_STAGE_COMPUTE_BIT);
            WF_VK_REQUIRE(offset == 0u);
            WF_VK_REQUIRE(size == sizeof(WavefrontPassConstants));
            ++gAudit.pushConstants;
        }

        VKAPI_ATTR void VKAPI_CALL AuditDispatch(
            VkCommandBuffer,
            std::uint32_t,
            std::uint32_t,
            std::uint32_t)
        {
            ++gAudit.directDispatches;
        }

        VKAPI_ATTR void VKAPI_CALL AuditDispatchIndirect(
            VkCommandBuffer,
            VkBuffer,
            const VkDeviceSize offset)
        {
            ++gAudit.indirectDispatches;
            gAudit.indirectOffsets.push_back(offset);
        }

        [[nodiscard]] VulkanWavefrontFunctions Functions() noexcept
        {
            return {
                &AuditBarrier,
                &AuditTimestamp,
                &AuditBindPipeline,
                &AuditBindDescriptorSets,
                &AuditPushConstants,
                &AuditDispatch,
                &AuditDispatchIndirect};
        }

        [[nodiscard]] VulkanWavefrontPipelines Pipelines()
        {
            VulkanWavefrontPipelines result{};
            result.layout = FakeHandle<VkPipelineLayout>(1u);
            std::uintptr_t handle = 2u;
            for (VkPipeline& pipeline : result.passes)
                pipeline = FakeHandle<VkPipeline>(handle++);
            return result;
        }

        [[nodiscard]] VulkanWavefrontRecordRequest MakeRequest(
            const std::span<const PreparedPass> passes)
        {
            VulkanWavefrontRecordRequest request{};
            request.commandBuffer = FakeHandle<VkCommandBuffer>(100u);
            request.passes = passes;
            request.pipelines = Pipelines();
            for (std::size_t index = 0u;
                index < request.descriptorSets.size(); ++index)
            {
                request.descriptorSets[index] =
                    FakeHandle<VkDescriptorSet>(200u + index);
            }
            request.indirectArguments = FakeHandle<VkBuffer>(300u);
            request.indirectBaseOffset = 64u;
            request.timestampQueryPool = FakeHandle<VkQueryPool>(400u);
            request.timestampQueryBase = 10u;
            request.writeTimestamps = true;
            return request;
        }
    }

    void RunVulkanWavefrontRecorderSelfTests()
    {
        const VulkanWavefrontResourceRequirements requirements =
            MakeVulkanWavefrontResourceRequirements(16u, 8u, 3u);
        WF_VK_REQUIRE(requirements.queues.pathCapacity == 128u);
        WF_VK_REQUIRE(requirements.indirectCommandCount == 9u);
        WF_VK_REQUIRE(requirements.indirectArgumentBytes
            == 9u * sizeof(DispatchCommandSlot));
        WF_VK_REQUIRE(requirements.queueHeaderBytes
            == static_cast<std::uint64_t>(QueueId::Count) * sizeof(QueueHeader));
        WF_VK_REQUIRE(requirements.bounceCounterBytes
            == 3u * sizeof(BounceCounters));
        WF_VK_REQUIRE(requirements.primarySurfaceV2Bytes
            == 16u * 8u * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2));
        WF_VK_REQUIRE(requirements.outputImageBytesEach
            == 128u * sizeof(Float4));

        WavefrontFrameConstants frame{};
        frame.imageSample = {16u, 8u, 0u, 2u};
        frame.capacityModeSeed = {
            requirements.queues.pathCapacity,
            static_cast<std::uint32_t>(QueueMode::AtomicAppend),
            7u,
            11u};
        frame.dispatchLimits = {65535u, 65535u, QueueThreadCount, 0u};
        const QueueAllocation allocation = MakeQueueAllocation(16u, 8u);
        const std::vector<PreparedPass> plan =
            BuildPreparedFramePlan(frame, allocation, true);
        WF_VK_REQUIRE(!plan.empty());

        gAudit = {};
        const VulkanWavefrontRecordRequest request = MakeRequest(plan);
        const VulkanWavefrontRecordResult recorded =
            RecordVulkanWavefront(Functions(), request);
        WF_VK_REQUIRE(recorded.Succeeded());
        WF_VK_REQUIRE(recorded.recordedPassCount == plan.size());
        WF_VK_REQUIRE(recorded.directDispatchCount
            + recorded.indirectDispatchCount == plan.size());
        WF_VK_REQUIRE(recorded.indirectDispatchCount != 0u);
        WF_VK_REQUIRE(recorded.timestampWriteCount == plan.size() * 2u);
        WF_VK_REQUIRE(gAudit.pipelineBinds == plan.size());
        WF_VK_REQUIRE(gAudit.descriptorBinds == plan.size());
        WF_VK_REQUIRE(gAudit.pushConstants == plan.size());
        WF_VK_REQUIRE(gAudit.timestampWrites == plan.size() * 2u);
        WF_VK_REQUIRE(!gAudit.queries.empty());
        WF_VK_REQUIRE(gAudit.queries.front()
            == request.timestampQueryBase
                + plan.front().scheduled.beginTimestampQuery);
        WF_VK_REQUIRE(gAudit.barriers == recorded.barrierCount);
        WF_VK_REQUIRE(gAudit.directDispatches == recorded.directDispatchCount);
        WF_VK_REQUIRE(gAudit.indirectDispatches == recorded.indirectDispatchCount);
        for (const VkDeviceSize offset : gAudit.indirectOffsets)
        {
            WF_VK_REQUIRE(offset >= request.indirectBaseOffset);
            WF_VK_REQUIRE((offset - request.indirectBaseOffset)
                % sizeof(DispatchCommandSlot) == 0u);
        }

        VulkanWavefrontFunctions incomplete = Functions();
        incomplete.cmdDispatchIndirect = nullptr;
        const VulkanWavefrontRecordResult missingFunction =
            RecordVulkanWavefront(incomplete, request);
        WF_VK_REQUIRE(missingFunction.status
            == VulkanWavefrontRecordStatus::MissingFunction);

        VulkanWavefrontRecordRequest missingPipeline = request;
        missingPipeline.pipelines.passes[
            static_cast<std::size_t>(plan.front().scheduled.kind)] = VK_NULL_HANDLE;
        const VulkanWavefrontRecordResult noPipeline =
            RecordVulkanWavefront(Functions(), missingPipeline);
        WF_VK_REQUIRE(noPipeline.status
            == VulkanWavefrontRecordStatus::MissingPipeline);

        const PreparedPass* indirectPass = nullptr;
        for (const PreparedPass& pass : plan)
        {
            if (pass.usesIndirectDispatch)
            {
                indirectPass = &pass;
                break;
            }
        }
        WF_VK_REQUIRE(indirectPass != nullptr);
        VulkanWavefrontRecordRequest unaligned = MakeRequest(
            std::span<const PreparedPass>{indirectPass, 1u});
        unaligned.indirectBaseOffset = 2u;
        const VulkanWavefrontRecordResult badOffset =
            RecordVulkanWavefront(Functions(), unaligned);
        WF_VK_REQUIRE(badOffset.status
            == VulkanWavefrontRecordStatus::InvalidRequest);
    }
}
