#include "reconstruction/VulkanReconstructionRecorder.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace rendering::reconstruction
{
    namespace
    {
        inline constexpr std::uint64_t MotionInputStride = 32u;
        inline constexpr std::uint64_t WorkingSignalStride = 32u;
        inline constexpr std::uint64_t HistoryStride = 80u;
        inline constexpr std::uint64_t TemporalDebugStride = 8u;
        inline constexpr std::uint64_t VarianceStride = sizeof(float);
        inline constexpr std::uint64_t OutputPixelStride = 16u;

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

        [[nodiscard]] bool UsesTemporalHistory(
            const std::span<const PreparedVulkanReconstructionPass> passes) noexcept
        {
            for (const PreparedVulkanReconstructionPass& pass : passes)
            {
                if (pass.kind
                    == VulkanReconstructionPassKind::TemporalAccumulation)
                {
                    return true;
                }
            }
            return false;
        }

        void Fail(
            VulkanReconstructionRecordResult& result,
            const VulkanReconstructionRecordStatus status,
            std::string reason) noexcept
        {
            result.status = status;
            result.reason = std::move(reason);
        }
    }

    VulkanHistoryResourceIndices ResolveVulkanHistoryResources(
        const std::uint64_t frameIndex,
        const std::uint32_t framesInFlight)
    {
        if (framesInFlight == 0u || framesInFlight > 8u)
        {
            throw std::invalid_argument(
                "Vulkan reconstruction history requires 1..8 frames in flight.");
        }
        const auto physicalIndex = [framesInFlight](
            const std::uint64_t frame) noexcept
        {
            const std::uint64_t generation =
                (frame / framesInFlight) & 1ull;
            const std::uint64_t flightSlot = frame % framesInFlight;
            return static_cast<std::uint32_t>(
                generation * framesInFlight + flightSlot);
        };

        VulkanHistoryResourceIndices result{};
        result.writePhysicalIndex = physicalIndex(frameIndex);
        if (frameIndex != 0u)
        {
            result.readPhysicalIndex = physicalIndex(frameIndex - 1u);
            result.hasPrevious = true;
        }
        return result;
    }

    VulkanReconstructionResourceRequirements
        MakeVulkanReconstructionResourceRequirements(
            const Extent2D extent,
            const std::uint32_t framesInFlight)
    {
        if (!extent.IsValid() || framesInFlight == 0u || framesInFlight > 8u)
        {
            throw std::invalid_argument(
                "Vulkan reconstruction resources require a valid extent and 1..8 frames in flight.");
        }
        const std::uint64_t pixelCount = CheckedMultiply(
            extent.width, extent.height,
            "Vulkan reconstruction pixel count overflowed.");
        VulkanReconstructionResourceRequirements result{};
        result.pixelCount = pixelCount;
        result.framesInFlight = framesInFlight;
        result.historyResourceCount = framesInFlight * 2u;
        result.gBufferBytes = CheckedMultiply(
            pixelCount,
            sizeof(RenderingEngine::Contracts::AbiV2::GpuGBufferRecordV2),
            "Vulkan reconstruction GBuffer bytes overflowed.");
        result.motionInputBytes = CheckedMultiply(
            pixelCount, MotionInputStride,
            "Vulkan reconstruction motion-input bytes overflowed.");
        result.rawSignalBytes = CheckedMultiply(
            pixelCount,
            sizeof(RenderingEngine::Contracts::AbiV2::GpuReconstructionSignalV2),
            "Vulkan reconstruction raw-signal bytes overflowed.");
        result.workingSignalBytesEach = CheckedMultiply(
            pixelCount, WorkingSignalStride,
            "Vulkan reconstruction working-signal bytes overflowed.");
        result.historyBytesEach = CheckedMultiply(
            pixelCount, HistoryStride,
            "Vulkan reconstruction history bytes overflowed.");
        result.temporalDebugBytes = CheckedMultiply(
            pixelCount, TemporalDebugStride,
            "Vulkan reconstruction debug bytes overflowed.");
        result.varianceBytesEach = CheckedMultiply(
            pixelCount, VarianceStride,
            "Vulkan reconstruction variance bytes overflowed.");
        result.outputImageBytes = CheckedMultiply(
            pixelCount, OutputPixelStride,
            "Vulkan reconstruction output image bytes overflowed.");
        return result;
    }

    std::vector<PreparedVulkanReconstructionPass>
        BuildVulkanReconstructionPlan(
            const ReconstructionOutput output,
            const std::uint32_t atrousIterationCount,
            const std::uint32_t firstTimestampQuery)
    {
        if (output != ReconstructionOutput::Raw
            && output != ReconstructionOutput::Temporal
            && output != ReconstructionOutput::ATrous
            && output != ReconstructionOutput::Svgf)
        {
            throw std::invalid_argument(
                "Vulkan reconstruction production plan supports Raw, Temporal, A-Trous, or SVGF output.");
        }
        if ((output == ReconstructionOutput::ATrous
                || output == ReconstructionOutput::Svgf)
            && (atrousIterationCount == 0u || atrousIterationCount > 8u))
        {
            throw std::invalid_argument(
                "Vulkan reconstruction A-Trous requires 1..8 iterations.");
        }

        std::vector<PreparedVulkanReconstructionPass> result;
        std::uint32_t nextQuery = firstTimestampQuery;
        const auto add = [&result, &nextQuery](
            const VulkanReconstructionPassKind kind,
            const std::uint32_t iteration = 0u)
        {
            if (nextQuery > std::numeric_limits<std::uint32_t>::max() - 2u)
            {
                throw std::overflow_error(
                    "Vulkan reconstruction timestamp query range overflowed.");
            }
            result.push_back({kind, iteration, nextQuery, nextQuery + 1u});
            nextQuery += 2u;
        };

        if (output == ReconstructionOutput::Raw)
        {
            add(VulkanReconstructionPassKind::Compose);
            return result;
        }

        if (output != ReconstructionOutput::ATrous)
        {
            add(VulkanReconstructionPassKind::MotionVectors);
        }
        add(VulkanReconstructionPassKind::PrepareSignal);
        if (output != ReconstructionOutput::ATrous)
        {
            add(VulkanReconstructionPassKind::TemporalAccumulation);
        }
        if (output == ReconstructionOutput::ATrous || output == ReconstructionOutput::Svgf)
        {
            add(VulkanReconstructionPassKind::VarianceBootstrap);
            for (std::uint32_t iteration = 0u;
                iteration < atrousIterationCount; ++iteration)
            {
                add(VulkanReconstructionPassKind::AtrousIteration, iteration);
            }
        }
        add(VulkanReconstructionPassKind::Compose);
        return result;
    }

    bool ValidateVulkanReconstructionPlan(
        const std::span<const PreparedVulkanReconstructionPass> passes) noexcept
    {
        if (passes.empty()
            || passes.back().kind != VulkanReconstructionPassKind::Compose)
        {
            return false;
        }
        if (passes.size() == 1u)
        {
            return passes.front().kind == VulkanReconstructionPassKind::Compose
                && passes.front().beginTimestampQuery
                    != std::numeric_limits<std::uint32_t>::max()
                && passes.front().endTimestampQuery
                    == passes.front().beginTimestampQuery + 1u;
        }
        if (passes.size() < 2u)
        {
            return false;
        }

        const bool spatialOnly =
            passes[0].kind == VulkanReconstructionPassKind::PrepareSignal;
        const std::size_t prefixCount = spatialOnly ? 1u : 3u;
        if ((!spatialOnly
                && (passes.size() < 4u
                    || passes[0].kind != VulkanReconstructionPassKind::MotionVectors
                    || passes[1].kind != VulkanReconstructionPassKind::PrepareSignal
                    || passes[2].kind
                        != VulkanReconstructionPassKind::TemporalAccumulation))
            || (spatialOnly && passes.size() < 4u))
        {
            return false;
        }

        bool sawVariance = false;
        std::uint32_t expectedAtrousIteration = 0u;
        for (std::size_t index = 0u; index < passes.size(); ++index)
        {
            const PreparedVulkanReconstructionPass& pass = passes[index];
            if (pass.beginTimestampQuery
                    == std::numeric_limits<std::uint32_t>::max()
                || pass.endTimestampQuery != pass.beginTimestampQuery + 1u
                || (index != 0u
                    && pass.beginTimestampQuery
                        != passes[index - 1u].endTimestampQuery + 1u))
            {
                return false;
            }
            if (index >= prefixCount && index + 1u < passes.size())
            {
                if (!sawVariance)
                {
                    if (pass.kind
                        != VulkanReconstructionPassKind::VarianceBootstrap)
                    {
                        return false;
                    }
                    sawVariance = true;
                }
                else
                {
                    if (pass.kind
                            != VulkanReconstructionPassKind::AtrousIteration
                        || pass.iteration != expectedAtrousIteration++)
                    {
                        return false;
                    }
                }
            }
        }
        return !sawVariance || expectedAtrousIteration != 0u;
    }

    bool VulkanReconstructionFunctions::Complete(
        const bool timestampsRequired) const noexcept
    {
        return cmdPipelineBarrier2 != nullptr
            && (!timestampsRequired || cmdWriteTimestamp2 != nullptr)
            && cmdBindPipeline != nullptr
            && cmdBindDescriptorSets != nullptr
            && cmdDispatch != nullptr;
    }

    VkPipeline VulkanReconstructionPipelines::For(
        const VulkanReconstructionPassKind kind) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        return index < passes.size() ? passes[index] : VK_NULL_HANDLE;
    }

    VulkanReconstructionRecordResult RecordVulkanReconstruction(
        const VulkanReconstructionFunctions& functions,
        const VulkanReconstructionRecordRequest& request) noexcept
    {
        VulkanReconstructionRecordResult result{};
        if (request.commandBuffer == VK_NULL_HANDLE || !request.extent.IsValid()
            || request.pipelines.layout == VK_NULL_HANDLE
            || !ValidateVulkanReconstructionPlan(request.passes)
            || request.descriptorSets.size() != request.passes.size())
        {
            Fail(result, VulkanReconstructionRecordStatus::InvalidRequest,
                "Vulkan reconstruction record request or pass plan is invalid.");
            return result;
        }
        for (const VkDescriptorSet descriptorSet : request.descriptorSets)
        {
            if (descriptorSet == VK_NULL_HANDLE)
            {
                Fail(result, VulkanReconstructionRecordStatus::InvalidRequest,
                    "Vulkan reconstruction requires one valid set-4 descriptor per pass.");
                return result;
            }
        }
        if (request.writeTimestamps
            && request.timestampQueryPool == VK_NULL_HANDLE)
        {
            Fail(result, VulkanReconstructionRecordStatus::InvalidRequest,
                "Vulkan reconstruction timestamp recording requires a query pool.");
            return result;
        }
        if (!functions.Complete(request.writeTimestamps))
        {
            Fail(result, VulkanReconstructionRecordStatus::MissingFunction,
                "Vulkan reconstruction function table is incomplete.");
            return result;
        }

        const std::uint32_t groupCountX =
            request.extent.width / 8u
            + (request.extent.width % 8u == 0u ? 0u : 1u);
        const std::uint32_t groupCountY =
            request.extent.height / 8u
            + (request.extent.height % 8u == 0u ? 0u : 1u);

        for (std::size_t index = 0u; index < request.passes.size(); ++index)
        {
            const PreparedVulkanReconstructionPass& pass = request.passes[index];
            const VkPipeline pipeline = request.pipelines.For(pass.kind);
            if (pipeline == VK_NULL_HANDLE)
            {
                Fail(result, VulkanReconstructionRecordStatus::MissingPipeline,
                    "Vulkan reconstruction pass has no compute pipeline.");
                return result;
            }

            const bool initialBarrier = index == 0u
                && request.insertInitialProducerBarrier;
            if (initialBarrier || index != 0u)
            {
                const VkMemoryBarrier2 memoryBarrier{
                    VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                    nullptr,
                    initialBarrier
                        ? request.initialSourceStage
                        : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    initialBarrier
                        ? request.initialSourceAccess
                        : VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT
                        | VK_ACCESS_2_SHADER_WRITE_BIT};
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
                functions.cmdWriteTimestamp2(
                    request.commandBuffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    request.timestampQueryPool,
                    pass.beginTimestampQuery);
                ++result.timestampWriteCount;
            }
            functions.cmdBindPipeline(
                request.commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline);
            const VkDescriptorSet descriptorSet = request.descriptorSets[index];
            functions.cmdBindDescriptorSets(
                request.commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                request.pipelines.layout,
                4u,
                1u,
                &descriptorSet,
                0u,
                nullptr);
            functions.cmdDispatch(
                request.commandBuffer, groupCountX, groupCountY, 1u);
            if (request.writeTimestamps)
            {
                functions.cmdWriteTimestamp2(
                    request.commandBuffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    request.timestampQueryPool,
                    pass.endTimestampQuery);
                ++result.timestampWriteCount;
            }
            ++result.recordedPassCount;
        }

        result.status = VulkanReconstructionRecordStatus::Succeeded;
        result.historyPrepared = UsesTemporalHistory(request.passes);
        result.reason.clear();
        return result;
    }
}
