#pragma once

#include "rt/gpu/CanonicalTraversalScene.hpp"
#include "rt/software_gpu/SoftwareGpu.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace RenderingEngine::Rt::SoftwareGpu
{
    enum class GpuLbvhBuildFault : std::uint32_t
    {
        None = 0u,
        InvalidArgument,
        UnsupportedDevice,
        MissingShader,
        VulkanFailure,
        InvalidPrimitive,
        DuplicatePrimitiveId,
        MalformedHierarchy,
        DepthOverflow,
    };

    struct GpuLbvhBuildOwnerCreateInfo
    {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        std::uint32_t queueFamilyIndex = 0u;
        // The pool remains caller-owned and must permit primary command buffers.
        // Build is synchronous and frees every command buffer before returning.
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::filesystem::path shaderDirectory;
        std::uint32_t maximumDepth = 64u;
    };

    // The descriptor infos are ABI-v1 traversal-set compatible: nodes are
    // binding 0 and triangles are binding 3 in SoftwareGpuTraversalBindings.
    // Their buffers remain valid until the owner is reset, destroyed, or a
    // later Build call succeeds. A rejected rebuild preserves the last output.
    struct GpuLbvhBuildOutput
    {
        GpuLbvhBuildFault fault = GpuLbvhBuildFault::InvalidArgument;
        std::string message;
        VkDescriptorBufferInfo nodes{};
        VkDescriptorBufferInfo triangles{};
        std::uint32_t nodeCount = 0u;
        std::uint32_t triangleCount = 0u;
        std::uint32_t maximumDepth = 0u;
        std::uint64_t sceneFingerprint = 0u;
        std::uint32_t sceneGeneration = 0u;
        double buildGpuMilliseconds = 0.0;
        bool buildGpuTimestampMeasured = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return fault == GpuLbvhBuildFault::None;
        }

        [[nodiscard]] bool HasTraversalBuffers() const noexcept
        {
            return Succeeded() && nodes.buffer != VK_NULL_HANDLE &&
                triangles.buffer != VK_NULL_HANDLE;
        }
    };

    // Owns the complete device-side LBVH build path. CPU work is limited to
    // input upload and the scene AABB used by Morton normalization; Morton
    // generation, stable radix sorting, Karras hierarchy construction, leaf
    // emission, depth validation, and bottom-up bounds all execute on Vulkan.
    class GpuLbvhBuildOwner final
    {
    public:
        GpuLbvhBuildOwner();
        ~GpuLbvhBuildOwner();
        GpuLbvhBuildOwner(const GpuLbvhBuildOwner&) = delete;
        GpuLbvhBuildOwner& operator=(const GpuLbvhBuildOwner&) = delete;
        GpuLbvhBuildOwner(GpuLbvhBuildOwner&&) noexcept;
        GpuLbvhBuildOwner& operator=(GpuLbvhBuildOwner&&) noexcept;

        [[nodiscard]] GpuLbvhBuildOutput Create(
            const GpuLbvhBuildOwnerCreateInfo& createInfo) noexcept;
        [[nodiscard]] GpuLbvhBuildOutput Build(
            std::span<const SoftwarePrimitiveRecord> canonicalPrimitives,
            std::span<const Gpu::CanonicalTraversalTriangle> canonicalTriangles,
            std::uint64_t sceneFingerprint,
            std::uint32_t sceneGeneration) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool IsCreated() const noexcept;
        [[nodiscard]] const GpuLbvhBuildOutput& Output() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
