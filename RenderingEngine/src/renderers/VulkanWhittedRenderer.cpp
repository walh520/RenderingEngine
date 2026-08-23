#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <windowsx.h>
#include <vulkan/vulkan.h>

#include "renderers/VulkanWhittedRenderer.hpp"

#include "core/Camera.hpp"
#include "scene/GpuScene.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RenderingEngine
{
    namespace
    {
        constexpr std::uint32_t kInitialWidth = 1280;
        constexpr std::uint32_t kInitialHeight = 720;
        constexpr std::uint32_t kFramesInFlight = 2;
        constexpr std::uint32_t kMaximumAccumulationSamples = 4096;
        constexpr VkFormat kHdrFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

#if defined(NDEBUG)
        constexpr bool kRequestValidation = false;
#else
        constexpr bool kRequestValidation = true;
#endif

        struct alignas(16) FrameConstants
        {
            Float4 cameraPositionTanHalfFov;
            Float4 cameraForwardAspect;
            Float4 cameraRightTime;
            Float4 cameraUpExposure;
            UInt4 imageAndScene;
            UInt4 lightAndTrace;
            UInt4 samplingAndDebug;
        };

        struct alignas(16) PresentConstants
        {
            float exposure = 1.0f;
            std::uint32_t applyManualGamma = 0;
            float padding[2]{};
        };

        static_assert(sizeof(FrameConstants) == 112);
        static_assert(sizeof(PresentConstants) == 16);

        struct Buffer
        {
            VkBuffer handle = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
            void* mapped = nullptr;
        };

        struct FrameResources
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkFence inFlight = VK_NULL_HANDLE;
            Buffer uniformBuffer;
            VkDescriptorSet computeDescriptorSet = VK_NULL_HANDLE;
        };

        struct SceneBufferLayout
        {
            VkDeviceSize materialsOffset = 0;
            VkDeviceSize materialsSize = 0;
            VkDeviceSize spheresOffset = 0;
            VkDeviceSize spheresSize = 0;
            VkDeviceSize planesOffset = 0;
            VkDeviceSize planesSize = 0;
            VkDeviceSize lightsOffset = 0;
            VkDeviceSize lightsSize = 0;
            VkDeviceSize totalSize = 0;
        };

        struct SwapchainSupport
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        [[nodiscard]] VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        void Check(VkResult result, std::string_view operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(
                    std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)));
            }
        }

        [[nodiscard]] std::filesystem::path ExecutableDirectory()
        {
            std::array<wchar_t, 32768> pathBuffer{};
            const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
            if (length == 0 || length == pathBuffer.size())
            {
                throw std::runtime_error("Unable to resolve the executable directory.");
            }
            return std::filesystem::path(pathBuffer.data(), pathBuffer.data() + length).parent_path();
        }

        [[nodiscard]] std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::ate | std::ios::binary);
            if (!stream)
            {
                throw std::runtime_error("Unable to open shader: " + path.string());
            }

            const std::streamsize byteCount = stream.tellg();
            if (byteCount <= 0 || (byteCount % 4) != 0)
            {
                throw std::runtime_error("Invalid SPIR-V byte count: " + path.string());
            }

            std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(words.data()), byteCount);
            if (!stream)
            {
                throw std::runtime_error("Unable to read shader: " + path.string());
            }
            return words;
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void*)
        {
            if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                std::cerr << "[Vulkan] " << callbackData->pMessage << '\n';
            }
            return VK_FALSE;
        }

        [[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerInfo()
        {
            VkDebugUtilsMessengerCreateInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            info.pfnUserCallback = DebugCallback;
            return info;
        }

        [[nodiscard]] constexpr std::string_view ShadowMethodName(ShadowMethod method)
        {
            switch (method)
            {
            case ShadowMethod::Pcf:
                return "PCF preview";
            case ShadowMethod::Pcss:
                return "PCSS preview";
            case ShadowMethod::Physical:
                return "physical area light";
            }
            return "unknown";
        }

        [[nodiscard]] constexpr std::string_view IntegratorName(Integrator integrator)
        {
            switch (integrator)
            {
            case Integrator::Pbr:
                return "PBR path tracer";
            case Integrator::Whitted:
                return "Whitted ray tracer";
            }
            return "unknown integrator";
        }

        [[nodiscard]] constexpr std::string_view DebugViewName(DebugView view)
        {
            switch (view)
            {
            case DebugView::Final:
                return "final";
            case DebugView::BaseColor:
                return "base color";
            case DebugView::Normal:
                return "normal";
            case DebugView::Roughness:
                return "roughness";
            case DebugView::Metallic:
                return "metallic";
            case DebugView::Emissive:
                return "emissive";
            }
            return "unknown";
        }
    }

    class VulkanWhittedRenderer::Impl final
    {
    public:
        ~Impl()
        {
            Cleanup();
        }

        void Run(const RunOptions& options)
        {
            if (initialized_)
            {
                throw std::logic_error("VulkanWhittedRenderer::Run may only be called once.");
            }
            if (!(options.exposure >= 0.01f && options.exposure <= 64.0f))
            {
                throw std::out_of_range("RunOptions::exposure must be from 0.01 to 64.");
            }
            if (options.maximumTraceDepth < 1 || options.maximumTraceDepth > 12)
            {
                throw std::out_of_range("RunOptions::maximumTraceDepth must be from 1 to 12.");
            }
            if (options.integrator != Integrator::Whitted && options.integrator != Integrator::Pbr)
            {
                throw std::invalid_argument("RunOptions::integrator is invalid.");
            }
            if (options.shadowMethod != ShadowMethod::Pcf
                && options.shadowMethod != ShadowMethod::Pcss
                && options.shadowMethod != ShadowMethod::Physical)
            {
                throw std::invalid_argument("RunOptions::shadowMethod is invalid.");
            }
            if (options.debugView != DebugView::Final
                && options.debugView != DebugView::BaseColor
                && options.debugView != DebugView::Normal
                && options.debugView != DebugView::Roughness
                && options.debugView != DebugView::Metallic
                && options.debugView != DebugView::Emissive)
            {
                throw std::invalid_argument("RunOptions::debugView is invalid.");
            }

            exposure_ = options.exposure;
            maximumTraceDepth_ = options.maximumTraceDepth;
            integrator_ = options.integrator;
            shadowMethod_ = options.shadowMethod;
            debugView_ = options.debugView;
            Initialize();
            MainLoop(options);
        }

    private:
        void Initialize()
        {
            CreateApplicationWindow();
            CreateInstance();
            CreateDebugMessenger();
            CreateSurface();
            PickPhysicalDevice();
            CreateLogicalDevice();
            CreateSwapchain();
            CreateCommandPool();
            CreateDescriptorSetLayouts();
            CreatePipelineLayouts();
            scene_ = CreateDemoScene();
            CreateSceneBuffer();
            CreateUniformBuffers();
            CreateOutputImage();
            CreateSampler();
            CreateDescriptorPoolAndSets();
            CreateComputePipeline();
            CreateGraphicsPipeline();
            AllocateCommandBuffers();
            CreateSyncObjects();
            initialized_ = true;
        }

        void CreateApplicationWindow()
        {
            windowInstance_ = GetModuleHandleW(nullptr);
            if (windowInstance_ == nullptr)
            {
                throw std::runtime_error("GetModuleHandleW failed.");
            }

            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.style = CS_HREDRAW | CS_VREDRAW;
            windowClass.lpfnWndProc = WindowProcedure;
            windowClass.hInstance = windowInstance_;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.lpszClassName = windowClassName_;
            windowClassAtom_ = RegisterClassExW(&windowClass);
            if (windowClassAtom_ == 0)
            {
                throw std::runtime_error("RegisterClassExW failed with Win32 error "
                    + std::to_string(GetLastError()) + '.');
            }

            RECT windowRectangle{
                0,
                0,
                static_cast<LONG>(kInitialWidth),
                static_cast<LONG>(kInitialHeight)
            };
            constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;
            if (AdjustWindowRectEx(&windowRectangle, windowStyle, FALSE, 0) == FALSE)
            {
                throw std::runtime_error("AdjustWindowRectEx failed.");
            }

            window_ = CreateWindowExW(
                0,
                windowClassName_,
                L"Vulkan HLSL Renderer",
                windowStyle,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                windowRectangle.right - windowRectangle.left,
                windowRectangle.bottom - windowRectangle.top,
                nullptr,
                nullptr,
                windowInstance_,
                this);
            if (window_ == nullptr)
            {
                throw std::runtime_error("CreateWindowExW failed with Win32 error "
                    + std::to_string(GetLastError()) + '.');
            }

            RAWINPUTDEVICE rawMouse{};
            rawMouse.usUsagePage = 0x01;
            rawMouse.usUsage = 0x02;
            rawMouse.dwFlags = 0;
            rawMouse.hwndTarget = window_;
            if (RegisterRawInputDevices(&rawMouse, 1, sizeof(rawMouse)) == FALSE)
            {
                throw std::runtime_error("RegisterRawInputDevices failed.");
            }

            ShowWindow(window_, SW_SHOW);
            UpdateWindow(window_);
            SetMouseCapture(true);
        }

        [[nodiscard]] bool ValidationLayerAvailable() const
        {
            std::uint32_t count = 0;
            Check(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties(count)");
            std::vector<VkLayerProperties> layers(count);
            Check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties");
            return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer)
            {
                return std::strcmp(layer.layerName, kValidationLayer) == 0;
            });
        }

        void CreateInstance()
        {
            validationEnabled_ = kRequestValidation && ValidationLayerAvailable();
            if (kRequestValidation && !validationEnabled_)
            {
                std::cerr << "[Vulkan] Validation layer is unavailable; continuing without it.\n";
            }

            VkApplicationInfo applicationInfo{};
            applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            applicationInfo.pApplicationName = "Vulkan HLSL Renderer";
            applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
            applicationInfo.pEngineName = "RenderingEngine";
            applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
            applicationInfo.apiVersion = VK_API_VERSION_1_3;

            std::vector<const char*> extensions{
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_WIN32_SURFACE_EXTENSION_NAME
            };
            if (validationEnabled_)
            {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }

            VkDebugUtilsMessengerCreateInfoEXT debugInfo = MakeDebugMessengerInfo();
            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &applicationInfo;
            createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();
            if (validationEnabled_)
            {
                createInfo.enabledLayerCount = 1;
                createInfo.ppEnabledLayerNames = &kValidationLayer;
                createInfo.pNext = &debugInfo;
            }

            Check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
        }

        void CreateDebugMessenger()
        {
            if (!validationEnabled_)
            {
                return;
            }

            const auto createFunction = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            if (createFunction == nullptr)
            {
                throw std::runtime_error("VK_EXT_debug_utils was enabled but its entry point is unavailable.");
            }

            const VkDebugUtilsMessengerCreateInfoEXT info = MakeDebugMessengerInfo();
            Check(createFunction(instance_, &info, nullptr, &debugMessenger_), "vkCreateDebugUtilsMessengerEXT");
        }

        void CreateSurface()
        {
            VkWin32SurfaceCreateInfoKHR createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            createInfo.hinstance = windowInstance_;
            createInfo.hwnd = window_;
            Check(vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_), "vkCreateWin32SurfaceKHR");
        }

        [[nodiscard]] std::optional<std::uint32_t> FindUnifiedQueueFamily(VkPhysicalDevice device) const
        {
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> properties(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

            for (std::uint32_t index = 0; index < count; ++index)
            {
                VkBool32 supportsPresent = VK_FALSE;
                Check(
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &supportsPresent),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
                const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
                if ((properties[index].queueFlags & required) == required && supportsPresent == VK_TRUE)
                {
                    return index;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool HasDeviceExtension(VkPhysicalDevice device, const char* requiredExtension) const
        {
            std::uint32_t count = 0;
            Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
                "vkEnumerateDeviceExtensionProperties(count)");
            std::vector<VkExtensionProperties> extensions(count);
            Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
                "vkEnumerateDeviceExtensionProperties");
            return std::any_of(extensions.begin(), extensions.end(), [requiredExtension](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, requiredExtension) == 0;
            });
        }

        [[nodiscard]] SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const
        {
            SwapchainSupport support;
            Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &support.capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

            std::uint32_t formatCount = 0;
            Check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
            support.formats.resize(formatCount);
            if (formatCount > 0)
            {
                Check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, support.formats.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR");
            }

            std::uint32_t presentModeCount = 0;
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
            support.presentModes.resize(presentModeCount);
            if (presentModeCount > 0)
            {
                Check(vkGetPhysicalDeviceSurfacePresentModesKHR(
                    device, surface_, &presentModeCount, support.presentModes.data()),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR");
            }
            return support;
        }

        [[nodiscard]] int ScorePhysicalDevice(VkPhysicalDevice device) const
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1
                || (VK_API_VERSION_MAJOR(properties.apiVersion) == 1
                    && VK_API_VERSION_MINOR(properties.apiVersion) < 3))
            {
                return -1;
            }

            if (!FindUnifiedQueueFamily(device).has_value()
                || !HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                return -1;
            }

            const SwapchainSupport swapchainSupport = QuerySwapchainSupport(device);
            if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
            {
                return -1;
            }

            VkPhysicalDeviceVulkan13Features vulkan13Features{};
            vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceFeatures2 features{};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &vulkan13Features;
            vkGetPhysicalDeviceFeatures2(device, &features);
            if (vulkan13Features.dynamicRendering != VK_TRUE || vulkan13Features.synchronization2 != VK_TRUE)
            {
                return -1;
            }

            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(device, kHdrFormat, &formatProperties);
            const VkFormatFeatureFlags requiredFormatFeatures =
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if ((formatProperties.optimalTilingFeatures & requiredFormatFeatures) != requiredFormatFeatures)
            {
                return -1;
            }

            int score = static_cast<int>(properties.limits.maxImageDimension2D);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                score += 100000;
            }
            return score;
        }

        void PickPhysicalDevice()
        {
            std::uint32_t count = 0;
            Check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices(count)");
            if (count == 0)
            {
                throw std::runtime_error("No Vulkan physical device was found.");
            }

            std::vector<VkPhysicalDevice> devices(count);
            Check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices");

            int bestScore = -1;
            for (VkPhysicalDevice device : devices)
            {
                const int score = ScorePhysicalDevice(device);
                if (score > bestScore)
                {
                    bestScore = score;
                    physicalDevice_ = device;
                }
            }

            if (physicalDevice_ == VK_NULL_HANDLE)
            {
                throw std::runtime_error(
                    "No Vulkan 1.3 device supports graphics + compute + present, dynamic rendering, synchronization2, and RGBA32F storage images.");
            }

            queueFamilyIndex_ = *FindUnifiedQueueFamily(physicalDevice_);
            vkGetPhysicalDeviceProperties(physicalDevice_, &physicalDeviceProperties_);
            std::cout << "Using GPU: " << physicalDeviceProperties_.deviceName
                << " (Vulkan " << VK_API_VERSION_MAJOR(physicalDeviceProperties_.apiVersion)
                << '.' << VK_API_VERSION_MINOR(physicalDeviceProperties_.apiVersion) << ")\n";
        }

        void CreateLogicalDevice()
        {
            constexpr float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndex_;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;

            VkPhysicalDeviceVulkan13Features vulkan13Features{};
            vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            vulkan13Features.dynamicRendering = VK_TRUE;
            vulkan13Features.synchronization2 = VK_TRUE;

            constexpr const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
            VkDeviceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pNext = &vulkan13Features;
            createInfo.queueCreateInfoCount = 1;
            createInfo.pQueueCreateInfos = &queueInfo;
            createInfo.enabledExtensionCount = 1;
            createInfo.ppEnabledExtensionNames = extensions;

            Check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
            vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);
        }

        [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
        {
            const auto preferred = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format)
            {
                return format.format == VK_FORMAT_B8G8R8A8_SRGB
                    && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
            if (preferred != formats.end())
            {
                return *preferred;
            }
            return formats.front();
        }

        [[nodiscard]] VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
        {
            if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        [[nodiscard]] VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
        {
            if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
            {
                return capabilities.currentExtent;
            }

            RECT clientRectangle{};
            if (GetClientRect(window_, &clientRectangle) == FALSE)
            {
                throw std::runtime_error("GetClientRect failed.");
            }
            const LONG width = clientRectangle.right - clientRectangle.left;
            const LONG height = clientRectangle.bottom - clientRectangle.top;
            return {
                std::clamp(static_cast<std::uint32_t>(width),
                    capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(static_cast<std::uint32_t>(height),
                    capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
            };
        }

        void CreateSwapchain()
        {
            const SwapchainSupport support = QuerySwapchainSupport(physicalDevice_);
            const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
            const VkPresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
            const VkExtent2D extent = ChooseExtent(support.capabilities);

            std::uint32_t imageCount = support.capabilities.minImageCount + 1;
            if (support.capabilities.maxImageCount > 0)
            {
                imageCount = std::min(imageCount, support.capabilities.maxImageCount);
            }

            VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            if ((support.capabilities.supportedCompositeAlpha & compositeAlpha) == 0)
            {
                constexpr VkCompositeAlphaFlagBitsKHR candidates[] = {
                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                };
                for (const VkCompositeAlphaFlagBitsKHR candidate : candidates)
                {
                    if ((support.capabilities.supportedCompositeAlpha & candidate) != 0)
                    {
                        compositeAlpha = candidate;
                        break;
                    }
                }
            }

            VkSwapchainCreateInfoKHR createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            createInfo.surface = surface_;
            createInfo.minImageCount = imageCount;
            createInfo.imageFormat = surfaceFormat.format;
            createInfo.imageColorSpace = surfaceFormat.colorSpace;
            createInfo.imageExtent = extent;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.preTransform = support.capabilities.currentTransform;
            createInfo.compositeAlpha = compositeAlpha;
            createInfo.presentMode = presentMode;
            createInfo.clipped = VK_TRUE;

            Check(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");
            swapchainFormat_ = surfaceFormat.format;
            swapchainExtent_ = extent;
            swapchainIsSrgb_ = swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB
                || swapchainFormat_ == VK_FORMAT_R8G8B8A8_SRGB;

            Check(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR(count)");
            swapchainImages_.resize(imageCount);
            Check(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data()),
                "vkGetSwapchainImagesKHR");

            swapchainImageViews_.resize(swapchainImages_.size());
            for (std::size_t index = 0; index < swapchainImages_.size(); ++index)
            {
                swapchainImageViews_[index] = CreateImageView(swapchainImages_[index], swapchainFormat_);
            }
        }

        [[nodiscard]] VkImageView CreateImageView(VkImage image, VkFormat format) const
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = image;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = format;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.layerCount = 1;

            VkImageView view = VK_NULL_HANDLE;
            Check(vkCreateImageView(device_, &createInfo, nullptr, &view), "vkCreateImageView");
            return view;
        }

        void CreateCommandPool()
        {
            VkCommandPoolCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            createInfo.queueFamilyIndex = queueFamilyIndex_;
            Check(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_), "vkCreateCommandPool");
        }

        void CreateDescriptorSetLayouts()
        {
            std::array<VkDescriptorSetLayoutBinding, 6> computeBindings{};
            computeBindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            for (std::uint32_t binding = 1; binding <= 4; ++binding)
            {
                computeBindings[binding] = {
                    binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
                };
            }
            computeBindings[5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

            VkDescriptorSetLayoutCreateInfo computeInfo{};
            computeInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            computeInfo.bindingCount = static_cast<std::uint32_t>(computeBindings.size());
            computeInfo.pBindings = computeBindings.data();
            Check(vkCreateDescriptorSetLayout(device_, &computeInfo, nullptr, &computeDescriptorSetLayout_),
                "vkCreateDescriptorSetLayout(compute)");

            std::array<VkDescriptorSetLayoutBinding, 2> presentBindings{};
            presentBindings[0] = { 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            presentBindings[1] = { 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };

            VkDescriptorSetLayoutCreateInfo presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            presentInfo.bindingCount = static_cast<std::uint32_t>(presentBindings.size());
            presentInfo.pBindings = presentBindings.data();
            Check(vkCreateDescriptorSetLayout(device_, &presentInfo, nullptr, &presentDescriptorSetLayout_),
                "vkCreateDescriptorSetLayout(present)");
        }

        void CreatePipelineLayouts()
        {
            VkPipelineLayoutCreateInfo computeInfo{};
            computeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            computeInfo.setLayoutCount = 1;
            computeInfo.pSetLayouts = &computeDescriptorSetLayout_;
            Check(vkCreatePipelineLayout(device_, &computeInfo, nullptr, &computePipelineLayout_),
                "vkCreatePipelineLayout(compute)");

            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.size = sizeof(PresentConstants);

            VkPipelineLayoutCreateInfo presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            presentInfo.setLayoutCount = 1;
            presentInfo.pSetLayouts = &presentDescriptorSetLayout_;
            presentInfo.pushConstantRangeCount = 1;
            presentInfo.pPushConstantRanges = &pushConstantRange;
            Check(vkCreatePipelineLayout(device_, &presentInfo, nullptr, &presentPipelineLayout_),
                "vkCreatePipelineLayout(present)");
        }

        [[nodiscard]] std::uint32_t FindMemoryType(
            std::uint32_t allowedTypes,
            VkMemoryPropertyFlags properties) const
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
            for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
            {
                if ((allowedTypes & (1u << index)) != 0
                    && (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties)
                {
                    return index;
                }
            }
            throw std::runtime_error("No compatible Vulkan memory type was found.");
        }

        void CreateBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags memoryProperties,
            Buffer& buffer)
        {
            buffer.size = size;
            VkBufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            createInfo.size = size;
            createInfo.usage = usage;
            createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            Check(vkCreateBuffer(device_, &createInfo, nullptr, &buffer.handle), "vkCreateBuffer");

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device_, buffer.handle, &requirements);
            VkMemoryAllocateInfo allocationInfo{};
            allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocationInfo.allocationSize = requirements.size;
            allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, memoryProperties);
            Check(vkAllocateMemory(device_, &allocationInfo, nullptr, &buffer.memory), "vkAllocateMemory(buffer)");
            Check(vkBindBufferMemory(device_, buffer.handle, buffer.memory, 0), "vkBindBufferMemory");
        }

        void DestroyBuffer(Buffer& buffer)
        {
            if (buffer.mapped != nullptr)
            {
                vkUnmapMemory(device_, buffer.memory);
                buffer.mapped = nullptr;
            }
            if (buffer.handle != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device_, buffer.handle, nullptr);
                buffer.handle = VK_NULL_HANDLE;
            }
            if (buffer.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_, buffer.memory, nullptr);
                buffer.memory = VK_NULL_HANDLE;
            }
        }

        template<typename Record>
        static void CopyRecords(
            std::byte* destination,
            VkDeviceSize offset,
            const std::vector<Record>& records)
        {
            std::memcpy(destination + offset, records.data(), records.size() * sizeof(Record));
        }

        void CreateSceneBuffer()
        {
            const VkDeviceSize alignment = std::max<VkDeviceSize>(
                16, physicalDeviceProperties_.limits.minStorageBufferOffsetAlignment);
            sceneBufferLayout_.materialsSize = scene_.materials.size() * sizeof(GpuMaterial);
            sceneBufferLayout_.materialsOffset = 0;
            sceneBufferLayout_.spheresSize = scene_.spheres.size() * sizeof(GpuSphere);
            sceneBufferLayout_.spheresOffset = AlignUp(sceneBufferLayout_.materialsSize, alignment);
            sceneBufferLayout_.planesSize = scene_.planes.size() * sizeof(GpuPlane);
            sceneBufferLayout_.planesOffset = AlignUp(
                sceneBufferLayout_.spheresOffset + sceneBufferLayout_.spheresSize, alignment);
            sceneBufferLayout_.lightsSize = scene_.lights.size() * sizeof(GpuLight);
            sceneBufferLayout_.lightsOffset = AlignUp(
                sceneBufferLayout_.planesOffset + sceneBufferLayout_.planesSize, alignment);
            sceneBufferLayout_.totalSize = sceneBufferLayout_.lightsOffset + sceneBufferLayout_.lightsSize;

            Buffer staging;
            CreateBuffer(
                sceneBufferLayout_.totalSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging);
            Check(vkMapMemory(device_, staging.memory, 0, staging.size, 0, &staging.mapped), "vkMapMemory(scene staging)");
            auto* bytes = static_cast<std::byte*>(staging.mapped);
            std::memset(bytes, 0, static_cast<std::size_t>(staging.size));
            CopyRecords(bytes, sceneBufferLayout_.materialsOffset, scene_.materials);
            CopyRecords(bytes, sceneBufferLayout_.spheresOffset, scene_.spheres);
            CopyRecords(bytes, sceneBufferLayout_.planesOffset, scene_.planes);
            CopyRecords(bytes, sceneBufferLayout_.lightsOffset, scene_.lights);
            vkUnmapMemory(device_, staging.memory);
            staging.mapped = nullptr;

            CreateBuffer(
                sceneBufferLayout_.totalSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                sceneBuffer_);

            ImmediateSubmit([&](VkCommandBuffer commandBuffer)
            {
                VkBufferCopy copy{};
                copy.size = sceneBufferLayout_.totalSize;
                vkCmdCopyBuffer(commandBuffer, staging.handle, sceneBuffer_.handle, 1, &copy);
            });
            DestroyBuffer(staging);
        }

        void CreateUniformBuffers()
        {
            for (FrameResources& frame : frames_)
            {
                CreateBuffer(
                    sizeof(FrameConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    frame.uniformBuffer);
                Check(vkMapMemory(
                    device_, frame.uniformBuffer.memory, 0, frame.uniformBuffer.size, 0, &frame.uniformBuffer.mapped),
                    "vkMapMemory(frame constants)");
            }
        }

        void CreateOutputImage()
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = kHdrFormat;
            imageInfo.extent = { swapchainExtent_.width, swapchainExtent_.height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Check(vkCreateImage(device_, &imageInfo, nullptr, &outputImage_), "vkCreateImage(HDR output)");

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device_, outputImage_, &requirements);
            VkMemoryAllocateInfo allocationInfo{};
            allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocationInfo.allocationSize = requirements.size;
            allocationInfo.memoryTypeIndex = FindMemoryType(
                requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Check(vkAllocateMemory(device_, &allocationInfo, nullptr, &outputImageMemory_),
                "vkAllocateMemory(HDR output)");
            Check(vkBindImageMemory(device_, outputImage_, outputImageMemory_, 0),
                "vkBindImageMemory(HDR output)");
            outputImageView_ = CreateImageView(outputImage_, kHdrFormat);

            ImmediateSubmit([&](VkCommandBuffer commandBuffer)
            {
                TransitionImage(
                    commandBuffer,
                    outputImage_,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            });
        }

        void CreateSampler()
        {
            VkSamplerCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            // The HDR image and swapchain always have the same extent, so the
            // fullscreen pass is a one-to-one copy. Nearest filtering avoids
            // requiring optional linear-filter support for RGBA32F images.
            createInfo.magFilter = VK_FILTER_NEAREST;
            createInfo.minFilter = VK_FILTER_NEAREST;
            createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            createInfo.maxLod = 0.0f;
            Check(vkCreateSampler(device_, &createInfo, nullptr, &sampler_), "vkCreateSampler");
        }

        void CreateDescriptorPoolAndSets()
        {
            const std::array<VkDescriptorPoolSize, 5> sizes = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight * 4 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kFramesInFlight },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1 }
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.maxSets = kFramesInFlight + 1;
            poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
            poolInfo.pPoolSizes = sizes.data();
            Check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_),
                "vkCreateDescriptorPool");

            std::array<VkDescriptorSetLayout, kFramesInFlight> computeLayouts{};
            computeLayouts.fill(computeDescriptorSetLayout_);
            std::array<VkDescriptorSet, kFramesInFlight> computeSets{};
            VkDescriptorSetAllocateInfo computeAllocateInfo{};
            computeAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            computeAllocateInfo.descriptorPool = descriptorPool_;
            computeAllocateInfo.descriptorSetCount = kFramesInFlight;
            computeAllocateInfo.pSetLayouts = computeLayouts.data();
            Check(vkAllocateDescriptorSets(device_, &computeAllocateInfo, computeSets.data()),
                "vkAllocateDescriptorSets(compute)");
            for (std::size_t index = 0; index < frames_.size(); ++index)
            {
                frames_[index].computeDescriptorSet = computeSets[index];
            }

            VkDescriptorSetAllocateInfo presentAllocateInfo{};
            presentAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            presentAllocateInfo.descriptorPool = descriptorPool_;
            presentAllocateInfo.descriptorSetCount = 1;
            presentAllocateInfo.pSetLayouts = &presentDescriptorSetLayout_;
            Check(vkAllocateDescriptorSets(device_, &presentAllocateInfo, &presentDescriptorSet_),
                "vkAllocateDescriptorSets(present)");
            UpdateAllDescriptors();
        }

        void UpdateAllDescriptors()
        {
            for (FrameResources& frame : frames_)
            {
                const VkDescriptorBufferInfo uniformInfo{
                    frame.uniformBuffer.handle, 0, sizeof(FrameConstants)
                };
                const VkDescriptorBufferInfo sphereInfo{
                    sceneBuffer_.handle, sceneBufferLayout_.spheresOffset, sceneBufferLayout_.spheresSize
                };
                const VkDescriptorBufferInfo planeInfo{
                    sceneBuffer_.handle, sceneBufferLayout_.planesOffset, sceneBufferLayout_.planesSize
                };
                const VkDescriptorBufferInfo materialInfo{
                    sceneBuffer_.handle, sceneBufferLayout_.materialsOffset, sceneBufferLayout_.materialsSize
                };
                const VkDescriptorBufferInfo lightInfo{
                    sceneBuffer_.handle, sceneBufferLayout_.lightsOffset, sceneBufferLayout_.lightsSize
                };
                const VkDescriptorImageInfo storageImageInfo{
                    VK_NULL_HANDLE, outputImageView_, VK_IMAGE_LAYOUT_GENERAL
                };

                std::array<VkWriteDescriptorSet, 6> writes{};
                const std::array<const VkDescriptorBufferInfo*, 5> bufferInfos = {
                    &uniformInfo, &sphereInfo, &planeInfo, &materialInfo, &lightInfo
                };
                const std::array<VkDescriptorType, 5> descriptorTypes = {
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                };
                for (std::uint32_t binding = 0; binding < 5; ++binding)
                {
                    writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[binding].dstSet = frame.computeDescriptorSet;
                    writes[binding].dstBinding = binding;
                    writes[binding].descriptorCount = 1;
                    writes[binding].descriptorType = descriptorTypes[binding];
                    writes[binding].pBufferInfo = bufferInfos[binding];
                }
                writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[5].dstSet = frame.computeDescriptorSet;
                writes[5].dstBinding = 5;
                writes[5].descriptorCount = 1;
                writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[5].pImageInfo = &storageImageInfo;
                vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }

            const VkDescriptorImageInfo sampledImageInfo{
                VK_NULL_HANDLE, outputImageView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            const VkDescriptorImageInfo samplerInfo{
                sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED
            };
            std::array<VkWriteDescriptorSet, 2> presentWrites{};
            presentWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            presentWrites[0].dstSet = presentDescriptorSet_;
            presentWrites[0].dstBinding = 0;
            presentWrites[0].descriptorCount = 1;
            presentWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            presentWrites[0].pImageInfo = &sampledImageInfo;
            presentWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            presentWrites[1].dstSet = presentDescriptorSet_;
            presentWrites[1].dstBinding = 1;
            presentWrites[1].descriptorCount = 1;
            presentWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            presentWrites[1].pImageInfo = &samplerInfo;
            vkUpdateDescriptorSets(
                device_, static_cast<std::uint32_t>(presentWrites.size()), presentWrites.data(), 0, nullptr);
        }

        [[nodiscard]] VkShaderModule CreateShaderModule(const std::filesystem::path& path) const
        {
            const std::vector<std::uint32_t> code = ReadSpirv(path);
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size() * sizeof(std::uint32_t);
            createInfo.pCode = code.data();
            VkShaderModule module = VK_NULL_HANDLE;
            Check(vkCreateShaderModule(device_, &createInfo, nullptr, &module), "vkCreateShaderModule");
            return module;
        }

        void CreateComputePipeline()
        {
            const char* shaderFileName = integrator_ == Integrator::Pbr
                ? "PbrPathTrace.comp.spv"
                : "WhittedTrace.comp.spv";
            const VkShaderModule shader = CreateShaderModule(ExecutableDirectory() / shaderFileName);
            VkPipelineShaderStageCreateInfo stage{};
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module = shader;
            stage.pName = "CSMain";

            VkComputePipelineCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            createInfo.stage = stage;
            createInfo.layout = computePipelineLayout_;
            const VkResult result = vkCreateComputePipelines(
                device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &computePipeline_);
            vkDestroyShaderModule(device_, shader, nullptr);
            Check(result, "vkCreateComputePipelines");
        }

        void CreateGraphicsPipeline()
        {
            const VkShaderModule vertexShader = CreateShaderModule(ExecutableDirectory() / "Present.vert.spv");
            const VkShaderModule fragmentShader = CreateShaderModule(ExecutableDirectory() / "Present.frag.spv");

            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertexShader;
            stages[0].pName = "VSMain";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragmentShader;
            stages[1].pName = "PSMain";

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterization{};
            rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterization.polygonMode = VK_POLYGON_MODE_FILL;
            rasterization.cullMode = VK_CULL_MODE_NONE;
            rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterization.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &blendAttachment;

            constexpr VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };
            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            VkPipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = &swapchainFormat_;

            VkGraphicsPipelineCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            createInfo.pNext = &renderingInfo;
            createInfo.stageCount = static_cast<std::uint32_t>(stages.size());
            createInfo.pStages = stages.data();
            createInfo.pVertexInputState = &vertexInput;
            createInfo.pInputAssemblyState = &inputAssembly;
            createInfo.pViewportState = &viewportState;
            createInfo.pRasterizationState = &rasterization;
            createInfo.pMultisampleState = &multisample;
            createInfo.pColorBlendState = &blend;
            createInfo.pDynamicState = &dynamicState;
            createInfo.layout = presentPipelineLayout_;

            const VkResult result = vkCreateGraphicsPipelines(
                device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &presentPipeline_);
            vkDestroyShaderModule(device_, fragmentShader, nullptr);
            vkDestroyShaderModule(device_, vertexShader, nullptr);
            Check(result, "vkCreateGraphicsPipelines");
        }

        void AllocateCommandBuffers()
        {
            std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
            VkCommandBufferAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocateInfo.commandPool = commandPool_;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = kFramesInFlight;
            Check(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers.data()),
                "vkAllocateCommandBuffers");
            for (std::size_t index = 0; index < frames_.size(); ++index)
            {
                frames_[index].commandBuffer = commandBuffers[index];
            }
        }

        void CreateSyncObjects()
        {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            for (FrameResources& frame : frames_)
            {
                Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAvailable),
                    "vkCreateSemaphore(image available)");
                Check(vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlight), "vkCreateFence");
            }
            CreatePresentSemaphores();
        }

        void CreatePresentSemaphores()
        {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            presentCompleteSemaphores_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
            for (VkSemaphore& semaphore : presentCompleteSemaphores_)
            {
                Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
                    "vkCreateSemaphore(present complete)");
            }
        }

        template<typename Recorder>
        void ImmediateSubmit(Recorder&& recorder)
        {
            VkCommandBufferAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocateInfo.commandPool = commandPool_;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            Check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
                "vkAllocateCommandBuffers(immediate)");

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(immediate)");
            recorder(commandBuffer);
            Check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(immediate)");

            VkCommandBufferSubmitInfo commandInfo{};
            commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            commandInfo.commandBuffer = commandBuffer;
            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandInfo;
            Check(vkQueueSubmit2(queue_, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit2(immediate)");
            Check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(immediate)");
            vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        }

        static void TransitionImage(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkPipelineStageFlags2 sourceStage,
            VkAccessFlags2 sourceAccess,
            VkPipelineStageFlags2 destinationStage,
            VkAccessFlags2 destinationAccess)
        {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = sourceStage;
            barrier.srcAccessMask = sourceAccess;
            barrier.dstStageMask = destinationStage;
            barrier.dstAccessMask = destinationAccess;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        }

        void UpdateFrameConstants(FrameResources& frame)
        {
            constexpr float degreesToRadians = 0.01745329251994329577f;
            const Vec3 position = camera_.Position();
            const Vec3 forward = camera_.Forward();
            const Vec3 right = camera_.Right();
            const Vec3 up = camera_.Up();
            const float aspect = static_cast<float>(swapchainExtent_.width)
                / static_cast<float>(swapchainExtent_.height);
            const float tanHalfFov = std::tan(camera_.VerticalFovDegrees() * 0.5f * degreesToRadians);

            FrameConstants constants{};
            constants.cameraPositionTanHalfFov = { position.x, position.y, position.z, tanHalfFov };
            constants.cameraForwardAspect = { forward.x, forward.y, forward.z, aspect };
            const float elapsedSeconds = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - applicationStartTime_).count();
            constants.cameraRightTime = { right.x, right.y, right.z, elapsedSeconds };
            constants.cameraUpExposure = { up.x, up.y, up.z, exposure_ };
            constants.imageAndScene = {
                swapchainExtent_.width,
                swapchainExtent_.height,
                static_cast<std::uint32_t>(scene_.spheres.size()),
                static_cast<std::uint32_t>(scene_.planes.size())
            };
            constants.lightAndTrace = {
                static_cast<std::uint32_t>(scene_.lights.size()),
                maximumTraceDepth_,
                static_cast<std::uint32_t>(shadowMethod_),
                0
            };
            constants.samplingAndDebug = {
                accumulationFrame_,
                static_cast<std::uint32_t>(debugView_),
                0,
                0
            };
            std::memcpy(frame.uniformBuffer.mapped, &constants, sizeof(constants));
        }

        void RecordCommandBuffer(FrameResources& frame, std::uint32_t imageIndex)
        {
            Check(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer");
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");

            TransitionImage(
                frame.commandBuffer,
                outputImage_,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
            vkCmdBindDescriptorSets(
                frame.commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                computePipelineLayout_,
                0,
                1,
                &frame.computeDescriptorSet,
                0,
                nullptr);
            vkCmdDispatch(
                frame.commandBuffer,
                (swapchainExtent_.width + 7) / 8,
                (swapchainExtent_.height + 7) / 8,
                1);

            TransitionImage(
                frame.commandBuffer,
                outputImage_,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            TransitionImage(
                frame.commandBuffer,
                swapchainImages_[imageIndex],
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                // The acquire semaphore wait is scoped to color output. Put the
                // discard/layout transition in that same stage so it cannot race
                // the presentation engine's preceding read of this image.
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            const VkClearValue clearColor{ { { 0.0f, 0.0f, 0.0f, 1.0f } } };
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = swapchainImageViews_[imageIndex];
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = clearColor;

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.extent = swapchainExtent_;
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

            const VkViewport viewport{
                0.0f,
                0.0f,
                static_cast<float>(swapchainExtent_.width),
                static_cast<float>(swapchainExtent_.height),
                0.0f,
                1.0f
            };
            const VkRect2D scissor{ { 0, 0 }, swapchainExtent_ };
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline_);
            vkCmdBindDescriptorSets(
                frame.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                presentPipelineLayout_,
                0,
                1,
                &presentDescriptorSet_,
                0,
                nullptr);

            PresentConstants presentConstants{};
            presentConstants.exposure = exposure_;
            presentConstants.applyManualGamma = swapchainIsSrgb_ ? 0u : 1u;
            vkCmdPushConstants(
                frame.commandBuffer,
                presentPipelineLayout_,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(presentConstants),
                &presentConstants);
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(frame.commandBuffer);

            TransitionImage(
                frame.commandBuffer,
                swapchainImages_[imageIndex],
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE);

            Check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
        }

        void DrawFrame()
        {
            FrameResources& frame = frames_[currentFrame_];
            Check(vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences");

            std::uint32_t imageIndex = 0;
            const VkResult acquireResult = vkAcquireNextImageKHR(
                device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
            {
                RecreateSwapchain();
                return;
            }
            if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
            {
                Check(acquireResult, "vkAcquireNextImageKHR");
            }
            const VkSemaphore presentComplete = presentCompleteSemaphores_[imageIndex];

            Check(vkResetFences(device_, 1, &frame.inFlight), "vkResetFences");
            UpdateFrameConstants(frame);
            RecordCommandBuffer(frame, imageIndex);

            VkSemaphoreSubmitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.semaphore = frame.imageAvailable;
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkCommandBufferSubmitInfo commandInfo{};
            commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            commandInfo.commandBuffer = frame.commandBuffer;

            VkSemaphoreSubmitInfo signalInfo{};
            signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            // A presentation operation does not signal the frame fence. Indexing
            // this binary semaphore by the acquired swapchain image proves that
            // presentation released it before it can be signaled again.
            signalInfo.semaphore = presentComplete;
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.waitSemaphoreInfoCount = 1;
            submitInfo.pWaitSemaphoreInfos = &waitInfo;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandInfo;
            submitInfo.signalSemaphoreInfoCount = 1;
            submitInfo.pSignalSemaphoreInfos = &signalInfo;
            Check(vkQueueSubmit2(queue_, 1, &submitInfo, frame.inFlight), "vkQueueSubmit2(frame)");

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &presentComplete;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain_;
            presentInfo.pImageIndices = &imageIndex;
            const VkResult presentResult = vkQueuePresentKHR(queue_, &presentInfo);

            bool recreatedSwapchain = false;
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR
                || presentResult == VK_SUBOPTIMAL_KHR
                || framebufferResized_)
            {
                framebufferResized_ = false;
                RecreateSwapchain();
                recreatedSwapchain = true;
            }
            else if (presentResult != VK_SUCCESS)
            {
                Check(presentResult, "vkQueuePresentKHR");
            }

            currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
            ++totalFrames_;
            // RecreateSwapchain allocates a new, uninitialized accumulation
            // image and resets its sample index to zero. Do not advance that
            // new index for the frame that rendered into the discarded image.
            if (!recreatedSwapchain
                && debugView_ == DebugView::Final
                && accumulationFrame_ < kMaximumAccumulationSamples)
            {
                ++accumulationFrame_;
            }
        }

        void ResetAccumulation()
        {
            accumulationFrame_ = 0;
        }

        void SetShadowMethod(ShadowMethod method)
        {
            if (shadowMethod_ != method)
            {
                shadowMethod_ = method;
                ResetAccumulation();
            }
        }

        void ProcessInput(float deltaSeconds)
        {
            const auto keyDown = [](int virtualKey)
            {
                return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            };

            if (keyDown(VK_ESCAPE))
            {
                shouldClose_ = true;
            }

            const float forward = static_cast<float>(keyDown('W')) - static_cast<float>(keyDown('S'));
            const float right = static_cast<float>(keyDown('D')) - static_cast<float>(keyDown('A'));
            const float vertical = static_cast<float>(keyDown(VK_SPACE)) - static_cast<float>(keyDown(VK_CONTROL));
            const bool sprint = keyDown(VK_SHIFT);
            if (forward != 0.0f || right != 0.0f || vertical != 0.0f)
            {
                camera_.Move(forward, right, vertical, deltaSeconds, sprint);
                ResetAccumulation();
            }

            const bool tabPressed = keyDown(VK_TAB);
            if (tabPressed && !tabWasPressed_)
            {
                SetMouseCapture(!mouseCaptured_);
            }
            tabWasPressed_ = tabPressed;

            if (keyDown('1'))
            {
                SetShadowMethod(ShadowMethod::Pcf);
            }
            else if (keyDown('2'))
            {
                SetShadowMethod(ShadowMethod::Pcss);
            }
            else if (keyDown('3'))
            {
                SetShadowMethod(ShadowMethod::Physical);
            }
        }

        void SetMouseCapture(bool captured)
        {
            mouseCaptured_ = captured;
            if (window_ == nullptr)
            {
                return;
            }

            if (captured)
            {
                SetCapture(window_);
                UpdateCursorClip();
                while (ShowCursor(FALSE) >= 0)
                {
                }
            }
            else
            {
                ReleaseCapture();
                ClipCursor(nullptr);
                while (ShowCursor(TRUE) < 0)
                {
                }
            }
        }

        void UpdateCursorClip() const
        {
            if (!mouseCaptured_ || window_ == nullptr)
            {
                return;
            }

            RECT rectangle{};
            GetClientRect(window_, &rectangle);
            POINT upperLeft{ rectangle.left, rectangle.top };
            POINT lowerRight{ rectangle.right, rectangle.bottom };
            ClientToScreen(window_, &upperLeft);
            ClientToScreen(window_, &lowerRight);
            rectangle = { upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y };
            ClipCursor(&rectangle);
        }

        void PumpMessages()
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
            {
                if (message.message == WM_QUIT)
                {
                    shouldClose_ = true;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        void ResizeClientArea(std::uint32_t width, std::uint32_t height) const
        {
            constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;
            RECT rectangle{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
            if (AdjustWindowRectEx(&rectangle, windowStyle, FALSE, 0) == FALSE)
            {
                return;
            }
            SetWindowPos(
                window_,
                nullptr,
                0,
                0,
                rectangle.right - rectangle.left,
                rectangle.bottom - rectangle.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        void MainLoop(const RunOptions& options)
        {
            using Clock = std::chrono::steady_clock;
            auto previousTime = Clock::now();
            const auto benchmarkStart = previousTime;
            auto titleUpdateTime = previousTime;
            std::uint32_t titleFrameCount = 0;
            const std::uint32_t firstFrame = totalFrames_;

            while (!shouldClose_)
            {
                PumpMessages();
                if (shouldClose_)
                {
                    break;
                }
                const auto now = Clock::now();
                const float deltaSeconds = std::min(
                    std::chrono::duration<float>(now - previousTime).count(), 0.1f);
                previousTime = now;
                ProcessInput(deltaSeconds);
                DrawFrame();
                ++titleFrameCount;

                const std::uint32_t completedFrames = totalFrames_ - firstFrame;
                if (options.resizeTest && completedFrames == 30)
                {
                    ResizeClientArea(960, 540);
                }
                else if (options.resizeTest && completedFrames == 60)
                {
                    ResizeClientArea(kInitialWidth, kInitialHeight);
                }

                const float titleInterval = std::chrono::duration<float>(now - titleUpdateTime).count();
                if (titleInterval >= 0.5f)
                {
                    const float framesPerSecond = static_cast<float>(titleFrameCount) / titleInterval;
                    std::ostringstream title;
                    title << "Vulkan HLSL | " << IntegratorName(integrator_) << " | "
                        << DebugViewName(debugView_) << " | "
                        << ShadowMethodName(shadowMethod_) << " | "
                        << std::fixed << std::setprecision(1)
                        << framesPerSecond << " FPS | " << (1000.0f / std::max(framesPerSecond, 0.001f))
                        << " ms | " << swapchainExtent_.width << 'x' << swapchainExtent_.height;
                    if (debugView_ == DebugView::Final)
                    {
                        title << " | " << accumulationFrame_ << " spp";
                    }
                    const std::string narrowTitle = title.str();
                    const std::wstring wideTitle(narrowTitle.begin(), narrowTitle.end());
                    SetWindowTextW(window_, wideTitle.c_str());
                    titleFrameCount = 0;
                    titleUpdateTime = now;
                }

                if (options.frameLimit > 0 && completedFrames >= options.frameLimit)
                {
                    break;
                }
            }

            Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
            if (options.frameLimit > 0)
            {
                const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
                    Clock::now() - benchmarkStart).count();
                std::cout << "Rendered " << (totalFrames_ - firstFrame) << " frames in "
                    << std::fixed << std::setprecision(2) << elapsedMilliseconds << " ms ("
                    << elapsedMilliseconds / std::max<std::uint32_t>(totalFrames_ - firstFrame, 1u)
                    << " ms/frame including presentation, " << accumulationFrame_ << " spp, "
                    << IntegratorName(integrator_) << ", "
                    << ShadowMethodName(shadowMethod_) << ", debug view "
                    << DebugViewName(debugView_) << ").\n";
            }
        }

        void RecreateSwapchain()
        {
            RECT clientRectangle{};
            GetClientRect(window_, &clientRectangle);
            LONG width = clientRectangle.right - clientRectangle.left;
            LONG height = clientRectangle.bottom - clientRectangle.top;
            while ((width == 0 || height == 0) && !shouldClose_)
            {
                WaitMessage();
                PumpMessages();
                GetClientRect(window_, &clientRectangle);
                width = clientRectangle.right - clientRectangle.left;
                height = clientRectangle.bottom - clientRectangle.top;
            }
            if (shouldClose_)
            {
                return;
            }

            Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(swapchain recreation)");
            CleanupSwapchainResources();
            CreateSwapchain();
            CreatePresentSemaphores();
            CreateOutputImage();
            CreateGraphicsPipeline();
            UpdateAllDescriptors();
            ResetAccumulation();
        }

        void CleanupSwapchainResources()
        {
            for (VkSemaphore semaphore : presentCompleteSemaphores_)
            {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
            presentCompleteSemaphores_.clear();
            if (presentPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device_, presentPipeline_, nullptr);
                presentPipeline_ = VK_NULL_HANDLE;
            }
            if (outputImageView_ != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_, outputImageView_, nullptr);
                outputImageView_ = VK_NULL_HANDLE;
            }
            if (outputImage_ != VK_NULL_HANDLE)
            {
                vkDestroyImage(device_, outputImage_, nullptr);
                outputImage_ = VK_NULL_HANDLE;
            }
            if (outputImageMemory_ != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_, outputImageMemory_, nullptr);
                outputImageMemory_ = VK_NULL_HANDLE;
            }
            for (VkImageView view : swapchainImageViews_)
            {
                vkDestroyImageView(device_, view, nullptr);
            }
            swapchainImageViews_.clear();
            swapchainImages_.clear();
            if (swapchain_ != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device_, swapchain_, nullptr);
                swapchain_ = VK_NULL_HANDLE;
            }
        }

        void Cleanup()
        {
            if (device_ != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(device_);
                for (FrameResources& frame : frames_)
                {
                    if (frame.inFlight != VK_NULL_HANDLE)
                    {
                        vkDestroyFence(device_, frame.inFlight, nullptr);
                    }
                    if (frame.imageAvailable != VK_NULL_HANDLE)
                    {
                        vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
                    }
                    DestroyBuffer(frame.uniformBuffer);
                }

                if (descriptorPool_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
                }
                if (sampler_ != VK_NULL_HANDLE)
                {
                    vkDestroySampler(device_, sampler_, nullptr);
                }
                DestroyBuffer(sceneBuffer_);
                if (computePipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, computePipeline_, nullptr);
                }
                CleanupSwapchainResources();
                if (presentPipelineLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device_, presentPipelineLayout_, nullptr);
                }
                if (computePipelineLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);
                }
                if (presentDescriptorSetLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(device_, presentDescriptorSetLayout_, nullptr);
                }
                if (computeDescriptorSetLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(device_, computeDescriptorSetLayout_, nullptr);
                }
                if (commandPool_ != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(device_, commandPool_, nullptr);
                }
                vkDestroyDevice(device_, nullptr);
                device_ = VK_NULL_HANDLE;
            }

            if (debugMessenger_ != VK_NULL_HANDLE)
            {
                const auto destroyFunction = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
                if (destroyFunction != nullptr)
                {
                    destroyFunction(instance_, debugMessenger_, nullptr);
                }
            }
            if (surface_ != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(instance_, surface_, nullptr);
            }
            if (instance_ != VK_NULL_HANDLE)
            {
                vkDestroyInstance(instance_, nullptr);
            }
            if (window_ != nullptr)
            {
                if (mouseCaptured_)
                {
                    SetMouseCapture(false);
                }
                DestroyWindow(window_);
                window_ = nullptr;
            }
            if (windowClassAtom_ != 0 && windowInstance_ != nullptr)
            {
                UnregisterClassW(windowClassName_, windowInstance_);
                windowClassAtom_ = 0;
            }
        }

        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter)
        {
            Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(longParameter);
                self = static_cast<Impl*>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (self != nullptr)
            {
                return self->HandleWindowMessage(window, message, wordParameter, longParameter);
            }
            return DefWindowProcW(window, message, wordParameter, longParameter);
        }

        LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter)
        {
            switch (message)
            {
            case WM_CLOSE:
                shouldClose_ = true;
                return 0;
            case WM_DESTROY:
                shouldClose_ = true;
                PostQuitMessage(0);
                return 0;
            case WM_SIZE:
                if (initialized_ && wordParameter != SIZE_MINIMIZED)
                {
                    framebufferResized_ = true;
                }
                UpdateCursorClip();
                return 0;
            case WM_INPUT:
                if (mouseCaptured_)
                {
                    RAWINPUT rawInput{};
                    UINT byteCount = sizeof(rawInput);
                    const UINT copiedBytes = GetRawInputData(
                        reinterpret_cast<HRAWINPUT>(longParameter),
                        RID_INPUT,
                        &rawInput,
                        &byteCount,
                        sizeof(RAWINPUTHEADER));
                    if (copiedBytes != UINT_MAX && rawInput.header.dwType == RIM_TYPEMOUSE)
                    {
                        const LONG deltaX = rawInput.data.mouse.lLastX;
                        const LONG deltaY = rawInput.data.mouse.lLastY;
                        if (deltaX != 0 || deltaY != 0)
                        {
                            camera_.Rotate(
                                static_cast<float>(deltaX),
                                static_cast<float>(-deltaY));
                            ResetAccumulation();
                        }
                    }
                }
                return 0;
            case WM_MOUSEWHEEL:
                if (GET_WHEEL_DELTA_WPARAM(wordParameter) != 0)
                {
                    camera_.Zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wordParameter))
                        / static_cast<float>(WHEEL_DELTA));
                    ResetAccumulation();
                }
                return 0;
            case WM_ACTIVATE:
                if (LOWORD(wordParameter) == WA_INACTIVE)
                {
                    ClipCursor(nullptr);
                }
                else
                {
                    UpdateCursorClip();
                }
                return 0;
            case WM_SETCURSOR:
                if (mouseCaptured_ && LOWORD(longParameter) == HTCLIENT)
                {
                    SetCursor(nullptr);
                    return TRUE;
                }
                break;
            default:
                break;
            }
            return DefWindowProcW(window, message, wordParameter, longParameter);
        }

        static constexpr const wchar_t* windowClassName_ = L"RenderingEngineVulkanWindow";
        HWND window_ = nullptr;
        HINSTANCE windowInstance_ = nullptr;
        ATOM windowClassAtom_ = 0;
        bool initialized_ = false;
        bool validationEnabled_ = false;
        bool framebufferResized_ = false;
        bool mouseCaptured_ = true;
        bool tabWasPressed_ = false;
        bool shouldClose_ = false;
        const std::chrono::steady_clock::time_point applicationStartTime_ = std::chrono::steady_clock::now();

        Camera camera_;
        SceneData scene_;
        float exposure_ = 1.0f;
        std::uint32_t maximumTraceDepth_ = 8;
        Integrator integrator_ = Integrator::Whitted;
        ShadowMethod shadowMethod_ = ShadowMethod::Physical;
        DebugView debugView_ = DebugView::Final;
        std::uint32_t accumulationFrame_ = 0;
        std::uint32_t totalFrames_ = 0;
        std::uint32_t currentFrame_ = 0;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties physicalDeviceProperties_{};
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue queue_ = VK_NULL_HANDLE;
        std::uint32_t queueFamilyIndex_ = 0;

        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent_{};
        bool swapchainIsSrgb_ = false;
        std::vector<VkImage> swapchainImages_;
        std::vector<VkImageView> swapchainImageViews_;
        std::vector<VkSemaphore> presentCompleteSemaphores_;

        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        std::array<FrameResources, kFramesInFlight> frames_{};
        VkDescriptorSetLayout computeDescriptorSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout presentDescriptorSetLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout presentPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline computePipeline_ = VK_NULL_HANDLE;
        VkPipeline presentPipeline_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet presentDescriptorSet_ = VK_NULL_HANDLE;

        Buffer sceneBuffer_;
        SceneBufferLayout sceneBufferLayout_{};
        VkImage outputImage_ = VK_NULL_HANDLE;
        VkDeviceMemory outputImageMemory_ = VK_NULL_HANDLE;
        VkImageView outputImageView_ = VK_NULL_HANDLE;
        VkSampler sampler_ = VK_NULL_HANDLE;
    };

    VulkanWhittedRenderer::VulkanWhittedRenderer()
        : impl_(std::make_unique<Impl>())
    {
    }

    VulkanWhittedRenderer::~VulkanWhittedRenderer() = default;
    VulkanWhittedRenderer::VulkanWhittedRenderer(VulkanWhittedRenderer&&) noexcept = default;
    VulkanWhittedRenderer& VulkanWhittedRenderer::operator=(VulkanWhittedRenderer&&) noexcept = default;

    void VulkanWhittedRenderer::Run(const RunOptions& options)
    {
        impl_->Run(options);
    }
}
