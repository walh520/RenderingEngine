#include "renderers/VulkanReSTIRRecorder.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace RenderingEngine::Renderers
{
    namespace
    {
        [[nodiscard]] ReSTIRRuntimeStatus MakeStatus(
            const ReSTIRRuntimeStatusCode code,
            std::string reason)
        {
            return { code, std::move(reason) };
        }

        [[nodiscard]] std::string VulkanFailure(
            const char* operation,
            const VkResult result)
        {
            return std::string(operation) + " failed with VkResult="
                + std::to_string(static_cast<int>(result));
        }

        [[nodiscard]] bool IsEmptyHandle(const VkDescriptorSet handle) noexcept
        {
            return handle == VK_NULL_HANDLE;
        }

        [[nodiscard]] bool IsEmptyHandle(const VkDescriptorSetLayout handle) noexcept
        {
            return handle == VK_NULL_HANDLE;
        }

        [[nodiscard]] bool IsEmptyHandle(const VkPipeline handle) noexcept
        {
            return handle == VK_NULL_HANDLE;
        }

        [[nodiscard]] bool SameFrameIdentity(
            const ReSTIRFrameIdentity& left,
            const ReSTIRFrameIdentity& right) noexcept
        {
            return left.frameIndex == right.frameIndex
                && left.configGeneration == right.configGeneration
                && left.sceneGeneration == right.sceneGeneration
                && left.resourceGeneration == right.resourceGeneration
                && left.lightGeneration == right.lightGeneration
                && left.width == right.width
                && left.height == right.height
                && left.shadowMethod == right.shadowMethod;
        }
    }

    VulkanReSTIRRecorder::~VulkanReSTIRRecorder()
    {
        // Destruction is the last-resort RAII path. A normal owner should call
        // Shutdown only after the device is no longer using this recorder.
        activeFrame_ = false;
        (void)Shutdown();
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::Fail(
        const ReSTIRRuntimeStatusCode code,
        std::string reason) const
    {
        return MakeStatus(code, std::move(reason));
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::Initialize(
        const VulkanReSTIRRecorderCreateInfo& createInfo)
    {
        if (initialized_ || device_ != VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "VulkanReSTIRRecorder is already initialized; call Shutdown first");
        }
        if (createInfo.device == VK_NULL_HANDLE
            || createInfo.descriptorPool == VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "Vulkan ReSTIR recorder requires a device and descriptor pool");
        }
        if (createInfo.framesInFlight < 2u || createInfo.framesInFlight > 4u)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "Vulkan ReSTIR recorder supports two through four frame slots");
        }

        device_ = createInfo.device;
        descriptorPool_ = createInfo.descriptorPool;
        framesInFlight_ = createInfo.framesInFlight;
        pipelines_ = createInfo.pipelines;
        destroyPipelinesOnShutdown_ = createInfo.destroyPipelinesOnShutdown;

        ReSTIRRuntimeStatus status = CreateSetLayout();
        if (!status)
        {
            (void)Shutdown();
            return status;
        }
        status = CreatePipelineLayout(createInfo);
        if (!status)
        {
            (void)Shutdown();
            return status;
        }
        status = AllocateDescriptorSets(createInfo.framesInFlight);
        if (!status)
        {
            (void)Shutdown();
            return status;
        }

        initialized_ = true;
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::SetPipelines(
        const std::array<VkPipeline, kVulkanReSTIRPassCount>& pipelines)
    {
        if (!initialized_ || activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR pipelines may be published only after initialization and outside a frame");
        }
        for (std::size_t index = 0u; index < pipelines.size(); ++index)
        {
            if (static_cast<ReSTIRPass>(index) != ReSTIRPass::Reconstruction
                && pipelines[index] == VK_NULL_HANDLE)
            {
                return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "ReSTIR production pipeline table is incomplete");
            }
        }
        pipelines_ = pipelines;
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::CreateSetLayout()
    {
        if (device_ == VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "cannot create the ReSTIR set layout without a device");
        }
        std::array<VkDescriptorSetLayoutBinding,
            kVulkanReSTIRSet5BindingCount> bindings{};
        for (std::uint32_t binding = 0u;
            binding < kVulkanReSTIRSet5BindingCount;
            ++binding)
        {
            VkDescriptorSetLayoutBinding& descriptor = bindings[binding];
            descriptor.binding = binding;
            descriptor.descriptorType = DescriptorTypeForBinding(binding);
            descriptor.descriptorCount = 1u;
            descriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        const VkDescriptorSetLayoutCreateInfo createInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            nullptr,
            0u,
            static_cast<std::uint32_t>(bindings.size()),
            bindings.data()};
        const VkResult result = vkCreateDescriptorSetLayout(
            device_, &createInfo, nullptr, &restirSetLayout_);
        if (result != VK_SUCCESS)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                VulkanFailure("vkCreateDescriptorSetLayout(ReSTIR set5)", result));
        }
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::CreatePipelineLayout(
        const VulkanReSTIRRecorderCreateInfo& createInfo)
    {
        if (createInfo.pipelineLayout != VK_NULL_HANDLE)
        {
            pipelineLayout_ = createInfo.pipelineLayout;
            ownsPipelineLayout_ = false;
            return {};
        }
        for (std::size_t index = 0u; index < kVulkanReSTIRSetCount; ++index)
        {
            if (index != static_cast<std::size_t>(Contracts::AbiV3::DescriptorSet::Restir)
                && IsEmptyHandle(createInfo.pipelineSetLayouts[index]))
            {
                return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "canonical set-0..4 and set-6 layouts are required for ReSTIR recording");
            }
        }
        std::array<VkDescriptorSetLayout, kVulkanReSTIRSetCount> layouts =
            createInfo.pipelineSetLayouts;
        layouts[static_cast<std::size_t>(Contracts::AbiV3::DescriptorSet::Restir)] =
            restirSetLayout_;

        const VkPipelineLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0u,
            static_cast<std::uint32_t>(layouts.size()),
            layouts.data(),
            0u,
            nullptr};
        const VkResult result = vkCreatePipelineLayout(
            device_, &layoutInfo, nullptr, &pipelineLayout_);
        if (result != VK_SUCCESS)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                VulkanFailure("vkCreatePipelineLayout(ReSTIR set0-6)", result));
        }
        ownsPipelineLayout_ = true;
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::AllocateDescriptorSets(
        const std::uint32_t framesInFlight)
    {
        if (descriptorPool_ == VK_NULL_HANDLE || restirSetLayout_ == VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "descriptor pool and ReSTIR set layout must exist before allocation");
        }
        std::array<VkDescriptorSetLayout, 4u> layouts{};
        std::fill_n(layouts.begin(), framesInFlight, restirSetLayout_);
        const VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            nullptr,
            descriptorPool_,
            framesInFlight,
            layouts.data()};
        const VkResult result = vkAllocateDescriptorSets(
            device_, &allocateInfo, descriptorSets_.data());
        if (result != VK_SUCCESS)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                VulkanFailure("vkAllocateDescriptorSets(ReSTIR frame slots)", result));
        }
        descriptorSetCount_ = framesInFlight;
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::ValidateFrameContext(
        const VulkanReSTIRFrameContext& context) const
    {
        if (!initialized_)
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "Vulkan ReSTIR recorder is not initialized");
        }
        if (context.commandBuffer == VK_NULL_HANDLE
            || context.referenceTraversalSet == VK_NULL_HANDLE
            || context.winnerTraversalSet == VK_NULL_HANDLE
            || context.reconstructionRecorder == nullptr)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "frame context requires a command buffer, separate reference/winner traversal sets, and the Wave 3 reconstruction recorder");
        }
        if (context.referenceTraversalSet == context.winnerTraversalSet)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "reference and winner traversal descriptor sets must be distinct");
        }
        for (std::size_t set = 0u; set < kVulkanReSTIRSetCount; ++set)
        {
            if (set != static_cast<std::size_t>(Contracts::AbiV3::DescriptorSet::Restir)
                && context.descriptorSets[set] == VK_NULL_HANDLE)
            {
                return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "production frame context is missing a canonical descriptor set");
            }
        }
        if (context.sceneFingerprint == 0u || context.sceneGeneration == 0u)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "scene fingerprint and generation must be non-zero");
        }
        const std::uint32_t physicalHistoryCount = 2u * framesInFlight_;
        if (context.frameSlot >= framesInFlight_
            || context.historyWritePhysicalIndex >= physicalHistoryCount)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "frame slot or history write slot is outside the configured N/2N counts");
        }
        if (context.historyReadPhysicalIndex != kVulkanReSTIRInvalidHistoryIndex
            && (context.historyReadPhysicalIndex >= physicalHistoryCount
                || context.historyReadPhysicalIndex == context.historyWritePhysicalIndex))
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "history read/write slots must be valid and non-aliasing");
        }

        for (std::uint32_t binding = 0u;
            binding < kVulkanReSTIRSet5BindingCount;
            ++binding)
        {
            if (!context.resources.valid[binding])
            {
                return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "all abi-v3 set-5 resource bindings must be explicitly supplied");
            }
            if (binding == Contracts::AbiV3::RestirBinding::DebugImage)
            {
                if (context.resources.debugImage.imageView == VK_NULL_HANDLE
                    || context.resources.debugImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL
                    || !DebugImageMetadataValid(
                        context.resources.debugImageFormat,
                        context.resources.debugImageExtent))
                {
                    return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                        "set-5 DebugImage requires a non-empty R32G32B32A32_SFLOAT storage-image view in GENERAL layout");
                }
            }
            else if (context.resources.buffers[binding].buffer == VK_NULL_HANDLE
                || context.resources.buffers[binding].range == 0u
                || context.resources.buffers[binding].range == VK_WHOLE_SIZE
                || context.resources.buffers[binding].offset
                    > std::numeric_limits<VkDeviceSize>::max()
                        - context.resources.buffers[binding].range)
            {
                return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "set-5 buffer binding requires a finite, non-overflowing explicit range");
            }
        }

        const VkDescriptorBufferInfo& shadowRays = context.resources.buffers[
            Contracts::AbiV3::RestirBinding::ShadowRayQueue];
        if (!SameBufferView(
                context.referenceTraversalHits,
                context.resources.buffers[
                    Contracts::AbiV3::RestirBinding::ReferenceVisibility])
            || !SameBufferView(
                context.winnerTraversalHits,
                context.resources.buffers[
                    Contracts::AbiV3::RestirBinding::VisibilityResults])
            || !SameBufferView(context.referenceTraversalRays, shadowRays)
            || !SameBufferView(context.winnerTraversalRays, shadowRays))
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "set-5 shadow-ray/visibility views must exactly match traversal set-2 ray inputs and hit outputs");
        }
        if (!TraceAnyViewsNonOverlapping(
                shadowRays,
                context.referenceTraversalHits,
                context.winnerTraversalHits))
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "shadow-ray input and reference/winner traversal hit outputs must not overlap");
        }

        const auto Overlaps = [&context](
            const std::uint32_t left,
            const std::uint32_t right) noexcept
        {
            return BufferViewsOverlap(
                context.resources.buffers[left], context.resources.buffers[right]);
        };
        if (Overlaps(
                Contracts::AbiV3::RestirBinding::CurrentToPreviousLightIndex,
                Contracts::AbiV3::RestirBinding::PreviousToCurrentLightIndex))
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "current-to-previous and previous-to-current light maps must not alias");
        }
        if (Overlaps(
                Contracts::AbiV3::RestirBinding::DirectDiffuse,
                Contracts::AbiV3::RestirBinding::DirectSpecular))
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "DirectDiffuse and DirectSpecular must be distinct AOV resources");
        }
        constexpr std::array<std::uint32_t, 5u> reservoirBindings = {
            Contracts::AbiV3::RestirBinding::InitialReservoir,
            Contracts::AbiV3::RestirBinding::TemporalReservoir,
            Contracts::AbiV3::RestirBinding::SpatialReservoir,
            Contracts::AbiV3::RestirBinding::PublishedReservoir,
            Contracts::AbiV3::RestirBinding::PreviousPublishedReservoir};
        for (std::size_t left = 0u; left < reservoirBindings.size(); ++left)
        {
            for (std::size_t right = left + 1u;
                right < reservoirBindings.size();
                ++right)
            {
                if (Overlaps(reservoirBindings[left], reservoirBindings[right]))
                {
                    return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                        "Initial/Temporal/Spatial/current-published/previous-published reservoir resources must not alias");
                }
            }
        }
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::ValidateResourceRanges(
        const ReSTIRFramePlan& plan) const
    {
        for (std::uint32_t binding = 0u;
            binding < kVulkanReSTIRSet5BindingCount;
            ++binding)
        {
            if (binding == Contracts::AbiV3::RestirBinding::DebugImage)
            {
                continue;
            }
            const std::uint64_t minimum =
                plan.minimumSet5BufferRanges[binding];
            if (frameContext_.resources.buffers[binding].range < minimum)
            {
                return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                    "set-5 binding " + std::to_string(binding)
                    + " is smaller than the validated abi-v3 frame-plan minimum");
            }
        }
        if (plan.writeDebug && !DebugImageCoversPlan(
                frameContext_.resources.debugImageExtent,
                plan.identity.width,
                plan.identity.height))
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "set-5 binding 16 debug image extent is smaller than the frame plan");
        }
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::UpdateDescriptorSet(
        const VulkanReSTIRFrameContext& context)
    {
        std::array<VkWriteDescriptorSet, kVulkanReSTIRSet5BindingCount> writes{};
        for (std::uint32_t binding = 0u;
            binding < kVulkanReSTIRSet5BindingCount;
            ++binding)
        {
            VkWriteDescriptorSet& write = writes[binding];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets_[context.frameSlot];
            write.dstBinding = binding;
            write.descriptorCount = 1u;
            write.descriptorType = DescriptorTypeForBinding(binding);
            if (binding == Contracts::AbiV3::RestirBinding::DebugImage)
            {
                write.pImageInfo = &context.resources.debugImage;
            }
            else
            {
                write.pBufferInfo = &context.resources.buffers[binding];
            }
        }
        vkUpdateDescriptorSets(
            device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::SetFrameContext(
        const VulkanReSTIRFrameContext& context)
    {
        if (activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "frame context cannot change while a ReSTIR command sequence is active");
        }
        if (ReSTIRRuntimeStatus status = ValidateFrameContext(context); !status)
        {
            return status;
        }
        if (ReSTIRRuntimeStatus status = UpdateDescriptorSet(context); !status)
        {
            return status;
        }
        frameContext_ = context;
        frameContextBound_ = true;
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::ValidatePassPipeline(
        const ReSTIRPass pass) const
    {
        const std::size_t index = PassIndex(pass);
        if (index >= pipelines_.size() || IsEmptyHandle(pipelines_[index]))
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "ReSTIR compute pipeline handle is missing for "
                + std::string(ToString(pass)));
        }
        if (pipelineLayout_ == VK_NULL_HANDLE || restirSetLayout_ == VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "ReSTIR pipeline and descriptor layouts are not available");
        }
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::BeginFrame(
        const ReSTIRFramePlan& plan)
    {
        if (!initialized_ || pipelineLayout_ == VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "Vulkan ReSTIR recorder is not initialized with a pipeline layout");
        }
        if (activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "a ReSTIR frame is already active");
        }
        if (!ValidateReSTIRFramePlan(plan))
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "refusing to begin an invalid ReSTIR frame plan");
        }
        if (!frameContextBound_)
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "SetFrameContext must attach production resources before BeginFrame");
        }
        if (plan.framesInFlight != framesInFlight_
            || frameContext_.frameSlot
                != plan.identity.frameIndex % framesInFlight_
            || frameContext_.sceneGeneration != plan.identity.sceneGeneration
            || frameContext_.historyWritePhysicalIndex != plan.historyWritePhysicalIndex
            || frameContext_.historyReadPhysicalIndex != plan.historyReadPhysicalIndex)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "frame context history/scene identity does not match the frame plan");
        }
        if (ReSTIRRuntimeStatus status = ValidateResourceRanges(plan); !status)
        {
            return status;
        }
        for (const ReSTIRScheduledPass& scheduled : plan.passes)
        {
            if (scheduled.recordsExternalReconstruction)
            {
                continue;
            }
            if (ReSTIRRuntimeStatus status =
                ValidatePassPipeline(scheduled.pass); !status)
            {
                return status;
            }
        }

        activePlan_ = plan;
        activeFrame_ = true;
        nextPassIndex_ = 0u;
        lastPass_ = ReSTIRPass::ClearStatistics;
        pendingBarrier_ = false;
        traceBarrierRecorded_ = false;
        awaitingTraversalBarrier_ = false;
        traceBatchBuilt_ = false;
        referenceTraceRecorded_ = false;
        winnerTraceRecorded_ = false;
        referenceResolved_ = false;
        winnerResolved_ = false;
        publishSignalRecorded_ = false;
        reconstructionRecorded_ = false;
        publishHistoryRecorded_ = false;
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::RecordCompute(
        const ReSTIRPass pass,
        const std::uint32_t dispatchGroupCountX,
        const std::uint32_t dispatchGroupCountY,
        const std::uint32_t dispatchGroupCountZ)
    {
        if (!activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "RecordCompute requires an active ReSTIR frame");
        }
        if (pass == ReSTIRPass::Reconstruction)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "external Wave 3 reconstruction must use RecordReconstruction");
        }
        if (pendingBarrier_ || awaitingTraversalBarrier_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "the previous ReSTIR pass has not completed its required barrier/trace sequence");
        }
        if (nextPassIndex_ >= activePlan_.passes.size())
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "ReSTIR pass sequence contains an unexpected extra pass");
        }
        const ReSTIRScheduledPass& scheduled = activePlan_.passes[nextPassIndex_];
        if (scheduled.pass != pass
            || scheduled.dispatchGroupCountX != dispatchGroupCountX
            || scheduled.dispatchGroupCountY != dispatchGroupCountY
            || scheduled.dispatchGroupCountZ != dispatchGroupCountZ)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "recorded ReSTIR dispatch does not match the validated frame plan");
        }
        if (ReSTIRRuntimeStatus status = ValidatePassPipeline(pass); !status)
        {
            return status;
        }
        if (pass == ReSTIRPass::PrepareReferenceVisibility && referenceTraceRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "reference trace-any may be prepared only once per frame");
        }
        if (pass == ReSTIRPass::PrepareWinnerVisibility && winnerTraceRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "winner trace-any may be prepared only once per frame");
        }
        if (pass == ReSTIRPass::ResolveReferenceVisibility && !referenceTraceRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "reference visibility resolve requires its separate trace-any batch");
        }
        if (pass == ReSTIRPass::ResolveWinnerVisibility && !winnerTraceRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "winner visibility resolve requires its separate trace-any batch");
        }
        if (pass == ReSTIRPass::PublishSplitDirectSignal && !winnerResolved_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "split direct AOV publication requires the resolved winner visibility");
        }
        if (pass == ReSTIRPass::PublishHistory && !reconstructionRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "history publication must follow completed Wave 3 reconstruction");
        }
        if (pass == ReSTIRPass::WriteDebug
            && !activePlan_.passes.empty()
            && !frameContext_.resources.valid[
                Contracts::AbiV3::RestirBinding::DebugImage])
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "debug pass requires the abi-v3 DebugImage binding");
        }
        if (dispatchGroupCountX == 0u
            || dispatchGroupCountY == 0u
            || dispatchGroupCountZ == 0u)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR dispatch dimensions must be non-zero");
        }

        vkCmdBindPipeline(
            frameContext_.commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelines_[PassIndex(pass)]);
        std::array<VkDescriptorSet, kVulkanReSTIRSetCount> descriptorSets =
            frameContext_.descriptorSets;
        descriptorSets[static_cast<std::size_t>(Contracts::AbiV3::DescriptorSet::Restir)] =
            descriptorSets_[frameContext_.frameSlot];
        vkCmdBindDescriptorSets(
            frameContext_.commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout_,
            0u,
            static_cast<std::uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0u,
            nullptr);
        vkCmdDispatch(
            frameContext_.commandBuffer,
            dispatchGroupCountX,
            dispatchGroupCountY,
            dispatchGroupCountZ);

        lastPass_ = pass;
        ++nextPassIndex_;
        if (IsTracePreparation(pass))
        {
            pendingBarrier_ = true;
            pendingBarrierKind_ = ReSTIRBarrier::ComputeToTraversal;
            traceBarrierRecorded_ = false;
            awaitingTraversalBarrier_ = true;
            traceBatchBuilt_ = false;
        }
        else if (RequiresFollowingComputeBarrier(
            pass, nextPassIndex_ < activePlan_.passes.size()))
        {
            pendingBarrier_ = true;
            pendingBarrierKind_ = ExpectedBarrierAfter(pass);
        }
        if (pass == ReSTIRPass::ResolveReferenceVisibility)
        {
            referenceResolved_ = true;
        }
        if (pass == ReSTIRPass::ResolveWinnerVisibility)
        {
            winnerResolved_ = true;
        }
        if (pass == ReSTIRPass::PublishSplitDirectSignal)
        {
            publishSignalRecorded_ = true;
        }
        if (pass == ReSTIRPass::PublishHistory)
        {
            publishHistoryRecorded_ = true;
        }
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::RecordMemoryBarrier(
        const ReSTIRBarrier barrier)
    {
        if (frameContext_.commandBuffer == VK_NULL_HANDLE)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "cannot record a Vulkan barrier without a command buffer");
        }
        VkMemoryBarrier2 memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        switch (barrier)
        {
        case ReSTIRBarrier::ComputeToCompute:
            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                | VK_ACCESS_2_SHADER_WRITE_BIT;
            break;
        case ReSTIRBarrier::ComputeToReconstruction:
            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        case ReSTIRBarrier::ReconstructionToCompute:
            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                | VK_ACCESS_2_SHADER_WRITE_BIT;
            break;
        case ReSTIRBarrier::ComputeToTraversal:
            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        case ReSTIRBarrier::TraversalToCompute:
            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                | VK_ACCESS_2_MEMORY_WRITE_BIT;
            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                | VK_ACCESS_2_SHADER_WRITE_BIT;
            break;
        }
        const VkDependencyInfo dependencyInfo{
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            nullptr,
            0u,
            1u,
            &memoryBarrier,
            0u,
            nullptr,
            0u,
            nullptr};
        vkCmdPipelineBarrier2(frameContext_.commandBuffer, &dependencyInfo);
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::RecordBarrier(
        const ReSTIRBarrier barrier)
    {
        if (!activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "RecordBarrier requires an active ReSTIR frame");
        }
        if (barrier == ReSTIRBarrier::TraversalToCompute)
        {
            if (!awaitingTraversalBarrier_ || !traceBatchBuilt_)
            {
                return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                    "traversal-to-compute barrier requires a built reference/winner batch");
            }
            if (ReSTIRRuntimeStatus status = RecordMemoryBarrier(barrier); !status)
            {
                return status;
            }
            awaitingTraversalBarrier_ = false;
            traceBatchBuilt_ = false;
            traceBarrierRecorded_ = false;
            return {};
        }
        if (!pendingBarrier_ || barrier != pendingBarrierKind_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "Vulkan barrier does not match the current ReSTIR pass dependency");
        }
        if (ReSTIRRuntimeStatus status = RecordMemoryBarrier(barrier); !status)
        {
            return status;
        }
        pendingBarrier_ = false;
        if (barrier == ReSTIRBarrier::ComputeToTraversal)
        {
            traceBarrierRecorded_ = true;
        }
        return {};
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::RecordReconstruction(
        const ReSTIRFramePlan& plan)
    {
        if (!activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "RecordReconstruction requires an active ReSTIR frame");
        }
        if (pendingBarrier_ || awaitingTraversalBarrier_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "split-direct writes must be made visible before reconstruction");
        }
        if (!SameFrameIdentity(plan.identity, activePlan_.identity)
            || nextPassIndex_ >= activePlan_.passes.size())
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "reconstruction plan identity does not match BeginFrame");
        }
        const ReSTIRScheduledPass& scheduled = activePlan_.passes[nextPassIndex_];
        if (!scheduled.recordsExternalReconstruction
            || scheduled.pass != ReSTIRPass::Reconstruction
            || !publishSignalRecorded_ || reconstructionRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "Wave 3 reconstruction must occur exactly once after split-direct publication");
        }
        if (frameContext_.reconstructionRecorder == nullptr)
        {
            return Fail(ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "Wave 3 reconstruction recorder is no longer attached");
        }

        std::array<VkDescriptorSet, kVulkanReSTIRSetCount> descriptorSets =
            frameContext_.descriptorSets;
        descriptorSets[static_cast<std::size_t>(Contracts::AbiV3::DescriptorSet::Restir)] =
            descriptorSets_[frameContext_.frameSlot];
        if (ReSTIRRuntimeStatus status =
            frameContext_.reconstructionRecorder->RecordReconstruction(
                plan, frameContext_.commandBuffer, descriptorSets); !status)
        {
            return status;
        }

        lastPass_ = ReSTIRPass::Reconstruction;
        ++nextPassIndex_;
        reconstructionRecorded_ = true;
        pendingBarrier_ = true;
        pendingBarrierKind_ = ReSTIRBarrier::ReconstructionToCompute;
        return {};
    }

    Rt::Gpu::GpuTraceBatch VulkanReSTIRRecorder::BuildTraceAnyBatch(
        const ReSTIRPass preparePass,
        const std::uint32_t maximumRayCount)
    {
        const auto invalidBatch = []()
        {
            Rt::Gpu::GpuTraceBatch invalid{};
            // Every legal trace pass has a strictly positive exact budget.
            // Zero is therefore an unambiguous fail-closed sentinel even when
            // maximumRayCount is UINT32_MAX.
            invalid.rayCount = 0u;
            return invalid;
        };
        if (!activeFrame_ || !traceBarrierRecorded_ || !awaitingTraversalBarrier_
            || !IsTracePreparation(preparePass)
            || lastPass_ != preparePass
            || maximumRayCount == 0u)
        {
            return invalidBatch();
        }
        const bool reference = preparePass == ReSTIRPass::PrepareReferenceVisibility;
        const bool winner = preparePass == ReSTIRPass::PrepareWinnerVisibility;
        if ((reference && referenceTraceRecorded_) || (winner && winnerTraceRecorded_))
        {
            return invalidBatch();
        }
        const std::uint64_t expectedRayCount = reference
            ? activePlan_.maximumReferenceVisibilityRays
            : activePlan_.maximumWinnerVisibilityRays;
        if (expectedRayCount != maximumRayCount
            || expectedRayCount > std::numeric_limits<std::uint32_t>::max())
        {
            return invalidBatch();
        }
        Rt::Gpu::GpuTraceBatch batch{};
        batch.commandBuffer = frameContext_.commandBuffer;
        batch.canonicalSceneSet = frameContext_.descriptorSets[
            static_cast<std::size_t>(Contracts::AbiV3::DescriptorSet::Scene)];
        batch.traversalSet = reference
            ? frameContext_.referenceTraversalSet
            : frameContext_.winnerTraversalSet;
        batch.sceneFingerprint = frameContext_.sceneFingerprint;
        batch.rayOffset = 0u;
        batch.hitOffset = 0u;
        batch.rayCount = maximumRayCount;
        batch.sceneGeneration = frameContext_.sceneGeneration;
        traceBatchBuilt_ = true;
        if (reference)
        {
            referenceTraceRecorded_ = true;
        }
        else
        {
            winnerTraceRecorded_ = true;
        }
        return batch;
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::EndFrame(
        const ReSTIRFramePlan& plan)
    {
        if (!activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "EndFrame requires an active ReSTIR frame");
        }
        if (!SameFrameIdentity(plan.identity, activePlan_.identity)
            || plan.historyReadPhysicalIndex != activePlan_.historyReadPhysicalIndex
            || plan.historyWritePhysicalIndex != activePlan_.historyWritePhysicalIndex)
        {
            return Fail(ReSTIRRuntimeStatusCode::InvalidRequest,
                "EndFrame plan identity does not match BeginFrame");
        }
        if (nextPassIndex_ != activePlan_.passes.size()
            || pendingBarrier_ || awaitingTraversalBarrier_ || traceBatchBuilt_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "ReSTIR frame ended before all planned passes and barriers were recorded");
        }
        if (!winnerTraceRecorded_ || !winnerResolved_
            || !publishSignalRecorded_ || !reconstructionRecorded_
            || !publishHistoryRecorded_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "winner visibility, split direct AOV, reconstruction, and history publication are mandatory");
        }
        const bool referenceExpected = activePlan_.maximumReferenceVisibilityRays != 0u;
        if (referenceExpected != referenceTraceRecorded_ || (referenceExpected && !referenceResolved_))
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "reference visibility batch/resolve does not match estimator mode");
        }
        ResetActiveFrameState();
        return {};
    }

    void VulkanReSTIRRecorder::AbortFrame() noexcept
    {
        ResetActiveFrameState();
    }

    void VulkanReSTIRRecorder::ResetActiveFrameState() noexcept
    {
        activePlan_ = {};
        activeFrame_ = false;
        nextPassIndex_ = 0u;
        lastPass_ = ReSTIRPass::ClearStatistics;
        pendingBarrier_ = false;
        pendingBarrierKind_ = ReSTIRBarrier::ComputeToCompute;
        traceBarrierRecorded_ = false;
        awaitingTraversalBarrier_ = false;
        traceBatchBuilt_ = false;
        referenceTraceRecorded_ = false;
        winnerTraceRecorded_ = false;
        referenceResolved_ = false;
        winnerResolved_ = false;
        publishSignalRecorded_ = false;
        reconstructionRecorded_ = false;
        publishHistoryRecorded_ = false;
    }

    ReSTIRRuntimeStatus VulkanReSTIRRecorder::Shutdown()
    {
        if (activeFrame_)
        {
            return Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                "cannot destroy ReSTIR Vulkan resources while a frame is active");
        }
        ReSTIRRuntimeStatus status{};
        if (device_ != VK_NULL_HANDLE && descriptorPool_ != VK_NULL_HANDLE
            && descriptorSetCount_ > 0u)
        {
            const VkResult result = vkFreeDescriptorSets(
                device_, descriptorPool_, descriptorSetCount_, descriptorSets_.data());
            if (result != VK_SUCCESS)
            {
                status = Fail(ReSTIRRuntimeStatusCode::RecordingFailed,
                    VulkanFailure("vkFreeDescriptorSets(ReSTIR frame slots)", result));
            }
        }
        if (destroyPipelinesOnShutdown_ && device_ != VK_NULL_HANDLE)
        {
            for (VkPipeline& pipeline : pipelines_)
            {
                if (pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
            }
        }
        if (ownsPipelineLayout_ && device_ != VK_NULL_HANDLE
            && pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE && restirSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, restirSetLayout_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        descriptorPool_ = VK_NULL_HANDLE;
        restirSetLayout_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        descriptorSets_.fill(VK_NULL_HANDLE);
        pipelines_.fill(VK_NULL_HANDLE);
        framesInFlight_ = 0u;
        descriptorSetCount_ = 0u;
        ownsPipelineLayout_ = false;
        destroyPipelinesOnShutdown_ = false;
        initialized_ = false;
        frameContext_ = {};
        frameContextBound_ = false;
        ResetActiveFrameState();
        return status;
    }

    VkDescriptorSet VulkanReSTIRRecorder::DescriptorSet(
        const std::uint32_t frameSlot) const noexcept
    {
        return frameSlot < descriptorSetCount_
            ? descriptorSets_[frameSlot]
            : VK_NULL_HANDLE;
    }

    std::size_t VulkanReSTIRRecorder::PassIndex(const ReSTIRPass pass) noexcept
    {
        return static_cast<std::size_t>(pass);
    }

    bool VulkanReSTIRRecorder::IsTracePreparation(const ReSTIRPass pass) noexcept
    {
        return pass == ReSTIRPass::PrepareReferenceVisibility
            || pass == ReSTIRPass::PrepareWinnerVisibility;
    }

    bool VulkanReSTIRRecorder::IsTraceResolution(const ReSTIRPass pass) noexcept
    {
        return pass == ReSTIRPass::ResolveReferenceVisibility
            || pass == ReSTIRPass::ResolveWinnerVisibility;
    }

    ReSTIRBarrier VulkanReSTIRRecorder::ExpectedBarrierAfter(
        const ReSTIRPass pass) noexcept
    {
        if (pass == ReSTIRPass::PublishSplitDirectSignal)
        {
            return ReSTIRBarrier::ComputeToReconstruction;
        }
        if (IsTraceResolution(pass))
        {
            return ReSTIRBarrier::ComputeToCompute;
        }
        return ReSTIRBarrier::ComputeToCompute;
    }
}
