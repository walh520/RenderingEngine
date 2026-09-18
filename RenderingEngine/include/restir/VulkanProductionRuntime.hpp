#pragma once

#include "renderers/VulkanReSTIRRecorder.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace RenderingEngine::Restir
{
    inline constexpr VkDeviceSize kPbrLightGpuL6Stride = 96u;
    inline constexpr VkDeviceSize kReSTIRDummyDescriptorBytes = 16u;

    enum class VulkanProductionIssue : std::uint64_t
    {
        None = 0u,
        InvalidFramePlan = 1ull << 0u,
        UnsupportedLightTier = 1ull << 1u,
        ResourceSizeOverflow = 1ull << 2u,
        MissingCommandBuffer = 1ull << 3u,
        MissingCanonicalDescriptorSet = 1ull << 4u,
        InvalidTraversalAttachment = 1ull << 5u,
        MissingPrimarySurfaceV2 = 1ull << 6u,
        MissingMotionVectorsV2 = 1ull << 7u,
        MissingPbrLightTableL6 = 1ull << 8u,
        LightCountMismatch = 1ull << 9u,
        MissingReconstructionOutput = 1ull << 10u,
        MissingReconstructionRecorder = 1ull << 11u,
        MissingSet5Resource = 1ull << 12u,
        Set5RangeTooSmall = 1ull << 13u,
        InvalidDebugImage = 1ull << 14u,
        InvalidFrameIdentity = 1ull << 15u,
        InvalidHistorySlot = 1ull << 16u,
        MissingComputePipeline = 1ull << 17u,
        PipelineLayoutMismatch = 1ull << 18u
    };

    [[nodiscard]] constexpr VulkanProductionIssue operator|(
        const VulkanProductionIssue left,
        const VulkanProductionIssue right) noexcept
    {
        return static_cast<VulkanProductionIssue>(
            static_cast<std::uint64_t>(left) | static_cast<std::uint64_t>(right));
    }

    struct VulkanReSTIRResourcePlan final
    {
        Renderers::ReSTIRRuntimeStatus status{};
        std::array<VkDeviceSize, Renderers::kVulkanReSTIRSet5BindingCount>
            descriptorBufferBytes{};
        std::array<bool, Renderers::kVulkanReSTIRSet5BindingCount>
            requiresDistinctAllocation{};
        VkExtent2D debugImageExtent{};
        VkFormat debugImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        // These are descriptor/image payload minima. VkMemoryRequirements
        // remains authoritative for device-specific allocation alignment and
        // padding; the bridge never reports these values as measured VRAM.
        VkDeviceSize perFrameTransientBytes = 0u;
        VkDeviceSize historyRingSlotBytes = 0u;
        VkDeviceSize historyRingBytes = 0u;
        VkDeviceSize debugImageTexelBytes = 0u;
        VkDeviceSize minimumOwnerPayloadBytes = 0u;
        std::uint32_t historyRingSlotCount = 0u;

        [[nodiscard]] bool IsReady() const noexcept
        {
            return static_cast<bool>(status);
        }
    };

    // Explicit views exported by the existing production frame. Descriptor
    // sets are opaque in Vulkan, so the owner supplies the exact buffer/image
    // views and generation identities used to populate set 0, set 3 and set 4. This
    // is evidence for attachment, not a substitute producer.
    struct VulkanReSTIRProductionEvidence final
    {
        VkDescriptorBufferInfo primarySurfaceV2{};
        // Full ABI-v2 GpuGBufferRecordV2 storage owning the compact motion
        // member. ReSTIR reads `.motion` with the published 208-byte stride.
        VkDescriptorBufferInfo motionVectorsV2{};
        VkDescriptorBufferInfo pbrLightTableL6{};
        std::uint32_t pbrLightCount = 0u;

        VkDescriptorImageInfo reconstructionOutput{};
        VkExtent2D reconstructionOutputExtent{};

        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t sceneGeneration = 0u;
        std::uint32_t resourceGeneration = 0u;
        std::uint32_t lightGeneration = 0u;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::array<VkPipeline, Renderers::kVulkanReSTIRPassCount> pipelines{};
    };

    struct VulkanReSTIRProductionReport final
    {
        Renderers::ReSTIRRuntimeStatus status{};
        VulkanProductionIssue issues = VulkanProductionIssue::None;
        VulkanReSTIRResourcePlan resources{};

        [[nodiscard]] bool IsReady() const noexcept
        {
            return static_cast<bool>(status)
                && issues == VulkanProductionIssue::None;
        }
        [[nodiscard]] bool HasIssue(
            const VulkanProductionIssue issue) const noexcept
        {
            return (static_cast<std::uint64_t>(issues)
                & static_cast<std::uint64_t>(issue)) != 0u;
        }
    };

    [[nodiscard]] VulkanReSTIRResourcePlan BuildVulkanReSTIRResourcePlan(
        const Renderers::ReSTIRFramePlan& framePlan) noexcept;

    [[nodiscard]] VulkanReSTIRProductionReport
        ValidateVulkanReSTIRProductionAttachment(
            const Renderers::ReSTIRFramePlan& framePlan,
            const Renderers::VulkanReSTIRFrameContext& frameContext,
            const VulkanReSTIRProductionEvidence& evidence,
            const Rt::Gpu::IGpuTraversalBackend& traversalBackend) noexcept;

    // This is the sole lane-local transition from validated production
    // resources to the ABI-v3 recorder. It records no fallback counters or
    // synthetic image when attachment validation fails.
    [[nodiscard]] Renderers::ReSTIRRuntimeStatus
        RecordVulkanReSTIRProductionFrame(
            const Renderers::ReSTIRFramePlan& framePlan,
            const Renderers::VulkanReSTIRFrameContext& frameContext,
            const VulkanReSTIRProductionEvidence& evidence,
            Renderers::VulkanReSTIRRecorder& recorder,
            Rt::Gpu::IGpuTraversalBackend& traversalBackend);

    [[nodiscard]] std::string_view VulkanReSTIRShaderFileName(
        Renderers::ReSTIRPass pass) noexcept;
}
