#pragma once

#include "contracts/AbiV1.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Rt::Gpu
{
    enum class GpuTraversalStatusCode : std::uint32_t
    {
        Success = 0u,
        InvalidArgument,
        Unsupported,
        MissingScene,
        RecordingFailed
    };

    struct GpuTraversalStatus
    {
        GpuTraversalStatusCode code = GpuTraversalStatusCode::Success;
        std::string message;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return code == GpuTraversalStatusCode::Success;
        }
    };

    struct GpuTraversalBackendDescriptor
    {
        std::string_view stableToken;
        std::string_view displayName;
        bool supportsClosest = false;
        bool supportsAny = false;
        bool requiresAccelerationStructure = false;
    };

    // Scene buffers are supplied through the canonical scene descriptor set.
    // Backend-private build inputs are configured when the concrete backend is
    // constructed; this request records only the shared lifetime identity.
    struct GpuSceneBuildRequest
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkDescriptorSet canonicalSceneSet = VK_NULL_HANDLE;
        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t sceneGeneration = 0u;
        bool topologyChanged = true;
    };

    // Rays and hits are bound through abi-v1 traversal-set bindings 1 and 2.
    // Offsets are record indices, never byte offsets. Fingerprint plus
    // generation must exactly identify the scene accepted by the preceding
    // BuildOrUpdateScene call.
    struct GpuTraceBatch
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkDescriptorSet canonicalSceneSet = VK_NULL_HANDLE;
        VkDescriptorSet traversalSet = VK_NULL_HANDLE;
        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t rayOffset = 0u;
        std::uint32_t hitOffset = 0u;
        std::uint32_t rayCount = 0u;
        std::uint32_t sceneGeneration = 0u;
    };

    class IGpuTraversalBackend
    {
    public:
        virtual ~IGpuTraversalBackend() = default;

        [[nodiscard]] virtual GpuTraversalBackendDescriptor Descriptor() const noexcept = 0;
        [[nodiscard]] virtual GpuTraversalStatus BuildOrUpdateScene(
            const GpuSceneBuildRequest& request) = 0;
        [[nodiscard]] virtual GpuTraversalStatus RecordTraceClosestBatch(
            const GpuTraceBatch& batch) = 0;
        [[nodiscard]] virtual GpuTraversalStatus RecordTraceAnyBatch(
            const GpuTraceBatch& batch) = 0;
    };

    using LegacyBuildCallback = GpuTraversalStatus(*)(
        void* userData,
        const GpuSceneBuildRequest& request);
    using LegacyTraceCallback = GpuTraversalStatus(*)(
        void* userData,
        const GpuTraceBatch& batch);

    struct LegacyAnalyticTraversalCallbacks
    {
        void* userData = nullptr;
        LegacyBuildCallback buildOrUpdateScene = nullptr;
        LegacyTraceCallback recordTraceClosestBatch = nullptr;
        LegacyTraceCallback recordTraceAnyBatch = nullptr;
    };

    // Adapts the existing analytic renderer's command-recording callbacks to
    // the shared interface without exposing renderer ownership to L4/L5/L6.
    class LegacyAnalyticTraversalAdapter final : public IGpuTraversalBackend
    {
    public:
        explicit LegacyAnalyticTraversalAdapter(
            LegacyAnalyticTraversalCallbacks callbacks) noexcept;

        [[nodiscard]] bool IsConfigured() const noexcept;
        [[nodiscard]] GpuTraversalBackendDescriptor Descriptor() const noexcept override;
        [[nodiscard]] GpuTraversalStatus BuildOrUpdateScene(
            const GpuSceneBuildRequest& request) override;
        [[nodiscard]] GpuTraversalStatus RecordTraceClosestBatch(
            const GpuTraceBatch& batch) override;
        [[nodiscard]] GpuTraversalStatus RecordTraceAnyBatch(
            const GpuTraceBatch& batch) override;

    private:
        LegacyAnalyticTraversalCallbacks callbacks_;
    };

    enum class MockTraversalOperation : std::uint32_t
    {
        BuildOrUpdateScene = 0u,
        TraceClosest,
        TraceAny
    };

    struct MockTraversalCall
    {
        MockTraversalOperation operation = MockTraversalOperation::BuildOrUpdateScene;
        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t sceneGeneration = 0u;
        std::uint32_t rayOffset = 0u;
        std::uint32_t hitOffset = 0u;
        std::uint32_t rayCount = 0u;
        bool topologyChanged = false;
    };

    struct GpuTraversalMockPolicy
    {
        // False keeps the mock usable by CPU-only integration harnesses. A
        // production command-recording audit can opt into strict handle checks.
        bool requireVulkanHandles = false;
    };

    class GpuTraversalBackendMock final : public IGpuTraversalBackend
    {
    public:
        explicit GpuTraversalBackendMock(GpuTraversalMockPolicy policy = {}) noexcept;

        [[nodiscard]] GpuTraversalBackendDescriptor Descriptor() const noexcept override;
        [[nodiscard]] GpuTraversalStatus BuildOrUpdateScene(
            const GpuSceneBuildRequest& request) override;
        [[nodiscard]] GpuTraversalStatus RecordTraceClosestBatch(
            const GpuTraceBatch& batch) override;
        [[nodiscard]] GpuTraversalStatus RecordTraceAnyBatch(
            const GpuTraceBatch& batch) override;

        [[nodiscard]] std::span<const MockTraversalCall> Calls() const noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] GpuTraversalStatus RecordTrace(
            MockTraversalOperation operation,
            const GpuTraceBatch& batch);

        GpuTraversalMockPolicy policy_;
        std::vector<MockTraversalCall> calls_;
        std::uint64_t sceneFingerprint_ = 0u;
        std::uint32_t sceneGeneration_ = 0u;
    };
}
