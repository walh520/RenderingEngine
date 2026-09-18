#include "AccelerationStructures.hpp"
#include "DeviceBuffer.hpp"
#include "DeviceDispatch.hpp"
#include "RayQueryBackend.hpp"
#include "RtPipelineBackend.hpp"
#include "contracts/GpuRecordsAbiV1.hpp"
#include "contracts/RayHitAbiV0.hpp"
#include "integrators/megakernel/MegakernelBridge.hpp"
#include "integrators/megakernel/include/ReferenceBsdf.hpp"
#include "rt/cpu/Bvh.hpp"
#include "rt/gpu/CanonicalTraversalScene.hpp"
#include "rt/software_gpu/SoftwareGpu.hpp"
#include "rt/software_gpu/SoftwareGpuBackend.hpp"
#include "scene/CanonicalScene.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace Abi0 = RenderingEngine::Contracts::AbiV0;
    namespace Abi1 = RenderingEngine::Contracts::AbiV1;
    namespace Cpu = RenderingEngine::Rt::Cpu;
    namespace Gpu = RenderingEngine::Rt::Gpu;
    namespace Hardware = RenderingEngine::Rt::Hardware;
    namespace L4 = RenderingEngine::Rt::SoftwareGpu;
    namespace Mega = RenderingEngine::Integrators::Megakernel;
    namespace ProbeBsdf =
        RenderingEngine::Integrators::Megakernel::ReferenceBsdfL6;

    // The production monolithic variants deliberately disable globally
    // contended profiler atomics. This gate still proves real dispatch,
    // dual-backend image parity and SPIR-V validation; counter semantics are
    // covered by the smaller sampling/traversal kernels.
    constexpr bool kMegakernelProfilerCountersEnabled = false;
    namespace Scene = RenderingEngine::Scene;

    constexpr std::uint32_t kTimestampCount = 16u;
    constexpr float kDistanceRelativeTolerance = 1.0e-4f;
    constexpr float kBarycentricTolerance = 4.0e-4f;
    constexpr float kNormalTolerance = 1.0e-3f;

    enum TimestampSlot : std::uint32_t
    {
        BlasBegin = 0u,
        BlasEnd,
        TlasBegin,
        TlasEnd,
        SoftwareClosestBegin,
        SoftwareClosestEnd,
        SoftwareAnyBegin,
        SoftwareAnyEnd,
        RayQueryClosestBegin,
        RayQueryClosestEnd,
        RayQueryAnyBegin,
        RayQueryAnyEnd,
        RtPipelineClosestBegin,
        RtPipelineClosestEnd,
        RtPipelineAnyBegin,
        RtPipelineAnyEnd
    };

    class GateError final : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    void CheckVk(const VkResult result, const std::string_view operation)
    {
        if (result != VK_SUCCESS)
        {
            throw GateError(std::string(operation) + " failed with VkResult " +
                std::to_string(static_cast<std::int32_t>(result)));
        }
    }

    void Require(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            throw GateError(std::string(message));
        }
    }

    void Require(const Hardware::Status& status, const std::string_view operation)
    {
        if (!status)
        {
            throw GateError(std::string(operation) + ": " + status.message +
                " (VkResult " + std::to_string(static_cast<std::int32_t>(status.result)) + ")");
        }
    }

    void Require(const Gpu::GpuTraversalStatus& status, const std::string_view operation)
    {
        if (!status)
        {
            throw GateError(std::string(operation) + ": " + status.message);
        }
    }

    template <typename Property>
    [[nodiscard]] bool HasName(
        const std::span<const Property> properties,
        const char* const expected) noexcept
    {
        return std::any_of(properties.begin(), properties.end(), [expected](const Property& property)
        {
            if constexpr (std::is_same_v<Property, VkLayerProperties>)
            {
                return std::strcmp(property.layerName, expected) == 0;
            }
            else
            {
                return std::strcmp(property.extensionName, expected) == 0;
            }
        });
    }

    struct ValidationState final
    {
        std::uint32_t warnings{};
        std::uint32_t errors{};
        std::vector<std::string> messages{};
    };

    VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(
        const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        const VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* const callbackData,
        void* const userData)
    {
        auto& validation = *static_cast<ValidationState*>(userData);
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u)
        {
            ++validation.errors;
        }
        else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0u)
        {
            ++validation.warnings;
        }
        if (callbackData != nullptr && callbackData->pMessage != nullptr &&
            validation.messages.size() < 128u)
        {
            validation.messages.emplace_back(callbackData->pMessage);
        }
        return VK_FALSE;
    }

    class VulkanContext final
    {
    public:
        VulkanContext()
        {
            try
            {
                CreateInstance();
                SelectPhysicalDevice();
                CreateDevice();
                CreateCommandObjects();
                CreateEmptyLayout();
            }
            catch (...)
            {
                Destroy();
                throw;
            }
        }

        ~VulkanContext()
        {
            Destroy();
        }

        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        [[nodiscard]] VkDevice Device() const noexcept { return device_; }
        [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept { return physicalDevice_; }
        [[nodiscard]] VkDescriptorSetLayout EmptyLayout() const noexcept { return emptyLayout_; }
        [[nodiscard]] const Hardware::DeviceDispatch& Dispatch() const noexcept { return dispatch_; }
        [[nodiscard]] std::uint32_t MaximumGroupCountX() const noexcept
        {
            return properties_.limits.maxComputeWorkGroupCount[0];
        }
        [[nodiscard]] VkDeviceSize ScratchAlignment() const noexcept
        {
            return accelerationStructureProperties_.minAccelerationStructureScratchOffsetAlignment;
        }
        [[nodiscard]] float TimestampPeriod() const noexcept { return properties_.limits.timestampPeriod; }
        [[nodiscard]] VkDeviceSize UniformBufferAlignment() const noexcept
        {
            return properties_.limits.minUniformBufferOffsetAlignment;
        }
        [[nodiscard]] std::string_view DeviceName() const noexcept { return properties_.deviceName; }
        [[nodiscard]] const ValidationState& Validation() const noexcept { return validation_; }
        [[nodiscard]] Hardware::HardwareRtLimits RtPipelineLimits() const noexcept
        {
            Hardware::HardwareRtLimits limits{};
            limits.minAccelerationStructureScratchOffsetAlignment =
                accelerationStructureProperties_
                    .minAccelerationStructureScratchOffsetAlignment;
            limits.shaderGroupHandleSize =
                rayTracingPipelineProperties_.shaderGroupHandleSize;
            limits.shaderGroupHandleAlignment =
                rayTracingPipelineProperties_.shaderGroupHandleAlignment;
            limits.shaderGroupBaseAlignment =
                rayTracingPipelineProperties_.shaderGroupBaseAlignment;
            limits.maxShaderGroupStride =
                rayTracingPipelineProperties_.maxShaderGroupStride;
            limits.maxRayRecursionDepth =
                rayTracingPipelineProperties_.maxRayRecursionDepth;
            limits.maxRayDispatchInvocationCount =
                rayTracingPipelineProperties_.maxRayDispatchInvocationCount;
            limits.maxRayHitAttributeSize =
                rayTracingPipelineProperties_.maxRayHitAttributeSize;
            limits.maxComputeWorkGroupCountX =
                properties_.limits.maxComputeWorkGroupCount[0];
            return limits;
        }

        void SubmitAndWait(const std::function<void(VkCommandBuffer)>& record)
        {
            CheckVk(vkResetCommandPool(device_, commandPool_, 0u), "vkResetCommandPool");
            CheckVk(vkResetFences(device_, 1u, &fence_), "vkResetFences");
            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocateInfo.commandPool = commandPool_;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            CheckVk(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
                "vkAllocateCommandBuffers");
            try
            {
                VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
                record(commandBuffer);
                CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
                VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submitInfo.commandBufferCount = 1u;
                submitInfo.pCommandBuffers = &commandBuffer;
                CheckVk(vkQueueSubmit(queue_, 1u, &submitInfo, fence_), "vkQueueSubmit");
                CheckVk(vkWaitForFences(device_, 1u, &fence_, VK_TRUE,
                    (std::numeric_limits<std::uint64_t>::max)()), "vkWaitForFences");
            }
            catch (...)
            {
                static_cast<void>(vkDeviceWaitIdle(device_));
                vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
                throw;
            }
            vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
        }

        [[nodiscard]] std::uint32_t FindMemoryType(
            const std::uint32_t memoryTypeBits,
            const VkMemoryPropertyFlags required) const
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
            for (std::uint32_t index = 0u; index < memoryProperties.memoryTypeCount; ++index)
            {
                if ((memoryTypeBits & (1u << index)) != 0u &&
                    (memoryProperties.memoryTypes[index].propertyFlags & required) == required)
                {
                    return index;
                }
            }
            throw GateError("No Vulkan memory type satisfies the requested properties.");
        }

    private:
        void CreateInstance()
        {
            std::uint32_t layerCount{};
            CheckVk(vkEnumerateInstanceLayerProperties(&layerCount, nullptr),
                "vkEnumerateInstanceLayerProperties(count)");
            std::vector<VkLayerProperties> layers(layerCount);
            CheckVk(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()),
                "vkEnumerateInstanceLayerProperties(data)");
            constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";
            Require(HasName<VkLayerProperties>(layers, validationLayer),
                "Wave 2 gate requires VK_LAYER_KHRONOS_validation.");

            std::uint32_t extensionCount{};
            CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
                "vkEnumerateInstanceExtensionProperties(count)");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            CheckVk(vkEnumerateInstanceExtensionProperties(
                nullptr, &extensionCount, extensions.data()),
                "vkEnumerateInstanceExtensionProperties(data)");
            Require(HasName<VkExtensionProperties>(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME),
                "Wave 2 gate requires VK_EXT_debug_utils.");

            std::uint32_t validationExtensionCount{};
            CheckVk(vkEnumerateInstanceExtensionProperties(
                validationLayer, &validationExtensionCount, nullptr),
                "vkEnumerateInstanceExtensionProperties(validation count)");
            std::vector<VkExtensionProperties> validationExtensions(validationExtensionCount);
            CheckVk(vkEnumerateInstanceExtensionProperties(
                validationLayer, &validationExtensionCount, validationExtensions.data()),
                "vkEnumerateInstanceExtensionProperties(validation data)");
            const bool validationFeaturesAvailable =
                HasName<VkExtensionProperties>(extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) ||
                HasName<VkExtensionProperties>(validationExtensions,
                    VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            Require(validationFeaturesAvailable,
                "Wave 2 gate requires VK_EXT_validation_features for synchronization validation.");

            VkDebugUtilsMessengerCreateInfoEXT debugInfo{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = ValidationCallback;
            debugInfo.pUserData = &validation_;

            constexpr VkValidationFeatureEnableEXT synchronizationValidation =
                VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
            VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
            validationFeatures.pNext = &debugInfo;
            validationFeatures.enabledValidationFeatureCount = 1u;
            validationFeatures.pEnabledValidationFeatures = &synchronizationValidation;

            VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            applicationInfo.pApplicationName = "RenderingEngine Wave 2 traversal gate";
            applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
            applicationInfo.pEngineName = "RenderingEngine";
            applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
            applicationInfo.apiVersion = VK_API_VERSION_1_3;
            const std::array enabledExtensions{
                VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
            VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            createInfo.pNext = &validationFeatures;
            createInfo.pApplicationInfo = &applicationInfo;
            createInfo.enabledLayerCount = 1u;
            createInfo.ppEnabledLayerNames = &validationLayer;
            createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
            createInfo.ppEnabledExtensionNames = enabledExtensions.data();
            CheckVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");

            const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            destroyMessenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            Require(createMessenger != nullptr && destroyMessenger_ != nullptr,
                "Debug-utils messenger entry points are unavailable.");
            CheckVk(createMessenger(instance_, &debugInfo, nullptr, &debugMessenger_),
                "vkCreateDebugUtilsMessengerEXT");
        }

        void SelectPhysicalDevice()
        {
            std::uint32_t deviceCount{};
            CheckVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
                "vkEnumeratePhysicalDevices(count)");
            Require(deviceCount != 0u, "No Vulkan physical device is available.");
            std::vector<VkPhysicalDevice> devices(deviceCount);
            CheckVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
                "vkEnumeratePhysicalDevices(data)");

            for (const VkPhysicalDevice candidate : devices)
            {
                VkPhysicalDeviceProperties candidateProperties{};
                vkGetPhysicalDeviceProperties(candidate, &candidateProperties);
                if (candidateProperties.apiVersion < VK_API_VERSION_1_3 ||
                    candidateProperties.limits.maxComputeWorkGroupInvocations < 64u ||
                    candidateProperties.limits.maxComputeWorkGroupSize[0] < 64u)
                {
                    continue;
                }
                std::uint32_t extensionCount{};
                CheckVk(vkEnumerateDeviceExtensionProperties(
                    candidate, nullptr, &extensionCount, nullptr),
                    "vkEnumerateDeviceExtensionProperties(count)");
                std::vector<VkExtensionProperties> extensions(extensionCount);
                CheckVk(vkEnumerateDeviceExtensionProperties(
                    candidate, nullptr, &extensionCount, extensions.data()),
                    "vkEnumerateDeviceExtensionProperties(data)");
                if (!HasName<VkExtensionProperties>(extensions,
                        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
                    !HasName<VkExtensionProperties>(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME) ||
                    !HasName<VkExtensionProperties>(extensions,
                        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) ||
                    !HasName<VkExtensionProperties>(extensions,
                        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME))
                {
                    continue;
                }

                VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
                VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
                VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
                VkPhysicalDeviceSynchronization2Features synchronization2{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
                VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
                VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                features.pNext = &bufferDeviceAddress;
                bufferDeviceAddress.pNext = &accelerationStructure;
                accelerationStructure.pNext = &rayQuery;
                rayQuery.pNext = &rayTracingPipeline;
                rayTracingPipeline.pNext = &synchronization2;
                vkGetPhysicalDeviceFeatures2(candidate, &features);
                if (bufferDeviceAddress.bufferDeviceAddress != VK_TRUE ||
                    accelerationStructure.accelerationStructure != VK_TRUE ||
                    rayQuery.rayQuery != VK_TRUE ||
                    rayTracingPipeline.rayTracingPipeline != VK_TRUE ||
                    synchronization2.synchronization2 != VK_TRUE)
                {
                    continue;
                }

                std::uint32_t familyCount{};
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
                std::vector<VkQueueFamilyProperties> families(familyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
                for (std::uint32_t family = 0u; family < familyCount; ++family)
                {
                    if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u &&
                        families[family].timestampValidBits != 0u)
                    {
                        physicalDevice_ = candidate;
                        queueFamilyIndex_ = family;
                        properties_ = candidateProperties;
                        return;
                    }
                }
            }
            throw GateError(
                "No Vulkan 1.3 compute device supports BDA, acceleration structures, Ray Query, RT Pipeline, sync2, and timestamps.");
        }

        void CreateDevice()
        {
            constexpr float queuePriority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = queueFamilyIndex_;
            queueInfo.queueCount = 1u;
            queueInfo.pQueuePriorities = &queuePriority;

            VkPhysicalDeviceSynchronization2Features synchronization2{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
            synchronization2.synchronization2 = VK_TRUE;
            VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
            rayQuery.rayQuery = VK_TRUE;
            VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
            rayTracingPipeline.rayTracingPipeline = VK_TRUE;
            rayTracingPipeline.pNext = &synchronization2;
            rayQuery.pNext = &rayTracingPipeline;
            VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
            accelerationStructure.accelerationStructure = VK_TRUE;
            accelerationStructure.pNext = &rayQuery;
            VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
            bufferDeviceAddress.bufferDeviceAddress = VK_TRUE;
            bufferDeviceAddress.pNext = &accelerationStructure;

            const std::array extensions{
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                VK_KHR_RAY_QUERY_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME};
            VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            createInfo.pNext = &bufferDeviceAddress;
            createInfo.queueCreateInfoCount = 1u;
            createInfo.pQueueCreateInfos = &queueInfo;
            createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();
            CheckVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "vkCreateDevice");
            vkGetDeviceQueue(device_, queueFamilyIndex_, 0u, &queue_);
            Require(Hardware::DeviceDispatch::Load(device_, true, dispatch_),
                "DeviceDispatch::Load");

            accelerationStructureProperties_ = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
            rayTracingPipelineProperties_ = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
            accelerationStructureProperties_.pNext =
                &rayTracingPipelineProperties_;
            VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties2.pNext = &accelerationStructureProperties_;
            vkGetPhysicalDeviceProperties2(physicalDevice_, &properties2);
        }

        void CreateCommandObjects()
        {
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            poolInfo.queueFamilyIndex = queueFamilyIndex_;
            CheckVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
                "vkCreateCommandPool");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            CheckVk(vkCreateFence(device_, &fenceInfo, nullptr, &fence_), "vkCreateFence");
        }

        void CreateEmptyLayout()
        {
            VkDescriptorSetLayoutCreateInfo createInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            CheckVk(vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &emptyLayout_),
                "vkCreateDescriptorSetLayout(empty)");
        }

        void Destroy() noexcept
        {
            if (device_ != VK_NULL_HANDLE)
            {
                static_cast<void>(vkDeviceWaitIdle(device_));
                if (emptyLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(device_, emptyLayout_, nullptr);
                }
                if (fence_ != VK_NULL_HANDLE)
                {
                    vkDestroyFence(device_, fence_, nullptr);
                }
                if (commandPool_ != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(device_, commandPool_, nullptr);
                }
                vkDestroyDevice(device_, nullptr);
                device_ = VK_NULL_HANDLE;
            }
            if (debugMessenger_ != VK_NULL_HANDLE && destroyMessenger_ != nullptr)
            {
                destroyMessenger_(instance_, debugMessenger_, nullptr);
            }
            if (instance_ != VK_NULL_HANDLE)
            {
                vkDestroyInstance(instance_, nullptr);
                instance_ = VK_NULL_HANDLE;
            }
        }

        ValidationState validation_{};
        VkInstance instance_{VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger_{nullptr};
        VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
        VkPhysicalDeviceProperties properties_{};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties_{};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties_{};
        std::uint32_t queueFamilyIndex_{};
        VkDevice device_{VK_NULL_HANDLE};
        VkQueue queue_{VK_NULL_HANDLE};
        VkCommandPool commandPool_{VK_NULL_HANDLE};
        VkFence fence_{VK_NULL_HANDLE};
        VkDescriptorSetLayout emptyLayout_{VK_NULL_HANDLE};
        Hardware::DeviceDispatch dispatch_{};
    };

    [[nodiscard]] std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        Require(stream.good(), "Unable to open SPIR-V file: " + path.string());
        const std::streamsize byteCount = stream.tellg();
        Require(byteCount > 0 && byteCount % 4 == 0,
            "SPIR-V file has an invalid byte count: " + path.string());
        stream.seekg(0, std::ios::beg);
        std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / 4u);
        Require(stream.read(reinterpret_cast<char*>(words.data()), byteCount).good(),
            "Unable to read SPIR-V file: " + path.string());
        return words;
    }

    template <typename Value>
    [[nodiscard]] Hardware::DeviceBuffer UploadBuffer(
        const Hardware::DeviceBufferAllocator& allocator,
        const std::span<const Value> values,
        const VkBufferUsageFlags usage,
        const bool requireDeviceAddress = false)
    {
        Require(!values.empty(), "A Wave 2 uploaded buffer cannot be empty.");
        Hardware::DeviceBuffer buffer{};
        Require(allocator.Create(values.size_bytes(), usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            requireDeviceAddress, true, buffer), "DeviceBufferAllocator::Create(upload)");
        Require(allocator.Upload(buffer, std::as_bytes(values)),
            "DeviceBufferAllocator::Upload");
        return buffer;
    }

    [[nodiscard]] Hardware::DeviceBuffer CreateMappedBuffer(
        const Hardware::DeviceBufferAllocator& allocator,
        const VkDeviceSize byteCount,
        const VkBufferUsageFlags usage)
    {
        Hardware::DeviceBuffer buffer{};
        Require(allocator.Create(byteCount, usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            false, true, buffer), "DeviceBufferAllocator::Create(mapped)");
        std::memset(buffer.MappedData(), 0, static_cast<std::size_t>(byteCount));
        return buffer;
    }

    [[nodiscard]] VkDescriptorBufferInfo Descriptor(const Hardware::DeviceBuffer& buffer)
    {
        return {buffer.Handle(), 0u, buffer.Size()};
    }

    void RecordHostReadBarrier(
        const Hardware::DeviceDispatch& dispatch,
        const VkCommandBuffer commandBuffer,
        const std::span<const VkBuffer> buffers,
        const VkPipelineStageFlags2 sourceStage =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
    {
        std::vector<VkBufferMemoryBarrier2> barriers;
        barriers.reserve(buffers.size());
        for (const VkBuffer buffer : buffers)
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = sourceStage;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer;
            barrier.offset = 0u;
            barrier.size = VK_WHOLE_SIZE;
            barriers.push_back(barrier);
        }
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.bufferMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dependency.pBufferMemoryBarriers = barriers.data();
        dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);
    }

    class DummyAlphaResources final
    {
    public:
        DummyAlphaResources(const VulkanContext& context)
            : device_(context.Device())
        {
            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent = {1u, 1u, 1u};
            imageInfo.mipLevels = 1u;
            imageInfo.arrayLayers = 2u;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            CheckVk(vkCreateImage(device_, &imageInfo, nullptr, &image_), "vkCreateImage(dummy alpha)");
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device_, image_, &requirements);
            VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocationInfo.allocationSize = requirements.size;
            allocationInfo.memoryTypeIndex = context.FindMemoryType(
                requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            CheckVk(vkAllocateMemory(device_, &allocationInfo, nullptr, &memory_),
                "vkAllocateMemory(dummy alpha)");
            CheckVk(vkBindImageMemory(device_, image_, memory_, 0u),
                "vkBindImageMemory(dummy alpha)");

            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = image_;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1u;
            viewInfo.subresourceRange.layerCount = 2u;
            CheckVk(vkCreateImageView(device_, &viewInfo, nullptr, &view_),
                "vkCreateImageView(dummy alpha)");
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.subresourceRange.layerCount = 1u;
            CheckVk(vkCreateImageView(device_, &viewInfo, nullptr, &view2D_),
                "vkCreateImageView(dummy environment)");
            VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 0.0f;
            CheckVk(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_),
                "vkCreateSampler(dummy alpha)");
        }

        ~DummyAlphaResources()
        {
            if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
            if (view2D_ != VK_NULL_HANDLE) vkDestroyImageView(device_, view2D_, nullptr);
            if (view_ != VK_NULL_HANDLE) vkDestroyImageView(device_, view_, nullptr);
            if (image_ != VK_NULL_HANDLE) vkDestroyImage(device_, image_, nullptr);
            if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
        }

        DummyAlphaResources(const DummyAlphaResources&) = delete;
        DummyAlphaResources& operator=(const DummyAlphaResources&) = delete;

        void RecordTransition(
            const Hardware::DeviceDispatch& dispatch,
            const VkCommandBuffer commandBuffer) const
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image_;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1u;
            barrier.subresourceRange.layerCount = 2u;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1u;
            dependency.pImageMemoryBarriers = &barrier;
            dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);

            VkImageSubresourceRange layer{};
            layer.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            layer.levelCount = 1u;
            layer.layerCount = 1u;
            VkClearColorValue transparent{};
            transparent.float32[0] = 1.0f;
            transparent.float32[1] = 1.0f;
            transparent.float32[2] = 1.0f;
            transparent.float32[3] = 0.0f;
            layer.baseArrayLayer = 0u;
            vkCmdClearColorImage(commandBuffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &transparent, 1u, &layer);
            VkClearColorValue opaque = transparent;
            opaque.float32[3] = 1.0f;
            layer.baseArrayLayer = 1u;
            vkCmdClearColorImage(commandBuffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &opaque, 1u, &layer);

            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);
        }

        [[nodiscard]] VkDescriptorImageInfo ImageInfo() const noexcept
        {
            return {VK_NULL_HANDLE, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        [[nodiscard]] VkDescriptorImageInfo SamplerInfo() const noexcept
        {
            return {sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        }
        [[nodiscard]] VkDescriptorImageInfo EnvironmentImageInfo() const noexcept
        {
            return {VK_NULL_HANDLE, view2D_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }

    private:
        VkDevice device_{VK_NULL_HANDLE};
        VkImage image_{VK_NULL_HANDLE};
        VkDeviceMemory memory_{VK_NULL_HANDLE};
        VkImageView view_{VK_NULL_HANDLE};
        VkImageView view2D_{VK_NULL_HANDLE};
        VkSampler sampler_{VK_NULL_HANDLE};
    };

    [[nodiscard]] Cpu::Vec3<float> ToVec3(const Abi0::AbiFloat4 value) noexcept
    {
        return {value.x, value.y, value.z};
    }

    [[nodiscard]] Cpu::Vec3<float> Normalize(const Cpu::Vec3<float> value)
    {
        const float lengthSquared = Cpu::LengthSquared(value);
        Require(lengthSquared > 0.0f && std::isfinite(lengthSquared),
            "Cannot normalize a zero or non-finite vector.");
        return value * (1.0f / std::sqrt(lengthSquared));
    }

    [[nodiscard]] Abi1::GpuRayQueueRecordV1 MakeRay(
        const Cpu::Vec3<float> origin,
        const Cpu::Vec3<float> direction,
        const std::uint32_t rayId,
        const float tMin = 1.0e-4f,
        const float tMax = 100.0f,
        const std::uint32_t pathId = 17u,
        const std::uint32_t visibilityMask = 0xffu)
    {
        Abi1::GpuRayQueueRecordV1 ray{};
        ray.originTMin = {origin.x, origin.y, origin.z, tMin};
        ray.directionTMax = {direction.x, direction.y, direction.z, tMax};
        ray.identity = {rayId, pathId, 0u, visibilityMask};
        ray.rng = {0x12345678u, 0x9abcdef0u, 0u, 0u};
        return ray;
    }

    [[nodiscard]] std::vector<Abi1::GpuRayQueueRecordV1> BuildRayCorpus(
        const Scene::CanonicalScene& canonical)
    {
        Require(!canonical.cameras.empty(), "Canonical scene has no fixed camera.");
        const Scene::CameraPreset& camera = canonical.cameras.front();
        const Cpu::Vec3<float> eye = ToVec3(camera.eye);
        const Cpu::Vec3<float> target = ToVec3(camera.target);
        const Cpu::Vec3<float> upReference = ToVec3(camera.up);
        const Cpu::Vec3<float> forward = Normalize(target - eye);
        const Cpu::Vec3<float> right = Normalize(Cpu::Cross(forward, upReference));
        const Cpu::Vec3<float> up = Normalize(Cpu::Cross(right, forward));
        constexpr std::uint32_t width = 16u;
        constexpr std::uint32_t height = 16u;
        const float tangent = std::tan(camera.verticalFovDegrees *
            (std::numbers::pi_v<float> / 180.0f) * 0.5f);
        std::vector<Abi1::GpuRayQueueRecordV1> rays;
        rays.reserve(width * height + 5u);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const float screenX = (2.0f * (static_cast<float>(x) + 0.5f) /
                    static_cast<float>(width) - 1.0f) * tangent;
                const float screenY = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) /
                    static_cast<float>(height)) * tangent;
                rays.push_back(MakeRay(eye, Normalize(forward + right * screenX + up * screenY),
                    static_cast<std::uint32_t>(rays.size())));
            }
        }

        // Deterministic boundary and miss probes supplement the fixed-camera corpus.
        rays.push_back(MakeRay(eye, forward, static_cast<std::uint32_t>(rays.size())));
        rays.push_back(MakeRay(eye, Normalize(forward + right * 0.8f),
            static_cast<std::uint32_t>(rays.size())));
        rays.push_back(MakeRay(eye, Normalize(forward - right * 0.8f),
            static_cast<std::uint32_t>(rays.size())));
        rays.push_back(MakeRay(eye, Normalize(forward + up * 0.8f),
            static_cast<std::uint32_t>(rays.size())));
        // The canonical gate builds every visible instance with mask 0xff.
        // A zero ray mask must therefore miss in CPU, flattened Software GPU,
        // and Ray Query without being classified as an invalid ray.
        rays.push_back(MakeRay(eye, forward,
            static_cast<std::uint32_t>(rays.size()), 1.0e-4f, 100.0f, 17u, 0u));

        if (canonical.stableId == Scene::kWave1CanonicalTriangleStableId)
        {
            // The canonical triangle lies exactly at z=0. These two rays prove
            // that tMin/tMax are both exclusive across CPU, Software GPU, and
            // Vulkan Ray Query rather than relying on implementation endpoint
            // behavior.
            rays.push_back(MakeRay({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
                static_cast<std::uint32_t>(rays.size()), 1.0f, 2.0f));
            rays.push_back(MakeRay({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
                static_cast<std::uint32_t>(rays.size()), 0.0f, 1.0f));
        }

        // Invalid rays are part of the ABI-v1 parity contract and must never reach traversal.
        Abi1::GpuRayQueueRecordV1 invalid = MakeRay(
            eye, {0.0f, 0.0f, 0.0f}, static_cast<std::uint32_t>(rays.size()));
        rays.push_back(invalid);
        return rays;
    }

    [[nodiscard]] bool IsCanonicalRayValid(const Abi1::GpuRayQueueRecordV1& ray) noexcept
    {
        const auto finite4 = [](const Abi0::AbiFloat4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z) && std::isfinite(value.w);
        };
        const Cpu::Vec3<float> direction = ToVec3(ray.directionTMax);
        const float lengthSquared = Cpu::LengthSquared(direction);
        return finite4(ray.originTMin) && finite4(ray.directionTMax) &&
            std::isfinite(lengthSquared) && std::abs(lengthSquared - 1.0f) <= 1.0e-4f &&
            ray.originTMin.w >= 0.0f && ray.originTMin.w < ray.directionTMax.w &&
            (ray.identity.w & ~0xffu) == 0u;
    }

    [[nodiscard]] bool TriangleIsDoubleSided(
        const Gpu::CanonicalTraversalTriangle& triangle) noexcept
    {
        return (triangle.metadata.x & Abi0::GeometryFlagDoubleSided) != 0u ||
            (triangle.metadata.y & Abi0::MaterialFlagDoubleSided) != 0u;
    }

    // CPU oracle for the two-layer gate atlas initialized by
    // DummyAlphaResources: layer 0 is fully transparent and layer 1 fully
    // opaque. This keeps alpha candidate confirmation independently checkable
    // without making CPU SAH depend on Vulkan image sampling.
    [[nodiscard]] bool TrianglePassesGateAlpha(
        const Scene::CanonicalScene& scene,
        const Gpu::CanonicalTraversalTriangle& triangle) noexcept
    {
        const bool alphaMasked =
            (triangle.metadata.x & Abi0::GeometryFlagAlphaMask) != 0u ||
            (triangle.metadata.y & Abi0::MaterialFlagAlphaMask) != 0u;
        if (!alphaMasked)
        {
            return true;
        }
        if (triangle.identity.w >= scene.materials.size())
        {
            return false;
        }
        const Abi0::GpuMaterialV0& material = scene.materials[triangle.identity.w];
        float alpha = material.baseColorFactor.w;
        const std::uint32_t layer = material.textureImageIndices.x;
        const std::uint32_t sampler = material.textureSamplerIndices.x;
        if (layer == Abi1::kInvalidId)
        {
            if (sampler != Abi1::kInvalidId)
            {
                return false;
            }
        }
        else
        {
            if (layer >= 2u || sampler != 0u)
            {
                return false;
            }
            alpha *= layer == 0u ? 0.0f : 1.0f;
        }
        return alpha >= material.surfaceParams.w;
    }

    struct CpuOracleHit final
    {
        Abi1::GpuHitQueueRecordV1 hit{};
        std::vector<Abi0::AbiUInt4> equivalentIds{};
    };

    [[nodiscard]] Abi1::GpuHitQueueRecordV1 MakeTerminal(
        const Abi1::GpuRayQueueRecordV1& ray,
        const Abi0::HitKind kind) noexcept
    {
        Abi1::GpuHitQueueRecordV1 result{};
        result.positionT.w = ray.directionTMax.w;
        result.ids = {Abi1::kInvalidId, Abi1::kInvalidId, Abi1::kInvalidId, Abi1::kInvalidId};
        result.metadata = {ray.identity.x, static_cast<std::uint32_t>(kind),
            Abi0::HitFlagNone, ray.identity.y};
        return result;
    }

    [[nodiscard]] CpuOracleHit TraceCpuClosest(
        const Cpu::Bvh<float>& bvh,
        const std::span<const Gpu::CanonicalTraversalTriangle> triangles,
        const Scene::CanonicalScene& canonical,
        const Abi1::GpuRayQueueRecordV1& queueRay)
    {
        CpuOracleHit oracle{};
        if (!IsCanonicalRayValid(queueRay))
        {
            oracle.hit = MakeTerminal(queueRay, Abi0::HitKindInvalid);
            return oracle;
        }
        if (queueRay.identity.w == 0u)
        {
            oracle.hit = MakeTerminal(queueRay, Abi0::HitKindMiss);
            return oracle;
        }
        Cpu::Ray<float> ray{ToVec3(queueRay.originTMin), ToVec3(queueRay.directionTMax),
            queueRay.originTMin.w, queueRay.directionTMax.w};
        Cpu::Hit<float> cpuHit{};
        for (std::size_t rejected = 0u; rejected <= triangles.size(); ++rejected)
        {
            cpuHit = bvh.TraceClosest(ray);
            if (!cpuHit.IsHit())
            {
                oracle.hit = MakeTerminal(queueRay, Abi0::HitKindMiss);
                return oracle;
            }
            Require(cpuHit.primitiveId < triangles.size(),
                "CPU SAH returned an out-of-range traversal ID.");
            if ((cpuHit.frontFace || TriangleIsDoubleSided(triangles[cpuHit.primitiveId])) &&
                TrianglePassesGateAlpha(canonical, triangles[cpuHit.primitiveId]))
            {
                break;
            }
            ray.tMin = cpuHit.t;
        }
        Require(cpuHit.IsHit(), "CPU SAH single-sided rejection did not terminate.");

        const Gpu::CanonicalTraversalTriangle& triangle = triangles[cpuHit.primitiveId];
        const float baryU = cpuHit.barycentric.y;
        const float baryV = cpuHit.barycentric.z;
        const float baryW = 1.0f - baryU - baryV;
        Cpu::Vec3<float> shadingNormal{
            triangle.normals[0].x * baryW + triangle.normals[1].x * baryU +
                triangle.normals[2].x * baryV,
            triangle.normals[0].y * baryW + triangle.normals[1].y * baryU +
                triangle.normals[2].y * baryV,
            triangle.normals[0].z * baryW + triangle.normals[1].z * baryU +
                triangle.normals[2].z * baryV};
        shadingNormal = Normalize(shadingNormal);
        Cpu::Vec3<float> geometricNormal = cpuHit.geometricNormal;
        if (!cpuHit.frontFace)
        {
            geometricNormal *= -1.0f;
            shadingNormal *= -1.0f;
        }
        const Cpu::Vec3<float> position = ray.At(cpuHit.t);
        oracle.hit.positionT = {position.x, position.y, position.z, cpuHit.t};
        oracle.hit.geometricNormalBaryU = {
            geometricNormal.x, geometricNormal.y, geometricNormal.z, baryU};
        oracle.hit.shadingNormalBaryV = {
            shadingNormal.x, shadingNormal.y, shadingNormal.z, baryV};
        oracle.hit.ids = triangle.identity;
        std::uint32_t hitFlags = cpuHit.frontFace
            ? Abi0::HitFlagFrontFace
            : Abi0::HitFlagNone;
        if ((triangle.metadata.x & Abi0::GeometryFlagAlphaMask) != 0u
            || (triangle.metadata.y & Abi0::MaterialFlagAlphaMask) != 0u)
        {
            hitFlags |= Abi0::HitFlagAlphaTested;
        }
        oracle.hit.metadata = {queueRay.identity.x, Abi0::HitKindTriangle,
            hitFlags, queueRay.identity.y};

        // Shared-edge candidates at the same distance are an equivalence set:
        // the Vulkan AS is not required to use L3's primitive-ID tie break.
        for (std::uint32_t index = 0u; index < triangles.size(); ++index)
        {
            Cpu::Hit<float> candidate{};
            if (Cpu::IntersectTriangle(
                    Cpu::Ray<float>{ToVec3(queueRay.originTMin), ToVec3(queueRay.directionTMax),
                        queueRay.originTMin.w, queueRay.directionTMax.w},
                    bvh.Triangles()[index], candidate) &&
                (candidate.frontFace || TriangleIsDoubleSided(triangles[index])) &&
                TrianglePassesGateAlpha(canonical, triangles[index]) &&
                std::abs(candidate.t - cpuHit.t) <= kDistanceRelativeTolerance
                    * std::max(std::abs(candidate.t), std::abs(cpuHit.t)))
            {
                oracle.equivalentIds.push_back(triangles[index].identity);
            }
        }
        if (oracle.equivalentIds.empty())
        {
            oracle.equivalentIds.push_back(triangle.identity);
        }
        return oracle;
    }

    [[nodiscard]] bool SameIds(const Abi0::AbiUInt4 left, const Abi0::AbiUInt4 right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
    }

    [[nodiscard]] bool ApproximatelyEqual(
        const float left,
        const float right,
        const float tolerance) noexcept
    {
        return std::abs(left - right) <= tolerance *
            std::max({1.0f, std::abs(left), std::abs(right)});
    }

    [[nodiscard]] bool RelativeDistanceEqual(
        const float left,
        const float right) noexcept
    {
        const float scale = std::max(std::abs(left), std::abs(right));
        return scale > 0.0f && std::abs(left - right) <=
            kDistanceRelativeTolerance * scale;
    }

    void CompareClosest(
        const std::string_view backend,
        const std::span<const CpuOracleHit> expected,
        const std::span<const Abi1::GpuHitQueueRecordV1> actual)
    {
        Require(expected.size() == actual.size(), "Closest-hit result count mismatch.");
        for (std::size_t index = 0u; index < expected.size(); ++index)
        {
            const Abi1::GpuHitQueueRecordV1& wanted = expected[index].hit;
            const Abi1::GpuHitQueueRecordV1& got = actual[index];
            if (wanted.metadata.y != got.metadata.y || wanted.metadata.x != got.metadata.x ||
                wanted.metadata.w != got.metadata.w)
            {
                throw GateError(std::string(backend) + " closest hit-kind/identity mismatch at ray " +
                    std::to_string(index));
            }
            if (wanted.metadata.y != Abi0::HitKindTriangle)
            {
                Require(SameIds(wanted.ids, got.ids),
                    std::string(backend) + " terminal IDs differ at ray " + std::to_string(index));
                continue;
            }
            const bool equivalent = std::any_of(expected[index].equivalentIds.begin(),
                expected[index].equivalentIds.end(), [&got](const Abi0::AbiUInt4 ids)
                {
                    return SameIds(ids, got.ids);
                });
            Require(equivalent,
                std::string(backend) + " canonical hit IDs differ at ray " + std::to_string(index));
            Require(RelativeDistanceEqual(wanted.positionT.w, got.positionT.w),
                std::string(backend) + " hit distance differs at ray " + std::to_string(index));
            const float u = got.geometricNormalBaryU.w;
            const float v = got.shadingNormalBaryV.w;
            Require(u >= -kBarycentricTolerance && v >= -kBarycentricTolerance &&
                u + v <= 1.0f + kBarycentricTolerance,
                std::string(backend) + " returned invalid barycentrics at ray " +
                    std::to_string(index));
            if (SameIds(wanted.ids, got.ids))
            {
                Require(wanted.metadata.z == got.metadata.z,
                    std::string(backend) + " hit flags differ at ray " +
                    std::to_string(index));
                Require(ApproximatelyEqual(wanted.geometricNormalBaryU.w, u,
                            kBarycentricTolerance) &&
                        ApproximatelyEqual(wanted.shadingNormalBaryV.w, v,
                            kBarycentricTolerance),
                    std::string(backend) + " barycentrics differ at ray " +
                    std::to_string(index));
                const auto normalMatches = [&](const Abi0::AbiFloat4 expectedNormal,
                                               const Abi0::AbiFloat4 actualNormal)
                {
                    return ApproximatelyEqual(expectedNormal.x, actualNormal.x, kNormalTolerance)
                        && ApproximatelyEqual(expectedNormal.y, actualNormal.y, kNormalTolerance)
                        && ApproximatelyEqual(expectedNormal.z, actualNormal.z, kNormalTolerance);
                };
                Require(normalMatches(wanted.geometricNormalBaryU, got.geometricNormalBaryU)
                        && normalMatches(wanted.shadingNormalBaryV, got.shadingNormalBaryV),
                    std::string(backend) + " hit normals differ at ray " +
                    std::to_string(index));
            }
        }
    }

    void CompareAny(
        const std::string_view backend,
        const std::span<const CpuOracleHit> expected,
        const std::span<const Abi1::GpuHitQueueRecordV1> actual)
    {
        Require(expected.size() == actual.size(), "Any-hit result count mismatch.");
        for (std::size_t index = 0u; index < expected.size(); ++index)
        {
            const std::uint32_t wantedKind = expected[index].hit.metadata.y;
            const std::uint32_t gotKind = actual[index].metadata.y;
            if (wantedKind != gotKind || expected[index].hit.metadata.x != actual[index].metadata.x ||
                expected[index].hit.metadata.w != actual[index].metadata.w)
            {
                throw GateError(std::string(backend) + " any-hit visibility mismatch at ray " +
                    std::to_string(index));
            }
        }
    }

    [[nodiscard]] VkDeviceSize AlignUp(
        const VkDeviceSize value,
        const VkDeviceSize alignment)
    {
        Require(alignment != 0u, "Alignment must be non-zero.");
        const VkDeviceSize remainder = value % alignment;
        return remainder == 0u ? value : value + alignment - remainder;
    }

    class MegakernelImages final
    {
    public:
        static constexpr std::size_t kCount = 6u;

        MegakernelImages(
            const VulkanContext& context,
            const std::uint32_t width,
            const std::uint32_t height)
            : device_(context.Device()), width_(width), height_(height)
        {
            try
            {
                for (Image& image : images_)
                {
                    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    imageInfo.imageType = VK_IMAGE_TYPE_2D;
                    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    imageInfo.extent = {width_, height_, 1u};
                    imageInfo.mipLevels = 1u;
                    imageInfo.arrayLayers = 1u;
                    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    CheckVk(vkCreateImage(device_, &imageInfo, nullptr, &image.handle),
                        "vkCreateImage(megakernel output)");

                    VkMemoryRequirements requirements{};
                    vkGetImageMemoryRequirements(device_, image.handle, &requirements);
                    VkMemoryAllocateInfo allocationInfo{
                        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    allocationInfo.allocationSize = requirements.size;
                    allocationInfo.memoryTypeIndex = context.FindMemoryType(
                        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    CheckVk(vkAllocateMemory(device_, &allocationInfo, nullptr, &image.memory),
                        "vkAllocateMemory(megakernel output)");
                    CheckVk(vkBindImageMemory(device_, image.handle, image.memory, 0u),
                        "vkBindImageMemory(megakernel output)");

                    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    viewInfo.image = image.handle;
                    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    viewInfo.subresourceRange.levelCount = 1u;
                    viewInfo.subresourceRange.layerCount = 1u;
                    CheckVk(vkCreateImageView(device_, &viewInfo, nullptr, &image.view),
                        "vkCreateImageView(megakernel output)");
                }
            }
            catch (...)
            {
                Destroy();
                throw;
            }
        }

        ~MegakernelImages()
        {
            Destroy();
        }

        MegakernelImages(const MegakernelImages&) = delete;
        MegakernelImages& operator=(const MegakernelImages&) = delete;

        [[nodiscard]] std::array<VkDescriptorImageInfo, kCount> Descriptors() const noexcept
        {
            std::array<VkDescriptorImageInfo, kCount> result{};
            for (std::size_t index = 0u; index < images_.size(); ++index)
            {
                result[index] = {
                    VK_NULL_HANDLE, images_[index].view, VK_IMAGE_LAYOUT_GENERAL};
            }
            return result;
        }

        void RecordInitialize(
            const Hardware::DeviceDispatch& dispatch,
            const VkCommandBuffer commandBuffer) const
        {
            std::array<VkImageMemoryBarrier2, kCount> barriers{};
            for (std::size_t index = 0u; index < images_.size(); ++index)
            {
                VkImageMemoryBarrier2& barrier = barriers[index];
                barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = images_[index].handle;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.layerCount = 1u;
            }
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
            dependency.pImageMemoryBarriers = barriers.data();
            dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);
        }

        void RecordSampleBarrier(
            const Hardware::DeviceDispatch& dispatch,
            const VkCommandBuffer commandBuffer) const
        {
            std::array<VkImageMemoryBarrier2, kCount> barriers{};
            for (std::size_t index = 0u; index < images_.size(); ++index)
            {
                VkImageMemoryBarrier2& barrier = barriers[index];
                barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = images_[index].handle;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.layerCount = 1u;
            }
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
            dependency.pImageMemoryBarriers = barriers.data();
            dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);
        }

        void RecordRawCopy(
            const Hardware::DeviceDispatch& dispatch,
            const VkCommandBuffer commandBuffer,
            const VkBuffer destination) const
        {
            VkImageMemoryBarrier2 imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = images_[0].handle;
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.levelCount = 1u;
            imageBarrier.subresourceRange.layerCount = 1u;
            VkDependencyInfo imageDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            imageDependency.imageMemoryBarrierCount = 1u;
            imageDependency.pImageMemoryBarriers = &imageBarrier;
            dispatch.cmdPipelineBarrier2(commandBuffer, &imageDependency);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1u;
            region.imageExtent = {width_, height_, 1u};
            vkCmdCopyImageToBuffer(commandBuffer, images_[0].handle,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination, 1u, &region);

            VkBufferMemoryBarrier2 bufferBarrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferBarrier.buffer = destination;
            bufferBarrier.offset = 0u;
            bufferBarrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo bufferDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            bufferDependency.bufferMemoryBarrierCount = 1u;
            bufferDependency.pBufferMemoryBarriers = &bufferBarrier;
            dispatch.cmdPipelineBarrier2(commandBuffer, &bufferDependency);
        }

    private:
        struct Image final
        {
            VkImage handle{VK_NULL_HANDLE};
            VkDeviceMemory memory{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
        };

        void Destroy() noexcept
        {
            for (Image& image : images_)
            {
                if (image.view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device_, image.view, nullptr);
                    image.view = VK_NULL_HANDLE;
                }
                if (image.handle != VK_NULL_HANDLE)
                {
                    vkDestroyImage(device_, image.handle, nullptr);
                    image.handle = VK_NULL_HANDLE;
                }
                if (image.memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(device_, image.memory, nullptr);
                    image.memory = VK_NULL_HANDLE;
                }
            }
        }

        VkDevice device_{VK_NULL_HANDLE};
        std::uint32_t width_{};
        std::uint32_t height_{};
        std::array<Image, kCount> images_{};
    };

    class MegakernelPipelines final
    {
    public:
        enum class Backend : std::size_t
        {
            Software = 0u,
            RayQuery = 1u,
            SamplingProbe = 2u,
            Count
        };

        MegakernelPipelines(
            const VkDevice device,
            const std::span<const std::uint32_t> softwareSpirv,
            const std::span<const std::uint32_t> rayQuerySpirv,
            const std::span<const std::uint32_t> samplingProbeSpirv,
            const VkDescriptorSetLayout sceneLayout,
            const VkDescriptorSetLayout softwareTraversalLayout,
            const VkDescriptorSetLayout rayQueryTraversalLayout,
            const std::uint32_t descriptorSetCount)
            : device_(device)
        {
            try
            {
                // Binding 19 is reserved by the L6 sampling probe. The image
                // gate does not populate it, but retaining the complete L6
                // set-0 layout keeps pipeline compatibility explicit.
                std::array<VkDescriptorSetLayoutBinding, 20u> bindings{};
                for (std::uint32_t binding = 0u; binding < bindings.size(); ++binding)
                {
                    bindings[binding].binding = binding;
                    bindings[binding].descriptorCount = 1u;
                    bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    if (binding == 0u)
                    {
                        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    }
                    else if (binding == 10u)
                    {
                        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    }
                    else if (binding == 11u)
                    {
                        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    }
                    else if (binding >= 12u && binding <= 17u)
                    {
                        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    }
                    else
                    {
                        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    }
                }
                VkDescriptorSetLayoutCreateInfo layoutInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
                layoutInfo.pBindings = bindings.data();
                CheckVk(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &set0Layout_),
                    "vkCreateDescriptorSetLayout(megakernel set 0)");

                const std::array<VkDescriptorPoolSize, 5u> poolSizes{
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSetCount},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11u * descriptorSetCount},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSetCount},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, descriptorSetCount},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6u * descriptorSetCount}};
                VkDescriptorPoolCreateInfo poolInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                poolInfo.maxSets = descriptorSetCount;
                poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();
                CheckVk(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_),
                    "vkCreateDescriptorPool(megakernel)");

                VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
                queryInfo.queryCount = 4u;
                CheckVk(vkCreateQueryPool(device_, &queryInfo, nullptr, &queryPool_),
                    "vkCreateQueryPool(megakernel)");

                CreatePipeline(Backend::RayQuery, rayQuerySpirv, sceneLayout,
                    rayQueryTraversalLayout, "CSMain");
                CreatePipeline(Backend::Software, softwareSpirv, sceneLayout,
                    softwareTraversalLayout, "CSMain");
                CreatePipeline(Backend::SamplingProbe, samplingProbeSpirv, sceneLayout,
                    softwareTraversalLayout, "ProbeMain");
            }
            catch (...)
            {
                Destroy();
                throw;
            }
        }

        ~MegakernelPipelines()
        {
            Destroy();
        }

        MegakernelPipelines(const MegakernelPipelines&) = delete;
        MegakernelPipelines& operator=(const MegakernelPipelines&) = delete;

        [[nodiscard]] std::vector<VkDescriptorSet> AllocateSets(
            const std::uint32_t count) const
        {
            std::vector<VkDescriptorSetLayout> layouts(count, set0Layout_);
            std::vector<VkDescriptorSet> sets(count, VK_NULL_HANDLE);
            VkDescriptorSetAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocateInfo.descriptorPool = descriptorPool_;
            allocateInfo.descriptorSetCount = count;
            allocateInfo.pSetLayouts = layouts.data();
            CheckVk(vkAllocateDescriptorSets(device_, &allocateInfo, sets.data()),
                "vkAllocateDescriptorSets(megakernel)");
            return sets;
        }

        [[nodiscard]] VkPipeline Pipeline(const Backend backend) const noexcept
        {
            return pipelines_[static_cast<std::size_t>(backend)];
        }

        [[nodiscard]] VkPipelineLayout PipelineLayout(const Backend backend) const noexcept
        {
            return pipelineLayouts_[static_cast<std::size_t>(backend)];
        }

        [[nodiscard]] VkQueryPool QueryPool() const noexcept { return queryPool_; }

    private:
        void CreatePipeline(
            const Backend backend,
            const std::span<const std::uint32_t> spirv,
            const VkDescriptorSetLayout sceneLayout,
            const VkDescriptorSetLayout traversalLayout,
            const char* const entryPoint)
        {
            const std::size_t index = static_cast<std::size_t>(backend);
            Require(!spirv.empty(), "Megakernel SPIR-V cannot be empty.");
            VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = spirv.size_bytes();
            moduleInfo.pCode = spirv.data();
            CheckVk(vkCreateShaderModule(device_, &moduleInfo, nullptr, &shaderModules_[index]),
                "vkCreateShaderModule(megakernel)");

            const std::array layouts{set0Layout_, sceneLayout, traversalLayout};
            VkPipelineLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
            layoutInfo.pSetLayouts = layouts.data();
            CheckVk(vkCreatePipelineLayout(
                device_, &layoutInfo, nullptr, &pipelineLayouts_[index]),
                "vkCreatePipelineLayout(megakernel)");

            VkPipelineShaderStageCreateInfo stageInfo{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = shaderModules_[index];
            stageInfo.pName = entryPoint;
            VkComputePipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = pipelineLayouts_[index];
            CheckVk(vkCreateComputePipelines(
                device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipelines_[index]),
                backend == Backend::Software
                    ? "vkCreateComputePipelines(megakernel software)"
                    : backend == Backend::RayQuery
                        ? "vkCreateComputePipelines(megakernel ray query)"
                        : "vkCreateComputePipelines(megakernel sampling probe)");
        }

        void Destroy() noexcept
        {
            for (VkPipeline& pipeline : pipelines_)
            {
                if (pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
            }
            for (VkPipelineLayout& layout : pipelineLayouts_)
            {
                if (layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device_, layout, nullptr);
                    layout = VK_NULL_HANDLE;
                }
            }
            for (VkShaderModule& module : shaderModules_)
            {
                if (module != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(device_, module, nullptr);
                    module = VK_NULL_HANDLE;
                }
            }
            if (queryPool_ != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device_, queryPool_, nullptr);
                queryPool_ = VK_NULL_HANDLE;
            }
            if (descriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
                descriptorPool_ = VK_NULL_HANDLE;
            }
            if (set0Layout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, set0Layout_, nullptr);
                set0Layout_ = VK_NULL_HANDLE;
            }
        }

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout set0Layout_{VK_NULL_HANDLE};
        VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
        VkQueryPool queryPool_{VK_NULL_HANDLE};
        static constexpr std::size_t kPipelineCount =
            static_cast<std::size_t>(Backend::Count);
        std::array<VkShaderModule, kPipelineCount> shaderModules_{};
        std::array<VkPipelineLayout, kPipelineCount> pipelineLayouts_{};
        std::array<VkPipeline, kPipelineCount> pipelines_{};
    };

    void UpdateMegakernelDescriptorSet(
        const VkDevice device,
        const VkDescriptorSet set,
        const VkDescriptorBufferInfo& frame,
        const std::array<VkDescriptorBufferInfo, 9u>& storageInputs,
        const VkDescriptorImageInfo& environment,
        const VkDescriptorImageInfo& environmentSampler,
        const std::array<VkDescriptorImageInfo, MegakernelImages::kCount>& outputs,
        const VkDescriptorBufferInfo& counters)
    {
        std::array<VkWriteDescriptorSet, 19u> writes{};
        for (std::uint32_t binding = 0u; binding < writes.size(); ++binding)
        {
            writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[binding].dstSet = set;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1u;
            if (binding == 0u)
            {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[binding].pBufferInfo = &frame;
            }
            else if (binding >= 1u && binding <= 9u)
            {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo = &storageInputs[binding - 1u];
            }
            else if (binding == 10u)
            {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                writes[binding].pImageInfo = &environment;
            }
            else if (binding == 11u)
            {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                writes[binding].pImageInfo = &environmentSampler;
            }
            else if (binding >= 12u && binding <= 17u)
            {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[binding].pImageInfo = &outputs[binding - 12u];
            }
            else
            {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo = &counters;
            }
        }
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()),
            writes.data(), 0u, nullptr);
    }

    void UpdateSamplingProbeDescriptorSet(
        const VkDevice device,
        const VkDescriptorSet set,
        const VkDescriptorBufferInfo& frame,
        const std::array<VkDescriptorBufferInfo, 9u>& storageInputs,
        const VkDescriptorImageInfo& environment,
        const VkDescriptorImageInfo& environmentSampler,
        const std::array<VkDescriptorImageInfo, MegakernelImages::kCount>& outputs,
        const VkDescriptorBufferInfo& counters,
        const VkDescriptorBufferInfo& probeOutput)
    {
        UpdateMegakernelDescriptorSet(device, set, frame, storageInputs,
            environment, environmentSampler, outputs, counters);
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = 19u;
        write.descriptorCount = 1u;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &probeOutput;
        vkUpdateDescriptorSets(device, 1u, &write, 0u, nullptr);
    }

    struct MegakernelSceneData final
    {
        std::vector<Mega::PbrMaterialGpu> materials{};
        std::vector<Mega::PbrLightGpu> lights{};
        std::vector<Mega::AliasEntryGpu> lightAlias{};
        std::vector<Mega::AliasEntryGpu> environmentRows{{1.0f, 1.0f, 0u, 0u}};
        std::vector<Mega::AliasEntryGpu> environmentColumns{{1.0f, 1.0f, 0u, 0u}};
        std::vector<float> lightSelectionPmf{};
        std::vector<Mega::EmitterMapEntryGpu> emitterMap{};
        std::uint32_t emitterMapCount{};
        std::vector<Mega::FixtureTriangleGpu> fixtureTriangles{};
        std::vector<Mega::FixtureSphereGpu> fixtureSpheres{1u};
        float sceneRadius{};
    };

    [[nodiscard]] Mega::Float4 ToMega(const Abi0::AbiFloat4 value) noexcept
    {
        return {value.x, value.y, value.z, value.w};
    }

    [[nodiscard]] MegakernelSceneData BuildMegakernelSceneData(
        const Scene::CanonicalScene& canonical,
        const Gpu::CanonicalTraversalScene& traversal)
    {
        constexpr std::uint32_t diffuseLobe = 1u << 0u;
        constexpr std::uint32_t glossyLobe = 1u << 1u;
        constexpr std::uint32_t specularLobe = 1u << 2u;
        constexpr std::uint32_t reflectionLobe = 1u << 3u;
        constexpr std::uint32_t transmissionLobe = 1u << 4u;
        constexpr std::uint32_t metallicRoughnessModel = 5u;
        constexpr std::uint32_t smoothGlassModel = 3u;

        MegakernelSceneData result{};
        result.materials.reserve(canonical.materials.size());
        for (const Abi0::GpuMaterialV0& source : canonical.materials)
        {
            Mega::PbrMaterialGpu material{};
            material.baseColorMetallic = {source.baseColorFactor.x,
                source.baseColorFactor.y, source.baseColorFactor.z, source.surfaceParams.x};
            material.emissiveRoughness = {
                source.emissiveFactorStrength.x * source.emissiveFactorStrength.w,
                source.emissiveFactorStrength.y * source.emissiveFactorStrength.w,
                source.emissiveFactorStrength.z * source.emissiveFactorStrength.w,
                source.surfaceParams.y};
            material.transmissionIor = {source.transmissionParams.x,
                source.transmissionParams.y, 0.0f, 0.0f};
            material.attenuationColorDistance = ToMega(source.attenuationColorDistance);
            material.f0 = {0.04f, 0.04f, 0.04f, 0.0f};
            material.conductorEta = {1.0f, 1.0f, 1.0f, 0.0f};
            material.conductorK = {0.0f, 0.0f, 0.0f, 0.0f};
            const bool smoothDielectric =
                source.metadata.x == Abi0::MaterialModelSmoothDielectric;
            std::uint32_t privateFlags = Mega::MaterialFlagNone;
            const float baseMaximum = std::max({source.baseColorFactor.x,
                source.baseColorFactor.y, source.baseColorFactor.z});
            const float emissionMaximum = std::max({material.emissiveRoughness.x,
                material.emissiveRoughness.y, material.emissiveRoughness.z});
            if (emissionMaximum > 0.0f && baseMaximum == 0.0f &&
                source.transmissionParams.x == 0.0f)
            {
                privateFlags |= Mega::MaterialFlagPureEmitter;
            }
            if ((source.metadata.y & Abi0::MaterialFlagThinWalled) != 0u)
            {
                privateFlags |= Mega::MaterialFlagThinWalled;
            }
            material.metadata = {
                smoothDielectric ? smoothGlassModel : metallicRoughnessModel,
                smoothDielectric
                    ? specularLobe | reflectionLobe | transmissionLobe
                    : diffuseLobe | glossyLobe | reflectionLobe,
                0u,
                privateFlags};
            result.materials.push_back(material);
        }

        result.lights.reserve(canonical.lights.size());
        for (std::size_t lightIndex = 0u; lightIndex < canonical.lights.size(); ++lightIndex)
        {
            const Abi0::GpuLightV0& source = canonical.lights[lightIndex];
            Mega::PbrLightGpu light{};
            light.positionRange = ToMega(source.positionRange);
            light.directionCosOuter = ToMega(source.directionCosOuter);
            light.radianceScale = ToMega(source.radianceScale);
            light.shapeParams = ToMega(source.shapeParams);
            light.identity = {source.identity.x, source.extra.y,
                source.identity.y, source.identity.w};
            light.payload = {
                source.extra.x, source.extra.z, source.extra.w, source.identity.z};
            if (source.identity.x == Abi0::LightTypeEmissiveTriangle)
            {
                const auto found = std::find_if(traversal.triangles.begin(),
                    traversal.triangles.end(), [&source](const auto& triangle)
                    {
                        return triangle.identity.x == source.identity.z &&
                            triangle.identity.y == source.identity.w;
                    });
                Require(found != traversal.triangles.end(),
                    "An emissive canonical light references no traversal triangle.");
                light.payload.x = static_cast<std::uint32_t>(
                    std::distance(traversal.triangles.begin(), found));
            }
            result.lights.push_back(light);
        }

        result.emitterMap = Mega::BuildEmitterMap(result.lights);
        Require(result.emitterMap.size() <=
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
            "The emitter map exceeds the uint32 shader count.");
        result.emitterMapCount = static_cast<std::uint32_t>(result.emitterMap.size());
        if (result.emitterMap.empty())
        {
            // Vulkan storage-buffer descriptors may not reference a zero-sized
            // allocation. distribution.w remains zero, so this sentinel is never read.
            result.emitterMap.push_back({});
        }

        result.fixtureTriangles.reserve(traversal.triangles.size());
        for (const Gpu::CanonicalTraversalTriangle& source : traversal.triangles)
        {
            Mega::FixtureTriangleGpu triangle{};
            triangle.p0 = ToMega(source.positions[0]);
            triangle.p1 = ToMega(source.positions[1]);
            triangle.p2 = ToMega(source.positions[2]);
            triangle.metadata = {source.identity.w, source.identity.y,
                source.identity.x, source.metadata.x};
            result.fixtureTriangles.push_back(triangle);
        }

        const Cpu::Vec3<float> boundsMinimum = ToVec3(canonical.constants.sceneBoundsMin);
        const Cpu::Vec3<float> boundsMaximum = ToVec3(canonical.constants.sceneBoundsMax);
        const Cpu::Vec3<float> halfExtent = (boundsMaximum - boundsMinimum) * 0.5f;
        result.sceneRadius = std::sqrt(Cpu::LengthSquared(halfExtent));
        Require(std::isfinite(result.sceneRadius) && result.sceneRadius > 0.0f,
            "Megakernel scene radius must be finite and positive.");

        Require(!result.lights.empty(), "Wave 2 image gates require at least one light.");
        const Mega::TopLevelLightDistribution distribution =
            Mega::BuildTopLevelLightDistribution(result.lights,
                Mega::LightProposal::Power, result.sceneRadius, 0.0);
        Require(!distribution.alias.empty(),
            "Wave 2 image gates require a non-empty enabled-light distribution.");
        result.lightAlias = distribution.alias;
        result.lightSelectionPmf = distribution.selectionPmfByLight;
        return result;
    }

    void AppendSamplingProbeMaterials(std::vector<Mega::PbrMaterialGpu>& materials)
    {
        const auto makeMaterial = [](
            const std::uint32_t model,
            const std::uint32_t lobes,
            const Mega::Float4 baseColorMetallic,
            const float roughness,
            const float transmission = 0.0f,
            const Mega::Float4 f0 = {0.04f, 0.04f, 0.04f, 0.0f},
            const std::uint32_t complexFresnel = 0u)
        {
            Mega::PbrMaterialGpu material{};
            material.baseColorMetallic = baseColorMetallic;
            material.emissiveRoughness.w = roughness;
            material.transmissionIor = {transmission, 1.5f, 0.0f, 0.0f};
            material.attenuationColorDistance = {1.0f, 1.0f, 1.0f, 0.0f};
            material.f0 = f0;
            material.conductorEta = {0.20f, 0.92f, 1.10f, 0.0f};
            material.conductorK = {3.90f, 2.45f, 2.14f, 0.0f};
            material.metadata = {model, lobes, complexFresnel, Mega::MaterialFlagNone};
            return material;
        };

        materials.push_back(makeMaterial(
            ProbeBsdf::kModelLambert,
            ProbeBsdf::kLobeDiffuseReflection,
            {0.80f, 0.35f, 0.10f, 0.0f}, 0.50f));
        materials.push_back(makeMaterial(
            ProbeBsdf::kModelGgxConductor,
            ProbeBsdf::kLobeGlossyReflection,
            {0.0f, 0.0f, 0.0f, 0.0f}, 0.45f, 0.0f,
            {0.90f, 0.70f, 0.50f, 0.0f}));
        materials.push_back(makeMaterial(
            ProbeBsdf::kModelGgxDielectricReflection,
            ProbeBsdf::kLobeGlossyReflection,
            {0.0f, 0.0f, 0.0f, 0.0f}, 0.35f));
        materials.push_back(makeMaterial(
            ProbeBsdf::kModelSmoothGlass,
            ProbeBsdf::kLobeSpecularReflection |
                ProbeBsdf::kLobeSpecularTransmission,
            {1.0f, 1.0f, 1.0f, 0.0f}, 0.0f, 1.0f));
        materials.push_back(makeMaterial(
            ProbeBsdf::kModelRoughDielectric,
            ProbeBsdf::kLobeGlossyReflection |
                ProbeBsdf::kLobeGlossyTransmission,
            {1.0f, 1.0f, 1.0f, 0.0f}, 0.35f, 1.0f));
        materials.push_back(makeMaterial(
            ProbeBsdf::kModelMetallicRoughness,
            ProbeBsdf::kLobeDiffuseReflection |
                ProbeBsdf::kLobeGlossyReflection,
            {0.70f, 0.40f, 0.20f, 0.35f}, 0.60f));
    }

    [[nodiscard]] Mega::MegakernelFrameConstantsGpu BuildMegakernelFrame(
        const Scene::CanonicalScene& canonical,
        const MegakernelSceneData& data,
        const std::uint32_t width,
        const std::uint32_t height,
        const std::uint32_t sampleIndex,
        const Mega::TraversalBackend backend,
        const std::uint32_t softwareNodeCount,
        const std::uint32_t softwareTriangleCount)
    {
        const Scene::CameraPreset& camera = canonical.cameras.front();
        const Cpu::Vec3<float> eye = ToVec3(camera.eye);
        const Cpu::Vec3<float> forward = Normalize(ToVec3(camera.target) - eye);
        const Cpu::Vec3<float> right = Normalize(Cpu::Cross(forward, ToVec3(camera.up)));
        const Cpu::Vec3<float> up = Normalize(Cpu::Cross(right, forward));
        const Cpu::Vec3<float> boundsMinimum = ToVec3(canonical.constants.sceneBoundsMin);
        const Cpu::Vec3<float> boundsMaximum = ToVec3(canonical.constants.sceneBoundsMax);
        const Cpu::Vec3<float> center = (boundsMinimum + boundsMaximum) * 0.5f;

        Mega::MegakernelFrameConstantsGpu frame{};
        const float tangent = std::tan(camera.verticalFovDegrees *
            (std::numbers::pi_v<float> / 180.0f) * 0.5f);
        frame.cameraPositionTanHalfFov = {eye.x, eye.y, eye.z, tangent};
        frame.cameraForwardAspect = {forward.x, forward.y, forward.z,
            static_cast<float>(width) / static_cast<float>(height)};
        frame.cameraRightLensRadius = {right.x, right.y, right.z, 0.0f};
        frame.cameraUpExposure = {up.x, up.y, up.z, 1.0f};
        frame.image = {width, height, sampleIndex, 5u};
        frame.trace = {static_cast<std::uint32_t>(data.fixtureTriangles.size()), 0u,
            static_cast<std::uint32_t>(data.materials.size()),
            static_cast<std::uint32_t>(data.lights.size())};
        frame.sampling = {0x13579bdfu, 0x2468ace0u,
            static_cast<std::uint32_t>(Mega::LightProposal::Power),
            static_cast<std::uint32_t>(Mega::DirectLightingEstimator::Mis)};
        frame.environment = {Mega::kInvalidIndex, 0u, 0u, 0u};
        frame.distribution = {static_cast<std::uint32_t>(data.lightAlias.size()), 0u, 0u,
            data.emitterMapCount};
        frame.russianRoulette = {3.0f, 0.05f, 0.95f, 1.0e-4f};
        frame.sceneCenterRadius = {center.x, center.y, center.z, data.sceneRadius};
        frame.environmentToWorld0 = {1.0f, 0.0f, 0.0f, 0.0f};
        frame.environmentToWorld1 = {0.0f, 1.0f, 0.0f, 0.0f};
        frame.environmentToWorld2 = {0.0f, 0.0f, 1.0f, 0.0f};
        frame.worldToEnvironment0 = frame.environmentToWorld0;
        frame.worldToEnvironment1 = frame.environmentToWorld1;
        frame.worldToEnvironment2 = frame.environmentToWorld2;
        frame.traversal = {static_cast<std::uint32_t>(backend),
            backend == Mega::TraversalBackend::FlattenedSah ? softwareNodeCount : 0u,
            backend == Mega::TraversalBackend::FlattenedSah ? softwareTriangleCount : 0u,
            2u};
        // The traversal gate uses the one-ray physical visibility mode; PCF
        // and PCSS have dedicated tuple/compile coverage and would turn this
        // small parity image into a filter-throughput benchmark.
        frame.output = {0x57a20001u, 0u, 0u, 2u};
        Require(Mega::ValidateMegakernelFrameConstants(frame),
            "Host rejected generated Megakernel frame constants.");
        return frame;
    }

    struct SamplingProbeResult final
    {
        std::uint32_t samples{};
        std::uint32_t validBsdfSamples{};
        std::uint32_t rejectedBsdfSamples{};
        std::uint32_t materialModelsCovered{};
        double uniformHemisphereChiSquare{};
        double maximumReferenceError{};
    };

    [[nodiscard]] ProbeBsdf::BsdfParamsL6 ToReferenceBsdf(
        const Mega::PbrMaterialGpu& material) noexcept
    {
        ProbeBsdf::BsdfParamsL6 parameters{};
        parameters.baseColor = {material.baseColorMetallic.x,
            material.baseColorMetallic.y, material.baseColorMetallic.z};
        parameters.metallic = material.baseColorMetallic.w;
        parameters.f0 = {material.f0.x, material.f0.y, material.f0.z};
        parameters.perceptualRoughness = material.emissiveRoughness.w;
        parameters.conductorEta = {
            material.conductorEta.x, material.conductorEta.y, material.conductorEta.z};
        parameters.transmission = material.transmissionIor.x;
        parameters.conductorK = {
            material.conductorK.x, material.conductorK.y, material.conductorK.z};
        parameters.model = material.metadata.x;
        parameters.allowedLobes = material.metadata.y;
        parameters.useComplexConductorFresnel = material.metadata.z;
        if (parameters.model == ProbeBsdf::kModelRoughDielectric &&
            std::isfinite(parameters.perceptualRoughness) &&
            parameters.perceptualRoughness >= 0.0 &&
            parameters.perceptualRoughness * parameters.perceptualRoughness <
                ProbeBsdf::kMinimumAlpha)
        {
            parameters.model = ProbeBsdf::kModelSmoothGlass;
            if ((parameters.allowedLobes & ProbeBsdf::kLobeGlossy) != 0u)
            {
                parameters.allowedLobes =
                    (parameters.allowedLobes & ~ProbeBsdf::kLobeGlossy) |
                    ProbeBsdf::kLobeSpecular;
            }
        }
        return parameters;
    }

    void RequireProbeNear(
        const float actual,
        const double expected,
        const double absoluteTolerance,
        const double relativeTolerance,
        const std::string_view field,
        const std::uint32_t sampleIndex,
        double& maximumReferenceError)
    {
        const double error = std::abs(static_cast<double>(actual) - expected);
        maximumReferenceError = std::max(maximumReferenceError, error);
        const double tolerance = absoluteTolerance +
            relativeTolerance * std::abs(expected);
        if (!std::isfinite(actual) || !std::isfinite(expected) || error > tolerance)
        {
            std::ostringstream message;
            message << "Sampling probe " << field << " mismatch at sample "
                << sampleIndex << ": GPU=" << actual << ", CPU=" << expected
                << ", error=" << error << ", tolerance=" << tolerance;
            throw GateError(message.str());
        }
    }

    [[nodiscard]] SamplingProbeResult ValidateSamplingProbe(
        const std::span<const Mega::Float4> records,
        const std::uint32_t probeSampleCount,
        const Mega::MegakernelFrameConstantsGpu& frame,
        const std::span<const Mega::PbrMaterialGpu> materials)
    {
        constexpr std::uint32_t recordStride = 7u;
        constexpr std::size_t histogramBinCount = 16u;
        Require(records.size() ==
                static_cast<std::size_t>(probeSampleCount) * recordStride,
            "Sampling probe readback has an unexpected record count.");
        Require(!materials.empty() && materials.size() == frame.trace.z,
            "Sampling probe material table does not match its frame contract.");

        const ProbeBsdf::Vec3 outgoing{0.0, 1.0, 0.0};
        ProbeBsdf::Vec3 evaluationDirection{};
        Require(ProbeBsdf::SafeNormalize(
                {0.3, 0.9, 0.1}, evaluationDirection),
            "Sampling probe CPU evaluation direction is invalid.");

        struct ReferenceMaterial final
        {
            ProbeBsdf::BsdfContextL6 context{};
            ProbeBsdf::BsdfParamsL6 parameters{};
            ProbeBsdf::BsdfEvalL6 fixedEvaluation{};
        };
        std::vector<ReferenceMaterial> references;
        references.reserve(materials.size());
        std::array<bool, 6u> coveredModels{};
        for (const Mega::PbrMaterialGpu& material : materials)
        {
            ReferenceMaterial reference{};
            reference.context.geometricNormal = {0.0, 1.0, 0.0};
            reference.context.shadingNormal = reference.context.geometricNormal;
            reference.context.tangent = {1.0, 0.0, 0.0};
            reference.context.etaIncident = 1.0;
            reference.context.etaTransmitted = material.transmissionIor.y;
            reference.context.transportMode = ProbeBsdf::kTransportRadiance;
            reference.parameters = ToReferenceBsdf(material);
            reference.fixedEvaluation = ProbeBsdf::EvaluateBsdfL6(
                reference.context, reference.parameters, outgoing, evaluationDirection);
            if (reference.parameters.model < coveredModels.size())
            {
                coveredModels[reference.parameters.model] = true;
            }
            references.push_back(reference);
        }

        const std::uint64_t baseSeed =
            static_cast<std::uint64_t>(frame.sampling.x) |
            (static_cast<std::uint64_t>(frame.sampling.y) << 32u);
        const std::uint32_t streamTag = frame.output.x;
        std::array<std::uint32_t, histogramBinCount> histogram{};
        SamplingProbeResult result{};
        result.samples = probeSampleCount;
        result.materialModelsCovered = static_cast<std::uint32_t>(std::count(
            coveredModels.begin(), coveredModels.end(), true));

        for (std::uint32_t sample = 0u; sample < probeSampleCount; ++sample)
        {
            const auto record = [&](const std::uint32_t index) -> const Mega::Float4&
            {
                return records[static_cast<std::size_t>(sample) * recordStride + index];
            };
            for (std::uint32_t index = 0u; index < recordStride; ++index)
            {
                const Mega::Float4& value = record(index);
                Require(std::isfinite(value.x) && std::isfinite(value.y) &&
                        std::isfinite(value.z) && std::isfinite(value.w),
                    "Sampling probe returned a non-finite record.");
            }

            const std::uint32_t sampleIndex = frame.image.z + sample;
            const ReferenceMaterial& reference =
                references[sample % references.size()];
            const ProbeBsdf::BsdfContextL6& context = reference.context;
            const ProbeBsdf::BsdfParamsL6& parameters = reference.parameters;
            const ProbeBsdf::BsdfEvalL6& expectedEvaluation =
                reference.fixedEvaluation;
            const float expectedRng0 = Mega::CounterRandomFloat(
                0u, sampleIndex, 0u, streamTag, baseSeed);
            const float expectedLightSelection = Mega::CounterRandomFloat(
                0u, sampleIndex,
                Mega::BounceSampleDimension(
                    0u, Mega::BounceDimension::LightSelection),
                streamTag, baseSeed);
            RequireProbeNear(record(0u).x, expectedRng0, 0.0, 0.0,
                "rng0", sample, result.maximumReferenceError);
            RequireProbeNear(record(0u).y, expectedLightSelection, 0.0, 0.0,
                "light-selection RNG", sample, result.maximumReferenceError);
            Require(record(0u).z >= 0.0f && record(0u).z <= 1.0f &&
                    record(0u).w >= 0.0f,
                "Sampling probe returned an invalid light PMF/PDF.");

            const ProbeBsdf::Vec3 uniformDirection{
                record(4u).x, record(4u).y, record(4u).z};
            Require(std::abs(ProbeBsdf::Dot(uniformDirection, uniformDirection) - 1.0)
                    <= 2.0e-5 && uniformDirection.z >= 0.0 &&
                    uniformDirection.z <= 1.0,
                "Sampling probe uniform-hemisphere direction is invalid.");
            RequireProbeNear(record(4u).z, expectedRng0, 2.0e-7, 0.0,
                "uniform-hemisphere z", sample, result.maximumReferenceError);
            RequireProbeNear(record(4u).w, 0.5 * ProbeBsdf::kInversePi,
                2.0e-7, 2.0e-6, "uniform-hemisphere PDF", sample,
                result.maximumReferenceError);
            const std::size_t bin = std::min<std::size_t>(
                static_cast<std::size_t>(uniformDirection.z * histogramBinCount),
                histogramBinCount - 1u);
            ++histogram[bin];

            RequireProbeNear(record(1u).x, expectedEvaluation.value.x,
                3.0e-5, 5.0e-4, "Evaluate.value.x", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(1u).y, expectedEvaluation.value.y,
                3.0e-5, 5.0e-4, "Evaluate.value.y", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(1u).z, expectedEvaluation.value.z,
                3.0e-5, 5.0e-4, "Evaluate.value.z", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(1u).w, expectedEvaluation.pdf,
                3.0e-5, 5.0e-4, "Evaluate.pdf", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(2u).x, expectedEvaluation.diffuseValue.x,
                3.0e-5, 5.0e-4, "Evaluate.diffuse.x", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(2u).y, expectedEvaluation.diffuseValue.y,
                3.0e-5, 5.0e-4, "Evaluate.diffuse.y", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(2u).z, expectedEvaluation.diffuseValue.z,
                3.0e-5, 5.0e-4, "Evaluate.diffuse.z", sample,
                result.maximumReferenceError);
            Require(record(2u).w == static_cast<float>(expectedEvaluation.isValid),
                "Sampling probe fixed-evaluation validity differs from the CPU oracle.");

            const ProbeBsdf::Vec3 randomSample{
                Mega::CounterRandomFloat(0u, sampleIndex,
                    Mega::BounceSampleDimension(0u, Mega::BounceDimension::BsdfLobe),
                    streamTag, baseSeed),
                Mega::CounterRandomFloat(0u, sampleIndex,
                    Mega::BounceSampleDimension(0u, Mega::BounceDimension::BsdfU),
                    streamTag, baseSeed),
                Mega::CounterRandomFloat(0u, sampleIndex,
                    Mega::BounceSampleDimension(0u, Mega::BounceDimension::BsdfV),
                    streamTag, baseSeed)};
            const ProbeBsdf::BsdfSampleL6 expectedSample =
                ProbeBsdf::SampleBsdfL6(
                    context, parameters, outgoing, randomSample);
            const bool gpuValid = record(5u).w == 1.0f;
            Require(record(5u).w == 0.0f || gpuValid,
                "Sampling probe returned a non-Boolean BSDF validity value.");
            Require(gpuValid == (expectedSample.isValid != 0u),
                "Sampling probe BSDF validity differs from the CPU oracle.");
            if (!gpuValid)
            {
                ++result.rejectedBsdfSamples;
                Require(record(3u).x == 0.0f && record(3u).y == 0.0f &&
                        record(3u).z == 0.0f && record(3u).w == 0.0f &&
                        record(6u).x == 0.0f,
                    "Rejected sampling-probe BSDF sample carries a contribution.");
                continue;
            }

            ++result.validBsdfSamples;
            RequireProbeNear(record(5u).x, expectedSample.direction.x,
                3.0e-4, 8.0e-4, "Sample.direction.x", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(5u).y, expectedSample.direction.y,
                3.0e-4, 8.0e-4, "Sample.direction.y", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(5u).z, expectedSample.direction.z,
                3.0e-4, 8.0e-4, "Sample.direction.z", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(3u).x, expectedSample.value.x,
                5.0e-4, 1.0e-3, "Sample.value.x", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(3u).y, expectedSample.value.y,
                5.0e-4, 1.0e-3, "Sample.value.y", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(3u).z, expectedSample.value.z,
                5.0e-4, 1.0e-3, "Sample.value.z", sample,
                result.maximumReferenceError);
            RequireProbeNear(record(3u).w, expectedSample.pdf,
                3.0e-4, 1.0e-3, "Sample.pdf", sample,
                result.maximumReferenceError);
            Require(record(6u).y == static_cast<float>(expectedSample.measure) &&
                    record(6u).z == static_cast<float>(expectedSample.isDelta) &&
                    record(6u).w == static_cast<float>(expectedSample.lobeFlags),
                "Sampling probe BSDF measure/delta/lobe metadata differs from the CPU oracle.");
            if (expectedSample.isDelta == 0u)
            {
                const double independentPdf = ProbeBsdf::PdfBsdfL6(
                    context, parameters, outgoing, expectedSample.direction);
                RequireProbeNear(record(6u).x, independentPdf,
                    3.0e-4, 1.0e-3, "independent PdfBsdf", sample,
                    result.maximumReferenceError);
                RequireProbeNear(record(6u).x, record(3u).w,
                    3.0e-6, 2.0e-5, "GPU Sample/Pdf consistency", sample,
                    result.maximumReferenceError);
            }
        }

        const double expectedPerBin =
            static_cast<double>(probeSampleCount) / histogramBinCount;
        for (const std::uint32_t observed : histogram)
        {
            const double difference = static_cast<double>(observed) - expectedPerBin;
            result.uniformHemisphereChiSquare +=
                difference * difference / expectedPerBin;
        }
        Require(result.uniformHemisphereChiSquare <= 45.0,
            "Sampling probe uniform-hemisphere chi-square gate failed.");
        Require(result.validBsdfSamples + result.rejectedBsdfSamples ==
                probeSampleCount,
            "Sampling probe did not classify every BSDF sample.");
        Require(result.materialModelsCovered == coveredModels.size(),
            "Sampling probe did not exercise every L6 BSDF model.");
        return result;
    }

    struct MegakernelGateResult final
    {
        double softwareMilliseconds{};
        double rayQueryMilliseconds{};
        double relativeRmse{};
        float maximumAbsoluteError{};
        double softwareMeanLuminance{};
        double rayQueryMeanLuminance{};
        std::uint32_t cameraRays{};
        std::uint32_t softwareRejectedBsdfSamples{};
        std::uint32_t rayQueryRejectedBsdfSamples{};
        SamplingProbeResult samplingProbe{};
    };

    [[nodiscard]] MegakernelGateResult RunMegakernelImageGate(
        VulkanContext& context,
        const Scene::CanonicalScene& canonical,
        const Gpu::CanonicalTraversalScene& traversal,
        const L4::FlatBuildResult& flattened,
        const Hardware::DeviceBufferAllocator& allocator,
        const Hardware::RayQueryBackend& rayQueryBackend,
        const L4::SoftwareGpuTraversalBackend& softwareBackend,
        const VkDescriptorSet sceneSet,
        const VkDescriptorSet softwareTraversalSet,
        const VkDescriptorSet rayQueryTraversalSet,
        const DummyAlphaResources& dummyEnvironment,
        const std::span<const std::uint32_t> softwareMegakernelSpirv,
        const std::span<const std::uint32_t> rayQueryMegakernelSpirv,
        const std::span<const std::uint32_t> samplingProbeSpirv)
    {
        constexpr std::uint32_t width = 16u;
        constexpr std::uint32_t height = 16u;
        // A single sample is sufficient to prove the monolithic route and
        // backend parity. Multi-SPP convergence belongs to the dedicated L6
        // statistical suite and can exceed the Windows watchdog when both
        // backends are serialized into this traversal gate submission.
        constexpr std::uint32_t samplesPerPixel = 1u;
        constexpr std::uint32_t probeSampleCount = 16384u;
        constexpr std::uint32_t probeRecordStride = 7u;
        constexpr std::uint32_t imageSetCount = samplesPerPixel * 2u;
        constexpr std::uint32_t probeSetIndex = imageSetCount;
        constexpr std::uint32_t setCount = imageSetCount + 1u;
        constexpr VkDeviceSize pixelBytes = sizeof(float) * 4u;
        constexpr VkDeviceSize imageBytes = width * height * pixelBytes;
        constexpr VkDeviceSize probeBytes =
            probeSampleCount * probeRecordStride * sizeof(Mega::Float4);
        constexpr VkDeviceSize counterBytes =
            sizeof(std::uint32_t) * static_cast<std::uint32_t>(Mega::Counter::Count);

        MegakernelSceneData data = BuildMegakernelSceneData(canonical, traversal);
        AppendSamplingProbeMaterials(data.materials);
        const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Hardware::DeviceBuffer materialBuffer = UploadBuffer(
            allocator, std::span<const Mega::PbrMaterialGpu>{data.materials}, storageUsage);
        Hardware::DeviceBuffer lightBuffer = UploadBuffer(
            allocator, std::span<const Mega::PbrLightGpu>{data.lights}, storageUsage);
        Hardware::DeviceBuffer aliasBuffer = UploadBuffer(
            allocator, std::span<const Mega::AliasEntryGpu>{data.lightAlias}, storageUsage);
        Hardware::DeviceBuffer environmentRowBuffer = UploadBuffer(
            allocator, std::span<const Mega::AliasEntryGpu>{data.environmentRows}, storageUsage);
        Hardware::DeviceBuffer environmentColumnBuffer = UploadBuffer(
            allocator, std::span<const Mega::AliasEntryGpu>{data.environmentColumns}, storageUsage);
        Hardware::DeviceBuffer selectionPmfBuffer = UploadBuffer(
            allocator, std::span<const float>{data.lightSelectionPmf}, storageUsage);
        Hardware::DeviceBuffer emitterMapBuffer = UploadBuffer(
            allocator, std::span<const Mega::EmitterMapEntryGpu>{data.emitterMap}, storageUsage);
        Hardware::DeviceBuffer fixtureTriangleBuffer = UploadBuffer(
            allocator, std::span<const Mega::FixtureTriangleGpu>{data.fixtureTriangles}, storageUsage);
        Hardware::DeviceBuffer fixtureSphereBuffer = UploadBuffer(
            allocator, std::span<const Mega::FixtureSphereGpu>{data.fixtureSpheres}, storageUsage);
        const std::array storageInputs{
            Descriptor(materialBuffer), Descriptor(lightBuffer), Descriptor(aliasBuffer),
            Descriptor(environmentRowBuffer), Descriptor(environmentColumnBuffer),
            Descriptor(selectionPmfBuffer), Descriptor(emitterMapBuffer),
            Descriptor(fixtureTriangleBuffer), Descriptor(fixtureSphereBuffer)};

        const VkDeviceSize frameStride = AlignUp(sizeof(Mega::MegakernelFrameConstantsGpu),
            std::max<VkDeviceSize>(16u, context.UniformBufferAlignment()));
        Hardware::DeviceBuffer frameBuffer = CreateMappedBuffer(
            allocator, frameStride * setCount, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        auto* const frameBytes = static_cast<std::byte*>(frameBuffer.MappedData());
        for (std::uint32_t sample = 0u; sample < samplesPerPixel; ++sample)
        {
            const Mega::MegakernelFrameConstantsGpu softwareFrame = BuildMegakernelFrame(
                canonical, data, width, height, sample, Mega::TraversalBackend::FlattenedSah,
                static_cast<std::uint32_t>(flattened.bvh.nodes.size()),
                static_cast<std::uint32_t>(flattened.bvh.primitives.size()));
            const Mega::MegakernelFrameConstantsGpu rayQueryFrame = BuildMegakernelFrame(
                canonical, data, width, height, sample,
                Mega::TraversalBackend::HardwareRayQuery, 0u, 0u);
            std::memcpy(frameBytes + frameStride * sample,
                &softwareFrame, sizeof(softwareFrame));
            std::memcpy(frameBytes + frameStride * (samplesPerPixel + sample),
                &rayQueryFrame, sizeof(rayQueryFrame));
        }
        Mega::MegakernelFrameConstantsGpu probeFrame = BuildMegakernelFrame(
            canonical, data, width, height, 0u,
            Mega::TraversalBackend::FlattenedSah,
            static_cast<std::uint32_t>(flattened.bvh.nodes.size()),
            static_cast<std::uint32_t>(flattened.bvh.primitives.size()));
        probeFrame.output.z = probeSampleCount;
        Require(Mega::ValidateMegakernelFrameConstants(probeFrame),
            "Host rejected generated sampling-probe frame constants.");
        std::memcpy(frameBytes + frameStride * probeSetIndex,
            &probeFrame, sizeof(probeFrame));

        Hardware::DeviceBuffer softwareCounters = CreateMappedBuffer(
            allocator, counterBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Hardware::DeviceBuffer rayQueryCounters = CreateMappedBuffer(
            allocator, counterBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Hardware::DeviceBuffer probeCounters = CreateMappedBuffer(
            allocator, counterBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Hardware::DeviceBuffer probeOutput = CreateMappedBuffer(
            allocator, probeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Hardware::DeviceBuffer softwareReadback = CreateMappedBuffer(
            allocator, imageBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Hardware::DeviceBuffer rayQueryReadback = CreateMappedBuffer(
            allocator, imageBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        MegakernelImages softwareImages(context, width, height);
        MegakernelImages rayQueryImages(context, width, height);

        MegakernelPipelines pipelines(context.Device(), softwareMegakernelSpirv,
            rayQueryMegakernelSpirv, samplingProbeSpirv,
            rayQueryBackend.SceneAdapterLayout(),
            softwareBackend.TraversalLayout(), rayQueryBackend.TraversalLayout(), setCount);
        const std::vector<VkDescriptorSet> sets = pipelines.AllocateSets(setCount);
        const auto softwareImageDescriptors = softwareImages.Descriptors();
        const auto rayQueryImageDescriptors = rayQueryImages.Descriptors();
        for (std::uint32_t sample = 0u; sample < samplesPerPixel; ++sample)
        {
            const VkDescriptorBufferInfo softwareFrame{
                frameBuffer.Handle(), frameStride * sample,
                sizeof(Mega::MegakernelFrameConstantsGpu)};
            const VkDescriptorBufferInfo rayQueryFrame{
                frameBuffer.Handle(), frameStride * (samplesPerPixel + sample),
                sizeof(Mega::MegakernelFrameConstantsGpu)};
            UpdateMegakernelDescriptorSet(context.Device(), sets[sample], softwareFrame,
                storageInputs, dummyEnvironment.EnvironmentImageInfo(),
                dummyEnvironment.SamplerInfo(), softwareImageDescriptors,
                Descriptor(softwareCounters));
            UpdateMegakernelDescriptorSet(context.Device(),
                sets[samplesPerPixel + sample], rayQueryFrame, storageInputs,
                dummyEnvironment.EnvironmentImageInfo(), dummyEnvironment.SamplerInfo(),
                rayQueryImageDescriptors, Descriptor(rayQueryCounters));
        }
        const VkDescriptorBufferInfo probeFrameDescriptor{
            frameBuffer.Handle(), frameStride * probeSetIndex,
            sizeof(Mega::MegakernelFrameConstantsGpu)};
        UpdateSamplingProbeDescriptorSet(context.Device(), sets[probeSetIndex],
            probeFrameDescriptor, storageInputs,
            dummyEnvironment.EnvironmentImageInfo(), dummyEnvironment.SamplerInfo(),
            softwareImageDescriptors, Descriptor(probeCounters), Descriptor(probeOutput));

        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdResetQueryPool(commandBuffer, pipelines.QueryPool(), 0u, 4u);
            softwareImages.RecordInitialize(context.Dispatch(), commandBuffer);
            rayQueryImages.RecordInitialize(context.Dispatch(), commandBuffer);
            vkCmdFillBuffer(commandBuffer, softwareCounters.Handle(), 0u,
                VK_WHOLE_SIZE, 0u);
            vkCmdFillBuffer(commandBuffer, rayQueryCounters.Handle(), 0u,
                VK_WHOLE_SIZE, 0u);
            vkCmdFillBuffer(commandBuffer, probeCounters.Handle(), 0u,
                VK_WHOLE_SIZE, 0u);
            std::array<VkBufferMemoryBarrier2, 3u> counterBarriers{};
            const std::array counterHandles{
                softwareCounters.Handle(), rayQueryCounters.Handle(), probeCounters.Handle()};
            for (std::size_t index = 0u; index < counterBarriers.size(); ++index)
            {
                counterBarriers[index] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                counterBarriers[index].srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
                counterBarriers[index].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                counterBarriers[index].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                counterBarriers[index].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                counterBarriers[index].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                counterBarriers[index].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                counterBarriers[index].buffer = counterHandles[index];
                counterBarriers[index].offset = 0u;
                counterBarriers[index].size = VK_WHOLE_SIZE;
            }
            VkDependencyInfo counterDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            counterDependency.bufferMemoryBarrierCount =
                static_cast<std::uint32_t>(counterBarriers.size());
            counterDependency.pBufferMemoryBarriers = counterBarriers.data();
            context.Dispatch().cmdPipelineBarrier2(commandBuffer, &counterDependency);

            const auto dispatchBackend = [&](const MegakernelPipelines::Backend backend,
                                             const VkDescriptorSet traversalSet,
                                             const std::uint32_t firstSet,
                                             const std::uint32_t beginQuery,
                                             const std::uint32_t endQuery,
                                             const MegakernelImages& images)
            {
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    pipelines.QueryPool(), beginQuery);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipelines.Pipeline(backend));
                for (std::uint32_t sample = 0u; sample < samplesPerPixel; ++sample)
                {
                    const std::array descriptorSets{
                        sets[firstSet + sample], sceneSet, traversalSet};
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipelines.PipelineLayout(backend), 0u,
                        static_cast<std::uint32_t>(descriptorSets.size()),
                        descriptorSets.data(), 0u, nullptr);
                    vkCmdDispatch(commandBuffer, width, height, 1u);
                    if (sample + 1u < samplesPerPixel)
                    {
                        images.RecordSampleBarrier(context.Dispatch(), commandBuffer);
                    }
                }
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    pipelines.QueryPool(), endQuery);
            };
            dispatchBackend(MegakernelPipelines::Backend::Software, softwareTraversalSet,
                0u, 0u, 1u, softwareImages);
            dispatchBackend(MegakernelPipelines::Backend::RayQuery, rayQueryTraversalSet,
                samplesPerPixel, 2u, 3u, rayQueryImages);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipelines.Pipeline(MegakernelPipelines::Backend::SamplingProbe));
            const std::array probeDescriptorSets{
                sets[probeSetIndex], sceneSet, softwareTraversalSet};
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipelines.PipelineLayout(MegakernelPipelines::Backend::SamplingProbe),
                0u, static_cast<std::uint32_t>(probeDescriptorSets.size()),
                probeDescriptorSets.data(), 0u, nullptr);
            vkCmdDispatch(commandBuffer, (probeSampleCount + 63u) / 64u, 1u, 1u);

            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{softwareCounters.Handle(), rayQueryCounters.Handle(),
                    probeCounters.Handle(), probeOutput.Handle()});
            softwareImages.RecordRawCopy(
                context.Dispatch(), commandBuffer, softwareReadback.Handle());
            rayQueryImages.RecordRawCopy(
                context.Dispatch(), commandBuffer, rayQueryReadback.Handle());
        });

        std::array<std::uint64_t, 4u> timestamps{};
        CheckVk(vkGetQueryPoolResults(context.Device(), pipelines.QueryPool(), 0u, 4u,
            sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
            "vkGetQueryPoolResults(megakernel)");
        const auto milliseconds = [&](const std::size_t begin, const std::size_t end)
        {
            Require(timestamps[end] >= timestamps[begin],
                "Megakernel timestamp interval is reversed.");
            return static_cast<double>(timestamps[end] - timestamps[begin]) *
                static_cast<double>(context.TimestampPeriod()) * 1.0e-6;
        };

        constexpr std::size_t valueCount = width * height * 4u;
        const std::span<const float> softwarePixels{
            static_cast<const float*>(softwareReadback.MappedData()), valueCount};
        const std::span<const float> rayQueryPixels{
            static_cast<const float*>(rayQueryReadback.MappedData()), valueCount};
        double squaredError = 0.0;
        double squaredReference = 0.0;
        double softwareLuminance = 0.0;
        double rayQueryLuminance = 0.0;
        float maximumAbsoluteError = 0.0f;
        for (std::size_t pixel = 0u; pixel < width * height; ++pixel)
        {
            for (std::size_t channel = 0u; channel < 4u; ++channel)
            {
                const float softwareValue = softwarePixels[pixel * 4u + channel];
                const float rayQueryValue = rayQueryPixels[pixel * 4u + channel];
                Require(std::isfinite(softwareValue) && std::isfinite(rayQueryValue),
                    "Megakernel returned a non-finite image value.");
                if (channel < 3u)
                {
                    Require(softwareValue >= -1.0e-6f && rayQueryValue >= -1.0e-6f,
                        "Megakernel returned negative radiance.");
                    const double difference =
                        static_cast<double>(softwareValue) - rayQueryValue;
                    squaredError += difference * difference;
                    squaredReference += static_cast<double>(rayQueryValue) * rayQueryValue;
                    maximumAbsoluteError = std::max(maximumAbsoluteError,
                        std::abs(softwareValue - rayQueryValue));
                }
            }
            softwareLuminance += 0.2126 * softwarePixels[pixel * 4u] +
                0.7152 * softwarePixels[pixel * 4u + 1u] +
                0.0722 * softwarePixels[pixel * 4u + 2u];
            rayQueryLuminance += 0.2126 * rayQueryPixels[pixel * 4u] +
                0.7152 * rayQueryPixels[pixel * 4u + 1u] +
                0.0722 * rayQueryPixels[pixel * 4u + 2u];
        }
        const double sampleCount = static_cast<double>(width * height * 3u);
        const double rmse = std::sqrt(squaredError / sampleCount);
        const double referenceRms = std::sqrt(squaredReference / sampleCount);
        const double relativeRmse = rmse / std::max(1.0e-6, referenceRms);
        softwareLuminance /= static_cast<double>(width * height);
        rayQueryLuminance /= static_cast<double>(width * height);
        Require(relativeRmse <= 1.0e-2 && maximumAbsoluteError <= 5.0e-2f,
            "Software and Ray Query Megakernel images exceed the Wave 2 parity tolerance.");
        if (canonical.stableId == Scene::kWave2CanonicalCornellStableId)
        {
            Require(softwareLuminance > 1.0e-4 && rayQueryLuminance > 1.0e-4,
                "Cornell Megakernel image contains no measurable light transport.");
        }

        std::array<std::uint32_t, static_cast<std::size_t>(Mega::Counter::Count)>
            softwareCounterValues{};
        std::array<std::uint32_t, static_cast<std::size_t>(Mega::Counter::Count)>
            rayQueryCounterValues{};
        std::array<std::uint32_t, static_cast<std::size_t>(Mega::Counter::Count)>
            probeCounterValues{};
        std::memcpy(softwareCounterValues.data(), softwareCounters.MappedData(), counterBytes);
        std::memcpy(rayQueryCounterValues.data(), rayQueryCounters.MappedData(), counterBytes);
        std::memcpy(probeCounterValues.data(), probeCounters.MappedData(), counterBytes);
        Require(std::all_of(probeCounterValues.begin(), probeCounterValues.end(),
                [](const std::uint32_t value) { return value == 0u; }),
            "Sampling probe unexpectedly incremented a Megakernel diagnostic counter.");
        const std::span<const Mega::Float4> probeRecords{
            static_cast<const Mega::Float4*>(probeOutput.MappedData()),
            static_cast<std::size_t>(probeSampleCount) * probeRecordStride};
        const SamplingProbeResult samplingProbe = ValidateSamplingProbe(
            probeRecords, probeSampleCount, probeFrame,
            std::span<const Mega::PbrMaterialGpu>{data.materials});
        // InvalidBsdfSample is deliberately not in this hard-failure set:
        // visible-normal GGX sampling can reflect below the macrosurface and
        // that event is a valid zero-contribution rejection. Both backends
        // must nevertheless report its actual count for parity review.
        constexpr std::array invalidCounters{
            Mega::Counter::ZeroPdf,
            Mega::Counter::NegativePdf,
            Mega::Counter::NonFinitePdf,
            Mega::Counter::NonFiniteBsdf,
            Mega::Counter::NonFiniteThroughput,
            Mega::Counter::NonFiniteRadiance,
            Mega::Counter::NegativeContribution,
            Mega::Counter::InvalidMaterial,
            Mega::Counter::InvalidBsdfEvaluation,
            Mega::Counter::InvalidFrame};
        for (const Mega::Counter counter : invalidCounters)
        {
            const std::size_t index = static_cast<std::size_t>(counter);
            if (softwareCounterValues[index] != 0u || rayQueryCounterValues[index] != 0u)
            {
                throw GateError("Megakernel diagnostic counter " +
                    std::to_string(index) + " is non-zero for scene " +
                    canonical.stableId + " (software=" +
                    std::to_string(softwareCounterValues[index]) + ", ray-query=" +
                    std::to_string(rayQueryCounterValues[index]) + ").");
            }
        }
        const std::size_t cameraRayIndex =
            static_cast<std::size_t>(Mega::Counter::CameraRays);
        const std::uint32_t expectedCameraRays = width * height * samplesPerPixel;
        if constexpr (kMegakernelProfilerCountersEnabled)
        {
            Require(softwareCounterValues[cameraRayIndex] == expectedCameraRays &&
                    rayQueryCounterValues[cameraRayIndex] == expectedCameraRays,
                "Megakernel camera-ray counter does not match the dispatched corpus.");
        }
        else
        {
            Require(std::all_of(
                    softwareCounterValues.begin(),
                    softwareCounterValues.end(),
                    [](const std::uint32_t value) { return value == 0u; }) &&
                    std::all_of(
                        rayQueryCounterValues.begin(),
                        rayQueryCounterValues.end(),
                        [](const std::uint32_t value) { return value == 0u; }),
                "No-profile Megakernel unexpectedly wrote profiler counters.");
        }

        return {
            milliseconds(0u, 1u),
            milliseconds(2u, 3u),
            relativeRmse,
            maximumAbsoluteError,
            softwareLuminance,
            rayQueryLuminance,
            expectedCameraRays,
            softwareCounterValues[static_cast<std::size_t>(Mega::Counter::InvalidBsdfSample)],
            rayQueryCounterValues[static_cast<std::size_t>(Mega::Counter::InvalidBsdfSample)],
            samplingProbe};
    }

    struct SceneReport final
    {
        std::string stableId{};
        std::size_t rays{};
        std::size_t closestHits{};
        double blasMilliseconds{};
        double tlasMilliseconds{};
        double softwareMilliseconds{};
        double rayQueryMilliseconds{};
        double rtPipelineMilliseconds{};
        MegakernelGateResult megakernel{};
        L4::GpuTraversalCounterReadback counters{};
    };

    [[nodiscard]] Scene::CanonicalScene BuildAlphaMaskGateScene()
    {
        Scene::CanonicalScene scene = Scene::BuildCanonicalTriangleScene();
        scene.stableId = "canonical-alpha-mask";
        scene.cameras.front().stableId = "camera:canonical-alpha-mask";

        Abi0::GpuMaterialV0 opaqueMaterial = scene.materials.front();
        opaqueMaterial.baseColorFactor = {0.10f, 0.65f, 0.18f, 1.0f};
        opaqueMaterial.metadata = {
            Abi0::MaterialModelMetallicRoughness, Abi0::MaterialFlagNone, 1u, 0u};

        Abi0::GpuMaterialV0& maskedMaterial = scene.materials.front();
        maskedMaterial.baseColorFactor.w = 1.0f;
        maskedMaterial.surfaceParams.w = 0.5f;
        maskedMaterial.textureImageIndices.x = 0u;
        maskedMaterial.textureSamplerIndices.x = 0u;
        maskedMaterial.metadata.y = Abi0::MaterialFlagAlphaMask;
        scene.materials.push_back(opaqueMaterial);

        const std::array frontVertices{
            scene.vertices[0], scene.vertices[1], scene.vertices[2]};
        for (Abi0::GpuVertexV0 vertex : frontVertices)
        {
            vertex.position.z = -0.35f;
            scene.vertices.push_back(vertex);
        }
        scene.indices = {0u, 1u, 2u, 3u, 4u, 5u};

        Abi0::GpuGeometryV0 front = scene.geometries.front();
        front.indexRange = {0u, 3u, 0u, 0u};
        front.identity = {0u, 0u, 0u, Abi0::GeometryFlagAlphaMask};
        Abi0::GpuGeometryV0 back = front;
        back.indexRange = {3u, 3u, 0u, 1u};
        back.identity = {1u, 0u, 1u, Abi0::GeometryFlagOpaque};
        back.localBoundsMin.z = -0.35f;
        back.localBoundsMax.z = -0.35f;
        scene.geometries = {front, back};
        scene.instances.front().metadata.y = 2u;

        scene.constants.counts0 = {6u, 6u, 2u, 1u};
        scene.constants.counts1 = {2u, 1u, 2u, 1u};
        scene.constants.sceneBoundsMin.z = -0.35f;
        scene.constants.versionFlags.z = Abi0::SceneFlagHasAlphaMask;
        const Scene::CanonicalSceneValidation validation = Scene::ValidateCanonicalScene(scene);
        Require(static_cast<bool>(validation),
            "Synthetic alpha-mask scene is invalid: " + validation.reason);
        return scene;
    }

    class RawSceneHandles final
    {
    public:
        explicit RawSceneHandles(const VkDevice device) noexcept : device_(device) {}
        ~RawSceneHandles()
        {
            if (queryPool != VK_NULL_HANDLE) vkDestroyQueryPool(device_, queryPool, nullptr);
            if (rtPipelinePool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, rtPipelinePool, nullptr);
            if (canonicalLinearPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, canonicalLinearPool, nullptr);
            if (softwarePool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, softwarePool, nullptr);
            if (rayQueryPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, rayQueryPool, nullptr);
        }
        VkDescriptorPool rayQueryPool{VK_NULL_HANDLE};
        VkDescriptorPool rtPipelinePool{VK_NULL_HANDLE};
        VkDescriptorPool softwarePool{VK_NULL_HANDLE};
        VkDescriptorPool canonicalLinearPool{VK_NULL_HANDLE};
        VkQueryPool queryPool{VK_NULL_HANDLE};
    private:
        VkDevice device_{VK_NULL_HANDLE};
    };

    [[nodiscard]] VkTransformMatrixKHR ToVkTransform(const Abi0::AbiMat4Rows& transform) noexcept
    {
        VkTransformMatrixKHR result{};
        const std::array rows{transform.row0, transform.row1, transform.row2};
        for (std::size_t row = 0u; row < rows.size(); ++row)
        {
            result.matrix[row][0] = rows[row].x;
            result.matrix[row][1] = rows[row].y;
            result.matrix[row][2] = rows[row].z;
            result.matrix[row][3] = rows[row].w;
        }
        return result;
    }

    [[nodiscard]] SceneReport RunScene(
        VulkanContext& context,
        const Scene::CanonicalScene& canonical,
        const std::span<const std::uint32_t> softwareSpirv,
        const std::span<const std::uint32_t> rayQuerySpirv,
        const std::span<const std::uint32_t> rtPipelineSpirv,
        const std::span<const std::uint32_t> softwareMegakernelSpirv,
        const std::span<const std::uint32_t> rayQueryMegakernelSpirv,
        const std::span<const std::uint32_t> samplingProbeSpirv)
    {
        const Scene::CanonicalSceneValidation validation = Scene::ValidateCanonicalScene(canonical);
        Require(static_cast<bool>(validation), "Canonical scene validation failed: " + validation.reason);
        const Scene::CanonicalSceneView view = Scene::MakeCanonicalSceneView(canonical);
        Gpu::CanonicalTraversalSceneBuild traversalBuild = Gpu::BuildCanonicalTraversalScene(view);
        Require(static_cast<bool>(traversalBuild),
            "Canonical traversal expansion failed: " + traversalBuild.error);
        Gpu::CanonicalTraversalScene& traversal = traversalBuild.scene;
        Cpu::Bvh<float> cpuBvh(traversal.cpuTriangles, Cpu::BvhBuildMethod::BinnedSah);
        L4::FlatBuildResult flattened = L4::FlattenCanonicalL3BinnedSah(
            cpuBvh, traversal.softwareBuildPrimitives);
        Require(flattened.Succeeded(), "L3-to-L4 flatten failed: " + flattened.message);
        Require(flattened.bvh.primitives.size() == traversal.triangles.size(),
            "Flattened L4 primitive count does not match canonical traversal triangles.");
        std::vector<Gpu::CanonicalTraversalTriangle> flattenedTriangles;
        flattenedTriangles.reserve(flattened.bvh.primitives.size());
        for (const L4::SoftwarePrimitiveRecord& primitive : flattened.bvh.primitives)
        {
            Require(primitive.identity.x < traversal.triangles.size(),
                "Flattened L4 primitive references an invalid traversal ID.");
            flattenedTriangles.push_back(traversal.triangles[primitive.identity.x]);
        }

        const std::vector<Abi1::GpuRayQueueRecordV1> rays = BuildRayCorpus(canonical);
        std::vector<CpuOracleHit> cpuOracle;
        cpuOracle.reserve(rays.size());
        for (const Abi1::GpuRayQueueRecordV1& ray : rays)
        {
            cpuOracle.push_back(TraceCpuClosest(
                cpuBvh, traversal.triangles, canonical, ray));
        }

        Hardware::DeviceBufferAllocator allocator(
            context.PhysicalDevice(), context.Device(), context.Dispatch());
        const VkBufferUsageFlags sceneStorageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        const VkBufferUsageFlags asInputUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        Hardware::DeviceBuffer constantsBuffer = UploadBuffer(
            allocator, std::span{&canonical.constants, 1u}, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        Hardware::DeviceBuffer vertexBuffer = UploadBuffer(
            allocator, std::span{canonical.vertices}, asInputUsage, true);
        Hardware::DeviceBuffer indexBuffer = UploadBuffer(
            allocator, std::span{canonical.indices}, asInputUsage, true);
        Hardware::DeviceBuffer geometryBuffer = UploadBuffer(
            allocator, std::span{canonical.geometries}, sceneStorageUsage);
        Hardware::DeviceBuffer instanceSceneBuffer = UploadBuffer(
            allocator, std::span{canonical.instances}, sceneStorageUsage);
        Hardware::DeviceBuffer materialBuffer = UploadBuffer(
            allocator, std::span{canonical.materials}, sceneStorageUsage);
        Hardware::DeviceBuffer lightBuffer = UploadBuffer(
            allocator, std::span{canonical.lights}, sceneStorageUsage);
        Hardware::DeviceBuffer rayBuffer = UploadBuffer(
            allocator, std::span{rays}, sceneStorageUsage);
        const VkDeviceSize hitBufferBytes = rays.size() * 2u * sizeof(Abi1::GpuHitQueueRecordV1);
        Hardware::DeviceBuffer softwareHitBuffer = CreateMappedBuffer(
            allocator, hitBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Hardware::DeviceBuffer canonicalLinearHitBuffer = CreateMappedBuffer(
            allocator, hitBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Hardware::DeviceBuffer rayQueryHitBuffer = CreateMappedBuffer(
            allocator, hitBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Hardware::DeviceBuffer rtPipelineClosestHitBuffer = CreateMappedBuffer(
            allocator, hitBufferBytes / 2u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Hardware::DeviceBuffer rtPipelineAnyHitBuffer = CreateMappedBuffer(
            allocator, hitBufferBytes / 2u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Hardware::DeviceBuffer softwareNodeBuffer = UploadBuffer(
            allocator,
            std::span<const L4::SoftwareNodeRecord>{flattened.bvh.nodes},
            sceneStorageUsage);
        Hardware::DeviceBuffer softwareTriangleBuffer = UploadBuffer(
            allocator,
            std::span<const Gpu::CanonicalTraversalTriangle>{flattenedTriangles},
            sceneStorageUsage);
        Hardware::DeviceBuffer canonicalLinearTriangleBuffer = UploadBuffer(
            allocator,
            std::span<const Gpu::CanonicalTraversalTriangle>{traversal.triangles},
            sceneStorageUsage);
        Hardware::DeviceBuffer softwareCounterBuffer = CreateMappedBuffer(
            allocator, sizeof(L4::GpuTraversalCounterReadback),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Hardware::DeviceBuffer canonicalLinearCounterBuffer = CreateMappedBuffer(
            allocator, sizeof(L4::GpuTraversalCounterReadback),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        Require(canonical.instances.size() == 1u && canonical.instances[0].metadata.x == 0u &&
                canonical.instances[0].metadata.y == canonical.geometries.size(),
            "Wave 2 canonical gate currently requires one instance covering the complete geometry array.");
        std::vector<Hardware::TriangleGeometryInput> geometryInputs;
        geometryInputs.reserve(canonical.geometries.size());
        for (const Abi0::GpuGeometryV0& geometry : canonical.geometries)
        {
            const bool alphaMasked = (geometry.identity.w & Abi0::GeometryFlagAlphaMask) != 0u ||
                (canonical.materials[geometry.identity.z].metadata.y &
                    Abi0::MaterialFlagAlphaMask) != 0u;
            const bool doubleSided = (geometry.identity.w & Abi0::GeometryFlagDoubleSided) != 0u ||
                (canonical.materials[geometry.identity.z].metadata.y &
                    Abi0::MaterialFlagDoubleSided) != 0u;
            Hardware::TriangleGeometryInput input{};
            input.vertexAddress = vertexBuffer.Address();
            input.vertexStride = sizeof(Abi0::GpuVertexV0);
            input.maxVertex = static_cast<std::uint32_t>(canonical.vertices.size() - 1u);
            input.indexAddress = indexBuffer.Address();
            input.indexType = VK_INDEX_TYPE_UINT32;
            input.primitiveCount = geometry.indexRange.y / 3u;
            input.primitiveOffset = geometry.indexRange.x * sizeof(std::uint32_t);
            input.firstVertex = geometry.indexRange.z;
            input.geometryFlags = Hardware::ChooseTriangleGeometryFlags(alphaMasked, doubleSided);
            geometryInputs.push_back(input);
        }

        Hardware::AccelerationStructureBuilder asBuilder(allocator, context.ScratchAlignment());
        Hardware::AccelerationStructureResource blas{};
        Hardware::AccelerationStructureResource tlas{};
        Require(asBuilder.CreateBottomLevel(geometryInputs,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, blas),
            "AccelerationStructureBuilder::CreateBottomLevel");
        Require(asBuilder.CreateTopLevel(1u,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, tlas),
            "AccelerationStructureBuilder::CreateTopLevel");

        VkAccelerationStructureInstanceKHR vkInstance{};
        vkInstance.transform = ToVkTransform(canonical.instances[0].objectToWorld);
        vkInstance.instanceCustomIndex = 0u;
        vkInstance.mask = 0xffu;
        vkInstance.instanceShaderBindingTableRecordOffset = 0u;
        vkInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstance.accelerationStructureReference = blas.Address();
        const std::array vkInstances{vkInstance};
        Hardware::DeviceBuffer asInstanceBuffer{};
        Require(asBuilder.CreateInstanceBuffer(vkInstances, asInstanceBuffer),
            "AccelerationStructureBuilder::CreateInstanceBuffer");
        Hardware::DeviceBuffer scratch{};
        Require(asBuilder.CreateScratchBuffer(
            std::max(blas.BuildScratchSize(), tlas.BuildScratchSize()), scratch),
            "AccelerationStructureBuilder::CreateScratchBuffer");

        Hardware::RayQueryBackend rayQueryBackend{};
        Require(rayQueryBackend.Create(context.Device(), rayQuerySpirv,
            context.MaximumGroupCountX()), "RayQueryBackend::Create");
        Hardware::RtPipelineBackend rtPipelineBackend{};
        Require(rtPipelineBackend.Create(
            allocator, context.Dispatch(), context.RtPipelineLimits(),
            rtPipelineSpirv), "RtPipelineBackend::Create");
        L4::SoftwareGpuTraversalBackend softwareBackend{};
        Require(softwareBackend.Create(L4::SoftwareGpuBackendCreateInfo{
            context.Device(), context.EmptyLayout(), rayQueryBackend.SceneAdapterLayout(),
            softwareSpirv, context.MaximumGroupCountX()}),
            "SoftwareGpuTraversalBackend::Create");
        L4::SoftwareGpuTraversalBackend canonicalLinearBackend{};
        Require(canonicalLinearBackend.Create(L4::SoftwareGpuBackendCreateInfo{
            context.Device(), context.EmptyLayout(), rayQueryBackend.SceneAdapterLayout(),
            softwareSpirv, context.MaximumGroupCountX(), true}),
            "SoftwareGpuTraversalBackend::Create(canonical-linear)");
        const Gpu::GpuTraversalBackendDescriptor flattenedDescriptor =
            softwareBackend.Descriptor();
        const Gpu::GpuTraversalBackendDescriptor canonicalLinearDescriptor =
            canonicalLinearBackend.Descriptor();
        Require(flattenedDescriptor.stableToken == "flattened-cpu-sah"
                && canonicalLinearDescriptor.stableToken == "canonical-linear"
                && flattenedDescriptor.stableToken != canonicalLinearDescriptor.stableToken
                && canonicalLinearDescriptor.supportsClosest
                && canonicalLinearDescriptor.supportsAny
                && !canonicalLinearDescriptor.requiresAccelerationStructure,
            "Software GPU backend descriptors do not distinguish flattened-SAH from canonical-linear.");
        Hardware::RayQueryTraversalAdapter rayQueryAdapter(rayQueryBackend, 2u, 0u);
        RawSceneHandles handles(context.Device());

        Require(rayQueryBackend.CreateDescriptorPool(1u, handles.rayQueryPool),
            "RayQueryBackend::CreateDescriptorPool");
        VkDescriptorSet sceneSet = VK_NULL_HANDLE;
        VkDescriptorSet rayQuerySet = VK_NULL_HANDLE;
        Require(rayQueryBackend.AllocateDescriptorSets(
            handles.rayQueryPool, sceneSet, rayQuerySet),
            "RayQueryBackend::AllocateDescriptorSets");
        Require(rayQueryBackend.UpdateSceneDescriptors(sceneSet,
            Hardware::HardwareSceneBufferBindings{
                Descriptor(constantsBuffer), Descriptor(vertexBuffer), Descriptor(indexBuffer),
                Descriptor(geometryBuffer), Descriptor(instanceSceneBuffer),
                Descriptor(materialBuffer), Descriptor(lightBuffer)}),
            "RayQueryBackend::UpdateSceneDescriptors");

        DummyAlphaResources dummyAlpha(context);
        Require(rayQueryBackend.UpdateTraversalDescriptors(rayQuerySet,
            Hardware::RayQueryTraversalBindings{
                tlas.Handle(), Descriptor(rayBuffer), Descriptor(rayQueryHitBuffer),
                dummyAlpha.ImageInfo(), dummyAlpha.SamplerInfo()}),
            "RayQueryBackend::UpdateTraversalDescriptors");

        Require(rtPipelineBackend.CreateDescriptorPool(
            2u, handles.rtPipelinePool),
            "RtPipelineBackend::CreateDescriptorPool");
        VkDescriptorSet rtClosestSceneSet = VK_NULL_HANDLE;
        VkDescriptorSet rtClosestTraversalSet = VK_NULL_HANDLE;
        VkDescriptorSet rtAnySceneSet = VK_NULL_HANDLE;
        VkDescriptorSet rtAnyTraversalSet = VK_NULL_HANDLE;
        Require(rtPipelineBackend.AllocateDescriptorSets(
            handles.rtPipelinePool, rtClosestSceneSet, rtClosestTraversalSet),
            "RtPipelineBackend::AllocateDescriptorSets(closest)");
        Require(rtPipelineBackend.AllocateDescriptorSets(
            handles.rtPipelinePool, rtAnySceneSet, rtAnyTraversalSet),
            "RtPipelineBackend::AllocateDescriptorSets(any)");
        const Hardware::HardwareSceneBufferBindings rtSceneBindings{
            Descriptor(constantsBuffer), Descriptor(vertexBuffer),
            Descriptor(indexBuffer), Descriptor(geometryBuffer),
            Descriptor(instanceSceneBuffer), Descriptor(materialBuffer),
            Descriptor(lightBuffer)};
        rtPipelineBackend.UpdateSceneDescriptors(
            rtClosestSceneSet, rtSceneBindings);
        rtPipelineBackend.UpdateSceneDescriptors(rtAnySceneSet, rtSceneBindings);
        rtPipelineBackend.UpdateTraversalDescriptors(
            rtClosestTraversalSet,
            Hardware::RayQueryTraversalBindings{
                tlas.Handle(), Descriptor(rayBuffer),
                Descriptor(rtPipelineClosestHitBuffer), dummyAlpha.ImageInfo(),
                dummyAlpha.SamplerInfo()});
        rtPipelineBackend.UpdateTraversalDescriptors(
            rtAnyTraversalSet,
            Hardware::RayQueryTraversalBindings{
                tlas.Handle(), Descriptor(rayBuffer),
                Descriptor(rtPipelineAnyHitBuffer), dummyAlpha.ImageInfo(),
                dummyAlpha.SamplerInfo()});

        Require(softwareBackend.CreateDescriptorPool(1u, handles.softwarePool),
            "SoftwareGpuTraversalBackend::CreateDescriptorPool");
        VkDescriptorSet softwareSet = VK_NULL_HANDLE;
        Require(softwareBackend.AllocateTraversalSet(handles.softwarePool, softwareSet),
            "SoftwareGpuTraversalBackend::AllocateTraversalSet");
        Require(softwareBackend.UpdateTraversalSet(softwareSet,
            L4::SoftwareGpuTraversalBindings{
                Descriptor(softwareNodeBuffer), Descriptor(rayBuffer),
                Descriptor(softwareHitBuffer), Descriptor(softwareTriangleBuffer),
                Descriptor(softwareCounterBuffer),
                dummyAlpha.ImageInfo(), dummyAlpha.SamplerInfo(),
                static_cast<std::uint32_t>(flattened.bvh.nodes.size()),
                static_cast<std::uint32_t>(flattenedTriangles.size()),
                std::min<std::uint32_t>(64u, flattened.bvh.maximumDepth + 1u),
                2u, 0u}),
            "SoftwareGpuTraversalBackend::UpdateTraversalSet");

        Require(canonicalLinearBackend.CreateDescriptorPool(
            1u, handles.canonicalLinearPool),
            "SoftwareGpuTraversalBackend::CreateDescriptorPool(canonical-linear)");
        VkDescriptorSet canonicalLinearSet = VK_NULL_HANDLE;
        Require(canonicalLinearBackend.AllocateTraversalSet(
            handles.canonicalLinearPool, canonicalLinearSet),
            "SoftwareGpuTraversalBackend::AllocateTraversalSet(canonical-linear)");
        Require(canonicalLinearBackend.UpdateTraversalSet(
            canonicalLinearSet,
            L4::SoftwareGpuTraversalBindings{
                // The statically declared node slot still needs a valid storage
                // descriptor. Alias the canonical triangle buffer itself so
                // this gate supplies no flattened-BVH data at all; zero
                // node/stack counts plus zero node counters prove it is unread.
                Descriptor(canonicalLinearTriangleBuffer), Descriptor(rayBuffer),
                Descriptor(canonicalLinearHitBuffer),
                Descriptor(canonicalLinearTriangleBuffer),
                Descriptor(canonicalLinearCounterBuffer),
                dummyAlpha.ImageInfo(), dummyAlpha.SamplerInfo(),
                0u, static_cast<std::uint32_t>(traversal.triangles.size()),
                0u, 2u, 0u}),
            "SoftwareGpuTraversalBackend::UpdateTraversalSet(canonical-linear)");

        VkQueryPoolCreateInfo queryPoolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = kTimestampCount;
        CheckVk(vkCreateQueryPool(context.Device(), &queryPoolInfo, nullptr, &handles.queryPool),
            "vkCreateQueryPool(timestamp)");

        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdResetQueryPool(commandBuffer, handles.queryPool, 0u, kTimestampCount);
            dummyAlpha.RecordTransition(context.Dispatch(), commandBuffer);
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                handles.queryPool, BlasBegin);
            Require(asBuilder.RecordBottomLevelBuild(
                commandBuffer, geometryInputs, scratch, blas),
                "AccelerationStructureBuilder::RecordBottomLevelBuild");
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                handles.queryPool, BlasEnd);
            asBuilder.RecordBuildToBuildBarrier(commandBuffer);
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                handles.queryPool, TlasBegin);
            Require(asBuilder.RecordTopLevelBuild(commandBuffer, asInstanceBuffer.Address(), 1u,
                scratch, tlas), "AccelerationStructureBuilder::RecordTopLevelBuild");
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                handles.queryPool, TlasEnd);
            asBuilder.RecordBuildToTraceBarrier(commandBuffer, true);

            const Gpu::GpuSceneBuildRequest request{
                commandBuffer, sceneSet, traversal.fingerprint, traversal.generation, true};
            Require(softwareBackend.BuildOrUpdateScene(request),
                "SoftwareGpuTraversalBackend::BuildOrUpdateScene");
            Require(canonicalLinearBackend.BuildOrUpdateScene(request),
                "SoftwareGpuTraversalBackend::BuildOrUpdateScene(canonical-linear)");
            Require(rayQueryAdapter.BuildOrUpdateScene(request),
                "RayQueryTraversalAdapter::BuildOrUpdateScene");
        });
        // State promotion is host-owned and occurs only after the build fence signaled.
        blas.MarkReady();
        tlas.MarkReady();
        Require(blas.State() == Hardware::AccelerationStructureState::Ready &&
                tlas.State() == Hardware::AccelerationStructureState::Ready,
            "Acceleration structures were not promoted after fence completion.");

        const std::uint32_t rayCount = static_cast<std::uint32_t>(rays.size());
        const auto makeBatch = [&](const VkCommandBuffer commandBuffer,
                                   const VkDescriptorSet traversalSet,
                                   const std::uint32_t hitOffset)
        {
            return Gpu::GpuTraceBatch{commandBuffer, sceneSet, traversalSet,
                traversal.fingerprint, 0u, hitOffset, rayCount, traversal.generation};
        };
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, SoftwareClosestBegin);
            Require(softwareBackend.RecordTraceClosestBatch(makeBatch(commandBuffer, softwareSet, 0u)),
                "SoftwareGpuTraversalBackend::RecordTraceClosestBatch");
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{softwareHitBuffer.Handle(), softwareCounterBuffer.Handle()});
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, SoftwareClosestEnd);
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, SoftwareAnyBegin);
            Require(softwareBackend.RecordTraceAnyBatch(
                makeBatch(commandBuffer, softwareSet, rayCount)),
                "SoftwareGpuTraversalBackend::RecordTraceAnyBatch");
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{softwareHitBuffer.Handle(), softwareCounterBuffer.Handle()});
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, SoftwareAnyEnd);
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            Require(canonicalLinearBackend.RecordTraceClosestBatch(
                makeBatch(commandBuffer, canonicalLinearSet, 0u)),
                "SoftwareGpuTraversalBackend::RecordTraceClosestBatch(canonical-linear)");
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{canonicalLinearHitBuffer.Handle(),
                    canonicalLinearCounterBuffer.Handle()});
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            Require(canonicalLinearBackend.RecordTraceAnyBatch(
                makeBatch(commandBuffer, canonicalLinearSet, rayCount)),
                "SoftwareGpuTraversalBackend::RecordTraceAnyBatch(canonical-linear)");
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{canonicalLinearHitBuffer.Handle(),
                    canonicalLinearCounterBuffer.Handle()});
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, RayQueryClosestBegin);
            Require(rayQueryAdapter.RecordTraceClosestBatch(
                makeBatch(commandBuffer, rayQuerySet, 0u)),
                "RayQueryTraversalAdapter::RecordTraceClosestBatch");
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{rayQueryHitBuffer.Handle()});
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, RayQueryClosestEnd);
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, RayQueryAnyBegin);
            Require(rayQueryAdapter.RecordTraceAnyBatch(
                makeBatch(commandBuffer, rayQuerySet, rayCount)),
                "RayQueryTraversalAdapter::RecordTraceAnyBatch");
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{rayQueryHitBuffer.Handle()});
            vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                handles.queryPool, RayQueryAnyEnd);
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdWriteTimestamp2(commandBuffer,
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                handles.queryPool, RtPipelineClosestBegin);
            rtPipelineBackend.RecordTraceClosestBatch(
                commandBuffer, rtClosestSceneSet, rtClosestTraversalSet,
                rayCount, 2u, 0u);
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{rtPipelineClosestHitBuffer.Handle()},
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
            vkCmdWriteTimestamp2(commandBuffer,
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                handles.queryPool, RtPipelineClosestEnd);
        });
        context.SubmitAndWait([&](const VkCommandBuffer commandBuffer)
        {
            vkCmdWriteTimestamp2(commandBuffer,
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                handles.queryPool, RtPipelineAnyBegin);
            rtPipelineBackend.RecordTraceAnyBatch(
                commandBuffer, rtAnySceneSet, rtAnyTraversalSet,
                rayCount, 2u, 0u);
            RecordHostReadBarrier(context.Dispatch(), commandBuffer,
                std::array{rtPipelineAnyHitBuffer.Handle()},
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
            vkCmdWriteTimestamp2(commandBuffer,
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                handles.queryPool, RtPipelineAnyEnd);
        });

        const auto* softwareHits = static_cast<const Abi1::GpuHitQueueRecordV1*>(
            softwareHitBuffer.MappedData());
        const auto* canonicalLinearHits =
            static_cast<const Abi1::GpuHitQueueRecordV1*>(
                canonicalLinearHitBuffer.MappedData());
        const auto* rayQueryHits = static_cast<const Abi1::GpuHitQueueRecordV1*>(
            rayQueryHitBuffer.MappedData());
        const auto* rtPipelineClosestHits =
            static_cast<const Abi1::GpuHitQueueRecordV1*>(
                rtPipelineClosestHitBuffer.MappedData());
        const auto* rtPipelineAnyHits =
            static_cast<const Abi1::GpuHitQueueRecordV1*>(
                rtPipelineAnyHitBuffer.MappedData());
        CompareClosest("Software GPU", cpuOracle,
            std::span{softwareHits, static_cast<std::size_t>(rayCount)});
        CompareAny("Software GPU", cpuOracle,
            std::span{softwareHits + rayCount, static_cast<std::size_t>(rayCount)});
        CompareClosest("Canonical Linear Software GPU", cpuOracle,
            std::span{canonicalLinearHits, static_cast<std::size_t>(rayCount)});
        CompareAny("Canonical Linear Software GPU", cpuOracle,
            std::span{canonicalLinearHits + rayCount,
                static_cast<std::size_t>(rayCount)});
        CompareClosest("Hardware Ray Query", cpuOracle,
            std::span{rayQueryHits, static_cast<std::size_t>(rayCount)});
        CompareAny("Hardware Ray Query", cpuOracle,
            std::span{rayQueryHits + rayCount, static_cast<std::size_t>(rayCount)});
        CompareClosest("Hardware RT Pipeline", cpuOracle,
            std::span{rtPipelineClosestHits,
                static_cast<std::size_t>(rayCount)});
        CompareAny("Hardware RT Pipeline", cpuOracle,
            std::span{rtPipelineAnyHits, static_cast<std::size_t>(rayCount)});

        L4::GpuTraversalCounterReadback counterReadback{};
        std::memcpy(&counterReadback, softwareCounterBuffer.MappedData(), sizeof(counterReadback));
        Require(counterReadback[L4::GpuTraversalCounterSlot::StackOverflows] == 0u,
            "Software GPU traversal reported a stack overflow.");
        Require(counterReadback[L4::GpuTraversalCounterSlot::InvalidHits] == 0u,
            "Software GPU traversal reported an invalid hit.");
        Require(counterReadback[L4::GpuTraversalCounterSlot::InvalidRays] == 1u,
            "Software GPU traversal did not report exactly the injected invalid ray.");
        L4::GpuTraversalCounterReadback canonicalLinearCounterReadback{};
        std::memcpy(&canonicalLinearCounterReadback,
            canonicalLinearCounterBuffer.MappedData(),
            sizeof(canonicalLinearCounterReadback));
        Require(canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::NodeTests] == 0u
                && canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::StackOverflows] == 0u
                && canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::MaximumStackDepth] == 0u
                && canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::LeafVisits] == 0u
                && canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::InvalidHits] == 0u,
            "Canonical-linear traversal unexpectedly consumed BVH node/stack state.");
        Require(canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::TriangleTests] != 0u
                && canonicalLinearCounterReadback[
                    L4::GpuTraversalCounterSlot::InvalidRays] == 1u,
            "Canonical-linear traversal did not enumerate canonical triangles or preserve invalid-ray accounting.");

        std::array<std::uint64_t, kTimestampCount> timestampValues{};
        CheckVk(vkGetQueryPoolResults(context.Device(), handles.queryPool, 0u, kTimestampCount,
            sizeof(timestampValues), timestampValues.data(), sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
            "vkGetQueryPoolResults(timestamp)");
        const auto milliseconds = [&](const TimestampSlot begin, const TimestampSlot end)
        {
            Require(timestampValues[end] >= timestampValues[begin],
                "GPU timestamp interval is reversed.");
            return static_cast<double>(timestampValues[end] - timestampValues[begin]) *
                static_cast<double>(context.TimestampPeriod()) * 1.0e-6;
        };

        SceneReport report{};
        report.stableId = canonical.stableId;
        report.rays = rays.size();
        report.closestHits = static_cast<std::size_t>(std::count_if(
            cpuOracle.begin(), cpuOracle.end(), [](const CpuOracleHit& hit)
            {
                return hit.hit.metadata.y == Abi0::HitKindTriangle;
            }));
        report.blasMilliseconds = milliseconds(BlasBegin, BlasEnd);
        report.tlasMilliseconds = milliseconds(TlasBegin, TlasEnd);
        report.softwareMilliseconds =
            milliseconds(SoftwareClosestBegin, SoftwareClosestEnd) +
            milliseconds(SoftwareAnyBegin, SoftwareAnyEnd);
        report.rayQueryMilliseconds =
            milliseconds(RayQueryClosestBegin, RayQueryClosestEnd) +
            milliseconds(RayQueryAnyBegin, RayQueryAnyEnd);
        report.rtPipelineMilliseconds =
            milliseconds(RtPipelineClosestBegin, RtPipelineClosestEnd) +
            milliseconds(RtPipelineAnyBegin, RtPipelineAnyEnd);
        report.megakernel = RunMegakernelImageGate(context, canonical, traversal,
            flattened, allocator, rayQueryBackend, softwareBackend, sceneSet,
            softwareSet, rayQuerySet, dummyAlpha, softwareMegakernelSpirv,
            rayQueryMegakernelSpirv, samplingProbeSpirv);
        report.counters = counterReadback;
        return report;
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 7,
            "Usage: RenderingEngine.Wave2TraversalGate <software.spv> <ray-query.spv> "
            "<rt-pipeline.spv> <megakernel-software.spv> <megakernel-ray-query.spv> <sampling-probe.spv>");
        const std::vector<std::uint32_t> softwareSpirv = ReadSpirv(argv[1]);
        const std::vector<std::uint32_t> rayQuerySpirv = ReadSpirv(argv[2]);
        const std::vector<std::uint32_t> rtPipelineSpirv = ReadSpirv(argv[3]);
        const std::vector<std::uint32_t> softwareMegakernelSpirv = ReadSpirv(argv[4]);
        const std::vector<std::uint32_t> rayQueryMegakernelSpirv = ReadSpirv(argv[5]);
        const std::vector<std::uint32_t> samplingProbeSpirv = ReadSpirv(argv[6]);

        VulkanContext context;
        const std::array scenes{
            Scene::BuildCanonicalTriangleScene(),
            Scene::BuildCanonicalCornellScene(),
            BuildAlphaMaskGateScene()};
        std::vector<SceneReport> reports;
        reports.reserve(scenes.size());
        for (const Scene::CanonicalScene& scene : scenes)
        {
            std::cout << "Wave 2/3 traversal gate running scene="
                << scene.stableId << std::endl;
            reports.push_back(RunScene(context, scene, softwareSpirv, rayQuerySpirv,
                rtPipelineSpirv, softwareMegakernelSpirv,
                rayQueryMegakernelSpirv, samplingProbeSpirv));
        }

        const ValidationState& validation = context.Validation();
        if (validation.errors != 0u)
        {
            std::ostringstream message;
            message << "Vulkan validation reported " << validation.errors << " error(s).";
            for (const std::string& entry : validation.messages)
            {
                message << '\n' << entry;
            }
            throw GateError(message.str());
        }

        std::cout << "Wave 2/3 traversal gate passed on " << context.DeviceName()
            << "; validation errors=" << validation.errors
            << ", warnings=" << validation.warnings << '\n';
        for (const SceneReport& report : reports)
        {
            std::cout << "  scene=" << report.stableId
                << " rays=" << report.rays
                << " closestHits=" << report.closestHits
                << std::fixed << std::setprecision(4)
                << " BLASms=" << report.blasMilliseconds
                << " TLASms=" << report.tlasMilliseconds
                << " SoftwareTraceMs=" << report.softwareMilliseconds
                << " RayQueryTraceMs=" << report.rayQueryMilliseconds
                << " RtPipelineTraceMs=" << report.rtPipelineMilliseconds
                << " L6SoftwareMs=" << report.megakernel.softwareMilliseconds
                << " L6RayQueryMs=" << report.megakernel.rayQueryMilliseconds
                << " L6RelativeRMSE=" << report.megakernel.relativeRmse
                << " L6MaxAbs=" << report.megakernel.maximumAbsoluteError
                << " L6SoftwareMeanY=" << report.megakernel.softwareMeanLuminance
                << " L6RayQueryMeanY=" << report.megakernel.rayQueryMeanLuminance
                << " L6CameraRays=" << report.megakernel.cameraRays
                << " L6RejectedBsdf="
                << report.megakernel.softwareRejectedBsdfSamples << '/'
                << report.megakernel.rayQueryRejectedBsdfSamples
                << " L6ProbeSamples=" << report.megakernel.samplingProbe.samples
                << " L6ProbeValidRejected="
                << report.megakernel.samplingProbe.validBsdfSamples << '/'
                << report.megakernel.samplingProbe.rejectedBsdfSamples
                << " L6ProbeModels="
                << report.megakernel.samplingProbe.materialModelsCovered
                << " L6ProbeChi2="
                << report.megakernel.samplingProbe.uniformHemisphereChiSquare
                << " L6ProbeMaxCpuError="
                << report.megakernel.samplingProbe.maximumReferenceError
                << " nodeTests="
                << report.counters[L4::GpuTraversalCounterSlot::NodeTests]
                << " triangleTests="
                << report.counters[L4::GpuTraversalCounterSlot::TriangleTests]
                << '\n';
        }
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Wave 2/3 traversal gate failed: " << exception.what() << '\n';
        return 1;
    }
}
