#pragma once

#include "DeviceBuffer.hpp"
#include "DeviceDispatch.hpp"
#include "HardwareRtCapabilities.hpp"
#include "HardwareRtStatus.hpp"
#include "RayQueryBackend.hpp"
#include "SbtBuilder.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace RenderingEngine::Rt::Hardware
{
    inline constexpr std::uint32_t kRtPipelinePayloadSize = 32u;
    inline constexpr std::uint32_t kRtPipelineTriangleAttributeSize = 8u;
    inline constexpr std::uint32_t kRtPipelineRecursionDepth = 1u;

    [[nodiscard]] std::uint32_t ComputeRtDispatchChunkCount(
        std::uint32_t rayCount,
        std::uint32_t maximumInvocationCount) noexcept;

    class RtPipelineBackend final
    {
    public:
        RtPipelineBackend() = default;
        ~RtPipelineBackend();
        RtPipelineBackend(const RtPipelineBackend&) = delete;
        RtPipelineBackend& operator=(const RtPipelineBackend&) = delete;
        RtPipelineBackend(RtPipelineBackend&& other) noexcept;
        RtPipelineBackend& operator=(RtPipelineBackend&& other) noexcept;

        [[nodiscard]] Status Create(
            const DeviceBufferAllocator& allocator,
            const DeviceDispatch& dispatch,
            const HardwareRtLimits& limits,
            std::span<const std::uint32_t> shaderLibrarySpirv) noexcept;
        void Reset() noexcept;

        [[nodiscard]] Status CreateDescriptorPool(
            std::uint32_t batchCount,
            VkDescriptorPool& output) const noexcept;
        [[nodiscard]] Status AllocateDescriptorSets(
            VkDescriptorPool pool,
            VkDescriptorSet& sceneSet,
            VkDescriptorSet& traversalSet) const noexcept;
        void UpdateSceneDescriptors(
            VkDescriptorSet sceneSet,
            const HardwareSceneBufferBindings& bindings) const noexcept;
        void UpdateTraversalDescriptors(
            VkDescriptorSet traversalSet,
            const RayQueryTraversalBindings& bindings) const noexcept;

        void RecordTraceClosestBatch(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            std::uint32_t rayCount,
            std::uint32_t alphaAtlasLayerCount,
            std::uint32_t alphaSamplerId = 0xffffffffu) const noexcept;
        void RecordTraceAnyBatch(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            std::uint32_t rayCount,
            std::uint32_t alphaAtlasLayerCount,
            std::uint32_t alphaSamplerId = 0xffffffffu) const noexcept;

        [[nodiscard]] VkDescriptorSetLayout SceneAdapterLayout() const noexcept;
        [[nodiscard]] VkDescriptorSetLayout TraversalLayout() const noexcept;
        [[nodiscard]] VkPipelineLayout PipelineLayout() const noexcept;
        [[nodiscard]] VkPipeline Pipeline() const noexcept;
        [[nodiscard]] const ShaderBindingTable& Sbt() const noexcept;

    private:
        void Record(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            const RayBatchPushConstants& constants) const noexcept;

        VkDevice device_{VK_NULL_HANDLE};
        const DeviceDispatch* dispatch_{nullptr};
        VkDescriptorSetLayout emptyFrameLayout_{VK_NULL_HANDLE};
        VkDescriptorSetLayout sceneAdapterLayout_{VK_NULL_HANDLE};
        VkDescriptorSetLayout traversalLayout_{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
        ShaderBindingTable sbt_{};
        std::uint32_t maxRayDispatchInvocationCount_{0u};
    };
}
