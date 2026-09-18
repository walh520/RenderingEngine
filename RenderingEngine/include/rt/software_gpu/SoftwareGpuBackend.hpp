#pragma once

#include "rt/gpu/IGpuTraversalBackend.hpp"
#include "rt/software_gpu/SoftwareGpu.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace RenderingEngine::Rt::SoftwareGpu
{
    inline constexpr std::uint32_t kWave2TraceWorkgroupSize = 64u;

    struct SoftwareGpuBackendCreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetLayout frameLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout canonicalSceneLayout = VK_NULL_HANDLE;
        std::span<const std::uint32_t> computeShaderSpirv;
        std::uint32_t maximumComputeGroupCountX = 0u;
        // Canonical-linear mode consumes the canonical triangle stream in its
        // original order and deliberately ignores the private BVH records.
        // The default preserves the flattened-SAH backend contract.
        bool canonicalLinear = false;
    };

    struct SoftwareGpuTraversalBindings
    {
        VkDescriptorBufferInfo nodes{};
        VkDescriptorBufferInfo rays{};
        VkDescriptorBufferInfo hits{};
        VkDescriptorBufferInfo triangles{};
        VkDescriptorBufferInfo counters{};
        VkDescriptorImageInfo alphaAtlas{};
        VkDescriptorImageInfo alphaSampler{};
        std::uint32_t nodeCount = 0u;
        std::uint32_t triangleCount = 0u;
        std::uint32_t stackCapacity = 64u;
        std::uint32_t alphaAtlasLayerCount = 0u;
        std::uint32_t alphaSamplerId = 0xffffffffu;
    };

    struct SoftwareGpuTracePushConstants
    {
        std::uint32_t nodeCount = 0u;
        std::uint32_t triangleCount = 0u;
        std::uint32_t rayCount = 0u;
        std::uint32_t queryMode = 0u;
        std::uint32_t rayOffset = 0u;
        std::uint32_t hitOffset = 0u;
        std::uint32_t stackCapacity = 64u;
        std::uint32_t alphaAtlasLayerCount = 0u;
        std::uint32_t alphaSamplerId = 0xffffffffu;
        // Kept in the ABI-reserved lane: 0 = flattened SAH, 1 = canonical linear.
        std::uint32_t reserved = 0u;
    };
    static_assert(sizeof(SoftwareGpuTracePushConstants) == 40u);

    // Vulkan command-recording implementation of the ABI-v1 traversal seam.
    // Scene upload and readback remain composition-owned; this object owns only
    // the compute pipeline/layout and validates the scene lifetime identity.
    class SoftwareGpuTraversalBackend final : public Gpu::IGpuTraversalBackend
    {
    public:
        SoftwareGpuTraversalBackend() = default;
        ~SoftwareGpuTraversalBackend() override;
        SoftwareGpuTraversalBackend(const SoftwareGpuTraversalBackend&) = delete;
        SoftwareGpuTraversalBackend& operator=(const SoftwareGpuTraversalBackend&) = delete;

        [[nodiscard]] Gpu::GpuTraversalStatus Create(
            const SoftwareGpuBackendCreateInfo& createInfo) noexcept;
        void Reset() noexcept;

        [[nodiscard]] Gpu::GpuTraversalStatus CreateDescriptorPool(
            std::uint32_t setCount,
            VkDescriptorPool& output) const noexcept;
        [[nodiscard]] Gpu::GpuTraversalStatus AllocateTraversalSet(
            VkDescriptorPool pool,
            VkDescriptorSet& output) const noexcept;
        [[nodiscard]] Gpu::GpuTraversalStatus UpdateTraversalSet(
            VkDescriptorSet set,
            const SoftwareGpuTraversalBindings& bindings) noexcept;

        [[nodiscard]] Gpu::GpuTraversalBackendDescriptor Descriptor() const noexcept override;
        [[nodiscard]] Gpu::GpuTraversalStatus BuildOrUpdateScene(
            const Gpu::GpuSceneBuildRequest& request) override;
        [[nodiscard]] Gpu::GpuTraversalStatus RecordTraceClosestBatch(
            const Gpu::GpuTraceBatch& batch) override;
        [[nodiscard]] Gpu::GpuTraversalStatus RecordTraceAnyBatch(
            const Gpu::GpuTraceBatch& batch) override;

        [[nodiscard]] VkDescriptorSetLayout TraversalLayout() const noexcept;
        [[nodiscard]] VkPipelineLayout PipelineLayout() const noexcept;
        [[nodiscard]] VkPipeline Pipeline() const noexcept;

    private:
        [[nodiscard]] Gpu::GpuTraversalStatus RecordTrace(
            const Gpu::GpuTraceBatch& batch,
            QueryMode mode) noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout traversalLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkDescriptorBufferInfo counterBinding_{};
        std::uint32_t maximumComputeGroupCountX_ = 0u;
        std::uint32_t nodeCount_ = 0u;
        std::uint32_t triangleCount_ = 0u;
        std::uint32_t stackCapacity_ = 0u;
        std::uint32_t alphaAtlasLayerCount_ = 0u;
        std::uint32_t alphaSamplerId_ = 0xffffffffu;
        std::uint64_t sceneFingerprint_ = 0u;
        std::uint32_t sceneGeneration_ = 0u;
        bool canonicalLinear_ = false;
        bool descriptorsConfigured_ = false;
    };
}
