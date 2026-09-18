#include "restir/VulkanProductionRuntime.hpp"

#include "contracts/DescriptorRegistryV3.hpp"
#include "contracts/ReconstructionAbiV2.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace RenderingEngine::Restir
{
    namespace
    {
        using Renderers::ReSTIRRuntimeStatusCode;

        [[nodiscard]] bool CheckedAdd(
            const VkDeviceSize value,
            VkDeviceSize& total) noexcept
        {
            if (value > std::numeric_limits<VkDeviceSize>::max() - total)
            {
                return false;
            }
            total += value;
            return true;
        }

        [[nodiscard]] bool CheckedMultiply(
            const VkDeviceSize left,
            const VkDeviceSize right,
            VkDeviceSize& result) noexcept
        {
            if (left != 0u
                && right > std::numeric_limits<VkDeviceSize>::max() / left)
            {
                return false;
            }
            result = left * right;
            return true;
        }

        [[nodiscard]] bool IsSupportedLightCount(
            const std::uint32_t lightCount) noexcept
        {
            return lightCount != 0u;
        }

        [[nodiscard]] bool IsFiniteBufferView(
            const VkDescriptorBufferInfo& view) noexcept
        {
            return view.buffer != VK_NULL_HANDLE && view.range != 0u
                && view.range != VK_WHOLE_SIZE
                && view.offset <= std::numeric_limits<VkDeviceSize>::max()
                    - view.range;
        }

        [[nodiscard]] bool BufferViewCovers(
            const VkDescriptorBufferInfo& view,
            const VkDeviceSize minimum) noexcept
        {
            return IsFiniteBufferView(view) && view.range >= minimum;
        }

        void AddIssue(
            VulkanReSTIRProductionReport& report,
            const VulkanProductionIssue issue,
            const ReSTIRRuntimeStatusCode code,
            const std::string_view reason)
        {
            report.issues = report.issues | issue;
            if (report.status.code == ReSTIRRuntimeStatusCode::Ready)
            {
                report.status.code = code;
                report.status.reason = std::string(reason);
            }
        }

        [[nodiscard]] bool RequiresPipeline(
            const Renderers::ReSTIRPass pass) noexcept
        {
            return pass != Renderers::ReSTIRPass::Reconstruction;
        }
    }

    VulkanReSTIRResourcePlan BuildVulkanReSTIRResourcePlan(
        const Renderers::ReSTIRFramePlan& framePlan) noexcept
    {
        using namespace Contracts::AbiV3;

        VulkanReSTIRResourcePlan result{};
        if (!framePlan.IsReady() || framePlan.framesInFlight < 2u
            || framePlan.framesInFlight > 4u || framePlan.identity.width == 0u
            || framePlan.identity.height == 0u)
        {
            result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                "resource planning requires a ready ABI-v3 frame plan" };
            return result;
        }
        if (!IsSupportedLightCount(framePlan.currentLightCount))
        {
            result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                "production ReSTIR resource planning requires a non-zero canonical light table" };
            return result;
        }

        for (std::uint32_t binding = 0u;
            binding < Renderers::kVulkanReSTIRSet5BindingCount; ++binding)
        {
            if (binding == RestirBinding::DebugImage)
            {
                continue;
            }
            const std::uint64_t minimum =
                framePlan.minimumSet5BufferRanges[binding];
            if (minimum > std::numeric_limits<VkDeviceSize>::max())
            {
                result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                    "set-5 range exceeds VkDeviceSize" };
                return result;
            }
            result.descriptorBufferBytes[binding] = std::max(
                static_cast<VkDeviceSize>(minimum),
                kReSTIRDummyDescriptorBytes);
        }

        constexpr std::array<std::uint32_t, 10u> distinctBindings = {
            RestirBinding::VisibilityResults,
            RestirBinding::ReferenceVisibility,
            RestirBinding::CurrentToPreviousLightIndex,
            RestirBinding::PreviousToCurrentLightIndex,
            RestirBinding::ShadowRayQueue,
            RestirBinding::DirectDiffuse,
            RestirBinding::DirectSpecular,
            RestirBinding::InitialReservoir,
            RestirBinding::TemporalReservoir,
            RestirBinding::SpatialReservoir};
        for (const std::uint32_t binding : distinctBindings)
        {
            result.requiresDistinctAllocation[binding] = true;
        }
        result.requiresDistinctAllocation[RestirBinding::PublishedReservoir] = true;
        result.requiresDistinctAllocation[
            RestirBinding::PreviousPublishedReservoir] = true;

        result.debugImageExtent = {
            framePlan.identity.width, framePlan.identity.height};
        result.historyRingSlotCount = 2u * framePlan.framesInFlight;
        result.historyRingSlotBytes = result.descriptorBufferBytes[
            RestirBinding::PublishedReservoir];
        if (!CheckedMultiply(
                result.historyRingSlotBytes,
                result.historyRingSlotCount,
                result.historyRingBytes))
        {
            result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                "2N ReSTIR history ring size overflow" };
            return result;
        }

        for (std::uint32_t binding = 0u;
            binding < Renderers::kVulkanReSTIRSet5BindingCount; ++binding)
        {
            if (binding == RestirBinding::DebugImage
                || binding == RestirBinding::PublishedReservoir
                || (binding == RestirBinding::PreviousPublishedReservoir
                    && framePlan.historyDecision
                        == Renderers::ReSTIRHistoryDecision::Reuse))
            {
                continue;
            }
            if (!CheckedAdd(result.descriptorBufferBytes[binding],
                    result.perFrameTransientBytes))
            {
                result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                    "per-frame ReSTIR resource size overflow" };
                return result;
            }
        }

        VkDeviceSize pixelCount = 0u;
        VkDeviceSize debugBytesPerFrame = 0u;
        VkDeviceSize allTransientBytes = 0u;
        if (!CheckedMultiply(framePlan.identity.width, framePlan.identity.height,
                pixelCount)
            || !CheckedMultiply(pixelCount, 16u, debugBytesPerFrame)
            || !CheckedMultiply(debugBytesPerFrame, framePlan.framesInFlight,
                result.debugImageTexelBytes)
            || !CheckedMultiply(result.perFrameTransientBytes,
                framePlan.framesInFlight, allTransientBytes))
        {
            result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR image or per-frame allocation size overflow" };
            return result;
        }
        if (!CheckedAdd(allTransientBytes, result.minimumOwnerPayloadBytes)
            || !CheckedAdd(result.historyRingBytes,
                result.minimumOwnerPayloadBytes)
            || !CheckedAdd(result.debugImageTexelBytes,
                result.minimumOwnerPayloadBytes))
        {
            result.status = { ReSTIRRuntimeStatusCode::InvalidRequest,
                "total ReSTIR owner allocation size overflow" };
            return result;
        }
        return result;
    }

    VulkanReSTIRProductionReport ValidateVulkanReSTIRProductionAttachment(
        const Renderers::ReSTIRFramePlan& framePlan,
        const Renderers::VulkanReSTIRFrameContext& frameContext,
        const VulkanReSTIRProductionEvidence& evidence,
        const Rt::Gpu::IGpuTraversalBackend& traversalBackend) noexcept
    {
        using namespace Contracts::AbiV3;

        VulkanReSTIRProductionReport report{};
        report.resources = BuildVulkanReSTIRResourcePlan(framePlan);
        if (!framePlan.IsReady() || !report.resources.IsReady())
        {
            AddIssue(report,
                !IsSupportedLightCount(framePlan.currentLightCount)
                    ? VulkanProductionIssue::UnsupportedLightTier
                    : VulkanProductionIssue::InvalidFramePlan,
                ReSTIRRuntimeStatusCode::InvalidRequest,
                report.resources.status.reason.empty()
                    ? "production attachment requires a ready ABI-v3 frame plan"
                    : report.resources.status.reason);
            return report;
        }

        if (frameContext.commandBuffer == VK_NULL_HANDLE)
        {
            AddIssue(report, VulkanProductionIssue::MissingCommandBuffer,
                ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "production frame has no Vulkan command buffer");
        }
        for (std::size_t set = 0u;
            set < Renderers::kVulkanReSTIRSetCount; ++set)
        {
            if (set != static_cast<std::size_t>(DescriptorSet::Restir)
                && frameContext.descriptorSets[set] == VK_NULL_HANDLE)
            {
                AddIssue(report,
                    VulkanProductionIssue::MissingCanonicalDescriptorSet,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "production frame is missing a canonical set 0-4 or set 6 descriptor");
            }
        }

        const Rt::Gpu::GpuTraversalBackendDescriptor traversal =
            traversalBackend.Descriptor();
        if (!traversal.supportsAny
            || frameContext.referenceTraversalSet == VK_NULL_HANDLE
            || frameContext.winnerTraversalSet == VK_NULL_HANDLE
            || frameContext.referenceTraversalSet
                == frameContext.winnerTraversalSet)
        {
            AddIssue(report, VulkanProductionIssue::InvalidTraversalAttachment,
                ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "production attachment requires TraceAny and distinct reference/winner traversal sets");
        }

        VkDeviceSize primarySurfaceBytes = 0u;
        VkDeviceSize motionBytes = 0u;
        VkDeviceSize lightBytes = 0u;
        const bool sizeOk = CheckedMultiply(framePlan.footprint.pixelCount,
                sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2), primarySurfaceBytes)
            && CheckedMultiply(framePlan.footprint.pixelCount,
                sizeof(Contracts::AbiV2::GpuGBufferRecordV2), motionBytes)
            && CheckedMultiply(framePlan.currentLightCount,
                kPbrLightGpuL6Stride, lightBytes);
        if (!sizeOk)
        {
            AddIssue(report, VulkanProductionIssue::ResourceSizeOverflow,
                ReSTIRRuntimeStatusCode::InvalidRequest,
                "external production resource size overflow");
        }
        else
        {
            if (!BufferViewCovers(evidence.primarySurfaceV2,
                    primarySurfaceBytes))
            {
                AddIssue(report,
                    VulkanProductionIssue::MissingPrimarySurfaceV2,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "set 3 binding 24 does not publish a complete GpuPrimarySurfaceV2 frame");
            }
            if (!BufferViewCovers(evidence.motionVectorsV2, motionBytes))
            {
                AddIssue(report,
                    VulkanProductionIssue::MissingMotionVectorsV2,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "set 4 binding 0 does not publish a complete ABI-v2 GBuffer motion frame");
            }
            if (!BufferViewCovers(evidence.pbrLightTableL6, lightBytes))
            {
                AddIssue(report,
                    VulkanProductionIssue::MissingPbrLightTableL6,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "set 0 binding 2 does not publish the complete 96-byte L6 light table");
            }
        }
        if (evidence.pbrLightCount != framePlan.currentLightCount)
        {
            AddIssue(report, VulkanProductionIssue::LightCountMismatch,
                ReSTIRRuntimeStatusCode::InvalidRequest,
                "published L6 light count does not match the ReSTIR tier");
        }

        if (evidence.reconstructionOutput.imageView == VK_NULL_HANDLE
            || evidence.reconstructionOutput.imageLayout
                != VK_IMAGE_LAYOUT_GENERAL
            || evidence.reconstructionOutputExtent.width
                < framePlan.identity.width
            || evidence.reconstructionOutputExtent.height
                < framePlan.identity.height)
        {
            AddIssue(report,
                VulkanProductionIssue::MissingReconstructionOutput,
                ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "set 4 binding 13 has no frame-sized reconstruction output in GENERAL layout");
        }
        if (frameContext.reconstructionRecorder == nullptr)
        {
            AddIssue(report,
                VulkanProductionIssue::MissingReconstructionRecorder,
                ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "Wave 3 reconstruction recorder is not attached");
        }

        for (std::uint32_t binding = 0u;
            binding < Renderers::kVulkanReSTIRSet5BindingCount; ++binding)
        {
            if (!frameContext.resources.valid[binding])
            {
                AddIssue(report, VulkanProductionIssue::MissingSet5Resource,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "an ABI-v3 set-5 binding is not explicitly supplied");
                continue;
            }
            if (binding == RestirBinding::DebugImage)
            {
                if (frameContext.resources.debugImage.imageView
                        == VK_NULL_HANDLE
                    || frameContext.resources.debugImage.imageLayout
                        != VK_IMAGE_LAYOUT_GENERAL
                    || !Renderers::VulkanReSTIRRecorder::DebugImageMetadataValid(
                        frameContext.resources.debugImageFormat,
                        frameContext.resources.debugImageExtent)
                    || (framePlan.writeDebug
                        && !Renderers::VulkanReSTIRRecorder::DebugImageCoversPlan(
                            frameContext.resources.debugImageExtent,
                            framePlan.identity.width,
                            framePlan.identity.height)))
                {
                    AddIssue(report, VulkanProductionIssue::InvalidDebugImage,
                        ReSTIRRuntimeStatusCode::ProviderUnavailable,
                        "set 5 binding 16 is not a frame-sized RGBA32F storage image");
                }
                continue;
            }
            if (!BufferViewCovers(frameContext.resources.buffers[binding],
                    report.resources.descriptorBufferBytes[binding]))
            {
                AddIssue(report, VulkanProductionIssue::Set5RangeTooSmall,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "an ABI-v3 set-5 buffer is missing or smaller than its finite descriptor plan");
            }
        }

        const VkDescriptorBufferInfo& shadowRays =
            frameContext.resources.buffers[RestirBinding::ShadowRayQueue];
        if (!Renderers::VulkanReSTIRRecorder::SameBufferView(
                frameContext.referenceTraversalRays, shadowRays)
            || !Renderers::VulkanReSTIRRecorder::SameBufferView(
                frameContext.winnerTraversalRays, shadowRays)
            || !Renderers::VulkanReSTIRRecorder::SameBufferView(
                frameContext.referenceTraversalHits,
                frameContext.resources.buffers[
                    RestirBinding::ReferenceVisibility])
            || !Renderers::VulkanReSTIRRecorder::SameBufferView(
                frameContext.winnerTraversalHits,
                frameContext.resources.buffers[
                    RestirBinding::VisibilityResults])
            || !Renderers::VulkanReSTIRRecorder::TraceAnyViewsNonOverlapping(
                shadowRays,
                frameContext.referenceTraversalHits,
                frameContext.winnerTraversalHits))
        {
            AddIssue(report, VulkanProductionIssue::InvalidTraversalAttachment,
                ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "TraceAny set-2 ray/hit views do not exactly match distinct set-5 queues");
        }

        if (frameContext.sceneFingerprint == 0u
            || frameContext.sceneFingerprint != evidence.sceneFingerprint
            || frameContext.sceneGeneration != framePlan.identity.sceneGeneration
            || evidence.sceneGeneration != framePlan.identity.sceneGeneration
            || evidence.resourceGeneration
                != framePlan.identity.resourceGeneration
            || evidence.lightGeneration != framePlan.identity.lightGeneration)
        {
            AddIssue(report, VulkanProductionIssue::InvalidFrameIdentity,
                ReSTIRRuntimeStatusCode::InvalidRequest,
                "scene/resource/light generation evidence does not match the immutable frame plan");
        }
        if (frameContext.frameSlot
                != framePlan.identity.frameIndex % framePlan.framesInFlight
            || frameContext.historyReadPhysicalIndex
                != framePlan.historyReadPhysicalIndex
            || frameContext.historyWritePhysicalIndex
                != framePlan.historyWritePhysicalIndex)
        {
            AddIssue(report, VulkanProductionIssue::InvalidHistorySlot,
                ReSTIRRuntimeStatusCode::InvalidRequest,
                "frame slot and 2N history views do not match the frame plan");
        }

        if (evidence.pipelineLayout == VK_NULL_HANDLE)
        {
            AddIssue(report, VulkanProductionIssue::PipelineLayoutMismatch,
                ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "ABI-v3 set 0-6 pipeline layout is not supplied");
        }
        for (const Renderers::ReSTIRScheduledPass& scheduled : framePlan.passes)
        {
            const std::size_t index = static_cast<std::size_t>(scheduled.pass);
            if (RequiresPipeline(scheduled.pass)
                && (index >= evidence.pipelines.size()
                    || evidence.pipelines[index] == VK_NULL_HANDLE))
            {
                AddIssue(report, VulkanProductionIssue::MissingComputePipeline,
                    ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "a scheduled ABI-v3 compute pass has no real Vulkan pipeline");
            }
        }
        return report;
    }

    std::string_view VulkanReSTIRShaderFileName(
        const Renderers::ReSTIRPass pass) noexcept
    {
        using Renderers::ReSTIRPass;
        switch (pass)
        {
        case ReSTIRPass::ClearStatistics: return "RestirClearV3.comp.spv";
        case ReSTIRPass::GenerateCandidates:
            return "pbr_restir_candidate_export_v3.spv";
        case ReSTIRPass::InitialReservoir: return "RestirInitialV3.comp.spv";
        case ReSTIRPass::TemporalReuse: return "RestirTemporalV3.comp.spv";
        case ReSTIRPass::SpatialReuse: return "RestirSpatialV3.comp.spv";
        case ReSTIRPass::PrepareReferenceVisibility:
            return "RestirReferenceVisibilityPrepareV3.comp.spv";
        case ReSTIRPass::ResolveReferenceVisibility:
            return "RestirReferenceResolveV3.comp.spv";
        case ReSTIRPass::PrepareWinnerVisibility:
            return "RestirWinnerVisibilityPrepareV3.comp.spv";
        case ReSTIRPass::ResolveWinnerVisibility:
            return "RestirWinnerResolveV3.comp.spv";
        case ReSTIRPass::PublishSplitDirectSignal:
            return "RestirPublishDirectV3.comp.spv";
        case ReSTIRPass::Reconstruction: return {};
        case ReSTIRPass::PublishHistory:
            return "RestirPublishHistoryV3.comp.spv";
        case ReSTIRPass::WriteDebug: return "RestirDebugV3.comp.spv";
        }
        return {};
    }
}
