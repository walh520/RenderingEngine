#include "VulkanSmoke.hpp"

#include "rt/software_gpu/SoftwareGpu.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace RenderingEngine::Rt::SoftwareGpu::VulkanSmoke
{
    namespace
    {
        constexpr std::uint32_t kTraceCounterCount =
            static_cast<std::uint32_t>(kGpuTraversalCounterCount);
        static_assert(kGpuTraversalCounterCount == 9u);
        constexpr std::uint32_t kTraceThreads = 64u;
        constexpr std::uint32_t kRadixThreads = 128u;
        constexpr std::uint32_t kBuildDepthLimit = 64u;

        class VulkanError final : public std::runtime_error
        {
        public:
            VulkanError(const std::string& operation, const VkResult result)
                : std::runtime_error(operation + " failed with VkResult " +
                                     std::to_string(static_cast<std::int32_t>(result))),
                  result_(result)
            {
            }

            [[nodiscard]] VkResult Result() const noexcept
            {
                return result_;
            }

        private:
            VkResult result_{};
        };

        void Check(const VkResult result, const char* const operation)
        {
            if (result != VK_SUCCESS)
            {
                throw VulkanError(operation, result);
            }
        }

        [[nodiscard]] std::uint32_t DivideRoundUp(
            const std::uint32_t value,
            const std::uint32_t divisor) noexcept
        {
            return (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] bool HasName(
            const std::span<const VkLayerProperties> properties,
            const char* const name) noexcept
        {
            return std::any_of(properties.begin(), properties.end(), [name](const VkLayerProperties& item)
            {
                return std::strcmp(item.layerName, name) == 0;
            });
        }

        [[nodiscard]] bool HasName(
            const std::span<const VkExtensionProperties> properties,
            const char* const name) noexcept
        {
            return std::any_of(
                properties.begin(),
                properties.end(),
                [name](const VkExtensionProperties& item)
                {
                    return std::strcmp(item.extensionName, name) == 0;
                });
        }

        struct ValidationState
        {
            std::uint32_t warnings{};
            std::uint32_t errors{};
            std::vector<std::string> messages{};
        };

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            const VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* const data,
            void* const userData)
        {
            ValidationState& state = *static_cast<ValidationState*>(userData);
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u)
            {
                ++state.errors;
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0u)
            {
                ++state.warnings;
            }
            if (data != nullptr && data->pMessage != nullptr &&
                state.messages.size() < 64u)
            {
                state.messages.emplace_back(data->pMessage);
            }
            return VK_FALSE;
        }

        class Context final
        {
        public:
            Context(Report& report)
                : report_(report)
            {
                try
                {
                    CreateInstance();
                    SelectPhysicalDevice();
                    CreateDevice();
                    CreateCommandResources();
                    CreateEmptySetLayout();
                }
                catch (...)
                {
                    Destroy();
                    throw;
                }
            }

            Context(const Context&) = delete;
            Context& operator=(const Context&) = delete;

            ~Context()
            {
                Destroy();
            }

            [[nodiscard]] VkDevice Device() const noexcept { return device_; }
            [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept { return physicalDevice_; }
            [[nodiscard]] VkDescriptorSetLayout EmptySetLayout() const noexcept { return emptySetLayout_; }

            void Submit(const std::function<void(VkCommandBuffer)>& record)
            {
                Check(vkResetCommandPool(device_, commandPool_, 0u), "vkResetCommandPool");
                VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocateInfo.commandPool = commandPool_;
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = 1u;
                VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
                Check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
                      "vkAllocateCommandBuffers");

                VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
                record(commandBuffer);
                Check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

                VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submitInfo.commandBufferCount = 1u;
                submitInfo.pCommandBuffers = &commandBuffer;
                Check(vkQueueSubmit(queue_, 1u, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
                Check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
                vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
            }

            [[nodiscard]] std::uint32_t FindMemoryType(
                const std::uint32_t typeBits,
                const VkMemoryPropertyFlags required) const
            {
                VkPhysicalDeviceMemoryProperties properties{};
                vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &properties);
                for (std::uint32_t index = 0u; index < properties.memoryTypeCount; ++index)
                {
                    const bool supported = (typeBits & (1u << index)) != 0u;
                    const bool hasFlags =
                        (properties.memoryTypes[index].propertyFlags & required) == required;
                    if (supported && hasFlags)
                    {
                        return index;
                    }
                }
                throw std::runtime_error("no Vulkan memory type satisfies the requested flags");
            }

        private:
            void Destroy() noexcept
            {
                if (device_ != VK_NULL_HANDLE)
                {
                    static_cast<void>(vkDeviceWaitIdle(device_));
                    if (emptySetLayout_ != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorSetLayout(device_, emptySetLayout_, nullptr);
                        emptySetLayout_ = VK_NULL_HANDLE;
                    }
                    if (commandPool_ != VK_NULL_HANDLE)
                    {
                        vkDestroyCommandPool(device_, commandPool_, nullptr);
                        commandPool_ = VK_NULL_HANDLE;
                    }
                    vkDestroyDevice(device_, nullptr);
                    device_ = VK_NULL_HANDLE;
                    queue_ = VK_NULL_HANDLE;
                }
                if (debugMessenger_ != VK_NULL_HANDLE && destroyDebugMessenger_ != nullptr)
                {
                    destroyDebugMessenger_(instance_, debugMessenger_, nullptr);
                    debugMessenger_ = VK_NULL_HANDLE;
                }
                if (instance_ != VK_NULL_HANDLE)
                {
                    vkDestroyInstance(instance_, nullptr);
                    instance_ = VK_NULL_HANDLE;
                }
                PublishValidation();
            }
            void CreateInstance()
            {
                std::uint32_t layerCount{};
                Check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr),
                      "vkEnumerateInstanceLayerProperties(count)");
                std::vector<VkLayerProperties> layers(layerCount);
                Check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()),
                      "vkEnumerateInstanceLayerProperties(data)");
                constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
                report_.validationLayerAvailable = HasName(layers, kValidationLayer);

                std::uint32_t extensionCount{};
                Check(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
                      "vkEnumerateInstanceExtensionProperties(count)");
                std::vector<VkExtensionProperties> extensions(extensionCount);
                Check(vkEnumerateInstanceExtensionProperties(
                          nullptr,
                          &extensionCount,
                          extensions.data()),
                      "vkEnumerateInstanceExtensionProperties(data)");
                const bool hasDebugUtils = HasName(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                std::vector<VkExtensionProperties> validationExtensions{};
                if (report_.validationLayerAvailable)
                {
                    std::uint32_t validationExtensionCount{};
                    Check(vkEnumerateInstanceExtensionProperties(
                              kValidationLayer,
                              &validationExtensionCount,
                              nullptr),
                          "vkEnumerateInstanceExtensionProperties(validation count)");
                    validationExtensions.resize(validationExtensionCount);
                    Check(vkEnumerateInstanceExtensionProperties(
                              kValidationLayer,
                              &validationExtensionCount,
                              validationExtensions.data()),
                          "vkEnumerateInstanceExtensionProperties(validation data)");
                }
                const bool hasValidationFeatures =
                    HasName(extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) ||
                    HasName(validationExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

                std::vector<const char*> enabledLayers{};
                if (report_.validationLayerAvailable)
                {
                    enabledLayers.push_back(kValidationLayer);
                    report_.validationLayerEnabled = true;
                }
                std::vector<const char*> enabledExtensions{};
                if (hasDebugUtils)
                {
                    enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                }
                if (report_.validationLayerAvailable && hasValidationFeatures)
                {
                    enabledExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                    report_.synchronizationValidationEnabled = true;
                }

                VkDebugUtilsMessengerCreateInfoEXT debugInfo{
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debugInfo.pfnUserCallback = DebugCallback;
                debugInfo.pUserData = &validation_;

                constexpr VkValidationFeatureEnableEXT synchronizationValidation =
                    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
                VkValidationFeaturesEXT validationFeatures{
                    VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
                validationFeatures.enabledValidationFeatureCount = 1u;
                validationFeatures.pEnabledValidationFeatures = &synchronizationValidation;
                if (report_.synchronizationValidationEnabled)
                {
                    validationFeatures.pNext = hasDebugUtils ? &debugInfo : nullptr;
                }

                VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
                applicationInfo.pApplicationName = "RenderingEngine L4 Vulkan smoke";
                applicationInfo.applicationVersion = 1u;
                applicationInfo.pEngineName = "RenderingEngine";
                applicationInfo.engineVersion = 1u;
                applicationInfo.apiVersion = VK_API_VERSION_1_3;

                VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
                createInfo.pNext = report_.synchronizationValidationEnabled
                                       ? static_cast<const void*>(&validationFeatures)
                                       : (hasDebugUtils ? static_cast<const void*>(&debugInfo)
                                                        : nullptr);
                createInfo.pApplicationInfo = &applicationInfo;
                createInfo.enabledLayerCount = static_cast<std::uint32_t>(enabledLayers.size());
                createInfo.ppEnabledLayerNames = enabledLayers.data();
                createInfo.enabledExtensionCount =
                    static_cast<std::uint32_t>(enabledExtensions.size());
                createInfo.ppEnabledExtensionNames = enabledExtensions.data();
                Check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");

                if (hasDebugUtils)
                {
                    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
                    destroyDebugMessenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
                    if (createMessenger != nullptr && destroyDebugMessenger_ != nullptr)
                    {
                        Check(createMessenger(instance_, &debugInfo, nullptr, &debugMessenger_),
                              "vkCreateDebugUtilsMessengerEXT");
                    }
                }
            }

            void SelectPhysicalDevice()
            {
                std::uint32_t deviceCount{};
                Check(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
                      "vkEnumeratePhysicalDevices(count)");
                if (deviceCount == 0u)
                {
                    throw std::runtime_error("SKIP:no Vulkan physical device is available");
                }
                std::vector<VkPhysicalDevice> devices(deviceCount);
                Check(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
                      "vkEnumeratePhysicalDevices(data)");

                for (const VkPhysicalDevice device : devices)
                {
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(device, &properties);
                    if (properties.apiVersion < VK_API_VERSION_1_3 ||
                        properties.limits.maxComputeWorkGroupInvocations < kRadixThreads ||
                        properties.limits.maxComputeWorkGroupSize[0] < kRadixThreads)
                    {
                        continue;
                    }
                    std::uint32_t familyCount{};
                    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
                    std::vector<VkQueueFamilyProperties> families(familyCount);
                    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
                    for (std::uint32_t family = 0u; family < familyCount; ++family)
                    {
                        if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
                        {
                            physicalDevice_ = device;
                            queueFamily_ = family;
                            report_.deviceName = properties.deviceName;
                            return;
                        }
                    }
                }
                throw std::runtime_error(
                    "a Vulkan runtime exists, but no Vulkan 1.3 device supports 128-thread compute groups");
            }

            void CreateDevice()
            {
                constexpr float priority = 1.0f;
                VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                queueInfo.queueFamilyIndex = queueFamily_;
                queueInfo.queueCount = 1u;
                queueInfo.pQueuePriorities = &priority;
                VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                createInfo.queueCreateInfoCount = 1u;
                createInfo.pQueueCreateInfos = &queueInfo;
                Check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                      "vkCreateDevice");
                vkGetDeviceQueue(device_, queueFamily_, 0u, &queue_);
            }

            void CreateCommandResources()
            {
                VkCommandPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                                   VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                createInfo.queueFamilyIndex = queueFamily_;
                Check(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_),
                      "vkCreateCommandPool");
            }

            void CreateEmptySetLayout()
            {
                VkDescriptorSetLayoutCreateInfo createInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                Check(vkCreateDescriptorSetLayout(
                          device_,
                          &createInfo,
                          nullptr,
                          &emptySetLayout_),
                      "vkCreateDescriptorSetLayout(empty)");
            }

            Report& report_;
            ValidationState validation_{};
            VkInstance instance_{VK_NULL_HANDLE};
            VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
            VkDevice device_{VK_NULL_HANDLE};
            VkQueue queue_{VK_NULL_HANDLE};
            std::uint32_t queueFamily_{};
            VkCommandPool commandPool_{VK_NULL_HANDLE};
            VkDescriptorSetLayout emptySetLayout_{VK_NULL_HANDLE};
            VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
            PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger_{};

        public:
            void PublishValidation()
            {
                report_.validationWarnings = validation_.warnings;
                report_.validationErrors = validation_.errors;
                report_.validationMessages = validation_.messages;
            }
        };

        class Buffer final
        {
        public:
            Buffer(
                Context& context,
                const VkDeviceSize size,
                const VkBufferUsageFlags usage,
                const VkMemoryPropertyFlags properties)
                : context_(&context), size_(size), properties_(properties)
            {
                if (size == 0u)
                {
                    throw std::runtime_error("zero-sized Vulkan buffer requested");
                }
                VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bufferInfo.size = size;
                bufferInfo.usage = usage;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                Check(vkCreateBuffer(context.Device(), &bufferInfo, nullptr, &buffer_),
                      "vkCreateBuffer");

                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(context.Device(), buffer_, &requirements);
                VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocationInfo.allocationSize = requirements.size;
                allocationInfo.memoryTypeIndex =
                    context.FindMemoryType(requirements.memoryTypeBits, properties);
                Check(vkAllocateMemory(context.Device(), &allocationInfo, nullptr, &memory_),
                      "vkAllocateMemory");
                Check(vkBindBufferMemory(context.Device(), buffer_, memory_, 0u),
                      "vkBindBufferMemory");
            }

            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;

            ~Buffer()
            {
                if (context_ != nullptr)
                {
                    if (mapped_ != nullptr)
                    {
                        vkUnmapMemory(context_->Device(), memory_);
                    }
                    if (buffer_ != VK_NULL_HANDLE)
                    {
                        vkDestroyBuffer(context_->Device(), buffer_, nullptr);
                    }
                    if (memory_ != VK_NULL_HANDLE)
                    {
                        vkFreeMemory(context_->Device(), memory_, nullptr);
                    }
                }
            }

            [[nodiscard]] VkBuffer Handle() const noexcept { return buffer_; }
            [[nodiscard]] VkDeviceSize Size() const noexcept { return size_; }

            void Write(const void* const data, const std::size_t byteCount)
            {
                if (byteCount > size_ ||
                    (properties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u)
                {
                    throw std::runtime_error("invalid host write to Vulkan buffer");
                }
                EnsureMapped();
                std::memcpy(mapped_, data, byteCount);
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = memory_;
                range.offset = 0u;
                range.size = VK_WHOLE_SIZE;
                Check(vkFlushMappedMemoryRanges(context_->Device(), 1u, &range),
                      "vkFlushMappedMemoryRanges");
            }

            void Read(void* const data, const std::size_t byteCount)
            {
                if (byteCount > size_ ||
                    (properties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u)
                {
                    throw std::runtime_error("invalid host read from Vulkan buffer");
                }
                EnsureMapped();
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = memory_;
                range.offset = 0u;
                range.size = VK_WHOLE_SIZE;
                Check(vkInvalidateMappedMemoryRanges(context_->Device(), 1u, &range),
                      "vkInvalidateMappedMemoryRanges");
                std::memcpy(data, mapped_, byteCount);
            }

        private:
            void EnsureMapped()
            {
                if (mapped_ == nullptr)
                {
                    Check(vkMapMemory(
                              context_->Device(),
                              memory_,
                              0u,
                              VK_WHOLE_SIZE,
                              0u,
                              &mapped_),
                          "vkMapMemory");
                }
            }

            Context* context_{};
            VkDeviceSize size_{};
            VkMemoryPropertyFlags properties_{};
            VkBuffer buffer_{VK_NULL_HANDLE};
            VkDeviceMemory memory_{VK_NULL_HANDLE};
            void* mapped_{};
        };

        void BufferBarrier(
            const VkCommandBuffer commandBuffer,
            const Buffer& buffer,
            const VkAccessFlags sourceAccess,
            const VkAccessFlags destinationAccess,
            const VkPipelineStageFlags sourceStage,
            const VkPipelineStageFlags destinationStage)
        {
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = sourceAccess;
            barrier.dstAccessMask = destinationAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer.Handle();
            barrier.offset = 0u;
            barrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage,
                destinationStage,
                0u,
                0u,
                nullptr,
                1u,
                &barrier,
                0u,
                nullptr);
        }

        void ComputeBoundary(const VkCommandBuffer commandBuffer)
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                                    VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                                    VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,
                1u,
                &barrier,
                0u,
                nullptr,
                0u,
                nullptr);
        }

        [[nodiscard]] std::unique_ptr<Buffer> CreateDeviceBuffer(
            Context& context,
            const VkDeviceSize size)
        {
            return std::make_unique<Buffer>(
                context,
                size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        [[nodiscard]] std::unique_ptr<Buffer> CreateUniformBuffer(
            Context& context,
            const VkDeviceSize size)
        {
            return std::make_unique<Buffer>(
                context,
                size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        }

        void Upload(
            Context& context,
            Buffer& destination,
            const void* const data,
            const std::size_t byteCount)
        {
            Buffer staging(
                context,
                static_cast<VkDeviceSize>(byteCount),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            staging.Write(data, byteCount);
            context.Submit([&](const VkCommandBuffer commandBuffer)
            {
                VkBufferCopy copy{};
                copy.size = static_cast<VkDeviceSize>(byteCount);
                vkCmdCopyBuffer(
                    commandBuffer,
                    staging.Handle(),
                    destination.Handle(),
                    1u,
                    &copy);
                BufferBarrier(
                    commandBuffer,
                    destination,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            });
        }

        template <typename T>
        void UploadVector(Context& context, Buffer& destination, const std::vector<T>& values)
        {
            Upload(context, destination, values.data(), values.size() * sizeof(T));
        }

        void Download(
            Context& context,
            const Buffer& source,
            void* const data,
            const std::size_t byteCount)
        {
            Buffer staging(
                context,
                static_cast<VkDeviceSize>(byteCount),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            context.Submit([&](const VkCommandBuffer commandBuffer)
            {
                BufferBarrier(
                    commandBuffer,
                    source,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
                VkBufferCopy copy{};
                copy.size = static_cast<VkDeviceSize>(byteCount);
                vkCmdCopyBuffer(
                    commandBuffer,
                    source.Handle(),
                    staging.Handle(),
                    1u,
                    &copy);
                BufferBarrier(
                    commandBuffer,
                    staging,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_HOST_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT);
            });
            staging.Read(data, byteCount);
        }

        template <typename T>
        [[nodiscard]] std::vector<T> DownloadVector(
            Context& context,
            const Buffer& source,
            const std::size_t count)
        {
            std::vector<T> values(count);
            Download(context, source, values.data(), values.size() * sizeof(T));
            return values;
        }

        [[nodiscard]] std::vector<std::uint32_t> LoadSpirv(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw std::runtime_error("missing SPIR-V file: " + path.string());
            }
            const std::streampos end = stream.tellg();
            if (end <= 0 || (static_cast<std::uint64_t>(end) % sizeof(std::uint32_t)) != 0u)
            {
                throw std::runtime_error("malformed SPIR-V byte count: " + path.string());
            }
            std::vector<std::uint32_t> words(
                static_cast<std::size_t>(end) / sizeof(std::uint32_t));
            stream.seekg(0, std::ios::beg);
            stream.read(
                reinterpret_cast<char*>(words.data()),
                static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
            if (!stream)
            {
                throw std::runtime_error("failed to read SPIR-V file: " + path.string());
            }
            return words;
        }

        struct DescriptorResource
        {
            Buffer* buffer{};
            VkDescriptorType type{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        };

        class ComputeKernel final
        {
        public:
            ComputeKernel(
                Context& context,
                const std::filesystem::path& shaderPath,
                const char* const entryPoint,
                const std::span<const VkDescriptorType> descriptorTypes)
                : context_(&context)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings{};
                bindings.reserve(descriptorTypes.size());
                std::uint32_t storageCount{};
                std::uint32_t uniformCount{};
                for (std::uint32_t index = 0u; index < descriptorTypes.size(); ++index)
                {
                    VkDescriptorSetLayoutBinding binding{};
                    binding.binding = index;
                    binding.descriptorType = descriptorTypes[index];
                    binding.descriptorCount = 1u;
                    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    bindings.push_back(binding);
                    if (descriptorTypes[index] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    {
                        ++uniformCount;
                    }
                    else
                    {
                        ++storageCount;
                    }
                }

                VkDescriptorSetLayoutCreateInfo setLayoutInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                setLayoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
                setLayoutInfo.pBindings = bindings.data();
                Check(vkCreateDescriptorSetLayout(
                          context.Device(),
                          &setLayoutInfo,
                          nullptr,
                          &setLayout_),
                      "vkCreateDescriptorSetLayout(kernel)");

                const std::array setLayouts{
                    context.EmptySetLayout(),
                    context.EmptySetLayout(),
                    setLayout_};
                VkPipelineLayoutCreateInfo pipelineLayoutInfo{
                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
                pipelineLayoutInfo.pSetLayouts = setLayouts.data();
                Check(vkCreatePipelineLayout(
                          context.Device(),
                          &pipelineLayoutInfo,
                          nullptr,
                          &pipelineLayout_),
                      "vkCreatePipelineLayout");

                const std::vector<std::uint32_t> code = LoadSpirv(shaderPath);
                VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
                moduleInfo.pCode = code.data();
                VkShaderModule module = VK_NULL_HANDLE;
                Check(vkCreateShaderModule(context.Device(), &moduleInfo, nullptr, &module),
                      "vkCreateShaderModule");

                VkPipelineShaderStageCreateInfo stageInfo{
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                stageInfo.module = module;
                stageInfo.pName = entryPoint;
                VkComputePipelineCreateInfo pipelineInfo{
                    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                pipelineInfo.stage = stageInfo;
                pipelineInfo.layout = pipelineLayout_;
                const VkResult pipelineResult = vkCreateComputePipelines(
                    context.Device(),
                    VK_NULL_HANDLE,
                    1u,
                    &pipelineInfo,
                    nullptr,
                    &pipeline_);
                vkDestroyShaderModule(context.Device(), module, nullptr);
                Check(pipelineResult, "vkCreateComputePipelines");

                std::vector<VkDescriptorPoolSize> poolSizes{};
                if (storageCount != 0u)
                {
                    poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageCount});
                }
                if (uniformCount != 0u)
                {
                    poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformCount});
                }
                VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                poolInfo.maxSets = 1u;
                poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();
                Check(vkCreateDescriptorPool(
                          context.Device(),
                          &poolInfo,
                          nullptr,
                          &descriptorPool_),
                      "vkCreateDescriptorPool");

                VkDescriptorSetAllocateInfo allocateInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocateInfo.descriptorPool = descriptorPool_;
                allocateInfo.descriptorSetCount = 1u;
                allocateInfo.pSetLayouts = &setLayout_;
                Check(vkAllocateDescriptorSets(context.Device(), &allocateInfo, &descriptorSet_),
                      "vkAllocateDescriptorSets");
            }

            ComputeKernel(const ComputeKernel&) = delete;
            ComputeKernel& operator=(const ComputeKernel&) = delete;

            ~ComputeKernel()
            {
                if (context_ != nullptr)
                {
                    if (descriptorPool_ != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorPool(context_->Device(), descriptorPool_, nullptr);
                    }
                    if (pipeline_ != VK_NULL_HANDLE)
                    {
                        vkDestroyPipeline(context_->Device(), pipeline_, nullptr);
                    }
                    if (pipelineLayout_ != VK_NULL_HANDLE)
                    {
                        vkDestroyPipelineLayout(context_->Device(), pipelineLayout_, nullptr);
                    }
                    if (setLayout_ != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorSetLayout(context_->Device(), setLayout_, nullptr);
                    }
                }
            }

            void Bind(const std::span<const DescriptorResource> resources)
            {
                std::vector<VkDescriptorBufferInfo> infos(resources.size());
                std::vector<VkWriteDescriptorSet> writes(resources.size());
                for (std::uint32_t index = 0u; index < resources.size(); ++index)
                {
                    if (resources[index].buffer == nullptr)
                    {
                        throw std::runtime_error("null buffer in descriptor update");
                    }
                    infos[index].buffer = resources[index].buffer->Handle();
                    infos[index].offset = 0u;
                    infos[index].range = resources[index].buffer->Size();
                    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[index].dstSet = descriptorSet_;
                    writes[index].dstBinding = index;
                    writes[index].descriptorCount = 1u;
                    writes[index].descriptorType = resources[index].type;
                    writes[index].pBufferInfo = &infos[index];
                }
                vkUpdateDescriptorSets(
                    context_->Device(),
                    static_cast<std::uint32_t>(writes.size()),
                    writes.data(),
                    0u,
                    nullptr);
            }

            void Dispatch(const std::uint32_t groupCountX)
            {
                if (groupCountX == 0u)
                {
                    throw std::runtime_error("zero-group compute dispatch requested");
                }
                context_->Submit([&](const VkCommandBuffer commandBuffer)
                {
                    ComputeBoundary(commandBuffer);
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipelineLayout_,
                        2u,
                        1u,
                        &descriptorSet_,
                        0u,
                        nullptr);
                    vkCmdDispatch(commandBuffer, groupCountX, 1u, 1u);
                });
            }

        private:
            Context* context_{};
            VkDescriptorSetLayout setLayout_{VK_NULL_HANDLE};
            VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
            VkPipeline pipeline_{VK_NULL_HANDLE};
            VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
            VkDescriptorSet descriptorSet_{VK_NULL_HANDLE};
        };

        struct alignas(16) TraceConfig
        {
            std::uint32_t nodeCount{};
            std::uint32_t primitiveCount{};
            std::uint32_t rayCount{};
            std::uint32_t stackCapacity{64u};
        };

        struct alignas(16) MortonConfig
        {
            Float4 sceneBoundsMin{};
            Float4 sceneBoundsMax{};
            std::uint32_t primitiveCount{};
            std::array<std::uint32_t, 3u> reserved{};
        };

        struct alignas(16) RadixConfig
        {
            std::uint32_t elementCount{};
            std::uint32_t groupCount{};
            std::uint32_t shift{};
            std::uint32_t field{};
        };

        struct alignas(16) HierarchyConfig
        {
            std::uint32_t primitiveCount{};
            std::uint32_t nodeCount{};
            std::array<std::uint32_t, 2u> reserved{};
        };

        struct alignas(16) BoundsConfig
        {
            std::uint32_t primitiveCount{};
            std::uint32_t nodeCount{};
            std::uint32_t currentDepth{};
            std::uint32_t maximumDepthLimit{kBuildDepthLimit};
        };

        static_assert(sizeof(TraceConfig) == 16u);
        static_assert(sizeof(MortonConfig) == 48u);
        static_assert(sizeof(RadixConfig) == 16u);
        static_assert(sizeof(HierarchyConfig) == 16u);
        static_assert(sizeof(BoundsConfig) == 16u);

        [[nodiscard]] std::vector<SoftwarePrimitiveRecord> MakeFixedPrimitives()
        {
            constexpr std::array<std::uint32_t, 8u> stableIds{
                41u,
                3u,
                77u,
                12u,
                8u,
                55u,
                1u,
                29u};
            std::vector<SoftwarePrimitiveRecord> primitives{};
            primitives.reserve(stableIds.size());
            for (std::uint32_t index = 0u; index < stableIds.size(); ++index)
            {
                const float x = static_cast<float>(index % 4u) * 2.0f;
                const float y = static_cast<float>(index / 4u) * 2.0f;
                primitives.push_back(MakeTriangle(
                    {x - 0.45f, y - 0.45f, 0.0f},
                    {x + 0.45f, y - 0.45f, 0.0f},
                    {x, y + 0.45f, 0.0f},
                    stableIds[index]));
            }
            return primitives;
        }

        [[nodiscard]] std::vector<SoftwareRayRecord> MakeFixedRays()
        {
            std::vector<SoftwareRayRecord> rays{};
            rays.reserve(11u);
            for (std::uint32_t index = 0u; index < 8u; ++index)
            {
                const float x = static_cast<float>(index % 4u) * 2.0f;
                const float y = static_cast<float>(index / 4u) * 2.0f;
                rays.push_back(MakeRay(
                    {x, y, 2.0f},
                    {0.0f, 0.0f, -1.0f},
                    0.001f,
                    100.0f,
                    100u + index,
                    QueryMode::Closest));
            }
            rays.push_back(MakeRay(
                {100.0f, 100.0f, 2.0f},
                {0.0f, 0.0f, -1.0f},
                0.001f,
                100.0f,
                900u,
                QueryMode::Closest));
            rays.push_back(MakeRay(
                {0.0f, 0.0f, -2.0f},
                {0.0f, 0.0f, 1.0f},
                0.001f,
                100.0f,
                901u,
                QueryMode::Closest));
            rays.push_back(MakeRay(
                {-4.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f},
                0.001f,
                100.0f,
                902u,
                QueryMode::Closest));
            return rays;
        }

        [[nodiscard]] Aabb FixedSceneBounds(
            const std::span<const SoftwarePrimitiveRecord> primitives)
        {
            if (primitives.empty())
            {
                throw std::runtime_error("fixed GPU scene is empty");
            }
            Aabb result = PrimitiveBounds(primitives.front());
            for (std::size_t index = 1u; index < primitives.size(); ++index)
            {
                const Aabb bounds = PrimitiveBounds(primitives[index]);
                result.minimum.x = (std::min)(result.minimum.x, bounds.minimum.x);
                result.minimum.y = (std::min)(result.minimum.y, bounds.minimum.y);
                result.minimum.z = (std::min)(result.minimum.z, bounds.minimum.z);
                result.maximum.x = (std::max)(result.maximum.x, bounds.maximum.x);
                result.maximum.y = (std::max)(result.maximum.y, bounds.maximum.y);
                result.maximum.z = (std::max)(result.maximum.z, bounds.maximum.z);
            }
            return result;
        }

        void RequireZeroCounter(
            Context& context,
            const Buffer& counter,
            const char* const stage)
        {
            const std::vector<std::uint32_t> values =
                DownloadVector<std::uint32_t>(context, counter, 1u);
            if (values[0] != 0u)
            {
                throw std::runtime_error(
                    std::string(stage) + " validation counter is " +
                    std::to_string(values[0]));
            }
        }

        [[nodiscard]] PathEvidence ExecuteTrace(
            Context& context,
            const std::filesystem::path& shaderDirectory,
            Buffer& nodes,
            Buffer& primitives,
            const std::uint32_t nodeCount,
            const std::uint32_t primitiveCount,
            const std::span<const SoftwarePrimitiveRecord> cpuPrimitives,
            const std::span<const SoftwareRayRecord> rays,
            const std::uint32_t maximumDepth,
            const std::uint64_t precedingUploadBytes)
        {
            PathEvidence evidence{};
            evidence.buildPassed = true;
            evidence.maximumDepth = maximumDepth;
            evidence.rayCount = static_cast<std::uint32_t>(rays.size());
            evidence.uploadBytes = precedingUploadBytes;

            auto rayBuffer = CreateDeviceBuffer(context, rays.size_bytes());
            auto hitBuffer = CreateDeviceBuffer(
                context,
                rays.size() * sizeof(SoftwareHitRecord));
            auto counterBuffer = CreateDeviceBuffer(
                context,
                kTraceCounterCount * sizeof(std::uint32_t));
            auto configBuffer = CreateUniformBuffer(context, sizeof(TraceConfig));
            Upload(context, *rayBuffer, rays.data(), rays.size_bytes());
            evidence.uploadBytes += rays.size_bytes();
            const TraceConfig config{
                nodeCount,
                primitiveCount,
                static_cast<std::uint32_t>(rays.size()),
                64u};
            configBuffer->Write(&config, sizeof(config));

            constexpr std::array descriptorTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
            ComputeKernel reset(
                context,
                shaderDirectory / "software_trace_reset.spv",
                "ResetCountersCS",
                descriptorTypes);
            ComputeKernel trace(
                context,
                shaderDirectory / "software_trace.spv",
                "CSMain",
                descriptorTypes);
            const std::array resources{
                DescriptorResource{&nodes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{&primitives, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{rayBuffer.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{hitBuffer.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{counterBuffer.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{configBuffer.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}};
            reset.Bind(resources);
            trace.Bind(resources);
            reset.Dispatch(1u);
            trace.Dispatch(DivideRoundUp(static_cast<std::uint32_t>(rays.size()), kTraceThreads));

            const std::vector<SoftwareHitRecord> gpuHits = DownloadVector<SoftwareHitRecord>(
                context,
                *hitBuffer,
                rays.size());
            const std::vector<std::uint32_t> counters = DownloadVector<std::uint32_t>(
                context,
                *counterBuffer,
                kTraceCounterCount);
            std::copy(
                counters.begin(),
                counters.end(),
                evidence.counters.slots.begin());
            evidence.readbackBytes = gpuHits.size() * sizeof(SoftwareHitRecord) +
                                     counters.size() * sizeof(std::uint32_t);

            for (std::size_t index = 0u; index < rays.size(); ++index)
            {
                const TraceResult expected =
                    BruteForce(cpuPrimitives, rays[index], QueryMode::Closest);
                if (!expected.Succeeded())
                {
                    throw std::runtime_error("CPU brute-force oracle rejected a fixed ray");
                }
                const SoftwareHitRecord& actual = gpuHits[index];
                const bool actualHit = actual.identity.x != kInvalidIndex;
                if (actual.identity.z != 0u || actual.identity.y != rays[index].query.x ||
                    actualHit != expected.IsHit())
                {
                    throw std::runtime_error(
                        "GPU trace hit/status/ray identity differs at ray " +
                        std::to_string(index));
                }
                if (actualHit)
                {
                    const float tolerance = (std::max)(
                        1.0e-5f,
                        std::fabs(expected.hit.tBary.x) * 1.0e-5f);
                    if (actual.identity.x != expected.hit.identity.x ||
                        std::fabs(actual.tBary.x - expected.hit.tBary.x) > tolerance ||
                        std::fabs(actual.tBary.y - expected.hit.tBary.y) > 1.0e-5f ||
                        std::fabs(actual.tBary.z - expected.hit.tBary.z) > 1.0e-5f)
                    {
                        throw std::runtime_error(
                            "GPU trace primitive/t/barycentric parity differs at ray " +
                            std::to_string(index));
                    }
                    ++evidence.hitCount;
                }
            }
            const TraversalCounters decoded = DecodeGpuTraversalCounters(
                evidence.counters,
                evidence.rayCount,
                evidence.hitCount);
            if (decoded.nodeTests == 0u || decoded.triangleTests == 0u ||
                decoded.stackOverflows != 0u || decoded.invalidRays != 0u ||
                decoded.invalidHits != 0u)
            {
                throw std::runtime_error("GPU traversal counters report missing work or an error");
            }
            evidence.tracePassed = true;
            return evidence;
        }

        [[nodiscard]] PathEvidence RunFlattenedSah(
            Context& context,
            const std::filesystem::path& shaderDirectory,
            const std::span<const SoftwarePrimitiveRecord> primitives,
            const std::span<const SoftwareRayRecord> rays)
        {
            const FlatBuildResult build = BuildFlattenedSah(primitives, {1u, kBuildDepthLimit});
            if (!build.Succeeded() || build.bvh.nodes.empty())
            {
                throw std::runtime_error("CPU flattened SAH fixture build failed");
            }
            auto nodes = CreateDeviceBuffer(
                context,
                build.bvh.nodes.size() * sizeof(SoftwareNodeRecord));
            auto reordered = CreateDeviceBuffer(
                context,
                build.bvh.primitives.size() * sizeof(SoftwarePrimitiveRecord));
            UploadVector(context, *nodes, build.bvh.nodes);
            UploadVector(context, *reordered, build.bvh.primitives);
            const std::uint64_t uploadBytes =
                build.bvh.nodes.size() * sizeof(SoftwareNodeRecord) +
                build.bvh.primitives.size() * sizeof(SoftwarePrimitiveRecord);
            return ExecuteTrace(
                context,
                shaderDirectory,
                *nodes,
                *reordered,
                static_cast<std::uint32_t>(build.bvh.nodes.size()),
                static_cast<std::uint32_t>(build.bvh.primitives.size()),
                primitives,
                rays,
                build.bvh.maximumDepth,
                uploadBytes);
        }

        [[nodiscard]] PathEvidence RunGpuLbvh(
            Context& context,
            const std::filesystem::path& shaderDirectory,
            const std::span<const SoftwarePrimitiveRecord> primitives,
            const std::span<const SoftwareRayRecord> rays)
        {
            const std::uint32_t primitiveCount = static_cast<std::uint32_t>(primitives.size());
            if (primitiveCount < 2u)
            {
                throw std::runtime_error("GPU LBVH smoke requires at least two primitives");
            }
            const std::uint32_t nodeCount = (primitiveCount * 2u) - 1u;
            const std::uint32_t radixGroupCount = DivideRoundUp(primitiveCount, kRadixThreads);

            auto sourcePrimitives = CreateDeviceBuffer(
                context,
                primitives.size_bytes());
            auto pairA = CreateDeviceBuffer(
                context,
                primitiveCount * sizeof(MortonPair));
            auto pairB = CreateDeviceBuffer(
                context,
                primitiveCount * sizeof(MortonPair));
            auto histogram = CreateDeviceBuffer(
                context,
                radixGroupCount * 16u * sizeof(std::uint32_t));
            auto offsets = CreateDeviceBuffer(
                context,
                radixGroupCount * 16u * sizeof(std::uint32_t));
            auto mortonInvalid = CreateDeviceBuffer(context, sizeof(std::uint32_t));
            auto stableIdInvalid = CreateDeviceBuffer(context, sizeof(std::uint32_t));
            auto mortonConfig = CreateUniformBuffer(context, sizeof(MortonConfig));
            auto radixConfig = CreateUniformBuffer(context, sizeof(RadixConfig));
            Upload(context, *sourcePrimitives, primitives.data(), primitives.size_bytes());

            const Aabb sceneBounds = FixedSceneBounds(primitives);
            const MortonConfig mortonSettings{
                {sceneBounds.minimum.x, sceneBounds.minimum.y, sceneBounds.minimum.z, 0.0f},
                {sceneBounds.maximum.x, sceneBounds.maximum.y, sceneBounds.maximum.z, 0.0f},
                primitiveCount,
                {0u, 0u, 0u}};
            mortonConfig->Write(&mortonSettings, sizeof(mortonSettings));
            constexpr std::array mortonTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
            ComputeKernel mortonReset(
                context,
                shaderDirectory / "software_lbvh_morton_reset.spv",
                "ResetMortonValidationCS",
                mortonTypes);
            ComputeKernel morton(
                context,
                shaderDirectory / "software_lbvh_morton.spv",
                "CSMain",
                mortonTypes);
            const std::array mortonResources{
                DescriptorResource{sourcePrimitives.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{pairA.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{mortonConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                DescriptorResource{mortonInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
            mortonReset.Bind(mortonResources);
            morton.Bind(mortonResources);
            mortonReset.Dispatch(1u);
            morton.Dispatch(DivideRoundUp(primitiveCount, kTraceThreads));
            RequireZeroCounter(context, *mortonInvalid, "Morton");

            constexpr std::array radixTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
            ComputeKernel histogramKernel(
                context,
                shaderDirectory / "software_lbvh_radix_histogram.spv",
                "HistogramCS",
                radixTypes);
            ComputeKernel prefixKernel(
                context,
                shaderDirectory / "software_lbvh_radix_prefix.spv",
                "PrefixCS",
                radixTypes);
            ComputeKernel scatterKernel(
                context,
                shaderDirectory / "software_lbvh_radix_scatter.spv",
                "ScatterCS",
                radixTypes);
            ComputeKernel stableReset(
                context,
                shaderDirectory / "software_lbvh_radix_validate_reset.spv",
                "ResetStableIdValidationCS",
                radixTypes);
            ComputeKernel stableValidate(
                context,
                shaderDirectory / "software_lbvh_radix_validate.spv",
                "ValidateStableIdsCS",
                radixTypes);

            Buffer* radixInput = pairA.get();
            Buffer* radixOutput = pairB.get();
            const auto bindRadix = [&](ComputeKernel& kernel)
            {
                const std::array resources{
                    DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{radixOutput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{histogram.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{offsets.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{radixConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                    DescriptorResource{stableIdInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
                kernel.Bind(resources);
            };
            const auto radixPass = [&](const std::uint32_t shift, const std::uint32_t field)
            {
                const RadixConfig settings{primitiveCount, radixGroupCount, shift, field};
                radixConfig->Write(&settings, sizeof(settings));
                bindRadix(histogramKernel);
                bindRadix(prefixKernel);
                bindRadix(scatterKernel);
                histogramKernel.Dispatch(radixGroupCount);
                prefixKernel.Dispatch(1u);
                scatterKernel.Dispatch(radixGroupCount);
                std::swap(radixInput, radixOutput);
            };

            for (std::uint32_t shift = 0u; shift < 32u; shift += 4u)
            {
                radixPass(shift, 0u);
            }
            const RadixConfig validateSettings{primitiveCount, radixGroupCount, 0u, 0u};
            radixConfig->Write(&validateSettings, sizeof(validateSettings));
            bindRadix(stableReset);
            bindRadix(stableValidate);
            stableReset.Dispatch(1u);
            stableValidate.Dispatch(radixGroupCount);
            RequireZeroCounter(context, *stableIdInvalid, "stable-ID radix");
            for (std::uint32_t shift = 0u; shift < 32u; shift += 4u)
            {
                radixPass(shift, 1u);
            }

            BuildStatus cpuSortStatus{};
            const std::vector<MortonPair> expectedPairs =
                StableRadixSortMorton(primitives, &cpuSortStatus);
            const std::vector<MortonPair> actualPairs = DownloadVector<MortonPair>(
                context,
                *radixInput,
                primitiveCount);
            if (cpuSortStatus != BuildStatus::Success || actualPairs != expectedPairs)
            {
                throw std::runtime_error("GPU Morton/radix full-key order differs from CPU");
            }

            auto nodes = CreateDeviceBuffer(
                context,
                nodeCount * sizeof(SoftwareNodeRecord));
            auto parents = CreateDeviceBuffer(
                context,
                nodeCount * sizeof(std::uint32_t));
            auto hierarchyInvalid = CreateDeviceBuffer(context, sizeof(std::uint32_t));
            auto hierarchyConfig = CreateUniformBuffer(context, sizeof(HierarchyConfig));
            const HierarchyConfig hierarchySettings{
                primitiveCount,
                nodeCount,
                {0u, 0u}};
            hierarchyConfig->Write(&hierarchySettings, sizeof(hierarchySettings));
            constexpr std::array hierarchyTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
            ComputeKernel hierarchyReset(
                context,
                shaderDirectory / "software_lbvh_reset.spv",
                "ResetCS",
                hierarchyTypes);
            ComputeKernel hierarchy(
                context,
                shaderDirectory / "software_lbvh_hierarchy.spv",
                "HierarchyCS",
                hierarchyTypes);
            const std::array hierarchyResources{
                DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{nodes.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{parents.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{hierarchyConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                DescriptorResource{hierarchyInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
            hierarchyReset.Bind(hierarchyResources);
            hierarchy.Bind(hierarchyResources);
            hierarchyReset.Dispatch(DivideRoundUp(nodeCount, kTraceThreads));
            hierarchy.Dispatch(DivideRoundUp(primitiveCount - 1u, kTraceThreads));
            RequireZeroCounter(context, *hierarchyInvalid, "Karras hierarchy");

            auto sortedPrimitives = CreateDeviceBuffer(
                context,
                primitiveCount * sizeof(SoftwarePrimitiveRecord));
            auto depths = CreateDeviceBuffer(
                context,
                nodeCount * sizeof(std::uint32_t));
            auto maximumDepth = CreateDeviceBuffer(context, sizeof(std::uint32_t));
            auto boundsConfig = CreateUniformBuffer(context, sizeof(BoundsConfig));
            BoundsConfig boundsSettings{
                primitiveCount,
                nodeCount,
                0u,
                kBuildDepthLimit};
            boundsConfig->Write(&boundsSettings, sizeof(boundsSettings));
            constexpr std::array boundsTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
            ComputeKernel boundsReset(
                context,
                shaderDirectory / "software_lbvh_bounds_reset.spv",
                "ResetBoundsValidationCS",
                boundsTypes);
            ComputeKernel emitLeaves(
                context,
                shaderDirectory / "software_lbvh_emit_leaves.spv",
                "EmitLeavesCS",
                boundsTypes);
            ComputeKernel computeDepths(
                context,
                shaderDirectory / "software_lbvh_depths.spv",
                "ComputeDepthsCS",
                boundsTypes);
            ComputeKernel internalBounds(
                context,
                shaderDirectory / "software_lbvh_internal_bounds.spv",
                "InternalBoundsCS",
                boundsTypes);
            const std::array boundsResources{
                DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{sourcePrimitives.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{sortedPrimitives.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{nodes.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{parents.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{depths.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{maximumDepth.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{hierarchyInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                DescriptorResource{boundsConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}};
            boundsReset.Bind(boundsResources);
            emitLeaves.Bind(boundsResources);
            computeDepths.Bind(boundsResources);
            internalBounds.Bind(boundsResources);
            boundsReset.Dispatch(1u);
            emitLeaves.Dispatch(DivideRoundUp(primitiveCount, kTraceThreads));
            computeDepths.Dispatch(DivideRoundUp(nodeCount, kTraceThreads));
            RequireZeroCounter(context, *hierarchyInvalid, "LBVH depth");
            const std::vector<std::uint32_t> maximumDepthValue =
                DownloadVector<std::uint32_t>(context, *maximumDepth, 1u);
            if (maximumDepthValue[0] == 0u || maximumDepthValue[0] > kBuildDepthLimit)
            {
                throw std::runtime_error("GPU LBVH maximum depth is invalid");
            }
            for (std::uint32_t depth = maximumDepthValue[0];; --depth)
            {
                boundsSettings.currentDepth = depth;
                boundsConfig->Write(&boundsSettings, sizeof(boundsSettings));
                internalBounds.Dispatch(DivideRoundUp(primitiveCount - 1u, kTraceThreads));
                if (depth == 0u)
                {
                    break;
                }
            }
            RequireZeroCounter(context, *hierarchyInvalid, "LBVH bottom-up bounds");

            const std::uint64_t buildUploadBytes = primitives.size_bytes();
            PathEvidence evidence = ExecuteTrace(
                context,
                shaderDirectory,
                *nodes,
                *sortedPrimitives,
                nodeCount,
                primitiveCount,
                primitives,
                rays,
                maximumDepthValue[0] + 1u,
                buildUploadBytes);
            evidence.readbackBytes +=
                actualPairs.size() * sizeof(MortonPair) + (3u * sizeof(std::uint32_t));

            // Negative preflight 1: equal stable IDs remain invalid even when their
            // Morton codes differ. ValidateStableIdsCS is intentionally run after
            // the stable-ID LSD phase where equal IDs are adjacent.
            const std::vector<MortonPair> duplicateIds{
                {1u, 7u, 0u, 0u},
                {9u, 7u, 1u, 0u},
                {5u, 11u, 2u, 0u}};
            UploadVector(context, *pairA, duplicateIds);
            radixInput = pairA.get();
            radixOutput = pairB.get();
            const RadixConfig negativeRadixSettings{3u, 1u, 0u, 0u};
            radixConfig->Write(&negativeRadixSettings, sizeof(negativeRadixSettings));
            bindRadix(stableReset);
            bindRadix(stableValidate);
            stableReset.Dispatch(1u);
            stableValidate.Dispatch(1u);
            const std::vector<std::uint32_t> duplicateCounter =
                DownloadVector<std::uint32_t>(context, *stableIdInvalid, 1u);
            if (duplicateCounter[0] == 0u)
            {
                throw std::runtime_error("GPU stable-ID negative preflight was not rejected");
            }

            // Negative preflight 2: a forest with every node marked as a root must
            // trip the depth validation counter for all non-root nodes.
            const std::vector<std::uint32_t> forestParents(nodeCount, kInvalidIndex);
            UploadVector(context, *parents, forestParents);
            boundsSettings.currentDepth = 0u;
            boundsConfig->Write(&boundsSettings, sizeof(boundsSettings));
            boundsReset.Dispatch(1u);
            computeDepths.Dispatch(DivideRoundUp(nodeCount, kTraceThreads));
            const std::vector<std::uint32_t> forestCounter =
                DownloadVector<std::uint32_t>(context, *hierarchyInvalid, 1u);
            if (forestCounter[0] == 0u)
            {
                throw std::runtime_error("GPU malformed-parent negative preflight was not rejected");
            }

            return evidence;
        }

    }

    Report Run(const std::filesystem::path& shaderDirectory) noexcept
    {
        Report report{};
        try
        {
            if (!std::filesystem::is_directory(shaderDirectory))
            {
                throw std::runtime_error(
                    "shader directory does not exist: " + shaderDirectory.string());
            }
            {
                Context context(report);
                const std::vector<SoftwarePrimitiveRecord> primitives =
                    MakeFixedPrimitives();
                const std::vector<SoftwareRayRecord> rays = MakeFixedRays();
                report.flattenedSah = RunFlattenedSah(
                    context,
                    shaderDirectory,
                    primitives,
                    rays);
                report.gpuLbvh = RunGpuLbvh(
                    context,
                    shaderDirectory,
                    primitives,
                    rays);
            }
            if (report.validationWarnings != 0u || report.validationErrors != 0u)
            {
                throw std::runtime_error(
                    "Vulkan validation reported " +
                    std::to_string(report.validationWarnings) + " warning(s) and " +
                    std::to_string(report.validationErrors) + " error(s)");
            }
            report.status = Status::Passed;
            report.reason = "Vulkan compute dispatch and CPU readback parity passed";
        }
        catch (const VulkanError& error)
        {
            if (error.Result() == VK_ERROR_INCOMPATIBLE_DRIVER)
            {
                report.status = Status::Skipped;
                report.reason = "the Vulkan loader reports no compatible driver";
            }
            else
            {
                report.status = Status::Failed;
                report.reason = error.what();
            }
        }
        catch (const std::exception& error)
        {
            constexpr std::string_view skipPrefix = "SKIP:";
            const std::string_view message = error.what();
            if (message.starts_with(skipPrefix))
            {
                report.status = Status::Skipped;
                report.reason = std::string(message.substr(skipPrefix.size()));
            }
            else
            {
                report.status = Status::Failed;
                report.reason = error.what();
            }
        }
        catch (...)
        {
            report.status = Status::Failed;
            report.reason = "unknown exception in Vulkan execution smoke";
        }
        return report;
    }
}
