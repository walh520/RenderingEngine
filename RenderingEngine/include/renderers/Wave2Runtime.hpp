#pragma once

#include "app/RuntimeConfig.hpp"
#include "core/Camera.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace RenderingEngine::Scene
{
    struct CanonicalScene;
    struct ExperimentEnvironment;
}

namespace RenderingEngine::Renderers
{
    inline constexpr std::uint32_t kWave2RuntimeFrameCount = 2u;
    inline constexpr std::size_t kWave2RuntimeCounterCount = 26u;
    inline constexpr std::size_t kWave2SoftwareTraversalCounterCount = 9u;
    inline constexpr std::size_t kWave2RuntimeSignalCount = 5u;

    struct Wave2RuntimeCreateInfo final
    {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkImageView outputImageView = VK_NULL_HANDLE;
        VkExtent2D outputExtent{};
        std::filesystem::path shaderDirectory;
    };

    struct Wave2RuntimeCamera final
    {
        Vec3 position{};
        Vec3 forward{0.0f, 0.0f, -1.0f};
        Vec3 right{1.0f, 0.0f, 0.0f};
        Vec3 up{0.0f, 1.0f, 0.0f};
        float verticalFovDegrees = 52.0f;
    };

    // Values in this record are copied only after the frame fence for the
    // corresponding slot has signaled. No mapped counter or query-pool result
    // is exposed while the GPU may still be writing it.
    struct Wave2RuntimeTelemetry final
    {
        bool available = false;
        TraversalBackend backend = TraversalBackend::GpuFlattenedSahBvh;
        std::uint64_t frameGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        double flattenedSahBuildMilliseconds = 0.0;
        double accelerationStructureBuildMilliseconds = 0.0;
        double traceMilliseconds = 0.0;
        std::uint32_t flattenedNodeCount = 0u;
        std::uint32_t flattenedTriangleCount = 0u;
        std::array<std::uint32_t, kWave2SoftwareTraversalCounterCount>
            softwareTraversalCounters{};
        bool softwareTraversalCountersAvailable = false;
        std::array<std::uint32_t, kWave2RuntimeCounterCount> megakernelCounters{};
        bool megakernelCountersAvailable = false;
        bool restirStatisticsAvailable = false;
        std::uint64_t restirGeneratedCandidates = 0u;
        std::uint64_t restirSubmittedVisibilityRays = 0u;
        std::uint64_t restirEvaluatedVisibilityRays = 0u;
        std::uint32_t wavefrontFatalMask = 0u;
        // At most one auxiliary signal is submitted beside Raw in a frame.
        // The selected-only policy avoids a known driver watchdog boundary
        // while keeping Raw and the visible signal on the same sample index.
        std::optional<std::size_t> signalAovIndex;
        std::string reason;
    };

    // Production Wave 2 owner. It composes the L4 flattened-SAH records, the
    // L5 BLAS/TLAS + Ray Query descriptors, and the L6 canonical Megakernel in
    // the application's existing Vulkan frame graph. The object never owns the
    // swapchain or output image; those remain with VulkanWhittedRenderer.
    class Wave2Runtime final
    {
    public:
        Wave2Runtime();
        ~Wave2Runtime();
        Wave2Runtime(const Wave2Runtime&) = delete;
        Wave2Runtime& operator=(const Wave2Runtime&) = delete;
        Wave2Runtime(Wave2Runtime&&) noexcept;
        Wave2Runtime& operator=(Wave2Runtime&&) noexcept;

        void Create(const Wave2RuntimeCreateInfo& createInfo);
        void SetScene(
            const Scene::CanonicalScene& scene,
            const Scene::ExperimentEnvironment* environment = nullptr,
            bool environmentEnabled = false);
        // Updates only rigid instance transforms. Topology, materials, lights,
        // stable IDs and buffer sizes must remain identical.
        void UpdateRigidTransforms(const Scene::CanonicalScene& scene);
        void SetOutput(VkImageView outputImageView, VkExtent2D outputExtent);
        void Reset() noexcept;
        void InvalidateWave3Histories() noexcept;
        void InvalidateProgressiveFilm() noexcept;

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] std::uint64_t ResourceGeneration() const noexcept;
        [[nodiscard]] std::uint64_t SceneGeneration() const noexcept;
        [[nodiscard]] bool UsesStagedRaw(
            ExecutionArchitecture architecture) const noexcept;
        [[nodiscard]] VkImage SignalImage(std::size_t index) const noexcept;
        [[nodiscard]] VkImageView SignalImageView(std::size_t index) const noexcept;

        void UpdateFrame(
            std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave2RuntimeCamera& camera,
            std::uint32_t sampleIndex,
            bool enableProfilerCounters,
            std::optional<std::size_t> signalAovIndex);
        void RecordFrame(
            VkCommandBuffer commandBuffer,
            std::uint32_t frameSlot,
            TraversalBackend backend);

        // Call only after the renderer has waited for the slot's in-flight
        // fence. The result is Fresh exactly once for each completed record.
        [[nodiscard]] Wave2RuntimeTelemetry CollectCompletedFrame(
            std::uint32_t frameSlot);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
