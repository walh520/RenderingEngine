#include "DeviceBuffer.hpp"

#include <cstring>
#include <utility>

namespace RenderingEngine::Rt::Hardware
{
    DeviceBuffer::~DeviceBuffer()
    {
        Reset();
    }

    DeviceBuffer::DeviceBuffer(DeviceBuffer&& other) noexcept
    {
        *this = std::move(other);
    }

    DeviceBuffer& DeviceBuffer::operator=(DeviceBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
            memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
            size_ = std::exchange(other.size_, 0u);
            address_ = std::exchange(other.address_, 0u);
            mappedData_ = std::exchange(other.mappedData_, nullptr);
        }
        return *this;
    }

    void DeviceBuffer::Reset() noexcept
    {
        if (device_ != VK_NULL_HANDLE && mappedData_ != nullptr)
        {
            vkUnmapMemory(device_, memory_);
        }
        if (device_ != VK_NULL_HANDLE && buffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, buffer_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, memory_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        size_ = 0u;
        address_ = 0u;
        mappedData_ = nullptr;
    }

    bool DeviceBuffer::IsValid() const noexcept { return buffer_ != VK_NULL_HANDLE; }
    VkBuffer DeviceBuffer::Handle() const noexcept { return buffer_; }
    VkDeviceMemory DeviceBuffer::Memory() const noexcept { return memory_; }
    VkDeviceSize DeviceBuffer::Size() const noexcept { return size_; }
    VkDeviceAddress DeviceBuffer::Address() const noexcept { return address_; }
    void* DeviceBuffer::MappedData() const noexcept { return mappedData_; }

    DeviceBufferAllocator::DeviceBufferAllocator(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        const DeviceDispatch& dispatch) noexcept
        : physicalDevice_(physicalDevice), device_(device), dispatch_(&dispatch)
    {
        if (physicalDevice_ != VK_NULL_HANDLE)
        {
            vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
        }
    }

    Status DeviceBufferAllocator::Create(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredMemoryProperties,
        bool requireDeviceAddress,
        bool persistentlyMapped,
        DeviceBuffer& output) const noexcept
    {
        if (device_ == VK_NULL_HANDLE || physicalDevice_ == VK_NULL_HANDLE || dispatch_ == nullptr || size == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid device, physical device, or buffer size");
        }

        DeviceBuffer created{};
        created.device_ = device_;
        created.size_ = size;

        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = usage | (requireDeviceAddress ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0u);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = vkCreateBuffer(device_, &bufferInfo, nullptr, &created.buffer_);
        if (result != VK_SUCCESS)
        {
            return Status::Failure(result, "vkCreateBuffer failed");
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device_, created.buffer_, &memoryRequirements);
        std::uint32_t memoryType = 0u;
        if (!FindMemoryType(memoryRequirements.memoryTypeBits, requiredMemoryProperties, memoryType))
        {
            return Status::Failure(VK_ERROR_FEATURE_NOT_PRESENT, "No compatible Vulkan memory type");
        }

        VkMemoryAllocateFlagsInfo allocationFlags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        allocationFlags.flags = requireDeviceAddress ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0u;
        VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocationInfo.pNext = requireDeviceAddress ? &allocationFlags : nullptr;
        allocationInfo.allocationSize = memoryRequirements.size;
        allocationInfo.memoryTypeIndex = memoryType;
        result = vkAllocateMemory(device_, &allocationInfo, nullptr, &created.memory_);
        if (result != VK_SUCCESS)
        {
            return Status::Failure(result, "vkAllocateMemory failed");
        }

        result = vkBindBufferMemory(device_, created.buffer_, created.memory_, 0u);
        if (result != VK_SUCCESS)
        {
            return Status::Failure(result, "vkBindBufferMemory failed");
        }

        if (persistentlyMapped)
        {
            if ((requiredMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u)
            {
                return Status::Failure(VK_ERROR_MEMORY_MAP_FAILED,
                    "Persistent mapping requires VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
            }
            result = vkMapMemory(device_, created.memory_, 0u, size, 0u, &created.mappedData_);
            if (result != VK_SUCCESS)
            {
                return Status::Failure(result, "vkMapMemory failed");
            }
        }

        if (requireDeviceAddress)
        {
            VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addressInfo.buffer = created.buffer_;
            created.address_ = dispatch_->getBufferDeviceAddress(device_, &addressInfo);
            if (created.address_ == 0u)
            {
                return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Buffer device address is zero");
            }
        }

        output = std::move(created);
        return Status::Success();
    }

    Status DeviceBufferAllocator::Upload(
        DeviceBuffer& destination,
        std::span<const std::byte> bytes,
        VkDeviceSize destinationOffset) const noexcept
    {
        if (!destination.IsValid() || destination.MappedData() == nullptr ||
            destinationOffset > destination.Size() || bytes.size_bytes() > destination.Size() - destinationOffset)
        {
            return Status::Failure(VK_ERROR_MEMORY_MAP_FAILED, "Upload destination is not mapped or range is invalid");
        }
        auto* destinationBytes = static_cast<std::byte*>(destination.MappedData());
        std::memcpy(destinationBytes + destinationOffset, bytes.data(), bytes.size_bytes());
        return Status::Success();
    }

    VkDevice DeviceBufferAllocator::Device() const noexcept { return device_; }
    const DeviceDispatch& DeviceBufferAllocator::Dispatch() const noexcept { return *dispatch_; }

    bool DeviceBufferAllocator::FindMemoryType(
        std::uint32_t memoryTypeBits,
        VkMemoryPropertyFlags requiredProperties,
        std::uint32_t& outputMemoryType) const noexcept
    {
        for (std::uint32_t index = 0u; index < memoryProperties_.memoryTypeCount; ++index)
        {
            const bool allowed = (memoryTypeBits & (1u << index)) != 0u;
            const VkMemoryPropertyFlags flags = memoryProperties_.memoryTypes[index].propertyFlags;
            if (allowed && (flags & requiredProperties) == requiredProperties)
            {
                outputMemoryType = index;
                return true;
            }
        }
        return false;
    }
}
