#pragma once

#include "DeviceDispatch.hpp"
#include "HardwareRtStatus.hpp"
#include "rt/gpu/IGpuTraversalBackend.hpp"

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
        // These bindings intentionally mirror the shared canonical scene set:
        // binding 0 is the scene constants UBO, followed by the v0 scene
        // arrays at bindings 1..6.  Do not compact this list for the private
        // Ray Query adapter; descriptor-set compatibility is part of ABI v1.
        VkDescriptorBufferInfo constants{};
        VkDescriptorBufferInfo vertices{};
        VkDescriptorBufferInfo indices{};
        VkDescriptorBufferInfo geometries{};
        VkDescriptorBufferInfo instances{};
        VkDescriptorBufferInfo materials{};
        VkDescriptorBufferInfo lights{};
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
        std::uint32_t hitOffset{0u};
    };
    static_assert(sizeof(RayBatchPushConstants) == 24u);

    // Both hardware paths carry independent v1 queue offsets. They remain
    // distinct types so neither backend can silently bind the other's layout.
    struct RayQueryPushConstants final
    {
        std::uint32_t rayCount{0u};
        RayBatchQuery query{RayBatchQuery::Closest};
        std::uint32_t alphaAtlasLayerCount{0u};
        std::uint32_t alphaSamplerId{0xffffffffu};
        std::uint32_t rayOffset{0u};
        std::uint32_t hitOffset{0u};
    };
    static_assert(sizeof(RayQueryPushConstants) == 24u);

    inline constexpr std::uint32_t kCanonicalSceneConstantsBinding = 0u;
    inline constexpr std::uint32_t kCanonicalSceneVerticesBinding = 1u;
    inline constexpr std::uint32_t kCanonicalSceneIndicesBinding = 2u;
    inline constexpr std::uint32_t kCanonicalSceneGeometriesBinding = 3u;
    inline constexpr std::uint32_t kCanonicalSceneInstancesBinding = 4u;
    inline constexpr std::uint32_t kCanonicalSceneMaterialsBinding = 5u;
    inline constexpr std::uint32_t kCanonicalSceneLightsBinding = 6u;

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
        [[nodiscard]] Status UpdateSceneDescriptors(
            VkDescriptorSet sceneSet,
            const HardwareSceneBufferBindings& bindings) const noexcept;
        [[nodiscard]] Status UpdateTraversalDescriptors(
            VkDescriptorSet traversalSet,
            const RayQueryTraversalBindings& bindings) const noexcept;

        [[nodiscard]] Status RecordTraceClosestBatch(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            std::uint32_t rayOffset,
            std::uint32_t hitOffset,
            std::uint32_t rayCount,
            std::uint32_t alphaAtlasLayerCount,
            std::uint32_t alphaSamplerId = 0xffffffffu) const noexcept;
        [[nodiscard]] Status RecordTraceAnyBatch(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            std::uint32_t rayOffset,
            std::uint32_t hitOffset,
            std::uint32_t rayCount,
            std::uint32_t alphaAtlasLayerCount,
            std::uint32_t alphaSamplerId = 0xffffffffu) const noexcept;

        // Compatibility overloads for callers that intentionally trace from
        // record zero.  The full overload above is the ABI-v1 entry point.
        [[nodiscard]] Status RecordTraceClosestBatch(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            std::uint32_t rayCount,
            std::uint32_t alphaAtlasLayerCount,
            std::uint32_t alphaSamplerId = 0xffffffffu) const noexcept;
        [[nodiscard]] Status RecordTraceAnyBatch(
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
        [[nodiscard]] Status Record(
            VkCommandBuffer commandBuffer,
            VkDescriptorSet sceneSet,
            VkDescriptorSet traversalSet,
            const RayQueryPushConstants& constants) const noexcept;

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout emptyFrameLayout_{VK_NULL_HANDLE};
        VkDescriptorSetLayout sceneAdapterLayout_{VK_NULL_HANDLE};
        VkDescriptorSetLayout traversalLayout_{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
        std::uint32_t maxComputeWorkGroupCountX_{0u};
    };

    // Command-recording adapter for the shared ABI-v1 traversal boundary.
    // It validates and remembers scene identity, then forwards only the trace
    // command.  AS construction, queue submission, fences, and readback stay
    // with the owning integration layer.
    class RayQueryTraversalAdapter final : public RenderingEngine::Rt::Gpu::IGpuTraversalBackend
    {
    public:
        explicit RayQueryTraversalAdapter(
            RayQueryBackend& backend,
            std::uint32_t alphaAtlasLayerCount = 0u,
            std::uint32_t alphaSamplerId = 0xffffffffu) noexcept;

        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalBackendDescriptor Descriptor() const noexcept override;
        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalStatus BuildOrUpdateScene(
            const RenderingEngine::Rt::Gpu::GpuSceneBuildRequest& request) override;
        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalStatus RecordTraceClosestBatch(
            const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch) override;
        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalStatus RecordTraceAnyBatch(
            const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch) override;

    private:
        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalStatus ValidateTrace(
            const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch) const;
        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalStatus ForwardTrace(
            const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch,
            RayBatchQuery query);

        RayQueryBackend* backend_{nullptr};
        VkDescriptorSet canonicalSceneSet_{VK_NULL_HANDLE};
        std::uint64_t sceneFingerprint_{0u};
        std::uint32_t sceneGeneration_{0u};
        std::uint32_t alphaAtlasLayerCount_{0u};
        std::uint32_t alphaSamplerId_{0xffffffffu};
        bool sceneReady_{false};
    };
}
