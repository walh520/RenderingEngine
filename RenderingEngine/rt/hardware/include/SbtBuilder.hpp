#pragma once

#include "DeviceBuffer.hpp"
#include "HardwareRtCapabilities.hpp"
#include "HardwareRtStatus.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Rt::Hardware
{
    struct SbtGroupCounts final
    {
        std::uint32_t rayGeneration{1u};
        std::uint32_t miss{1u};
        std::uint32_t hit{1u};
        std::uint32_t callable{0u};

        [[nodiscard]] std::uint32_t Total() const noexcept;
    };

    struct SbtRecordData final
    {
        std::span<const std::byte> inlineData{};
    };

    struct SbtComputedLayout final
    {
        VkDeviceSize recordStride{0u};
        VkDeviceSize rayGenerationOffset{0u};
        VkDeviceSize missOffset{0u};
        VkDeviceSize hitOffset{0u};
        VkDeviceSize callableOffset{0u};
        VkDeviceSize totalSize{0u};
        VkDeviceSize maxInlineDataSize{0u};
    };

    [[nodiscard]] Status ComputeSbtLayout(
        const HardwareRtLimits& limits,
        const SbtGroupCounts& counts,
        std::span<const SbtRecordData> records,
        SbtComputedLayout& output) noexcept;

    class ShaderBindingTable final
    {
    public:
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const VkStridedDeviceAddressRegionKHR& RayGenerationRegion() const noexcept;
        [[nodiscard]] const VkStridedDeviceAddressRegionKHR& MissRegion() const noexcept;
        [[nodiscard]] const VkStridedDeviceAddressRegionKHR& HitRegion() const noexcept;
        [[nodiscard]] const VkStridedDeviceAddressRegionKHR& CallableRegion() const noexcept;
        [[nodiscard]] const DeviceBuffer& Buffer() const noexcept;

    private:
        friend class SbtBuilder;
        DeviceBuffer buffer_{};
        VkStridedDeviceAddressRegionKHR rayGeneration_{};
        VkStridedDeviceAddressRegionKHR miss_{};
        VkStridedDeviceAddressRegionKHR hit_{};
        VkStridedDeviceAddressRegionKHR callable_{};
    };

    class SbtBuilder final
    {
    public:
        SbtBuilder(
            const DeviceBufferAllocator& allocator,
            const DeviceDispatch& dispatch,
            HardwareRtLimits limits) noexcept;

        [[nodiscard]] Status Build(
            VkPipeline pipeline,
            const SbtGroupCounts& counts,
            std::span<const SbtRecordData> records,
            ShaderBindingTable& output) const noexcept;

    private:
        const DeviceBufferAllocator* allocator_{nullptr};
        const DeviceDispatch* dispatch_{nullptr};
        HardwareRtLimits limits_{};
    };
}
