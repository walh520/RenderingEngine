#pragma once

#include "DeviceDispatch.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>

namespace RenderingEngine::Rt::Hardware
{
    class DeviceBuffer final
    {
    public:
        DeviceBuffer() = default;
        ~DeviceBuffer();

        DeviceBuffer(const DeviceBuffer&) = delete;
        DeviceBuffer& operator=(const DeviceBuffer&) = delete;
        DeviceBuffer(DeviceBuffer&& other) noexcept;
        DeviceBuffer& operator=(DeviceBuffer&& other) noexcept;

        void Reset() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkBuffer Handle() const noexcept;
        [[nodiscard]] VkDeviceMemory Memory() const noexcept;
        [[nodiscard]] VkDeviceSize Size() const noexcept;
        [[nodiscard]] VkDeviceAddress Address() const noexcept;
        [[nodiscard]] void* MappedData() const noexcept;

    private:
        friend class DeviceBufferAllocator;
        VkDevice device_{VK_NULL_HANDLE};
        VkBuffer buffer_{VK_NULL_HANDLE};
        VkDeviceMemory memory_{VK_NULL_HANDLE};
        VkDeviceSize size_{0u};
        VkDeviceAddress address_{0u};
        void* mappedData_{nullptr};
    };

    class DeviceBufferAllocator final
    {
    public:
        DeviceBufferAllocator(
            VkPhysicalDevice physicalDevice,
            VkDevice device,
            const DeviceDispatch& dispatch) noexcept;

        [[nodiscard]] Status Create(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags requiredMemoryProperties,
            bool requireDeviceAddress,
            bool persistentlyMapped,
            DeviceBuffer& output) const noexcept;

        [[nodiscard]] Status Upload(
            DeviceBuffer& destination,
            std::span<const std::byte> bytes,
            VkDeviceSize destinationOffset = 0u) const noexcept;

        [[nodiscard]] VkDevice Device() const noexcept;
        [[nodiscard]] const DeviceDispatch& Dispatch() const noexcept;

    private:
        [[nodiscard]] bool FindMemoryType(
            std::uint32_t memoryTypeBits,
            VkMemoryPropertyFlags requiredProperties,
            std::uint32_t& outputMemoryType) const noexcept;

        VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
        VkDevice device_{VK_NULL_HANDLE};
        const DeviceDispatch* dispatch_{nullptr};
        VkPhysicalDeviceMemoryProperties memoryProperties_{};
    };
}
