#pragma once

#include "DeviceDispatch.hpp"
#include "HardwareRtStatus.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace RenderingEngine::Rt::Hardware
{
    inline constexpr std::uint32_t kRayQueryWorkGroupSizeX = 64u;

    [[nodiscard]] std::uint32_t ComputeRayQueryDispatchChunkCount(
        std::uint32_t rayCount,
        std::uint32_t maxComputeWorkGroupCountX) noexcept;

    enum class RayBatchQuery : std::uint32_t
    {
        Closest = 0u,
        Any = 1u
    };

    struct HardwareSceneBufferBindings final
    {
        VkDescriptorBufferInfo vertices{};
        VkDescriptorBufferInfo indices{};
        VkDescriptorBufferInfo geometries{};
        VkDescriptorBufferInfo instances{};
        VkDescriptorBufferInfo materials{};
    };

    struct RayQueryTraversalBindings final
    {
        VkAccelerationStructureKHR topLevelAccelerationStructure{VK_NULL_HANDLE};
        VkDescriptorBufferInfo rays{};
        VkDescriptorBufferInfo hits{};
        VkDescriptorImageInfo alphaAtlas{};
        VkDescriptorImageInfo alphaSampler{};
    };

    struct RayBatchPushConstants final
    {
        std::uint32_t rayCount{0u};
        RayBatchQuery query{RayBatchQuery::Closest};
        std::uint32_t alphaAtlasLayerCount{0u};
        // Canonical sampler ID represented by the lane-private atlas sampler.
        // A textured alpha material with a different ID produces Invalid.
        std::uint32_t alphaSamplerId{0xffffffffu};
        std::uint32_t rayOffset{0u};
    };
    static_assert(sizeof(RayBatchPushConstants) == 20u);

    class RayQueryBackend final
    {
    public:
        RayQueryBackend() = default;
        ~RayQueryBackend();
        RayQueryBackend(const RayQueryBackend&) = delete;
        RayQueryBackend& operator=(const RayQueryBackend&) = delete;
        RayQueryBackend(RayQueryBackend&& other) noexcept;
        RayQueryBackend& operator=(RayQueryBackend&& other) noexcept;

        [[nodiscard]] Status Create(
            VkDevice device,
            std::span<const std::uint32_t> computeShaderSpirv,
            std::uint32_t maxComputeWorkGroupCountX) noexcept;
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

    private:
        void Record(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            const RayBatchPushConstants& constants) const noexcept;

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout emptyFrameLayout_{VK_NULL_HANDLE};
        VkDescriptorSetLayout sceneAdapterLayout_{VK_NULL_HANDLE};
        VkDescriptorSetLayout traversalLayout_{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
        std::uint32_t maxComputeWorkGroupCountX_{0u};
    };
}
