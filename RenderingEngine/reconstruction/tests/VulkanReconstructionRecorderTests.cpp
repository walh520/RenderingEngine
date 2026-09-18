#include "reconstruction/VulkanReconstructionRecorder.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace rendering::reconstruction::tests
{
    namespace
    {
        struct CommandAudit final
        {
            std::uint32_t barriers = 0u;
            std::uint32_t timestamps = 0u;
            std::uint32_t pipelineBinds = 0u;
            std::uint32_t descriptorBinds = 0u;
            std::uint32_t dispatches = 0u;
            std::uint32_t lastGroupCountX = 0u;
            std::uint32_t lastGroupCountY = 0u;
        };

        CommandAudit gAudit;

        void Require(const bool condition, const char* const expression)
        {
            if (!condition) throw std::runtime_error(expression);
        }

#define L8_VK_REQUIRE(expression) Require(static_cast<bool>(expression), #expression)

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
            L8_VK_REQUIRE(dependency != nullptr);
            L8_VK_REQUIRE(dependency->memoryBarrierCount == 1u);
            ++gAudit.barriers;
        }

        VKAPI_ATTR void VKAPI_CALL AuditTimestamp(
            VkCommandBuffer,
            VkPipelineStageFlags2,
            VkQueryPool,
            std::uint32_t)
        {
            ++gAudit.timestamps;
        }

        VKAPI_ATTR void VKAPI_CALL AuditBindPipeline(
            VkCommandBuffer,
            const VkPipelineBindPoint bindPoint,
            VkPipeline)
        {
            L8_VK_REQUIRE(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);
            ++gAudit.pipelineBinds;
        }

        VKAPI_ATTR void VKAPI_CALL AuditBindDescriptorSets(
            VkCommandBuffer,
            const VkPipelineBindPoint bindPoint,
            VkPipelineLayout,
            const std::uint32_t firstSet,
            const std::uint32_t descriptorSetCount,
            const VkDescriptorSet*,
            std::uint32_t,
            const std::uint32_t*)
        {
            L8_VK_REQUIRE(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);
            L8_VK_REQUIRE(firstSet == 4u);
            L8_VK_REQUIRE(descriptorSetCount == 1u);
            ++gAudit.descriptorBinds;
        }

        VKAPI_ATTR void VKAPI_CALL AuditDispatch(
            VkCommandBuffer,
            const std::uint32_t groupCountX,
            const std::uint32_t groupCountY,
            const std::uint32_t groupCountZ)
        {
            L8_VK_REQUIRE(groupCountZ == 1u);
            ++gAudit.dispatches;
            gAudit.lastGroupCountX = groupCountX;
            gAudit.lastGroupCountY = groupCountY;
        }

        [[nodiscard]] VulkanReconstructionFunctions Functions() noexcept
        {
            return {
                &AuditBarrier,
                &AuditTimestamp,
                &AuditBindPipeline,
                &AuditBindDescriptorSets,
                &AuditDispatch};
        }

        [[nodiscard]] VulkanReconstructionPipelines Pipelines()
        {
            VulkanReconstructionPipelines result{};
            result.layout = FakeHandle<VkPipelineLayout>(1u);
            std::uintptr_t handle = 2u;
            for (VkPipeline& pipeline : result.passes)
                pipeline = FakeHandle<VkPipeline>(handle++);
            return result;
        }
    }

    void RunVulkanReconstructionRecorderSelfTests()
    {
        const VulkanReconstructionResourceRequirements requirements =
            MakeVulkanReconstructionResourceRequirements({16u, 8u}, 3u);
        L8_VK_REQUIRE(requirements.pixelCount == 128u);
        L8_VK_REQUIRE(requirements.historyResourceCount == 6u);
        L8_VK_REQUIRE(requirements.gBufferBytes
            == 128u * sizeof(
                RenderingEngine::Contracts::AbiV2::GpuGBufferRecordV2));
        L8_VK_REQUIRE(requirements.rawSignalBytes
            == 128u * sizeof(
                RenderingEngine::Contracts::AbiV2::GpuReconstructionSignalV2));
        L8_VK_REQUIRE(requirements.historyBytesEach == 128u * 80u);

        const VulkanHistoryResourceIndices frame0 =
            ResolveVulkanHistoryResources(0u, 3u);
        const VulkanHistoryResourceIndices frame3 =
            ResolveVulkanHistoryResources(3u, 3u);
        const VulkanHistoryResourceIndices frame6 =
            ResolveVulkanHistoryResources(6u, 3u);
        L8_VK_REQUIRE(!frame0.hasPrevious);
        L8_VK_REQUIRE(frame0.writePhysicalIndex == 0u);
        L8_VK_REQUIRE(frame3.hasPrevious);
        L8_VK_REQUIRE(frame3.writePhysicalIndex == 3u);
        L8_VK_REQUIRE(frame3.readPhysicalIndex == 2u);
        L8_VK_REQUIRE(frame6.writePhysicalIndex == 0u);
        L8_VK_REQUIRE(frame6.readPhysicalIndex == 5u);

        const std::vector<PreparedVulkanReconstructionPass> raw =
            BuildVulkanReconstructionPlan(ReconstructionOutput::Raw, 0u, 4u);
        const std::vector<PreparedVulkanReconstructionPass> temporal =
            BuildVulkanReconstructionPlan(
                ReconstructionOutput::Temporal, 0u, 8u);
        const std::vector<PreparedVulkanReconstructionPass> spatial =
            BuildVulkanReconstructionPlan(
                ReconstructionOutput::ATrous, 3u, 14u);
        const std::vector<PreparedVulkanReconstructionPass> svgf =
            BuildVulkanReconstructionPlan(
                ReconstructionOutput::Svgf, 3u, 20u);
        L8_VK_REQUIRE(raw.size() == 1u);
        L8_VK_REQUIRE(temporal.size() == 4u);
        L8_VK_REQUIRE(spatial.size() == 6u);
        L8_VK_REQUIRE(svgf.size() == 8u);
        L8_VK_REQUIRE(ValidateVulkanReconstructionPlan(raw));
        L8_VK_REQUIRE(ValidateVulkanReconstructionPlan(temporal));
        L8_VK_REQUIRE(ValidateVulkanReconstructionPlan(spatial));
        L8_VK_REQUIRE(ValidateVulkanReconstructionPlan(svgf));
        L8_VK_REQUIRE(spatial.front().kind
            == VulkanReconstructionPassKind::PrepareSignal);
        L8_VK_REQUIRE(std::none_of(spatial.begin(), spatial.end(),
            [](const PreparedVulkanReconstructionPass& pass)
            {
                return pass.kind == VulkanReconstructionPassKind::MotionVectors
                    || pass.kind
                        == VulkanReconstructionPassKind::TemporalAccumulation;
            }));

        std::vector<VkDescriptorSet> descriptorSets(svgf.size());
        for (std::size_t index = 0u; index < descriptorSets.size(); ++index)
            descriptorSets[index] = FakeHandle<VkDescriptorSet>(100u + index);
        VulkanReconstructionRecordRequest request{};
        request.commandBuffer = FakeHandle<VkCommandBuffer>(200u);
        request.extent = {17u, 9u};
        request.passes = svgf;
        request.pipelines = Pipelines();
        request.descriptorSets = descriptorSets;
        request.timestampQueryPool = FakeHandle<VkQueryPool>(300u);
        request.writeTimestamps = true;

        gAudit = {};
        const VulkanReconstructionRecordResult recorded =
            RecordVulkanReconstruction(Functions(), request);
        L8_VK_REQUIRE(recorded.Succeeded());
        L8_VK_REQUIRE(recorded.historyPrepared);
        L8_VK_REQUIRE(recorded.recordedPassCount == svgf.size());
        L8_VK_REQUIRE(recorded.barrierCount == svgf.size());
        L8_VK_REQUIRE(recorded.timestampWriteCount == svgf.size() * 2u);
        L8_VK_REQUIRE(gAudit.pipelineBinds == svgf.size());
        L8_VK_REQUIRE(gAudit.descriptorBinds == svgf.size());
        L8_VK_REQUIRE(gAudit.dispatches == svgf.size());
        L8_VK_REQUIRE(gAudit.lastGroupCountX == 3u);
        L8_VK_REQUIRE(gAudit.lastGroupCountY == 2u);

        std::vector<VkDescriptorSet> spatialDescriptorSets(spatial.size());
        for (std::size_t index = 0u; index < spatialDescriptorSets.size(); ++index)
            spatialDescriptorSets[index] = FakeHandle<VkDescriptorSet>(400u + index);
        VulkanReconstructionRecordRequest spatialRequest = request;
        spatialRequest.passes = spatial;
        spatialRequest.descriptorSets = spatialDescriptorSets;
        const VulkanReconstructionRecordResult spatialRecorded =
            RecordVulkanReconstruction(Functions(), spatialRequest);
        L8_VK_REQUIRE(spatialRecorded.Succeeded());
        L8_VK_REQUIRE(!spatialRecorded.historyPrepared);

        VulkanReconstructionRecordRequest tooFewDescriptors = request;
        tooFewDescriptors.descriptorSets = std::span<const VkDescriptorSet>{
            descriptorSets.data(), descriptorSets.size() - 1u};
        L8_VK_REQUIRE(RecordVulkanReconstruction(
            Functions(), tooFewDescriptors).status
            == VulkanReconstructionRecordStatus::InvalidRequest);

        VulkanReconstructionRecordRequest missingPipeline = request;
        missingPipeline.pipelines.passes[
            static_cast<std::size_t>(
                VulkanReconstructionPassKind::TemporalAccumulation)] =
                    VK_NULL_HANDLE;
        L8_VK_REQUIRE(RecordVulkanReconstruction(
            Functions(), missingPipeline).status
            == VulkanReconstructionRecordStatus::MissingPipeline);

        VulkanReconstructionFunctions incomplete = Functions();
        incomplete.cmdDispatch = nullptr;
        L8_VK_REQUIRE(RecordVulkanReconstruction(
            incomplete, request).status
            == VulkanReconstructionRecordStatus::MissingFunction);
    }
}
