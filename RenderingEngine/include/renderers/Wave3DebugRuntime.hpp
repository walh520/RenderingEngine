#pragma once

#include "app/RuntimeConfig.hpp"
#include "contracts/AbiTypesV0.hpp"
#include "core/Camera.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace RenderingEngine::Rt::Gpu
{
    class IGpuTraversalBackend;
}

namespace RenderingEngine::Renderers
{
    inline constexpr std::uint32_t kWave3DebugFrameCount = 2u;
    inline constexpr std::size_t kWave3DebugSignalCount = 5u;

    struct Wave3DebugRuntimeCreateInfo final
    {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkImageView outputImageView = VK_NULL_HANDLE;
        std::array<VkImageView, kWave3DebugSignalCount> signalImageViews{};
        std::filesystem::path shaderDirectory;
        VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout canonicalSceneSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout softwareTraversalSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout rayQueryTraversalSetLayout = VK_NULL_HANDLE;
        std::uint32_t maximumComputeGroupCountX = 0u;
        std::uint32_t maximumComputeGroupCountY = 0u;
    };

    struct Wave3DebugFrameBindings final
    {
        VkDescriptorSet frameSet = VK_NULL_HANDLE;
        VkDescriptorSet canonicalSceneSet = VK_NULL_HANDLE;
        VkDescriptorSet softwareTraversalSet = VK_NULL_HANDLE;
        VkDescriptorSet rayQueryTraversalSet = VK_NULL_HANDLE;
        VkDescriptorSet restirReferenceTraversalSet = VK_NULL_HANDLE;
        VkDescriptorSet restirWinnerTraversalSet = VK_NULL_HANDLE;
        Rt::Gpu::IGpuTraversalBackend* traversalBackend = nullptr;
        VkDescriptorBufferInfo pbrLightTableL6{};
        std::uint32_t pbrLightCount = 0u;
        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t sceneGeneration = 0u;
        std::uint32_t resourceGeneration = 0u;
        std::uint32_t lightGeneration = 0u;
    };

    struct Wave3DebugReSTIRTraversalViews final
    {
        VkDescriptorBufferInfo shadowRays{};
        VkDescriptorBufferInfo referenceHits{};
        VkDescriptorBufferInfo winnerHits{};

        [[nodiscard]] bool IsReady() const noexcept
        {
            return shadowRays.buffer != VK_NULL_HANDLE
                && referenceHits.buffer != VK_NULL_HANDLE
                && winnerHits.buffer != VK_NULL_HANDLE;
        }
    };

    struct Wave3DebugCamera final
    {
        Vec3 position{};
        Vec3 forward{0.0f, 0.0f, -1.0f};
        Vec3 right{1.0f, 0.0f, 0.0f};
        Vec3 up{0.0f, 1.0f, 0.0f};
        float verticalFovDegrees = 52.0f;
    };

    // Fence-complete, production ABI-v3 counters copied from the exact frame
    // statistics buffer. These are observations, not configured budgets.
    struct Wave3ReSTIRFrameObservation final
    {
        bool available = false;
        std::uint64_t generatedCandidates = 0u;
        std::uint64_t submittedVisibilityRays = 0u;
        std::uint64_t evaluatedVisibilityRays = 0u;
    };

    // Debug production attachment for Wave 3. It owns the real L7 queue
    // buffers/pipelines and L8 ABI-v2 history/reconstruction resources while
    // consuming the same set0/set1/set2 scene and traversal descriptors as the
    // main renderer. It never substitutes fixture traversal or screen-derived
    // GBuffer data.
    class Wave3DebugRuntime final
    {
    public:
        Wave3DebugRuntime();
        ~Wave3DebugRuntime();
        Wave3DebugRuntime(const Wave3DebugRuntime&) = delete;
        Wave3DebugRuntime& operator=(const Wave3DebugRuntime&) = delete;
        Wave3DebugRuntime(Wave3DebugRuntime&&) noexcept;
        Wave3DebugRuntime& operator=(Wave3DebugRuntime&&) noexcept;

        void Create(const Wave3DebugRuntimeCreateInfo& createInfo);
        void Reset() noexcept;
        void InvalidateHistories() noexcept;
        void InvalidateProgressiveFilm() noexcept;
        void SetSceneTransforms(
            std::span<const Contracts::AbiV0::AbiMat4Rows> currentObjectToWorld,
            std::span<const Contracts::AbiV0::AbiMat4Rows> previousObjectToWorld);
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] std::uint32_t WavefrontFatalMask() const noexcept;
        [[nodiscard]] Wave3ReSTIRFrameObservation CollectCompletedFrame(
            std::uint32_t frameSlot) noexcept;
        [[nodiscard]] VkDescriptorSetLayout ReconstructionSetLayout() const noexcept;
        [[nodiscard]] VkDescriptorSet ReconstructionSet(
            std::uint32_t frameSlot) const noexcept;

        void UpdateFrame(
            std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugCamera& camera,
            std::uint32_t sampleIndex,
            const Wave3DebugFrameBindings& bindings);

        [[nodiscard]] Wave3DebugReSTIRTraversalViews ReSTIRTraversalViews(
            std::uint32_t frameSlot) const noexcept;

        void RecordFrame(
            VkCommandBuffer commandBuffer,
            std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugFrameBindings& bindings);

        // The ABI-v2 reconstruction/ReSTIR post stage is shared by all GPU
        // execution architectures. Megakernel and staged ports call
        // these after publishing their GBuffer/split signal through set 4.
        void UpdatePostIntegratorFrame(
            std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugCamera& camera,
            std::uint32_t sampleIndex,
            const Wave3DebugFrameBindings& bindings);
        void RecordPostIntegratorFrame(
            VkCommandBuffer commandBuffer,
            std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugFrameBindings& bindings);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
