#pragma once

#include "RtPipelineBackend.hpp"
#include "rt/gpu/IGpuTraversalBackend.hpp"

#include <cstdint>

namespace RenderingEngine::Rt::Hardware
{
    // ABI-v1 command-recording adapter for the Vulkan RT Pipeline backend.
    // BLAS/TLAS construction and descriptor ownership remain with the caller;
    // this object accepts the corresponding canonical scene identity and then
    // enforces it for every closest/any batch.
    class RtPipelineTraversalAdapter final : public Gpu::IGpuTraversalBackend
    {
    public:
        explicit RtPipelineTraversalAdapter(
            RtPipelineBackend& backend,
            std::uint32_t alphaAtlasLayerCount = 0u,
            std::uint32_t alphaSamplerId = 0xffffffffu) noexcept;

        [[nodiscard]] Gpu::GpuTraversalBackendDescriptor Descriptor() const noexcept override;
        [[nodiscard]] Gpu::GpuTraversalStatus BuildOrUpdateScene(
            const Gpu::GpuSceneBuildRequest& request) override;
        [[nodiscard]] Gpu::GpuTraversalStatus RecordTraceClosestBatch(
            const Gpu::GpuTraceBatch& batch) override;
        [[nodiscard]] Gpu::GpuTraversalStatus RecordTraceAnyBatch(
            const Gpu::GpuTraceBatch& batch) override;

    private:
        [[nodiscard]] Gpu::GpuTraversalStatus ValidateTrace(
            const Gpu::GpuTraceBatch& batch) const;
        [[nodiscard]] Gpu::GpuTraversalStatus ForwardTrace(
            const Gpu::GpuTraceBatch& batch,
            RayBatchQuery query);

        RtPipelineBackend* backend_{};
        VkDescriptorSet canonicalSceneSet_{VK_NULL_HANDLE};
        std::uint64_t sceneFingerprint_{};
        std::uint32_t sceneGeneration_{};
        std::uint32_t alphaAtlasLayerCount_{};
        std::uint32_t alphaSamplerId_{0xffffffffu};
        bool sceneReady_{};
    };
}
