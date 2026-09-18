#pragma once

#include "contracts/AbiV3.hpp"
#include "renderers/ReSTIRDIRuntime.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace RenderingEngine::Renderers
{
    inline constexpr std::uint32_t kVulkanReSTIRSetCount = 7u;
    inline constexpr std::uint32_t kVulkanReSTIRSet5BindingCount =
        static_cast<std::uint32_t>(kReSTIRSet5BindingCount);
    inline constexpr std::uint32_t kVulkanReSTIRInvalidHistoryIndex = 0xffffffffu;
    inline constexpr std::size_t kVulkanReSTIRPassCount =
        static_cast<std::size_t>(ReSTIRPass::WriteDebug) + 1u;

    // All set-5 resources are supplied by the production owner. The recorder
    // owns the descriptor layout/sets, but it does not guess resource memory
    // allocation or alias a stage-local private resource.
    struct VulkanReSTIRResourceTable final
    {
        std::array<VkDescriptorBufferInfo, kVulkanReSTIRSet5BindingCount> buffers{};
        VkDescriptorImageInfo debugImage{};
        // VkDescriptorImageInfo does not carry the view's format or extent.
        // The production owner supplies both so the recorder can validate the
        // set-5 binding-16 RWTexture2D<float4> contract before recording.
        VkExtent2D debugImageExtent{};
        VkFormat debugImageFormat = VK_FORMAT_UNDEFINED;
        std::array<bool, kVulkanReSTIRSet5BindingCount> valid{};
    };

    struct VulkanReSTIRRecorderCreateInfo final
    {
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

        // Set 5 is created by VulkanReSTIRRecorder. When pipelineLayout is
        // null, these six layouts (0-4 and 6) are required to create the
        // complete set-0..6 pipeline layout. When it is supplied, the caller
        // guarantees that its set-5 layout is ABI-v3 compatible.
        std::array<VkDescriptorSetLayout, kVulkanReSTIRSetCount> pipelineSetLayouts{};
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        // Pipelines are created by the shader build/runtime owner because the
        // recorder cannot compile HLSL. Missing handles fail BeginFrame.
        std::array<VkPipeline, kVulkanReSTIRPassCount> pipelines{};
        std::uint32_t framesInFlight = 2u;
        bool destroyPipelinesOnShutdown = false;
    };

    // Wave 3 owns reconstruction pipelines and their internal descriptors.
    // Wave 4 only supplies the command buffer and the complete canonical
    // descriptor tuple after publishing the split direct-light signal.
    class IVulkanReSTIRReconstructionRecorder
    {
    public:
        virtual ~IVulkanReSTIRReconstructionRecorder() = default;

        [[nodiscard]] virtual ReSTIRRuntimeStatus RecordReconstruction(
            const ReSTIRFramePlan& plan,
            VkCommandBuffer commandBuffer,
            const std::array<VkDescriptorSet, kVulkanReSTIRSetCount>&
                descriptorSets) = 0;
    };

    struct VulkanReSTIRFrameContext final
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Set 5 is ignored and replaced with the recorder-owned set for the
        // selected frame slot. Sets 0-4 and 6 must be valid production sets.
        std::array<VkDescriptorSet, kVulkanReSTIRSetCount> descriptorSets{};
        // Separate traversal descriptor sets make reference and winner
        // visibility batches non-aliasing at the shared backend boundary.
        VkDescriptorSet referenceTraversalSet = VK_NULL_HANDLE;
        VkDescriptorSet winnerTraversalSet = VK_NULL_HANDLE;
        // The shared traversal ABI writes GpuHitQueueRecordV1. These explicit
        // views prove that set-5 bindings 19/10 read the same output ranges as
        // set-2 binding 2 in the reference/winner traversal sets.
        VkDescriptorBufferInfo referenceTraversalHits{};
        VkDescriptorBufferInfo winnerTraversalHits{};
        // Both TraceAny batches reuse set-5 binding 23 as set-2 binding 1.
        // The explicit views prevent a traversal set from silently reading a
        // different ray queue than the prepare shaders populated.
        VkDescriptorBufferInfo referenceTraversalRays{};
        VkDescriptorBufferInfo winnerTraversalRays{};
        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t sceneGeneration = 0u;
        std::uint32_t frameSlot = 0u;
        // These indices are logical ownership evidence in the 2N history
        // ring; frameSlot remains frameIndex % framesInFlight. The resource table
        // must bind the corresponding current write at binding 29 and the
        // immutable previous-frame read at binding 30.
        std::uint32_t historyReadPhysicalIndex = kVulkanReSTIRInvalidHistoryIndex;
        std::uint32_t historyWritePhysicalIndex = kVulkanReSTIRInvalidHistoryIndex;
        IVulkanReSTIRReconstructionRecorder* reconstructionRecorder = nullptr;
        VulkanReSTIRResourceTable resources{};
    };

    // Production Vulkan command recorder for the ReSTIRDIRuntime frame plan.
    // It owns descriptor layout/sets and an optional full set-0..6 pipeline
    // layout. External scene/traversal sets, buffers, images and pipelines are
    // explicit inputs; no private L9 binding is silently reinterpreted.
    class VulkanReSTIRRecorder final : public IReSTIRGpuRecorder
    {
    public:
        VulkanReSTIRRecorder() = default;
        ~VulkanReSTIRRecorder() override;

        VulkanReSTIRRecorder(const VulkanReSTIRRecorder&) = delete;
        VulkanReSTIRRecorder& operator=(const VulkanReSTIRRecorder&) = delete;

        [[nodiscard]] ReSTIRRuntimeStatus Initialize(
            const VulkanReSTIRRecorderCreateInfo& createInfo);
        [[nodiscard]] ReSTIRRuntimeStatus SetFrameContext(
            const VulkanReSTIRFrameContext& context);
        // Pipeline creation needs the recorder-owned set-5 layout/pipeline
        // layout.  Production owners therefore initialize the recorder,
        // create shader pipelines against PipelineLayout(), then publish the
        // immutable pass table before BeginFrame.
        [[nodiscard]] ReSTIRRuntimeStatus SetPipelines(
            const std::array<VkPipeline, kVulkanReSTIRPassCount>& pipelines);
        [[nodiscard]] ReSTIRRuntimeStatus Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }
        [[nodiscard]] VkDescriptorSetLayout RestirSetLayout() const noexcept
        {
            return restirSetLayout_;
        }
        [[nodiscard]] VkPipelineLayout PipelineLayout() const noexcept
        {
            return pipelineLayout_;
        }
        [[nodiscard]] VkDescriptorSet DescriptorSet(
            const std::uint32_t frameSlot) const noexcept;

        [[nodiscard]] ReSTIRRuntimeStatus BeginFrame(
            const ReSTIRFramePlan& plan) override;
        [[nodiscard]] ReSTIRRuntimeStatus RecordCompute(
            ReSTIRPass pass,
            std::uint32_t dispatchGroupCountX,
            std::uint32_t dispatchGroupCountY,
            std::uint32_t dispatchGroupCountZ) override;
        [[nodiscard]] ReSTIRRuntimeStatus RecordBarrier(
            ReSTIRBarrier barrier) override;
        [[nodiscard]] ReSTIRRuntimeStatus RecordReconstruction(
            const ReSTIRFramePlan& plan) override;
        [[nodiscard]] Rt::Gpu::GpuTraceBatch BuildTraceAnyBatch(
            ReSTIRPass preparePass,
            std::uint32_t maximumRayCount) override;
        [[nodiscard]] ReSTIRRuntimeStatus EndFrame(
            const ReSTIRFramePlan& plan) override;
        void AbortFrame() noexcept override;

        [[nodiscard]] static constexpr VkDescriptorType DescriptorTypeForBinding(
            const std::uint32_t binding) noexcept
        {
            return binding == Contracts::AbiV3::RestirBinding::DebugImage
                ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                : (binding == Contracts::AbiV3::RestirBinding::Parameters
                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        [[nodiscard]] static constexpr bool BufferViewsOverlap(
            const VkDescriptorBufferInfo& left,
            const VkDescriptorBufferInfo& right) noexcept
        {
            if (left.buffer == VK_NULL_HANDLE || right.buffer == VK_NULL_HANDLE
                || left.buffer != right.buffer)
            {
                return false;
            }
            if (left.range == 0u || right.range == 0u
                || left.range == VK_WHOLE_SIZE || right.range == VK_WHOLE_SIZE
                || left.offset > std::numeric_limits<VkDeviceSize>::max() - left.range
                || right.offset > std::numeric_limits<VkDeviceSize>::max() - right.range)
            {
                return true;
            }
            const VkDeviceSize leftEnd = left.offset + left.range;
            const VkDeviceSize rightEnd = right.offset + right.range;
            return left.offset < rightEnd && right.offset < leftEnd;
        }
        [[nodiscard]] static constexpr bool SameBufferView(
            const VkDescriptorBufferInfo& left,
            const VkDescriptorBufferInfo& right) noexcept
        {
            return left.buffer == right.buffer
                && left.offset == right.offset && left.range == right.range;
        }
        [[nodiscard]] static constexpr bool TraceAnyViewsNonOverlapping(
            const VkDescriptorBufferInfo& shadowRays,
            const VkDescriptorBufferInfo& referenceHits,
            const VkDescriptorBufferInfo& winnerHits) noexcept
        {
            return !BufferViewsOverlap(shadowRays, referenceHits)
                && !BufferViewsOverlap(shadowRays, winnerHits)
                && !BufferViewsOverlap(referenceHits, winnerHits);
        }
        [[nodiscard]] static constexpr bool DebugImageMetadataValid(
            const VkFormat format,
            const VkExtent2D extent) noexcept
        {
            return format == VK_FORMAT_R32G32B32A32_SFLOAT
                && extent.width != 0u && extent.height != 0u;
        }
        [[nodiscard]] static constexpr bool DebugImageCoversPlan(
            const VkExtent2D extent,
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            return extent.width >= width && extent.height >= height;
        }
        [[nodiscard]] static constexpr bool ExternalSetLayoutsRequired(
            const VkPipelineLayout pipelineLayout) noexcept
        {
            return pipelineLayout == VK_NULL_HANDLE;
        }
        [[nodiscard]] static constexpr bool RequiresFollowingComputeBarrier(
            const ReSTIRPass pass,
            const bool hasFollowingPass) noexcept
        {
            return hasFollowingPass && pass != ReSTIRPass::WriteDebug;
        }

    private:
        [[nodiscard]] ReSTIRRuntimeStatus CreateSetLayout();
        [[nodiscard]] ReSTIRRuntimeStatus CreatePipelineLayout(
            const VulkanReSTIRRecorderCreateInfo& createInfo);
        [[nodiscard]] ReSTIRRuntimeStatus AllocateDescriptorSets(
            std::uint32_t framesInFlight);
        [[nodiscard]] ReSTIRRuntimeStatus UpdateDescriptorSet(
            const VulkanReSTIRFrameContext& context);
        [[nodiscard]] ReSTIRRuntimeStatus ValidateFrameContext(
            const VulkanReSTIRFrameContext& context) const;
        [[nodiscard]] ReSTIRRuntimeStatus ValidateResourceRanges(
            const ReSTIRFramePlan& plan) const;
        [[nodiscard]] ReSTIRRuntimeStatus ValidatePassPipeline(ReSTIRPass pass) const;
        [[nodiscard]] ReSTIRRuntimeStatus RecordMemoryBarrier(ReSTIRBarrier barrier);
        [[nodiscard]] ReSTIRRuntimeStatus Fail(
            ReSTIRRuntimeStatusCode code,
            std::string reason) const;
        [[nodiscard]] static std::size_t PassIndex(ReSTIRPass pass) noexcept;
        [[nodiscard]] static bool IsTracePreparation(ReSTIRPass pass) noexcept;
        [[nodiscard]] static bool IsTraceResolution(ReSTIRPass pass) noexcept;
        [[nodiscard]] static ReSTIRBarrier ExpectedBarrierAfter(
            ReSTIRPass pass) noexcept;
        void ResetActiveFrameState() noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout restirSetLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, 4u> descriptorSets_{};
        std::array<VkPipeline, kVulkanReSTIRPassCount> pipelines_{};
        std::uint32_t framesInFlight_ = 0u;
        std::uint32_t descriptorSetCount_ = 0u;
        bool ownsPipelineLayout_ = false;
        bool destroyPipelinesOnShutdown_ = false;
        bool initialized_ = false;

        VulkanReSTIRFrameContext frameContext_{};
        bool frameContextBound_ = false;
        ReSTIRFramePlan activePlan_{};
        bool activeFrame_ = false;
        std::size_t nextPassIndex_ = 0u;
        ReSTIRPass lastPass_ = ReSTIRPass::ClearStatistics;
        bool pendingBarrier_ = false;
        ReSTIRBarrier pendingBarrierKind_ = ReSTIRBarrier::ComputeToCompute;
        bool traceBarrierRecorded_ = false;
        bool awaitingTraversalBarrier_ = false;
        bool traceBatchBuilt_ = false;
        bool referenceTraceRecorded_ = false;
        bool winnerTraceRecorded_ = false;
        bool referenceResolved_ = false;
        bool winnerResolved_ = false;
        bool publishSignalRecorded_ = false;
        bool reconstructionRecorded_ = false;
        bool publishHistoryRecorded_ = false;
    };
}
