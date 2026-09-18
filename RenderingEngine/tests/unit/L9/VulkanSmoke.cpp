#include "VulkanSmoke.hpp"

#include "contracts/GpuRecordsAbiV1.hpp"
#include "contracts/RayHitAbiV0.hpp"
#include "contracts/ReconstructionAbiV2.hpp"
#include "contracts/RestirAbiV3.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RenderingEngine::Restir::Tests
{
    namespace
    {
        constexpr std::uint32_t kReservoirCount = 4u;
        constexpr std::uint32_t kCandidatesPerReservoir = 2u;
        constexpr std::uint32_t kNeighborCount = 1u;
        constexpr std::uint32_t kStatCount = 13u;
        constexpr std::uint32_t kReservoirFlagValid = 1u;
        constexpr std::uint32_t kSourceUniform = 0u;
        constexpr std::uint32_t kSourceInvalid = 0xffffffffu;

        struct UInt2 { std::uint32_t x; std::uint32_t y; };
        struct UInt4 { std::uint32_t x; std::uint32_t y; std::uint32_t z; std::uint32_t w; };
        struct Float2 { float x; float y; };
        struct Float4 { float x; float y; float z; float w; };

        struct GpuCandidate
        {
            UInt4 identity;
            UInt4 source;
            Float4 directionDistance;
            Float4 contributionTarget;
            Float4 proposalSupportCorrection;
        };

        struct GpuReservoir
        {
            GpuCandidate selected;
            Float4 weights;
            UInt4 metadata;
        };

        struct GpuSurface
        {
            Float4 positionDepth;
            Float4 normalThin;
            UInt4 identity;
        };

        struct GpuDebug
        {
            UInt4 selectedIdentity;
            UInt4 sourceAndState;
            Float4 weights;
            UInt4 validation;
        };

        struct InitialParams { UInt4 values; };
        struct TemporalParams
        {
            UInt4 counts;
            Float4 thresholds;
            UInt4 seedAndFlags;
        };
        struct SpatialParams
        {
            UInt4 counts;
            UInt4 modeAndSeed;
            Float4 thresholds;
        };
        struct VisibilityParams { UInt4 values; };
        struct DebugParams
        {
            std::uint32_t reservoirCount;
            UInt2 outputExtent;
            std::uint32_t debugMode;
        };

        static_assert(sizeof(GpuCandidate) == 80u);
        static_assert(sizeof(Float2) == 8u);
        static_assert(sizeof(GpuReservoir) == 112u);
        static_assert(sizeof(GpuSurface) == 48u);
        static_assert(sizeof(GpuDebug) == 64u);
        static_assert(sizeof(InitialParams) == 16u);
        static_assert(sizeof(TemporalParams) == 48u);
        static_assert(sizeof(SpatialParams) == 48u);
        static_assert(sizeof(VisibilityParams) == 16u);
        static_assert(sizeof(DebugParams) == 16u);

        class Unavailable final : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        template <typename Destination, typename Source>
        [[nodiscard]] Destination FunctionCast(const Source source) noexcept
        {
            static_assert(sizeof(Destination) == sizeof(Source));
            Destination destination{};
            std::memcpy(&destination, &source, sizeof(destination));
            return destination;
        }

        [[nodiscard]] std::string VkFailure(const std::string_view operation, const VkResult result)
        {
            std::ostringstream text;
            text << operation << " failed with VkResult " << static_cast<std::int32_t>(result);
            return text.str();
        }

        void Check(const VkResult result, const std::string_view operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(VkFailure(operation, result));
            }
        }

        struct ValidationCapture
        {
            std::atomic<std::uint32_t> errors{ 0u };
            std::mutex mutex{};
            std::vector<std::string> messages{};
        };

        VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(
            const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* const callbackData,
            void* const userData)
        {
            auto& capture = *static_cast<ValidationCapture*>(userData);
            const char* const message = callbackData != nullptr && callbackData->pMessage != nullptr
                ? callbackData->pMessage
                : "Vulkan validation emitted an empty message.";
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u)
            {
                capture.errors.fetch_add(1u, std::memory_order_relaxed);
                std::scoped_lock lock(capture.mutex);
                capture.messages.emplace_back(message);
            }
            std::cerr << "[L9 Vulkan validation] " << message << '\n';
            return VK_FALSE;
        }

        struct Buffer
        {
            VkBuffer handle = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize logicalSize = 0u;
            bool coherent = false;
        };

        struct Image
        {
            VkImage handle = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
        };

        struct Stage
        {
            VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        };

        struct ProductionStage
        {
            std::array<VkDescriptorSetLayout, 6u> descriptorSetLayouts{};
            std::array<VkDescriptorSet, 6u> descriptorSets{};
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
        };

        struct BindingSpec
        {
            std::uint32_t binding;
            VkDescriptorType type;
        };

        struct BufferWrite
        {
            std::uint32_t binding;
            std::size_t buffer;
        };

        struct ProductionBindingSpec
        {
            std::uint32_t set;
            std::uint32_t binding;
            VkDescriptorType type;
        };

        struct ProductionBufferWrite
        {
            std::uint32_t set;
            std::uint32_t binding;
            std::size_t buffer;
            VkDescriptorType type;
        };

        class VulkanHarness final
        {
        public:
            VulkanHarness() = default;
            VulkanHarness(const VulkanHarness&) = delete;
            VulkanHarness& operator=(const VulkanHarness&) = delete;

            ~VulkanHarness()
            {
                Cleanup();
            }

            [[nodiscard]] VulkanSmokeReport Execute()
            {
                LoadLoader();
                CreateInstance();
                SelectPhysicalDevice();
                CreateLogicalDevice();
                CreateResources();
                CreatePipelinesAndDescriptors();
                RecordAndSubmit();
                VulkanSmokeReport report = ReadBack();
                Cleanup();
                // Device lifetime errors are part of the test, not messages
                // arriving after an already-successful report was returned.
                report.validationErrorCount = validation_.errors.load(std::memory_order_relaxed);
                return report;
            }

        private:
            HMODULE loader_ = nullptr;
            VkInstance instance_ = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
            VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
            VkPhysicalDeviceProperties physicalProperties_{};
            VkPhysicalDeviceMemoryProperties memoryProperties_{};
            std::uint32_t queueFamilyIndex_ = 0u;
            VkDevice device_ = VK_NULL_HANDLE;
            VkQueue queue_ = VK_NULL_HANDLE;
            VkCommandPool commandPool_ = VK_NULL_HANDLE;
            VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
            VkDescriptorSetLayout emptySetLayout_ = VK_NULL_HANDLE;
            ValidationCapture validation_{};
            bool validationEnabled_ = false;
            bool synchronizationValidationEnabled_ = false;
            std::vector<Buffer> buffers_{};
            Image debugImage_{};
            std::vector<Stage> stages_{};
            std::vector<ProductionStage> productionStages_{};

            std::size_t initialParams_ = 0u;
            std::size_t temporalParams_ = 0u;
            std::size_t spatialParams_ = 0u;
            std::size_t visibilityParams_ = 0u;
            std::size_t debugParams_ = 0u;
            std::size_t candidates_ = 0u;
            std::size_t reservoirA_ = 0u;
            std::size_t reservoirB_ = 0u;
            std::size_t reservoirC_ = 0u;
            std::size_t reservoirD_ = 0u;
            std::size_t historyReservoirs_ = 0u;
            std::size_t currentSurfaces_ = 0u;
            std::size_t historySurfaces_ = 0u;
            std::size_t temporalMetadata_ = 0u;
            std::size_t historyAtCurrent_ = 0u;
            std::size_t referenceTargetMatrix_ = 0u;
            std::size_t referenceVisibility_ = 0u;
            std::size_t neighborIndices_ = 0u;
            std::size_t candidateAtCenter_ = 0u;
            std::size_t sourceLightMetadata_ = 0u;
            std::size_t pairwiseTargetSupport_ = 0u;
            std::size_t winnerVisibility_ = 0u;
            std::size_t validationReasons_ = 0u;
            std::size_t debugBuffer_ = 0u;
            std::size_t statsBuffer_ = 0u;
            std::size_t directLighting_ = 0u;
            std::size_t imageReadback_ = 0u;

            std::size_t productionParams_ = 0u;
            std::size_t productionSurfaces_ = 0u;
            std::size_t productionMotion_ = 0u;
            std::size_t productionHistorySurfaces_ = 0u;
            std::size_t productionCandidates_ = 0u;
            std::size_t productionInitial_ = 0u;
            std::size_t productionTemporal_ = 0u;
            std::size_t productionSpatial_ = 0u;
            std::size_t productionSpatialSnapshot_ = 0u;
            std::size_t productionHistory_ = 0u;
            std::size_t productionLightMap_ = 0u;
            std::size_t productionNeighbors_ = 0u;
            std::size_t productionReasons_ = 0u;
            std::size_t productionStats_ = 0u;
            std::size_t productionVisibility_ = 0u;
            std::size_t productionDebug_ = 0u;
            std::size_t productionImageReadback_ = 0u;

            PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_ = nullptr;
            PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion_ = nullptr;
            PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties_ = nullptr;
            PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties_ = nullptr;
            PFN_vkCreateInstance vkCreateInstance_ = nullptr;
            PFN_vkDestroyInstance vkDestroyInstance_ = nullptr;
            PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_ = nullptr;
            PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT_ = nullptr;
            PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices_ = nullptr;
            PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties_ = nullptr;
            PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2_ = nullptr;
            PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
            PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties_ = nullptr;
            PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties_ = nullptr;
            PFN_vkCreateDevice vkCreateDevice_ = nullptr;
            PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_ = nullptr;
            PFN_vkDestroyDevice vkDestroyDevice_ = nullptr;
            PFN_vkGetDeviceQueue vkGetDeviceQueue_ = nullptr;
            PFN_vkCreateBuffer vkCreateBuffer_ = nullptr;
            PFN_vkDestroyBuffer vkDestroyBuffer_ = nullptr;
            PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements_ = nullptr;
            PFN_vkAllocateMemory vkAllocateMemory_ = nullptr;
            PFN_vkFreeMemory vkFreeMemory_ = nullptr;
            PFN_vkBindBufferMemory vkBindBufferMemory_ = nullptr;
            PFN_vkMapMemory vkMapMemory_ = nullptr;
            PFN_vkUnmapMemory vkUnmapMemory_ = nullptr;
            PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges_ = nullptr;
            PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges_ = nullptr;
            PFN_vkCreateImage vkCreateImage_ = nullptr;
            PFN_vkDestroyImage vkDestroyImage_ = nullptr;
            PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements_ = nullptr;
            PFN_vkBindImageMemory vkBindImageMemory_ = nullptr;
            PFN_vkCreateImageView vkCreateImageView_ = nullptr;
            PFN_vkDestroyImageView vkDestroyImageView_ = nullptr;
            PFN_vkCreateShaderModule vkCreateShaderModule_ = nullptr;
            PFN_vkDestroyShaderModule vkDestroyShaderModule_ = nullptr;
            PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout_ = nullptr;
            PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout_ = nullptr;
            PFN_vkCreatePipelineLayout vkCreatePipelineLayout_ = nullptr;
            PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout_ = nullptr;
            PFN_vkCreateComputePipelines vkCreateComputePipelines_ = nullptr;
            PFN_vkDestroyPipeline vkDestroyPipeline_ = nullptr;
            PFN_vkCreateDescriptorPool vkCreateDescriptorPool_ = nullptr;
            PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool_ = nullptr;
            PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets_ = nullptr;
            PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets_ = nullptr;
            PFN_vkCreateCommandPool vkCreateCommandPool_ = nullptr;
            PFN_vkDestroyCommandPool vkDestroyCommandPool_ = nullptr;
            PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers_ = nullptr;
            PFN_vkBeginCommandBuffer vkBeginCommandBuffer_ = nullptr;
            PFN_vkEndCommandBuffer vkEndCommandBuffer_ = nullptr;
            PFN_vkCmdBindPipeline vkCmdBindPipeline_ = nullptr;
            PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets_ = nullptr;
            PFN_vkCmdDispatch vkCmdDispatch_ = nullptr;
            PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2_ = nullptr;
            PFN_vkCmdCopyBuffer vkCmdCopyBuffer_ = nullptr;
            PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer_ = nullptr;
            PFN_vkQueueSubmit2 vkQueueSubmit2_ = nullptr;
            PFN_vkQueueWaitIdle vkQueueWaitIdle_ = nullptr;
            PFN_vkDeviceWaitIdle vkDeviceWaitIdle_ = nullptr;

            template <typename Function>
            void LoadGlobal(Function& function, const char* const name, const bool required = true)
            {
                function = FunctionCast<Function>(vkGetInstanceProcAddr_(VK_NULL_HANDLE, name));
                if (required && function == nullptr)
                {
                    throw std::runtime_error(std::string("Vulkan loader omitted ") + name);
                }
            }

            template <typename Function>
            void LoadInstance(Function& function, const char* const name, const bool required = true)
            {
                function = FunctionCast<Function>(vkGetInstanceProcAddr_(instance_, name));
                if (required && function == nullptr)
                {
                    throw std::runtime_error(std::string("Vulkan instance omitted ") + name);
                }
            }

            template <typename Function>
            void LoadDevice(Function& function, const char* const name)
            {
                function = FunctionCast<Function>(vkGetDeviceProcAddr_(device_, name));
                if (function == nullptr)
                {
                    throw std::runtime_error(std::string("Vulkan device omitted ") + name);
                }
            }

            void LoadLoader()
            {
                loader_ = LoadLibraryW(L"vulkan-1.dll");
                if (loader_ == nullptr)
                {
                    throw Unavailable("vulkan-1.dll is unavailable");
                }
                const FARPROC rawProc = GetProcAddress(loader_, "vkGetInstanceProcAddr");
                if (rawProc == nullptr)
                {
                    throw Unavailable("Vulkan loader has no vkGetInstanceProcAddr");
                }
                vkGetInstanceProcAddr_ = FunctionCast<PFN_vkGetInstanceProcAddr>(rawProc);
                LoadGlobal(vkEnumerateInstanceVersion_, "vkEnumerateInstanceVersion", false);
                LoadGlobal(vkEnumerateInstanceLayerProperties_, "vkEnumerateInstanceLayerProperties");
                LoadGlobal(vkEnumerateInstanceExtensionProperties_, "vkEnumerateInstanceExtensionProperties");
                LoadGlobal(vkCreateInstance_, "vkCreateInstance");

                std::uint32_t version = VK_API_VERSION_1_0;
                if (vkEnumerateInstanceVersion_ == nullptr ||
                    vkEnumerateInstanceVersion_(&version) != VK_SUCCESS || version < VK_API_VERSION_1_3)
                {
                    throw Unavailable("Vulkan 1.3 loader is unavailable");
                }
            }

            [[nodiscard]] static bool ContainsLayer(
                const std::vector<VkLayerProperties>& values, const char* const name)
            {
                return std::ranges::any_of(values, [name](const VkLayerProperties& value) {
                    return std::strcmp(value.layerName, name) == 0;
                });
            }

            [[nodiscard]] static bool ContainsExtension(
                const std::vector<VkExtensionProperties>& values, const char* const name)
            {
                return std::ranges::any_of(values, [name](const VkExtensionProperties& value) {
                    return std::strcmp(value.extensionName, name) == 0;
                });
            }

            void CreateInstance()
            {
                std::uint32_t layerCount = 0u;
                Check(vkEnumerateInstanceLayerProperties_(&layerCount, nullptr),
                    "vkEnumerateInstanceLayerProperties(count)");
                std::vector<VkLayerProperties> layers(layerCount);
                Check(vkEnumerateInstanceLayerProperties_(&layerCount, layers.data()),
                    "vkEnumerateInstanceLayerProperties(data)");

                std::uint32_t extensionCount = 0u;
                Check(vkEnumerateInstanceExtensionProperties_(nullptr, &extensionCount, nullptr),
                    "vkEnumerateInstanceExtensionProperties(count)");
                std::vector<VkExtensionProperties> extensions(extensionCount);
                Check(vkEnumerateInstanceExtensionProperties_(nullptr, &extensionCount, extensions.data()),
                    "vkEnumerateInstanceExtensionProperties(data)");

                validationEnabled_ = ContainsLayer(layers, "VK_LAYER_KHRONOS_validation");
                const bool debugUtilsAvailable = ContainsExtension(
                    extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                synchronizationValidationEnabled_ = validationEnabled_ && ContainsExtension(
                    extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                if (validationEnabled_ && !debugUtilsAvailable)
                {
                    throw std::runtime_error(
                        "Validation layer exists but VK_EXT_debug_utils is unavailable, so errors cannot be captured");
                }

                std::vector<const char*> enabledLayers;
                std::vector<const char*> enabledExtensions;
                if (validationEnabled_)
                {
                    enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
                    enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                    if (synchronizationValidationEnabled_)
                    {
                        enabledExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                    }
                }

                VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
                };
                debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debugCreateInfo.pfnUserCallback = ValidationCallback;
                debugCreateInfo.pUserData = &validation_;

                const VkValidationFeatureEnableEXT enabledValidationFeature =
                    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
                VkValidationFeaturesEXT validationFeatures{
                    VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT
                };
                validationFeatures.enabledValidationFeatureCount = 1u;
                validationFeatures.pEnabledValidationFeatures = &enabledValidationFeature;
                if (validationEnabled_)
                {
                    validationFeatures.pNext = &debugCreateInfo;
                }

                VkApplicationInfo applicationInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
                applicationInfo.pApplicationName = "RenderingEngine L9 Vulkan Smoke";
                applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0u, 0u, 1u, 0u);
                applicationInfo.pEngineName = "RenderingEngine.L9.Private";
                applicationInfo.engineVersion = VK_MAKE_API_VERSION(0u, 0u, 1u, 0u);
                applicationInfo.apiVersion = VK_API_VERSION_1_3;

                VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
                createInfo.pApplicationInfo = &applicationInfo;
                createInfo.enabledLayerCount = static_cast<std::uint32_t>(enabledLayers.size());
                createInfo.ppEnabledLayerNames = enabledLayers.data();
                createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
                createInfo.ppEnabledExtensionNames = enabledExtensions.data();
                if (validationEnabled_)
                {
                    createInfo.pNext = synchronizationValidationEnabled_
                        ? static_cast<const void*>(&validationFeatures)
                        : static_cast<const void*>(&debugCreateInfo);
                }

                const VkResult result = vkCreateInstance_(&createInfo, nullptr, &instance_);
                if (result == VK_ERROR_INCOMPATIBLE_DRIVER)
                {
                    throw Unavailable("Vulkan 1.3 instance is unavailable");
                }
                Check(result, "vkCreateInstance");

                LoadInstance(vkDestroyInstance_, "vkDestroyInstance");
                LoadInstance(vkEnumeratePhysicalDevices_, "vkEnumeratePhysicalDevices");
                LoadInstance(vkGetPhysicalDeviceProperties_, "vkGetPhysicalDeviceProperties");
                LoadInstance(vkGetPhysicalDeviceFeatures2_, "vkGetPhysicalDeviceFeatures2");
                LoadInstance(vkGetPhysicalDeviceQueueFamilyProperties_,
                    "vkGetPhysicalDeviceQueueFamilyProperties");
                LoadInstance(vkGetPhysicalDeviceMemoryProperties_, "vkGetPhysicalDeviceMemoryProperties");
                LoadInstance(vkGetPhysicalDeviceFormatProperties_, "vkGetPhysicalDeviceFormatProperties");
                LoadInstance(vkCreateDevice_, "vkCreateDevice");
                LoadInstance(vkGetDeviceProcAddr_, "vkGetDeviceProcAddr");
                if (validationEnabled_)
                {
                    LoadInstance(vkCreateDebugUtilsMessengerEXT_, "vkCreateDebugUtilsMessengerEXT");
                    LoadInstance(vkDestroyDebugUtilsMessengerEXT_, "vkDestroyDebugUtilsMessengerEXT");
                    Check(vkCreateDebugUtilsMessengerEXT_(
                        instance_, &debugCreateInfo, nullptr, &debugMessenger_),
                        "vkCreateDebugUtilsMessengerEXT");
                }
            }

            void SelectPhysicalDevice()
            {
                std::uint32_t deviceCount = 0u;
                Check(vkEnumeratePhysicalDevices_(instance_, &deviceCount, nullptr),
                    "vkEnumeratePhysicalDevices(count)");
                if (deviceCount == 0u)
                {
                    throw Unavailable("no Vulkan physical device is available");
                }
                std::vector<VkPhysicalDevice> devices(deviceCount);
                Check(vkEnumeratePhysicalDevices_(instance_, &deviceCount, devices.data()),
                    "vkEnumeratePhysicalDevices(data)");

                for (const VkPhysicalDevice candidate : devices)
                {
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties_(candidate, &properties);
                    if (properties.apiVersion < VK_API_VERSION_1_3)
                    {
                        continue;
                    }
                    VkPhysicalDeviceVulkan13Features vulkan13Features{
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
                    };
                    VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
                    features.pNext = &vulkan13Features;
                    vkGetPhysicalDeviceFeatures2_(candidate, &features);
                    if (vulkan13Features.synchronization2 == VK_FALSE)
                    {
                        continue;
                    }
                    VkFormatProperties formatProperties{};
                    vkGetPhysicalDeviceFormatProperties_(
                        candidate, VK_FORMAT_R32G32B32A32_SFLOAT, &formatProperties);
                    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0u)
                    {
                        continue;
                    }

                    std::uint32_t familyCount = 0u;
                    vkGetPhysicalDeviceQueueFamilyProperties_(candidate, &familyCount, nullptr);
                    std::vector<VkQueueFamilyProperties> families(familyCount);
                    vkGetPhysicalDeviceQueueFamilyProperties_(candidate, &familyCount, families.data());
                    for (std::uint32_t family = 0u; family < familyCount; ++family)
                    {
                        if (families[family].queueCount > 0u &&
                            (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
                        {
                            physicalDevice_ = candidate;
                            physicalProperties_ = properties;
                            queueFamilyIndex_ = family;
                            vkGetPhysicalDeviceMemoryProperties_(candidate, &memoryProperties_);
                            return;
                        }
                    }
                }
                throw Unavailable(
                    "no Vulkan 1.3 compute device supports RGBA32F storage images");
            }

            void CreateLogicalDevice()
            {
                const float priority = 1.0f;
                VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
                queueCreateInfo.queueFamilyIndex = queueFamilyIndex_;
                queueCreateInfo.queueCount = 1u;
                queueCreateInfo.pQueuePriorities = &priority;
                VkPhysicalDeviceVulkan13Features vulkan13Features{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
                };
                vulkan13Features.synchronization2 = VK_TRUE;
                VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
                createInfo.pNext = &vulkan13Features;
                createInfo.queueCreateInfoCount = 1u;
                createInfo.pQueueCreateInfos = &queueCreateInfo;
                Check(vkCreateDevice_(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");

                LoadDevice(vkDestroyDevice_, "vkDestroyDevice");
                LoadDevice(vkGetDeviceQueue_, "vkGetDeviceQueue");
                LoadDevice(vkCreateBuffer_, "vkCreateBuffer");
                LoadDevice(vkDestroyBuffer_, "vkDestroyBuffer");
                LoadDevice(vkGetBufferMemoryRequirements_, "vkGetBufferMemoryRequirements");
                LoadDevice(vkAllocateMemory_, "vkAllocateMemory");
                LoadDevice(vkFreeMemory_, "vkFreeMemory");
                LoadDevice(vkBindBufferMemory_, "vkBindBufferMemory");
                LoadDevice(vkMapMemory_, "vkMapMemory");
                LoadDevice(vkUnmapMemory_, "vkUnmapMemory");
                LoadDevice(vkFlushMappedMemoryRanges_, "vkFlushMappedMemoryRanges");
                LoadDevice(vkInvalidateMappedMemoryRanges_, "vkInvalidateMappedMemoryRanges");
                LoadDevice(vkCreateImage_, "vkCreateImage");
                LoadDevice(vkDestroyImage_, "vkDestroyImage");
                LoadDevice(vkGetImageMemoryRequirements_, "vkGetImageMemoryRequirements");
                LoadDevice(vkBindImageMemory_, "vkBindImageMemory");
                LoadDevice(vkCreateImageView_, "vkCreateImageView");
                LoadDevice(vkDestroyImageView_, "vkDestroyImageView");
                LoadDevice(vkCreateShaderModule_, "vkCreateShaderModule");
                LoadDevice(vkDestroyShaderModule_, "vkDestroyShaderModule");
                LoadDevice(vkCreateDescriptorSetLayout_, "vkCreateDescriptorSetLayout");
                LoadDevice(vkDestroyDescriptorSetLayout_, "vkDestroyDescriptorSetLayout");
                LoadDevice(vkCreatePipelineLayout_, "vkCreatePipelineLayout");
                LoadDevice(vkDestroyPipelineLayout_, "vkDestroyPipelineLayout");
                LoadDevice(vkCreateComputePipelines_, "vkCreateComputePipelines");
                LoadDevice(vkDestroyPipeline_, "vkDestroyPipeline");
                LoadDevice(vkCreateDescriptorPool_, "vkCreateDescriptorPool");
                LoadDevice(vkDestroyDescriptorPool_, "vkDestroyDescriptorPool");
                LoadDevice(vkAllocateDescriptorSets_, "vkAllocateDescriptorSets");
                LoadDevice(vkUpdateDescriptorSets_, "vkUpdateDescriptorSets");
                LoadDevice(vkCreateCommandPool_, "vkCreateCommandPool");
                LoadDevice(vkDestroyCommandPool_, "vkDestroyCommandPool");
                LoadDevice(vkAllocateCommandBuffers_, "vkAllocateCommandBuffers");
                LoadDevice(vkBeginCommandBuffer_, "vkBeginCommandBuffer");
                LoadDevice(vkEndCommandBuffer_, "vkEndCommandBuffer");
                LoadDevice(vkCmdBindPipeline_, "vkCmdBindPipeline");
                LoadDevice(vkCmdBindDescriptorSets_, "vkCmdBindDescriptorSets");
                LoadDevice(vkCmdDispatch_, "vkCmdDispatch");
                LoadDevice(vkCmdPipelineBarrier2_, "vkCmdPipelineBarrier2");
                LoadDevice(vkCmdCopyBuffer_, "vkCmdCopyBuffer");
                LoadDevice(vkCmdCopyImageToBuffer_, "vkCmdCopyImageToBuffer");
                LoadDevice(vkQueueSubmit2_, "vkQueueSubmit2");
                LoadDevice(vkQueueWaitIdle_, "vkQueueWaitIdle");
                LoadDevice(vkDeviceWaitIdle_, "vkDeviceWaitIdle");

                vkGetDeviceQueue_(device_, queueFamilyIndex_, 0u, &queue_);
                VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
                poolInfo.queueFamilyIndex = queueFamilyIndex_;
                poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                Check(vkCreateCommandPool_(device_, &poolInfo, nullptr, &commandPool_),
                    "vkCreateCommandPool");
            }

            [[nodiscard]] std::uint32_t FindMemoryType(
                const std::uint32_t bits,
                const VkMemoryPropertyFlags required,
                const VkMemoryPropertyFlags preferred) const
            {
                std::uint32_t fallback = std::numeric_limits<std::uint32_t>::max();
                for (std::uint32_t index = 0u; index < memoryProperties_.memoryTypeCount; ++index)
                {
                    if ((bits & (1u << index)) == 0u)
                    {
                        continue;
                    }
                    const VkMemoryPropertyFlags flags = memoryProperties_.memoryTypes[index].propertyFlags;
                    if ((flags & required) != required)
                    {
                        continue;
                    }
                    if ((flags & preferred) == preferred)
                    {
                        return index;
                    }
                    fallback = index;
                }
                if (fallback == std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::runtime_error("No compatible Vulkan memory type was found");
                }
                return fallback;
            }

            [[nodiscard]] std::size_t CreateBuffer(
                const VkDeviceSize size,
                const VkBufferUsageFlags usage,
                const void* const initialData)
            {
                Buffer buffer{};
                buffer.logicalSize = size;
                VkBufferCreateInfo createInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
                createInfo.size = size;
                createInfo.usage = usage;
                createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                Check(vkCreateBuffer_(device_, &createInfo, nullptr, &buffer.handle), "vkCreateBuffer");

                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements_(device_, buffer.handle, &requirements);
                const std::uint32_t memoryType = FindMemoryType(
                    requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                buffer.coherent = (memoryProperties_.memoryTypes[memoryType].propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
                VkMemoryAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                allocateInfo.allocationSize = requirements.size;
                allocateInfo.memoryTypeIndex = memoryType;
                Check(vkAllocateMemory_(device_, &allocateInfo, nullptr, &buffer.memory),
                    "vkAllocateMemory(buffer)");
                Check(vkBindBufferMemory_(device_, buffer.handle, buffer.memory, 0u),
                    "vkBindBufferMemory");

                void* mapped = nullptr;
                Check(vkMapMemory_(device_, buffer.memory, 0u, VK_WHOLE_SIZE, 0u, &mapped),
                    "vkMapMemory(upload)");
                if (initialData != nullptr)
                {
                    std::memcpy(mapped, initialData, static_cast<std::size_t>(size));
                }
                else
                {
                    std::memset(mapped, 0, static_cast<std::size_t>(size));
                }
                if (!buffer.coherent)
                {
                    VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
                    range.memory = buffer.memory;
                    range.offset = 0u;
                    range.size = VK_WHOLE_SIZE;
                    Check(vkFlushMappedMemoryRanges_(device_, 1u, &range),
                        "vkFlushMappedMemoryRanges");
                }
                vkUnmapMemory_(device_, buffer.memory);
                buffers_.push_back(buffer);
                return buffers_.size() - 1u;
            }

            template <typename Value>
            [[nodiscard]] std::size_t CreateDataBuffer(
                const Value& value, const VkBufferUsageFlags usage)
            {
                return CreateBuffer(sizeof(Value), usage, &value);
            }

            template <typename Value>
            [[nodiscard]] std::size_t CreateDataBuffer(
                const std::span<const Value> values, const VkBufferUsageFlags usage)
            {
                return CreateBuffer(sizeof(Value) * values.size(), usage, values.data());
            }

            void CreateDebugImage()
            {
                VkImageCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
                createInfo.imageType = VK_IMAGE_TYPE_2D;
                createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                createInfo.extent = { 2u, 2u, 1u };
                createInfo.mipLevels = 1u;
                createInfo.arrayLayers = 1u;
                createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                createInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                Check(vkCreateImage_(device_, &createInfo, nullptr, &debugImage_.handle),
                    "vkCreateImage(debug)");
                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements_(device_, debugImage_.handle, &requirements);
                const std::uint32_t memoryType = FindMemoryType(
                    requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                VkMemoryAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                allocateInfo.allocationSize = requirements.size;
                allocateInfo.memoryTypeIndex = memoryType;
                Check(vkAllocateMemory_(device_, &allocateInfo, nullptr, &debugImage_.memory),
                    "vkAllocateMemory(image)");
                Check(vkBindImageMemory_(device_, debugImage_.handle, debugImage_.memory, 0u),
                    "vkBindImageMemory");
                VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                viewInfo.image = debugImage_.handle;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.levelCount = 1u;
                viewInfo.subresourceRange.layerCount = 1u;
                Check(vkCreateImageView_(device_, &viewInfo, nullptr, &debugImage_.view),
                    "vkCreateImageView(debug)");
            }

            [[nodiscard]] static GpuCandidate MakeCandidate(const std::uint32_t id, const float target)
            {
                return {
                    { id, id + 100u, id, 1u },
                    { kSourceUniform, kSourceInvalid, 0u, 0u },
                    { 0.0f, 1.0f, 0.0f, 1.0f },
                    { target, target * 0.5f, target * 0.25f, target },
                    { 0.5f, 1.0f, 1.0f, 0.0f }
                };
            }

            [[nodiscard]] static GpuReservoir MakeHistoryReservoir(const std::uint32_t id)
            {
                const GpuCandidate candidate = MakeCandidate(id, 1.0f + static_cast<float>(id));
                return { candidate, { 2.0f, 2.0f / candidate.contributionTarget.w, 0.0f, 0.0f },
                    { 1u, 0u, kReservoirFlagValid, 0u } };
            }

            void CreateResources()
            {
                buffers_.reserve(32u);
                constexpr VkBufferUsageFlags uniformUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                constexpr VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                const InitialParams initial{ { kReservoirCount, kCandidatesPerReservoir, 16u, 0x1234u } };
                const TemporalParams temporal{
                    { kReservoirCount, 16u, 20u, 0u },
                    { 0.8f, 0.1f, 1.0f, 0.1f },
                    { 0x2345u, 1u, 0u, 0u }
                };
                const SpatialParams spatial{
                    { kReservoirCount, kNeighborCount, 16u, 20u },
                    { 0u, 0x3456u, 1u, 0u },
                    { 0.8f, 0.1f, 1.0f, 0.1f }
                };
                const VisibilityParams visibility{ { kReservoirCount, 0u, 0u, 0u } };
                const DebugParams debug{ kReservoirCount, { 2u, 2u }, 0u };
                initialParams_ = CreateDataBuffer(initial, uniformUsage);
                temporalParams_ = CreateDataBuffer(temporal, uniformUsage);
                spatialParams_ = CreateDataBuffer(spatial, uniformUsage);
                visibilityParams_ = CreateDataBuffer(visibility, uniformUsage);
                debugParams_ = CreateDataBuffer(debug, uniformUsage);

                std::array<GpuCandidate, kReservoirCount * kCandidatesPerReservoir> candidates{};
                for (std::uint32_t index = 0u; index < candidates.size(); ++index)
                {
                    const std::uint32_t reservoirIndex = index / kCandidatesPerReservoir;
                    candidates[index] = MakeCandidate(
                        reservoirIndex, 1.0f + static_cast<float>(reservoirIndex));
                }
                candidates_ = CreateDataBuffer(std::span<const GpuCandidate>(candidates), storageUsage);
                reservoirA_ = CreateBuffer(sizeof(GpuReservoir) * kReservoirCount, storageUsage, nullptr);
                reservoirB_ = CreateBuffer(sizeof(GpuReservoir) * kReservoirCount, storageUsage, nullptr);
                reservoirC_ = CreateBuffer(sizeof(GpuReservoir) * kReservoirCount, storageUsage, nullptr);
                reservoirD_ = CreateBuffer(sizeof(GpuReservoir) * kReservoirCount, storageUsage, nullptr);

                std::array<GpuReservoir, kReservoirCount> histories{};
                std::array<GpuSurface, kReservoirCount> surfaces{};
                std::array<UInt4, kReservoirCount> temporalMetadata{};
                std::array<GpuCandidate, kReservoirCount> historyAtCurrent{};
                std::array<Float4, kReservoirCount> targetMatrix{};
                std::array<UInt2, kReservoirCount> referenceVisibility{};
                std::array<std::uint32_t, kReservoirCount> neighbors{};
                std::array<std::uint32_t, kReservoirCount> winnerVisibility{};
                std::array<UInt2, kReservoirCount> validationReasons{};
                for (std::uint32_t index = 0u; index < kReservoirCount; ++index)
                {
                    histories[index] = MakeHistoryReservoir(index);
                    surfaces[index] = {
                        { static_cast<float>(index) * 0.01f, 0.0f, 0.0f, 2.0f },
                        { 0.0f, 1.0f, 0.0f, 0.0f },
                        { 7u, 11u, 13u, 3u }
                    };
                    temporalMetadata[index] = { 1u | 2u | 16u, 1u, 0u, 0u };
                    historyAtCurrent[index] = histories[index].selected;
                    targetMatrix[index] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    referenceVisibility[index] = { 1u, 1u };
                    neighbors[index] = (index + 1u) % kReservoirCount;
                    winnerVisibility[index] = 1u;
                    validationReasons[index] = { 0u, 0u };
                }
                historyReservoirs_ = CreateDataBuffer(
                    std::span<const GpuReservoir>(histories), storageUsage);
                currentSurfaces_ = CreateDataBuffer(std::span<const GpuSurface>(surfaces), storageUsage);
                historySurfaces_ = CreateDataBuffer(std::span<const GpuSurface>(surfaces), storageUsage);
                temporalMetadata_ = CreateDataBuffer(std::span<const UInt4>(temporalMetadata), storageUsage);
                historyAtCurrent_ = CreateDataBuffer(
                    std::span<const GpuCandidate>(historyAtCurrent), storageUsage);
                referenceTargetMatrix_ = CreateDataBuffer(
                    std::span<const Float4>(targetMatrix), storageUsage);
                referenceVisibility_ = CreateDataBuffer(
                    std::span<const UInt2>(referenceVisibility), storageUsage);
                neighborIndices_ = CreateDataBuffer(std::span<const std::uint32_t>(neighbors), storageUsage);

                constexpr std::uint32_t sourceStride = kNeighborCount + 1u;
                std::array<GpuCandidate, kReservoirCount * sourceStride> atCenter{};
                std::array<UInt2, kReservoirCount * sourceStride> sourceMetadata{};
                std::array<Float2, kReservoirCount * sourceStride * sourceStride> pairwiseStorage{};
                for (std::uint32_t center = 0u; center < kReservoirCount; ++center)
                {
                    atCenter[center * sourceStride] = MakeCandidate(center, 1.0f);
                    atCenter[center * sourceStride + 1u] = MakeCandidate(neighbors[center], 1.5f);
                    sourceMetadata[center * sourceStride] = { 1u, 1u };
                    sourceMetadata[center * sourceStride + 1u] = { 1u, 1u };
                    for (std::uint32_t pair = 0u; pair < sourceStride * sourceStride; ++pair)
                    {
                        pairwiseStorage[center * sourceStride * sourceStride + pair] =
                            { 1.0f, 1.0f };
                    }
                }
                candidateAtCenter_ = CreateDataBuffer(
                    std::span<const GpuCandidate>(atCenter), storageUsage);
                sourceLightMetadata_ = CreateDataBuffer(
                    std::span<const UInt2>(sourceMetadata), storageUsage);
                pairwiseTargetSupport_ = CreateDataBuffer(
                    std::span<const Float2>(pairwiseStorage), storageUsage);
                winnerVisibility_ = CreateDataBuffer(
                    std::span<const std::uint32_t>(winnerVisibility), storageUsage);
                validationReasons_ = CreateDataBuffer(
                    std::span<const UInt2>(validationReasons), storageUsage);
                debugBuffer_ = CreateBuffer(sizeof(GpuDebug) * kReservoirCount, storageUsage, nullptr);
                statsBuffer_ = CreateBuffer(sizeof(std::uint32_t) * kStatCount, storageUsage, nullptr);
                directLighting_ = CreateBuffer(sizeof(Float4) * kReservoirCount, storageUsage, nullptr);
                imageReadback_ = CreateBuffer(
                    sizeof(Float4) * kReservoirCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
                CreateDebugImage();

                using namespace Contracts;
                using namespace Contracts::AbiV3;
                constexpr std::uint32_t productionPixelCount = 4u;
                constexpr VkBufferUsageFlags productionStorageUsage =
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                const GpuRestirParametersV3 productionParameters{
                    { 2u, 2u, productionPixelCount, 1u },
                    { 0u, 32u, 20u, 2u },
                    { 11u, 12u, 13u, 14u },
                    { 15u, 16u, 6u, RestirShadowPhysical },
                    { 0u, RestirCandidateUniformLight, 0u, 0u },
                    { 1u, 1u, 0u, 0u },
                    { 0.8f, 0.1f, 1.0f, 0.1f },
                    { 0.0f, 0.0f, 1.0f, 0.0f }
                };
                productionParams_ = CreateDataBuffer(
                    productionParameters, uniformUsage);

                std::array<AbiV2::GpuPrimarySurfaceV2, productionPixelCount>
                    productionSurfaces{};
                for (std::uint32_t index = 0u;
                    index < productionPixelCount; ++index)
                {
                    auto& surface = productionSurfaces[index];
                    surface.worldPositionLinearDepth = {
                        static_cast<float>(index), 0.0f, 0.0f, 1.0f };
                    surface.geometricNormalRoughness = {
                        0.0f, 0.0f, 1.0f, 0.5f };
                    surface.shadingNormalMetallic = {
                        0.0f, 0.0f, 1.0f, 0.0f };
                    surface.diffuseAlbedo = { 0.8f, 0.8f, 0.8f, 0.0f };
                    surface.specularAlbedo = { 0.04f, 0.04f, 0.04f, 0.0f };
                    surface.identity = {
                        7u, 11u + index, 13u + index,
                        index == 2u ? AbiV2::PrimarySurfaceFlagNone
                            : AbiV2::PrimarySurfaceFlagValid
                                | AbiV2::PrimarySurfaceFlagFrontFace
                                | AbiV2::PrimarySurfaceFlagHasDiffuse
                                | AbiV2::PrimarySurfaceFlagHasSpecular };
                }
                productionSurfaces_ = CreateDataBuffer(
                    std::span<const AbiV2::GpuPrimarySurfaceV2>(
                        productionSurfaces), productionStorageUsage);

                const auto makeProductionCandidate = [](
                    const std::uint32_t pixel,
                    const float target,
                    const float proposal,
                    const float correction)
                {
                    GpuRestirCandidateV3 candidate{};
                    candidate.sample.positionDistance = {
                        static_cast<float>(pixel), 1.0f, 0.0f, 1.0f };
                    candidate.sample.directionCombinedPdf = {
                        0.0f, 1.0f, 0.0f, proposal };
                    candidate.sample.radianceDiscretePdf = {
                        1.0f, 1.0f, 1.0f, 1.0f };
                    candidate.sample.identity = {
                        100u + pixel, 200u + pixel, pixel, 14u };
                    candidate.sample.generation = { 12u, 11u, 14u, 0u };
                    candidate.sample.metadata = {
                        AbiV1::SampleMeasureSolidAngle,
                        RestirSampleFlagValid, 0u, 0u };
                    candidate.sample.sourceIdentity = {
                        pixel, 7u, 11u + pixel, 13u + pixel };
                    candidate.targetProposalSupportCorrection = {
                        target, proposal, target > 0.0f ? 1.0f : 0.0f,
                        correction };
                    candidate.provenance = {
                        RestirCandidateUniformLight, RestirReuseNone,
                        pixel, 0u };
                    return candidate;
                };
                const std::array productionCandidates{
                    makeProductionCandidate(0u, 2.0f, 0.25f, 1.0f),
                    makeProductionCandidate(1u, 0.0f, 0.5f, 1.0f),
                    // Invalid surfaces are exported with correction=0 and
                    // must not masquerade as legal zero-target proposals.
                    makeProductionCandidate(2u, 0.0f, 0.5f, 0.0f),
                    makeProductionCandidate(3u, 1.0f, 0.5f, 1.0f)
                };
                productionCandidates_ = CreateDataBuffer(
                    std::span<const GpuRestirCandidateV3>(
                        productionCandidates), productionStorageUsage);

                productionInitial_ = CreateBuffer(
                    sizeof(GpuRestirReservoirV3) * productionPixelCount,
                    productionStorageUsage, nullptr);
                productionTemporal_ = CreateBuffer(
                    sizeof(GpuRestirReservoirV3) * productionPixelCount,
                    productionStorageUsage, nullptr);
                productionSpatial_ = CreateBuffer(
                    sizeof(GpuRestirReservoirV3) * productionPixelCount,
                    productionStorageUsage, nullptr);
                productionSpatialSnapshot_ = CreateBuffer(
                    sizeof(GpuRestirReservoirV3) * productionPixelCount,
                    productionStorageUsage, nullptr);
                productionHistory_ = CreateBuffer(
                    sizeof(GpuRestirReservoirV3) * productionPixelCount,
                    productionStorageUsage, nullptr);

                const std::array<AbiV2::GpuGBufferRecordV2,
                    productionPixelCount> motion{};
                const std::array<AbiV2::GpuPrimarySurfaceV2,
                    productionPixelCount> historySurfaces{};
                const std::array<UInt2, 1u> lightMap{ UInt2{ 0u, 12u } };
                const std::array<std::uint32_t, 1u> productionNeighbors{ 0u };
                const std::array<std::uint32_t,
                    productionPixelCount> reasons{};
                const GpuRestirStatisticsV3 productionStatistics{};
                productionMotion_ = CreateDataBuffer(
                    std::span<const AbiV2::GpuGBufferRecordV2>(motion),
                    productionStorageUsage);
                productionHistorySurfaces_ = CreateDataBuffer(
                    std::span<const AbiV2::GpuPrimarySurfaceV2>(
                        historySurfaces), productionStorageUsage);
                productionLightMap_ = CreateDataBuffer(
                    std::span<const UInt2>(lightMap), productionStorageUsage);
                productionNeighbors_ = CreateDataBuffer(
                    std::span<const std::uint32_t>(productionNeighbors),
                    productionStorageUsage);
                productionReasons_ = CreateDataBuffer(
                    std::span<const std::uint32_t>(reasons),
                    productionStorageUsage);
                productionStats_ = CreateDataBuffer(
                    productionStatistics, productionStorageUsage);

                std::array<AbiV1::GpuHitQueueRecordV1,
                    productionPixelCount> visibilityHits{};
                for (std::uint32_t index = 0u;
                    index < productionPixelCount; ++index)
                {
                    visibilityHits[index].positionT.w = 0.5f;
                    visibilityHits[index].metadata = {
                        index,
                        index == 3u ? AbiV0::HitKindTriangle
                            : AbiV0::HitKindMiss,
                        0u, index };
                }
                productionVisibility_ = CreateDataBuffer(
                    std::span<const AbiV1::GpuHitQueueRecordV1>(
                        visibilityHits), productionStorageUsage);
                productionDebug_ = CreateBuffer(
                    sizeof(GpuRestirDebugV3) * productionPixelCount,
                    productionStorageUsage, nullptr);
                productionImageReadback_ = CreateBuffer(
                    sizeof(Float4) * productionPixelCount,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
            }

            [[nodiscard]] static std::filesystem::path ShaderDirectory()
            {
                std::wstring executable(32768u, L'\0');
                const DWORD length = GetModuleFileNameW(
                    nullptr, executable.data(), static_cast<DWORD>(executable.size()));
                if (length == 0u || length >= executable.size())
                {
                    throw std::runtime_error("GetModuleFileNameW failed for L9 shader discovery");
                }
                executable.resize(length);
                const std::filesystem::path executablePath(executable);
                const std::filesystem::path configuration = executablePath.parent_path().filename();
                const std::filesystem::path repositoryRoot = executablePath.parent_path()
                    .parent_path().parent_path().parent_path();
                return repositoryRoot / "build" / "x64" / configuration /
                    "RenderingEngine.Restir.Shaders";
            }

            [[nodiscard]] static std::vector<std::uint32_t> ReadShader(
                const std::filesystem::path& path)
            {
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (!file)
                {
                    throw std::runtime_error("Missing L9 SPIR-V: " + path.string());
                }
                const std::streampos end = file.tellg();
                if (end <= 0 || (static_cast<std::uint64_t>(end) % sizeof(std::uint32_t)) != 0u)
                {
                    throw std::runtime_error("Invalid L9 SPIR-V size: " + path.string());
                }
                const auto byteCount = static_cast<std::size_t>(end);
                std::vector<std::uint32_t> words(byteCount / sizeof(std::uint32_t));
                file.seekg(0, std::ios::beg);
                file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byteCount));
                if (!file)
                {
                    throw std::runtime_error("Failed to read L9 SPIR-V: " + path.string());
                }
                return words;
            }

            [[nodiscard]] Stage CreateStage(
                const std::filesystem::path& shader,
                const std::span<const BindingSpec> bindings)
            {
                Stage stage{};
                std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
                layoutBindings.reserve(bindings.size());
                for (const BindingSpec binding : bindings)
                {
                    layoutBindings.push_back({
                        binding.binding,
                        binding.type,
                        1u,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        nullptr
                    });
                }
                VkDescriptorSetLayoutCreateInfo setInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
                };
                setInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
                setInfo.pBindings = layoutBindings.data();
                Check(vkCreateDescriptorSetLayout_(
                    device_, &setInfo, nullptr, &stage.descriptorSetLayout),
                    "vkCreateDescriptorSetLayout(stage)");

                const std::array<VkDescriptorSetLayout, 6u> setLayouts{
                    emptySetLayout_, emptySetLayout_, emptySetLayout_, emptySetLayout_, emptySetLayout_,
                    stage.descriptorSetLayout
                };
                VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
                layoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
                layoutInfo.pSetLayouts = setLayouts.data();
                Check(vkCreatePipelineLayout_(device_, &layoutInfo, nullptr, &stage.pipelineLayout),
                    "vkCreatePipelineLayout");

                const std::vector<std::uint32_t> words = ReadShader(shader);
                VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
                moduleInfo.pCode = words.data();
                VkShaderModule module = VK_NULL_HANDLE;
                Check(vkCreateShaderModule_(device_, &moduleInfo, nullptr, &module),
                    "vkCreateShaderModule");
                VkPipelineShaderStageCreateInfo shaderStage{
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
                };
                shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                shaderStage.module = module;
                shaderStage.pName = "main";
                VkComputePipelineCreateInfo pipelineInfo{
                    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
                };
                pipelineInfo.stage = shaderStage;
                pipelineInfo.layout = stage.pipelineLayout;
                const VkResult pipelineResult = vkCreateComputePipelines_(
                    device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &stage.pipeline);
                vkDestroyShaderModule_(device_, module, nullptr);
                Check(pipelineResult, "vkCreateComputePipelines");

                VkDescriptorSetAllocateInfo allocateInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
                };
                allocateInfo.descriptorPool = descriptorPool_;
                allocateInfo.descriptorSetCount = 1u;
                allocateInfo.pSetLayouts = &stage.descriptorSetLayout;
                Check(vkAllocateDescriptorSets_(device_, &allocateInfo, &stage.descriptorSet),
                    "vkAllocateDescriptorSets");
                return stage;
            }

            void WriteStage(
                const Stage& stage,
                const std::span<const BufferWrite> writes,
                const bool storageImage)
            {
                std::vector<VkDescriptorBufferInfo> bufferInfos(writes.size());
                std::vector<VkWriteDescriptorSet> descriptorWrites;
                descriptorWrites.reserve(writes.size() + (storageImage ? 1u : 0u));
                for (std::size_t index = 0u; index < writes.size(); ++index)
                {
                    const Buffer& buffer = buffers_[writes[index].buffer];
                    bufferInfos[index] = { buffer.handle, 0u, buffer.logicalSize };
                    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    write.dstSet = stage.descriptorSet;
                    write.dstBinding = writes[index].binding;
                    write.descriptorCount = 1u;
                    write.descriptorType = writes[index].binding == 0u
                        ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                        : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &bufferInfos[index];
                    descriptorWrites.push_back(write);
                }
                VkDescriptorImageInfo imageInfo{};
                if (storageImage)
                {
                    imageInfo.imageView = debugImage_.view;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    write.dstSet = stage.descriptorSet;
                    write.dstBinding = 10u;
                    write.descriptorCount = 1u;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    write.pImageInfo = &imageInfo;
                    descriptorWrites.push_back(write);
                }
                vkUpdateDescriptorSets_(device_, static_cast<std::uint32_t>(descriptorWrites.size()),
                    descriptorWrites.data(), 0u, nullptr);
            }

            [[nodiscard]] ProductionStage CreateProductionStage(
                const std::filesystem::path& shader,
                const std::span<const ProductionBindingSpec> bindings)
            {
                ProductionStage stage{};
                for (std::uint32_t set = 0u; set < 6u; ++set)
                {
                    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
                    for (const ProductionBindingSpec binding : bindings)
                    {
                        if (binding.set == set)
                        {
                            layoutBindings.push_back({ binding.binding, binding.type,
                                1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
                        }
                    }
                    VkDescriptorSetLayoutCreateInfo setInfo{
                        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                    setInfo.bindingCount = static_cast<std::uint32_t>(
                        layoutBindings.size());
                    setInfo.pBindings = layoutBindings.data();
                    Check(vkCreateDescriptorSetLayout_(device_, &setInfo, nullptr,
                            &stage.descriptorSetLayouts[set]),
                        "vkCreateDescriptorSetLayout(production-v3)");
                }

                VkPipelineLayoutCreateInfo layoutInfo{
                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
                layoutInfo.setLayoutCount = 6u;
                layoutInfo.pSetLayouts = stage.descriptorSetLayouts.data();
                Check(vkCreatePipelineLayout_(device_, &layoutInfo, nullptr,
                        &stage.pipelineLayout),
                    "vkCreatePipelineLayout(production-v3)");

                const std::vector<std::uint32_t> words = ReadShader(shader);
                VkShaderModuleCreateInfo moduleInfo{
                    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
                moduleInfo.pCode = words.data();
                VkShaderModule module = VK_NULL_HANDLE;
                Check(vkCreateShaderModule_(device_, &moduleInfo, nullptr, &module),
                    "vkCreateShaderModule(production-v3)");
                VkPipelineShaderStageCreateInfo shaderStage{
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
                shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                shaderStage.module = module;
                shaderStage.pName = "main";
                VkComputePipelineCreateInfo pipelineInfo{
                    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                pipelineInfo.stage = shaderStage;
                pipelineInfo.layout = stage.pipelineLayout;
                const VkResult pipelineResult = vkCreateComputePipelines_(
                    device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr,
                    &stage.pipeline);
                vkDestroyShaderModule_(device_, module, nullptr);
                Check(pipelineResult, "vkCreateComputePipelines(production-v3)");

                VkDescriptorSetAllocateInfo allocateInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocateInfo.descriptorPool = descriptorPool_;
                allocateInfo.descriptorSetCount = 6u;
                allocateInfo.pSetLayouts = stage.descriptorSetLayouts.data();
                Check(vkAllocateDescriptorSets_(device_, &allocateInfo,
                        stage.descriptorSets.data()),
                    "vkAllocateDescriptorSets(production-v3)");
                return stage;
            }

            void WriteProductionStage(
                const ProductionStage& stage,
                const std::span<const ProductionBufferWrite> writes,
                const bool storageImage)
            {
                std::vector<VkDescriptorBufferInfo> bufferInfos(writes.size());
                std::vector<VkWriteDescriptorSet> descriptorWrites;
                descriptorWrites.reserve(writes.size() + (storageImage ? 1u : 0u));
                for (std::size_t index = 0u; index < writes.size(); ++index)
                {
                    const ProductionBufferWrite& source = writes[index];
                    const Buffer& buffer = buffers_[source.buffer];
                    bufferInfos[index] = { buffer.handle, 0u, buffer.logicalSize };
                    VkWriteDescriptorSet write{
                        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    write.dstSet = stage.descriptorSets[source.set];
                    write.dstBinding = source.binding;
                    write.descriptorCount = 1u;
                    write.descriptorType = source.type;
                    write.pBufferInfo = &bufferInfos[index];
                    descriptorWrites.push_back(write);
                }
                VkDescriptorImageInfo imageInfo{};
                if (storageImage)
                {
                    imageInfo.imageView = debugImage_.view;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    VkWriteDescriptorSet write{
                        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    write.dstSet = stage.descriptorSets[5u];
                    write.dstBinding = 16u;
                    write.descriptorCount = 1u;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    write.pImageInfo = &imageInfo;
                    descriptorWrites.push_back(write);
                }
                vkUpdateDescriptorSets_(device_,
                    static_cast<std::uint32_t>(descriptorWrites.size()),
                    descriptorWrites.data(), 0u, nullptr);
            }

            void CreatePipelinesAndDescriptors()
            {
                const std::array<VkDescriptorPoolSize, 3u> poolSizes{
                    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16u },
                    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 96u },
                    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4u }
                };
                VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
                poolInfo.maxSets = 48u;
                poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();
                Check(vkCreateDescriptorPool_(device_, &poolInfo, nullptr, &descriptorPool_),
                    "vkCreateDescriptorPool");
                VkDescriptorSetLayoutCreateInfo emptyInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
                };
                Check(vkCreateDescriptorSetLayout_(device_, &emptyInfo, nullptr, &emptySetLayout_),
                    "vkCreateDescriptorSetLayout(empty)");

                constexpr VkDescriptorType uniform = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                constexpr VkDescriptorType storage = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                constexpr VkDescriptorType image = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                const std::filesystem::path directory = ShaderDirectory();
                const std::array initialBindings{
                    BindingSpec{ 0u, uniform }, BindingSpec{ 1u, storage }, BindingSpec{ 7u, storage },
                    BindingSpec{ 8u, storage }, BindingSpec{ 9u, storage }
                };
                const std::array temporalBindings{
                    BindingSpec{ 0u, uniform }, BindingSpec{ 2u, storage }, BindingSpec{ 3u, storage },
                    BindingSpec{ 4u, storage }, BindingSpec{ 5u, storage }, BindingSpec{ 6u, storage },
                    BindingSpec{ 7u, storage }, BindingSpec{ 8u, storage }, BindingSpec{ 9u, storage },
                    BindingSpec{ 10u, storage }, BindingSpec{ 11u, storage }, BindingSpec{ 12u, storage }
                };
                const std::array spatialBindings{
                    BindingSpec{ 0u, uniform }, BindingSpec{ 2u, storage }, BindingSpec{ 4u, storage },
                    BindingSpec{ 6u, storage }, BindingSpec{ 7u, storage }, BindingSpec{ 8u, storage },
                    BindingSpec{ 9u, storage }, BindingSpec{ 10u, storage }, BindingSpec{ 11u, storage },
                    BindingSpec{ 12u, storage }
                };
                const std::array visibilityBindings{
                    BindingSpec{ 0u, uniform }, BindingSpec{ 2u, storage }, BindingSpec{ 6u, storage },
                    BindingSpec{ 7u, storage }, BindingSpec{ 8u, storage }, BindingSpec{ 9u, storage },
                    BindingSpec{ 10u, storage }
                };
                const std::array debugBindings{
                    BindingSpec{ 0u, uniform }, BindingSpec{ 2u, storage }, BindingSpec{ 6u, storage },
                    BindingSpec{ 8u, storage }, BindingSpec{ 10u, image }
                };
                stages_.reserve(5u);
                stages_.push_back(CreateStage(directory / "RestirInitial.comp.spv", initialBindings));
                stages_.push_back(CreateStage(directory / "RestirTemporal.comp.spv", temporalBindings));
                stages_.push_back(CreateStage(directory / "RestirSpatial.comp.spv", spatialBindings));
                stages_.push_back(CreateStage(directory / "RestirVisibility.comp.spv", visibilityBindings));
                stages_.push_back(CreateStage(directory / "RestirDebug.comp.spv", debugBindings));

                const std::array initialWrites{
                    BufferWrite{ 0u, initialParams_ }, BufferWrite{ 1u, candidates_ },
                    BufferWrite{ 7u, reservoirA_ }, BufferWrite{ 8u, debugBuffer_ },
                    BufferWrite{ 9u, statsBuffer_ }
                };
                const std::array temporalWrites{
                    BufferWrite{ 0u, temporalParams_ }, BufferWrite{ 2u, reservoirA_ },
                    BufferWrite{ 3u, historyReservoirs_ }, BufferWrite{ 4u, currentSurfaces_ },
                    BufferWrite{ 5u, historySurfaces_ }, BufferWrite{ 6u, temporalMetadata_ },
                    BufferWrite{ 7u, reservoirB_ }, BufferWrite{ 8u, debugBuffer_ },
                    BufferWrite{ 9u, statsBuffer_ }, BufferWrite{ 10u, historyAtCurrent_ },
                    BufferWrite{ 11u, referenceTargetMatrix_ }, BufferWrite{ 12u, referenceVisibility_ }
                };
                const std::array spatialWrites{
                    BufferWrite{ 0u, spatialParams_ }, BufferWrite{ 2u, reservoirB_ },
                    BufferWrite{ 4u, currentSurfaces_ }, BufferWrite{ 6u, neighborIndices_ },
                    BufferWrite{ 7u, reservoirC_ }, BufferWrite{ 8u, debugBuffer_ },
                    BufferWrite{ 9u, statsBuffer_ }, BufferWrite{ 10u, candidateAtCenter_ },
                    BufferWrite{ 11u, sourceLightMetadata_ }, BufferWrite{ 12u, pairwiseTargetSupport_ }
                };
                const std::array visibilityWrites{
                    BufferWrite{ 0u, visibilityParams_ }, BufferWrite{ 2u, reservoirC_ },
                    BufferWrite{ 6u, winnerVisibility_ }, BufferWrite{ 7u, reservoirD_ },
                    BufferWrite{ 8u, debugBuffer_ }, BufferWrite{ 9u, statsBuffer_ },
                    BufferWrite{ 10u, directLighting_ }
                };
                const std::array debugWrites{
                    BufferWrite{ 0u, debugParams_ }, BufferWrite{ 2u, reservoirD_ },
                    BufferWrite{ 6u, validationReasons_ }, BufferWrite{ 8u, debugBuffer_ }
                };
                WriteStage(stages_[0], initialWrites, false);
                WriteStage(stages_[1], temporalWrites, false);
                WriteStage(stages_[2], spatialWrites, false);
                WriteStage(stages_[3], visibilityWrites, false);
                WriteStage(stages_[4], debugWrites, true);

                constexpr VkDescriptorType ubo = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                constexpr VkDescriptorType ssbo = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                const std::array productionInitialBindings{
                    ProductionBindingSpec{ 3u, 24u, ssbo },
                    ProductionBindingSpec{ 5u, 0u, ubo },
                    ProductionBindingSpec{ 5u, 1u, ssbo },
                    ProductionBindingSpec{ 5u, 9u, ssbo },
                    ProductionBindingSpec{ 5u, 26u, ssbo }
                };
                const std::array productionTemporalBindings{
                    ProductionBindingSpec{ 3u, 24u, ssbo },
                    ProductionBindingSpec{ 4u, 0u, ssbo },
                    ProductionBindingSpec{ 5u, 0u, ubo },
                    ProductionBindingSpec{ 5u, 5u, ssbo },
                    ProductionBindingSpec{ 5u, 9u, ssbo },
                    ProductionBindingSpec{ 5u, 15u, ssbo },
                    ProductionBindingSpec{ 5u, 21u, ssbo },
                    ProductionBindingSpec{ 5u, 26u, ssbo },
                    ProductionBindingSpec{ 5u, 27u, ssbo },
                    ProductionBindingSpec{ 5u, 30u, ssbo }
                };
                const std::array productionSpatialBindings{
                    ProductionBindingSpec{ 3u, 24u, ssbo },
                    ProductionBindingSpec{ 5u, 0u, ubo },
                    ProductionBindingSpec{ 5u, 9u, ssbo },
                    ProductionBindingSpec{ 5u, 15u, ssbo },
                    ProductionBindingSpec{ 5u, 22u, ssbo },
                    ProductionBindingSpec{ 5u, 27u, ssbo },
                    ProductionBindingSpec{ 5u, 28u, ssbo }
                };
                const std::array productionWinnerBindings{
                    ProductionBindingSpec{ 5u, 0u, ubo },
                    ProductionBindingSpec{ 5u, 9u, ssbo },
                    ProductionBindingSpec{ 5u, 10u, ssbo },
                    ProductionBindingSpec{ 5u, 28u, ssbo }
                };
                const std::array productionDebugBindings{
                    ProductionBindingSpec{ 5u, 0u, ubo },
                    ProductionBindingSpec{ 5u, 8u, ssbo },
                    ProductionBindingSpec{ 5u, 16u, image },
                    ProductionBindingSpec{ 5u, 29u, ssbo }
                };
                productionStages_.reserve(5u);
                productionStages_.push_back(CreateProductionStage(
                    directory / "RestirInitialV3.comp.spv",
                    productionInitialBindings));
                productionStages_.push_back(CreateProductionStage(
                    directory / "RestirTemporalV3.comp.spv",
                    productionTemporalBindings));
                productionStages_.push_back(CreateProductionStage(
                    directory / "RestirSpatialV3.comp.spv",
                    productionSpatialBindings));
                productionStages_.push_back(CreateProductionStage(
                    directory / "RestirWinnerResolveV3.comp.spv",
                    productionWinnerBindings));
                productionStages_.push_back(CreateProductionStage(
                    directory / "RestirDebugV3.comp.spv",
                    productionDebugBindings));

                const std::array productionInitialWrites{
                    ProductionBufferWrite{ 3u, 24u, productionSurfaces_, ssbo },
                    ProductionBufferWrite{ 5u, 0u, productionParams_, ubo },
                    ProductionBufferWrite{ 5u, 1u, productionCandidates_, ssbo },
                    ProductionBufferWrite{ 5u, 9u, productionStats_, ssbo },
                    ProductionBufferWrite{ 5u, 26u, productionInitial_, ssbo }
                };
                const std::array productionTemporalWrites{
                    ProductionBufferWrite{ 3u, 24u, productionSurfaces_, ssbo },
                    ProductionBufferWrite{ 4u, 0u, productionMotion_, ssbo },
                    ProductionBufferWrite{ 5u, 0u, productionParams_, ubo },
                    ProductionBufferWrite{ 5u, 5u, productionHistorySurfaces_, ssbo },
                    ProductionBufferWrite{ 5u, 9u, productionStats_, ssbo },
                    ProductionBufferWrite{ 5u, 15u, productionReasons_, ssbo },
                    ProductionBufferWrite{ 5u, 21u, productionLightMap_, ssbo },
                    ProductionBufferWrite{ 5u, 26u, productionInitial_, ssbo },
                    ProductionBufferWrite{ 5u, 27u, productionTemporal_, ssbo },
                    ProductionBufferWrite{ 5u, 30u, productionHistory_, ssbo }
                };
                const std::array productionSpatialWrites{
                    ProductionBufferWrite{ 3u, 24u, productionSurfaces_, ssbo },
                    ProductionBufferWrite{ 5u, 0u, productionParams_, ubo },
                    ProductionBufferWrite{ 5u, 9u, productionStats_, ssbo },
                    ProductionBufferWrite{ 5u, 15u, productionReasons_, ssbo },
                    ProductionBufferWrite{ 5u, 22u, productionNeighbors_, ssbo },
                    ProductionBufferWrite{ 5u, 27u, productionTemporal_, ssbo },
                    ProductionBufferWrite{ 5u, 28u, productionSpatial_, ssbo }
                };
                const std::array productionWinnerWrites{
                    ProductionBufferWrite{ 5u, 0u, productionParams_, ubo },
                    ProductionBufferWrite{ 5u, 9u, productionStats_, ssbo },
                    ProductionBufferWrite{ 5u, 10u, productionVisibility_, ssbo },
                    ProductionBufferWrite{ 5u, 28u, productionSpatial_, ssbo }
                };
                const std::array productionDebugWrites{
                    ProductionBufferWrite{ 5u, 0u, productionParams_, ubo },
                    ProductionBufferWrite{ 5u, 8u, productionDebug_, ssbo },
                    ProductionBufferWrite{ 5u, 29u, productionSpatial_, ssbo }
                };
                WriteProductionStage(productionStages_[0],
                    productionInitialWrites, false);
                WriteProductionStage(productionStages_[1],
                    productionTemporalWrites, false);
                WriteProductionStage(productionStages_[2],
                    productionSpatialWrites, false);
                WriteProductionStage(productionStages_[3],
                    productionWinnerWrites, false);
                WriteProductionStage(productionStages_[4],
                    productionDebugWrites, true);
            }

            void Dispatch(
                const VkCommandBuffer commandBuffer,
                const Stage& stage,
                const std::uint32_t x,
                const std::uint32_t y = 1u)
            {
                vkCmdBindPipeline_(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stage.pipeline);
                vkCmdBindDescriptorSets_(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    stage.pipelineLayout, 5u, 1u, &stage.descriptorSet, 0u, nullptr);
                vkCmdDispatch_(commandBuffer, x, y, 1u);
            }

            void BufferBarrier(
                const VkCommandBuffer commandBuffer,
                const std::span<const std::size_t> bufferIds,
                const VkPipelineStageFlags2 sourceStage,
                const VkAccessFlags2 sourceAccess,
                const VkPipelineStageFlags2 destinationStage,
                const VkAccessFlags2 destinationAccess)
            {
                std::vector<VkBufferMemoryBarrier2> barriers;
                barriers.reserve(bufferIds.size());
                for (const std::size_t id : bufferIds)
                {
                    VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                    barrier.srcStageMask = sourceStage;
                    barrier.srcAccessMask = sourceAccess;
                    barrier.dstStageMask = destinationStage;
                    barrier.dstAccessMask = destinationAccess;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer = buffers_[id].handle;
                    barrier.offset = 0u;
                    barrier.size = VK_WHOLE_SIZE;
                    barriers.push_back(barrier);
                }
                VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                dependency.bufferMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
                dependency.pBufferMemoryBarriers = barriers.data();
                vkCmdPipelineBarrier2_(commandBuffer, &dependency);
            }

            void DispatchProduction(const VkCommandBuffer commandBuffer,
                const std::size_t stageIndex)
            {
                const ProductionStage& stage = productionStages_.at(stageIndex);
                vkCmdBindPipeline_(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stage.pipeline);
                vkCmdBindDescriptorSets_(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    stage.pipelineLayout, 0u,
                    static_cast<std::uint32_t>(stage.descriptorSets.size()),
                    stage.descriptorSets.data(), 0u, nullptr);
                vkCmdDispatch_(commandBuffer, 1u, 1u, 1u);
            }

            void ImageBarrier(
                const VkCommandBuffer commandBuffer,
                const VkPipelineStageFlags2 sourceStage,
                const VkAccessFlags2 sourceAccess,
                const VkPipelineStageFlags2 destinationStage,
                const VkAccessFlags2 destinationAccess,
                const VkImageLayout oldLayout,
                const VkImageLayout newLayout)
            {
                VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                barrier.srcStageMask = sourceStage;
                barrier.srcAccessMask = sourceAccess;
                barrier.dstStageMask = destinationStage;
                barrier.dstAccessMask = destinationAccess;
                barrier.oldLayout = oldLayout;
                barrier.newLayout = newLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = debugImage_.handle;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.layerCount = 1u;
                VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                dependency.imageMemoryBarrierCount = 1u;
                dependency.pImageMemoryBarriers = &barrier;
                vkCmdPipelineBarrier2_(commandBuffer, &dependency);
            }

            void RecordAndSubmit()
            {
                VkCommandBufferAllocateInfo allocateInfo{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
                };
                allocateInfo.commandPool = commandPool_;
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = 1u;
                VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
                Check(vkAllocateCommandBuffers_(device_, &allocateInfo, &commandBuffer),
                    "vkAllocateCommandBuffers");
                VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(vkBeginCommandBuffer_(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

                const std::array initialOutputs{ reservoirA_, debugBuffer_, statsBuffer_ };
                Dispatch(commandBuffer, stages_[0], 1u);
                BufferBarrier(commandBuffer, initialOutputs,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                const std::array temporalOutputs{ reservoirB_, debugBuffer_, statsBuffer_ };
                Dispatch(commandBuffer, stages_[1], 1u);
                BufferBarrier(commandBuffer, temporalOutputs,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                const std::array spatialOutputs{ reservoirC_, debugBuffer_, statsBuffer_ };
                Dispatch(commandBuffer, stages_[2], 1u);
                BufferBarrier(commandBuffer, spatialOutputs,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                const std::array visibilityOutputs{
                    reservoirD_, debugBuffer_, statsBuffer_, directLighting_
                };
                Dispatch(commandBuffer, stages_[3], 1u);
                BufferBarrier(commandBuffer, visibilityOutputs,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                ImageBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
                Dispatch(commandBuffer, stages_[4], 1u, 1u);
                ImageBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                VkBufferImageCopy copy{};
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1u;
                copy.imageExtent = { 2u, 2u, 1u };
                vkCmdCopyImageToBuffer_(commandBuffer, debugImage_.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffers_[imageReadback_].handle, 1u, &copy);

                // Run the production ABI-v3 shaders, not the legacy smoke ABI.
                const std::array productionBuffers{
                    productionInitial_, productionTemporal_, productionSpatial_,
                    productionReasons_, productionStats_, productionDebug_
                };
                for (std::size_t stage = 0u; stage < 4u; ++stage)
                {
                    DispatchProduction(commandBuffer, stage);
                    BufferBarrier(commandBuffer, productionBuffers,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                }
                ImageBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
                DispatchProduction(commandBuffer, 4u);
                ImageBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                vkCmdCopyImageToBuffer_(commandBuffer, debugImage_.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    buffers_[productionImageReadback_].handle, 1u, &copy);
                const std::array hostReadback{
                    productionInitial_, productionTemporal_, productionSpatial_,
                    productionDebug_, productionStats_, productionImageReadback_,
                    reservoirD_, directLighting_, debugBuffer_, statsBuffer_, imageReadback_
                };
                BufferBarrier(commandBuffer, hostReadback,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
                Check(vkEndCommandBuffer_(commandBuffer), "vkEndCommandBuffer");

                VkCommandBufferSubmitInfo commandInfo{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO
                };
                commandInfo.commandBuffer = commandBuffer;
                VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
                submitInfo.commandBufferInfoCount = 1u;
                submitInfo.pCommandBufferInfos = &commandInfo;
                Check(vkQueueSubmit2_(queue_, 1u, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit2");
                Check(vkQueueWaitIdle_(queue_), "vkQueueWaitIdle");

                const std::uint32_t validationErrors = validation_.errors.load(std::memory_order_relaxed);
                if (validationErrors != 0u)
                {
                    std::ostringstream message;
                    message << validationErrors << " Vulkan validation error(s) during L9 smoke";
                    std::scoped_lock lock(validation_.mutex);
                    if (!validation_.messages.empty())
                    {
                        message << ": " << validation_.messages.front();
                    }
                    throw std::runtime_error(message.str());
                }
            }

            template <typename Value>
            [[nodiscard]] std::vector<Value> ReadVector(
                const std::size_t id, const std::size_t count)
            {
                Buffer& buffer = buffers_[id];
                if (sizeof(Value) * count > buffer.logicalSize)
                {
                    throw std::logic_error("L9 Vulkan readback exceeds buffer size");
                }
                void* mapped = nullptr;
                Check(vkMapMemory_(device_, buffer.memory, 0u, VK_WHOLE_SIZE, 0u, &mapped),
                    "vkMapMemory(readback)");
                if (!buffer.coherent)
                {
                    VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
                    range.memory = buffer.memory;
                    range.offset = 0u;
                    range.size = VK_WHOLE_SIZE;
                    Check(vkInvalidateMappedMemoryRanges_(device_, 1u, &range),
                        "vkInvalidateMappedMemoryRanges");
                }
                std::vector<Value> values(count);
                std::memcpy(values.data(), mapped, sizeof(Value) * count);
                vkUnmapMemory_(device_, buffer.memory);
                return values;
            }

            static void HashBytes(
                std::uint64_t& hash, const void* const values, const std::size_t byteCount) noexcept
            {
                constexpr std::uint64_t offset = 14695981039346656037ull;
                constexpr std::uint64_t prime = 1099511628211ull;
                if (hash == 0u)
                {
                    hash = offset;
                }
                const auto* const bytes = static_cast<const std::uint8_t*>(values);
                for (std::size_t index = 0u; index < byteCount; ++index)
                {
                    hash ^= bytes[index];
                    hash *= prime;
                }
            }

            [[nodiscard]] VulkanSmokeReport ReadBack()
            {
                using namespace Contracts::AbiV3;
                const auto initial = ReadVector<GpuRestirReservoirV3>(productionInitial_, 4u);
                const auto temporal = ReadVector<GpuRestirReservoirV3>(productionTemporal_, 4u);
                const auto resolved = ReadVector<GpuRestirReservoirV3>(productionSpatial_, 4u);
                const auto productionDebug = ReadVector<GpuRestirDebugV3>(productionDebug_, 4u);
                const auto productionImage = ReadVector<Float4>(productionImageReadback_, 4u);
                const auto require = [](const bool passed, const char* message) {
                    if (!passed) throw std::runtime_error(message);
                };
                // Independent scalar oracle: M=1, sum=t/q, W=1/q.
                require(initial[0].state.x == 1u && initial[0].weightState.x == 8.0f
                    && initial[0].weightState.y == 4.0f,
                    "Production V3 M=1 normalization disagrees with t/q and 1/q");
                require(initial[1].state.x == 1u && initial[1].weightState.x == 0.0f
                    && initial[1].weightState.y == 0.0f,
                    "Production V3 legal zero-target proposal must count M=1 without a winner");
                require(initial[2].state.x == 0u && initial[2].weightState.y == 0.0f,
                    "Production V3 invalid surface must not count as a legal zero proposal");
                for (std::size_t pixel = 0u; pixel < 4u; ++pixel)
                {
                    require(std::memcmp(&initial[pixel], &temporal[pixel], sizeof(GpuRestirReservoirV3)) == 0,
                        "Production V3 disabled temporal stage changed the reservoir");
                    require(std::memcmp(&temporal[pixel].selected, &resolved[pixel].selected,
                            sizeof(temporal[pixel].selected)) == 0
                        && std::memcmp(&temporal[pixel].weightState, &resolved[pixel].weightState,
                            sizeof(Float4)) == 0
                        && temporal[pixel].state.x == resolved[pixel].state.x,
                        "Production V3 no-reuse spatial stage changed sample, weight or M");
                    require(productionDebug[pixel].state.x == resolved[pixel].state.x
                        && productionDebug[pixel].scalar.x == resolved[pixel].weightState.x
                        && productionDebug[pixel].scalar.y == resolved[pixel].weightState.y
                        && productionDebug[pixel].scalar.w == resolved[pixel].selectedTerms.w,
                        "Production V3 debug record does not match published reservoir");
                }
                // Visibility is a fixed input fixture, not a traversal claim.
                require(resolved[0].selectedTerms.w == 4.0f
                    && resolved[3].selectedTerms.w == 0.0f
                    && productionImage[0].x == 1.0f && productionImage[3].x == 0.0f,
                    "Production V3 winner visibility or debug image disagrees with fixed hit inputs");
                std::cout << "L9 production ABI-v3 GPU oracle: M=1, zero target, invalid surface, no reuse, visibility and debug readback PASS\n";
                const std::vector<GpuReservoir> reservoirs = ReadVector<GpuReservoir>(
                    reservoirD_, kReservoirCount);
                const std::vector<Float4> direct = ReadVector<Float4>(directLighting_, kReservoirCount);
                const std::vector<GpuDebug> debug = ReadVector<GpuDebug>(debugBuffer_, kReservoirCount);
                const std::vector<std::uint32_t> stats = ReadVector<std::uint32_t>(
                    statsBuffer_, kStatCount);
                const std::vector<Float4> image = ReadVector<Float4>(imageReadback_, kReservoirCount);

                VulkanSmokeReport report{};
                report.status = VulkanSmokeStatus::Passed;
                report.message = synchronizationValidationEnabled_
                    ? "five dispatches, synchronization validation, barriers, and readbacks completed"
                    : "five dispatches, barriers, and readbacks completed";
                report.deviceName = physicalProperties_.deviceName;
                report.validationLayerEnabled = validationEnabled_;
                report.validationErrorCount = validation_.errors.load(std::memory_order_relaxed);
                report.reservoirCount = static_cast<std::uint32_t>(reservoirs.size());
                report.debugImagePixelCount = static_cast<std::uint32_t>(image.size());
                for (const GpuReservoir& reservoir : reservoirs)
                {
                    if (reservoir.metadata.x != 0u &&
                        (reservoir.metadata.z & kReservoirFlagValid) != 0u)
                    {
                        ++report.nonemptyReservoirCount;
                    }
                    if (!std::isfinite(reservoir.weights.x) || !std::isfinite(reservoir.weights.y))
                    {
                        throw std::runtime_error("L9 Vulkan reservoir readback contains non-finite weights");
                    }
                }
                report.finalVisibilityRays = stats[10u];
                for (const Float4 value : direct)
                {
                    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                        !std::isfinite(value.z) || !std::isfinite(value.w))
                    {
                        throw std::runtime_error("L9 Vulkan direct-lighting readback is non-finite");
                    }
                }
                for (const Float4 value : image)
                {
                    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                        !std::isfinite(value.z) || !std::isfinite(value.w))
                    {
                        throw std::runtime_error("L9 Vulkan debug-image readback is non-finite");
                    }
                }
                HashBytes(report.readbackChecksum, reservoirs.data(), reservoirs.size() * sizeof(GpuReservoir));
                HashBytes(report.readbackChecksum, direct.data(), direct.size() * sizeof(Float4));
                HashBytes(report.readbackChecksum, debug.data(), debug.size() * sizeof(GpuDebug));
                HashBytes(report.readbackChecksum, stats.data(), stats.size() * sizeof(std::uint32_t));
                HashBytes(report.readbackChecksum, image.data(), image.size() * sizeof(Float4));
                if (report.nonemptyReservoirCount == 0u || report.finalVisibilityRays == 0u ||
                    report.readbackChecksum == 0u)
                {
                    throw std::runtime_error(
                        "L9 Vulkan dispatch completed but expected structural readback writes were absent");
                }
                return report;
            }

            void Cleanup() noexcept
            {
                if (device_ != VK_NULL_HANDLE && vkDeviceWaitIdle_ != nullptr)
                {
                    static_cast<void>(vkDeviceWaitIdle_(device_));
                }
                if (device_ != VK_NULL_HANDLE)
                {
                    for (auto stage = productionStages_.rbegin();
                        stage != productionStages_.rend(); ++stage)
                    {
                        if (stage->pipeline != VK_NULL_HANDLE && vkDestroyPipeline_ != nullptr)
                            vkDestroyPipeline_(device_, stage->pipeline, nullptr);
                        if (stage->pipelineLayout != VK_NULL_HANDLE && vkDestroyPipelineLayout_ != nullptr)
                            vkDestroyPipelineLayout_(device_, stage->pipelineLayout, nullptr);
                        for (const VkDescriptorSetLayout layout : stage->descriptorSetLayouts)
                            if (layout != VK_NULL_HANDLE && vkDestroyDescriptorSetLayout_ != nullptr)
                                vkDestroyDescriptorSetLayout_(device_, layout, nullptr);
                    }
                    for (auto stage = stages_.rbegin(); stage != stages_.rend(); ++stage)
                    {
                        if (stage->pipeline != VK_NULL_HANDLE && vkDestroyPipeline_ != nullptr)
                            vkDestroyPipeline_(device_, stage->pipeline, nullptr);
                        if (stage->pipelineLayout != VK_NULL_HANDLE && vkDestroyPipelineLayout_ != nullptr)
                            vkDestroyPipelineLayout_(device_, stage->pipelineLayout, nullptr);
                        if (stage->descriptorSetLayout != VK_NULL_HANDLE &&
                            vkDestroyDescriptorSetLayout_ != nullptr)
                            vkDestroyDescriptorSetLayout_(device_, stage->descriptorSetLayout, nullptr);
                    }
                    if (emptySetLayout_ != VK_NULL_HANDLE && vkDestroyDescriptorSetLayout_ != nullptr)
                        vkDestroyDescriptorSetLayout_(device_, emptySetLayout_, nullptr);
                    if (descriptorPool_ != VK_NULL_HANDLE && vkDestroyDescriptorPool_ != nullptr)
                        vkDestroyDescriptorPool_(device_, descriptorPool_, nullptr);
                    if (debugImage_.view != VK_NULL_HANDLE && vkDestroyImageView_ != nullptr)
                        vkDestroyImageView_(device_, debugImage_.view, nullptr);
                    if (debugImage_.handle != VK_NULL_HANDLE && vkDestroyImage_ != nullptr)
                        vkDestroyImage_(device_, debugImage_.handle, nullptr);
                    if (debugImage_.memory != VK_NULL_HANDLE && vkFreeMemory_ != nullptr)
                        vkFreeMemory_(device_, debugImage_.memory, nullptr);
                    for (auto buffer = buffers_.rbegin(); buffer != buffers_.rend(); ++buffer)
                    {
                        if (buffer->handle != VK_NULL_HANDLE && vkDestroyBuffer_ != nullptr)
                            vkDestroyBuffer_(device_, buffer->handle, nullptr);
                        if (buffer->memory != VK_NULL_HANDLE && vkFreeMemory_ != nullptr)
                            vkFreeMemory_(device_, buffer->memory, nullptr);
                    }
                    if (commandPool_ != VK_NULL_HANDLE && vkDestroyCommandPool_ != nullptr)
                        vkDestroyCommandPool_(device_, commandPool_, nullptr);
                    if (vkDestroyDevice_ != nullptr)
                        vkDestroyDevice_(device_, nullptr);
                    device_ = VK_NULL_HANDLE;
                }
                if (instance_ != VK_NULL_HANDLE)
                {
                    if (debugMessenger_ != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT_ != nullptr)
                        vkDestroyDebugUtilsMessengerEXT_(instance_, debugMessenger_, nullptr);
                    if (vkDestroyInstance_ != nullptr)
                        vkDestroyInstance_(instance_, nullptr);
                    instance_ = VK_NULL_HANDLE;
                }
                if (loader_ != nullptr)
                {
                    FreeLibrary(loader_);
                    loader_ = nullptr;
                }
            }
        };
    }

    VulkanSmokeReport RunVulkanSmoke()
    {
        try
        {
            VulkanHarness harness;
            return harness.Execute();
        }
        catch (const Unavailable& unavailable)
        {
            VulkanSmokeReport report{};
            report.status = VulkanSmokeStatus::SkippedUnavailable;
            report.message = unavailable.what();
            return report;
        }
    }
}
