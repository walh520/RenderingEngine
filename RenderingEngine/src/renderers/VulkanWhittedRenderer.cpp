#include <vulkan/vulkan.h>

#include "renderers/VulkanWhittedRenderer.hpp"

#include "app/CapabilityTable.hpp"
#include "app/ArtifactLayout.hpp"
#include "app/RuntimeStatusText.hpp"
#include "core/Camera.hpp"
#include "core/ExecutablePath.hpp"
#include "demos/CaptureBundleWriter.hpp"
#include "demos/CapturePreview.hpp"
#include "demos/ShowcaseProgram.hpp"
#include "platform/IPlatformHost.hpp"
#include "platform/glfw/GlfwPlatformHost.hpp"
#include "renderers/Wave2Runtime.hpp"
#include "renderers/Wave5ShowcaseRuntime.hpp"
#include "scene/CanonicalScene.hpp"
#include "scene/ExperimentScenes.hpp"
#include "HardwareRtCapabilities.hpp"
#include "ui/DebugProfilerModel.hpp"
#include "ui/GlfwActionAdapter.hpp"
#include "ui/ImGuiShowcasePanels.hpp"
#include "ui/ShowcaseController.hpp"
#include "ui/Wave2TelemetryAdapter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
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
        constexpr std::uint32_t kFramesInFlight = 2;
        constexpr std::uint32_t kMaximumAccumulationSamples = 4096;
        constexpr VkFormat kHdrFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

#if defined(NDEBUG)
        constexpr bool kRequestValidation = false;
#else
        constexpr bool kRequestValidation = true;
#endif

        [[nodiscard]] constexpr bool ResolveRuntimeToggle(
            RuntimeToggle toggle,
            bool rendererDefault) noexcept
        {
            switch (toggle)
            {
            case RuntimeToggle::Enabled:
                return true;
            case RuntimeToggle::Disabled:
                return false;
            case RuntimeToggle::RendererDefault:
            default:
                return rendererDefault;
            }
        }

        [[nodiscard]] constexpr bool IsRuntimeToggleValid(RuntimeToggle toggle) noexcept
        {
            return toggle == RuntimeToggle::RendererDefault
                || toggle == RuntimeToggle::Enabled
                || toggle == RuntimeToggle::Disabled;
        }

        struct alignas(16) PresentConstants
        {
            float exposure = 1.0f;
            std::uint32_t applyManualGamma = 0;
            float padding[2]{};
        };

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
        };

        struct SwapchainSupport
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        void Check(VkResult result, std::string_view operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(
                    std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)));
            }
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

        void HashBytes(
            std::uint64_t& hash,
            const void* data,
            const std::size_t byteCount) noexcept
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < byteCount; ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
        }

        [[nodiscard]] std::string FormatHash(const std::uint64_t hash)
        {
            std::ostringstream output;
            output << "fnv1a64:" << std::hex << std::setfill('0')
                << std::setw(16) << hash;
            return output.str();
        }

        [[nodiscard]] std::uint64_t ExperimentEnvironmentFingerprint(
            const Scene::ExperimentEnvironment& environment) noexcept
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashBytes(hash, &environment.width, sizeof(environment.width));
            HashBytes(hash, &environment.height, sizeof(environment.height));
            if (!environment.linearRgba.empty())
            {
                HashBytes(
                    hash,
                    environment.linearRgba.data(),
                    environment.linearRgba.size()
                        * sizeof(environment.linearRgba.front()));
            }
            return hash;
        }

        [[nodiscard]] std::string FormatVulkanVersion(const std::uint32_t version)
        {
            std::ostringstream output;
            output << VK_API_VERSION_MAJOR(version) << '.'
                << VK_API_VERSION_MINOR(version) << '.'
                << VK_API_VERSION_PATCH(version);
            return output.str();
        }

        [[nodiscard]] std::string FormatUtcNow()
        {
            const std::time_t now = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());
            std::tm utc{};
#if defined(_WIN32)
            if (gmtime_s(&utc, &now) != 0)
#else
            if (gmtime_r(&now, &utc) == nullptr)
#endif
            {
                throw std::runtime_error("Unable to convert the capture timestamp to UTC.");
            }
            std::ostringstream output;
            output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return output.str();
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
                return "PCF";
            case ShadowMethod::Pcss:
                return "PCSS";
            case ShadowMethod::Physical:
                return "physical area light";
            }
            return "unknown";
        }

        [[nodiscard]] constexpr std::string_view RendererTransportLabel(
            TransportModel transport)
        {
            switch (transport)
            {
            case TransportModel::Pbr:
                return "PBR path transport";
            case TransportModel::Whitted:
                return "Whitted specular transport";
            }
            return "unknown transport model";
        }

        [[nodiscard]] std::string_view RendererDebugViewLabel(DebugView view)
        {
            return DebugViewName(view);
        }

        [[nodiscard]] constexpr bool IsWave2Backend(
            const TraversalBackend backend) noexcept
        {
            return backend == TraversalBackend::CanonicalLinearGpu
                || backend == TraversalBackend::GpuFlattenedSahBvh
                || backend == TraversalBackend::VulkanRayQuery;
        }

        [[nodiscard]] constexpr bool IsBuiltTransport(
            const TransportModel transport) noexcept
        {
            return transport == TransportModel::Pbr
                || transport == TransportModel::Whitted;
        }

        [[nodiscard]] constexpr bool IsInteractiveGpuRuntimeConfig(
            const RuntimeConfig& config) noexcept
        {
            return IsBuiltTransport(config.transportModel)
                && (config.executionArchitecture == ExecutionArchitecture::Staged
                    || config.executionArchitecture == ExecutionArchitecture::Megakernel
                    || config.executionArchitecture == ExecutionArchitecture::Wavefront)
                && IsWave2Backend(config.backend);
        }

        [[nodiscard]] Scene::ExperimentSceneBuildOptions MakeSceneBuildOptions(
            const RuntimeConfig& config) noexcept
        {
            Scene::ExperimentSceneBuildOptions result;
            result.variantStableId = config.sceneVariant;
            result.manyLightsCount = ResolveManyLightsCount(
                config.restir.manyLightsTier);
            result.animateLights = config.restir.animateLights;
            const bool temporalScene =
                config.scene == ScenePreset::TemporalStabilityCorridor;
            result.animateCamera = temporalScene || config.restir.animateLights;
            result.animateRigidOccluders = temporalScene
                || config.restir.animateRigidOccluders;
            return result;
        }

        [[nodiscard]] Demos::ShowcaseProgramModel BuildWave5DebugShowcaseProgram()
        {
            std::vector<Demos::ShowcaseSceneRegistryEntry> sceneRegistry;
            sceneRegistry.reserve(Demos::GetShowcaseSceneCards().size());
            for (const Scene::ExperimentSceneDescriptor& descriptor
                : Scene::ExperimentSceneRegistry())
            {
                const bool built = Scene::IsExperimentSceneBuilt(descriptor.preset);
                const std::string unavailableReason = built
                    ? std::string{}
                    : std::string{
                        "Pinned Intel Sponza asset, CC BY attribution, hash, texture-capable glTF ingestion, and production provider are not present."};
                sceneRegistry.push_back({
                    std::string(descriptor.runtimeToken),
                    std::string(descriptor.displayName),
                    built
                        ? "Canonical experiment scene connected to the shared mixed GPU frame loop."
                        : "Licensed external-asset scene remains fail-closed.",
                    "L2 canonical experiment scene provider",
                    "scene:" + std::string(descriptor.runtimeToken),
                    built ? Demos::Availability::Available
                        : Demos::Availability::Unavailable,
                    unavailableReason,
                    std::string(descriptor.fixedCameraStableId),
                    "camera:" + std::string(descriptor.runtimeToken),
                    built ? Demos::Availability::Available
                        : Demos::Availability::Unavailable,
                    unavailableReason
                });
            }

            constexpr std::array productionAlgorithms{
                std::string_view{"algorithm:whitted"},
                std::string_view{"algorithm:pbr"},
                std::string_view{"algorithm:gpu-flattened-sah"},
                std::string_view{"algorithm:ray-query"},
                std::string_view{"algorithm:megakernel"},
                std::string_view{"algorithm:bsdf-only"},
                std::string_view{"algorithm:nee"},
                std::string_view{"algorithm:mis"},
                std::string_view{"algorithm:ggx-bsdf"},
                std::string_view{"algorithm:environment-importance"},
                std::string_view{"algorithm:uniform-one-light"},
                std::string_view{"algorithm:power-weighted-one-light"}
            };
            std::vector<Demos::AlgorithmCompletionClaim> claims;
            claims.reserve(Demos::GetAlgorithmCatalog().size());
            for (const Demos::AlgorithmDescriptor& algorithm : Demos::GetAlgorithmCatalog())
            {
                const bool connected = std::find(
                    productionAlgorithms.begin(),
                    productionAlgorithms.end(),
                    algorithm.providerToken) != productionAlgorithms.end();
                claims.push_back({
                    algorithm.providerToken,
                    connected
                        ? Demos::AlgorithmCompletionState::Implemented
                        : Demos::AlgorithmCompletionState::Declared,
                    connected
                        ? "Connected to a production renderer path; live runtime evidence is still reported separately."
                        : "Source/contract surface exists, but no production Wave 5 frame-loop provider is connected.",
                    std::nullopt
                });
            }

            constexpr std::array providers{
                Demos::ProviderAvailability{
                    Demos::kRendererReadbackProviderToken,
                    Demos::Availability::Available,
                    {}}
            };
            return Demos::BuildShowcaseProgramModel(
                sceneRegistry,
                providers,
                claims,
                std::span<const Demos::AssetLicenseEntry>{});
        }
    }

    class VulkanWhittedRenderer::Impl final
    {
    public:
        explicit Impl(std::unique_ptr<IPlatformHost> platform)
            : platform_(std::move(platform))
        {
            if (platform_ == nullptr)
            {
                throw std::invalid_argument("VulkanWhittedRenderer requires a platform host.");
            }
        }

        ~Impl()
        {
            Cleanup();
        }

        void Run(const RuntimeConfig& config)
        {
            Run(config, nullptr);
        }

        void Run(const RuntimeConfig& config, const Scene::CanonicalScene* canonicalScene)
        {
            const CapabilityDecision capability = CapabilityTable::Evaluate(config);
            if (!capability.IsSupported())
            {
                throw std::invalid_argument(
                    "VulkanWhittedRenderer received an unsupported RuntimeConfig: "
                    + std::string(capability.reason));
            }
            if (!IsInteractiveGpuRuntimeConfig(config))
            {
                throw std::invalid_argument(
                    "VulkanWhittedRenderer requires an interactive GPU execution architecture; "
                    "CPU reference is owned by the headless application runner.");
            }

            requestedRuntimeConfig_ = config;
            liveConfig_ = config;
            appliedRuntimeConfig_ = config;
            activeExperimentScene_.emplace(Scene::BuildExperimentScene(
                static_cast<Scene::ExperimentScenePreset>(config.scene),
                MakeSceneBuildOptions(config)));
            if (canonicalScene != nullptr)
            {
                // Application composition may provide the canonical payload it
                // already validated. Keep an owned copy so hot scene switches
                // never retain a dangling view into Application stack state.
                activeExperimentScene_->canonical = *canonicalScene;
            }

            const Scene::CanonicalScene& activeCanonical =
                activeExperimentScene_->canonical;
            const Scene::CanonicalSceneValidation validation =
                Scene::ValidateCanonicalScene(activeCanonical);
            if (!validation)
            {
                throw std::invalid_argument(
                    "VulkanWhittedRenderer received an invalid canonical scene: "
                    + validation.reason);
            }
            canonicalSceneStableId_ = activeCanonical.stableId;
            canonicalSceneFingerprint_ = Scene::CanonicalSceneFingerprint(activeCanonical);
            canonicalSceneGeneration_ = activeCanonical.generation;
            std::cout << "[场景就绪] " << ScenePresetName(config.scene)
                << "；canonical=" << canonicalSceneStableId_
                << "；variant=" << activeExperimentScene_->activeVariantStableId
                << "；fingerprint=" << FormatHash(canonicalSceneFingerprint_) << '\n';
            renderStartedAtUtc_ = FormatUtcNow();
            cliCapturePending_ = config.run.captureDirectory.has_value();
            semanticInputEnabled_ = QueryGlfwPlatformStateSource(*platform_) != nullptr;
            Ui::ShowcaseFeatureAvailability availability;
            availability.platformCommands = semanticInputEnabled_;
            availability.sceneCommands = semanticInputEnabled_;
            availability.historyResetConsumer = semanticInputEnabled_;
            availability.captureProvider = semanticInputEnabled_;
            availability.shaderReloadProvider = semanticInputEnabled_;
            showcaseController_.SetFeatureAvailability(availability);
            // Start with an unobstructed render.  F2/F3/F7 and the menu keep
            // every inspection surface one key away without covering the
            // low-SPP image during its first accumulation frames.
            showcaseController_.Panels().algorithm = false;
            showcaseController_.Panels().debug = false;
            showcaseProgram_ = BuildWave5DebugShowcaseProgram();
            showcaseController_.SetSceneResourceGenerations(
                canonicalSceneGeneration_, 1u);
            RunConfigured(config);
        }

    private:
        void RunConfigured(const RuntimeConfig& config)
        {
            if (initialized_)
            {
                throw std::logic_error("VulkanWhittedRenderer::Run may only be called once.");
            }
            if (!(config.render.exposure >= 0.01f && config.render.exposure <= 64.0f))
            {
                throw std::out_of_range("RuntimeConfig::render.exposure must be from 0.01 to 64.");
            }
            if (config.render.maximumBounce < 1 || config.render.maximumBounce > 12)
            {
                throw std::out_of_range("RuntimeConfig::render.maximumBounce must be from 1 to 12.");
            }
            if (config.render.width == 0 || config.render.height == 0)
            {
                throw std::out_of_range("RuntimeConfig render extent must be non-zero.");
            }
            if (config.render.targetSamplesPerPixel > kMaximumAccumulationSamples)
            {
                throw std::out_of_range("RuntimeConfig::render.targetSamplesPerPixel exceeds the accumulation limit.");
            }
            if (!(config.render.verticalFovDegrees >= 25.0f && config.render.verticalFovDegrees <= 80.0f))
            {
                throw std::out_of_range("RuntimeConfig::render.verticalFovDegrees must be from 25 to 80.");
            }
            if (!IsRuntimeToggleValid(config.render.vsync) || !IsRuntimeToggleValid(config.run.validation))
            {
                throw std::invalid_argument("RuntimeConfig contains an invalid runtime toggle.");
            }
            if (!IsBuiltTransport(config.transportModel))
            {
                throw std::invalid_argument("RuntimeConfig::transportModel is invalid.");
            }
            if (config.shadowMethod != ShadowMethod::Pcf
                && config.shadowMethod != ShadowMethod::Pcss
                && config.shadowMethod != ShadowMethod::Physical)
            {
                throw std::invalid_argument("RuntimeConfig::shadowMethod is invalid.");
            }
            if (!CapabilityTable::IsBuilt(config.debugView))
            {
                throw std::invalid_argument("RuntimeConfig::debugView is invalid.");
            }

            exposure_ = config.render.exposure;
            maximumTraceDepth_ = config.render.maximumBounce;
            transportModel_ = config.transportModel;
            shadowMethod_ = config.shadowMethod;
            debugView_ = config.debugView;
            targetSamplesPerPixel_ = config.render.targetSamplesPerPixel;
            baseSeed_ = config.render.baseSeed;
            vsyncMode_ = config.render.vsync;
            validationMode_ = config.run.validation;
            camera_.SetVerticalFovDegrees(config.render.verticalFovDegrees);
            Initialize();
            MainLoop(config);
        }

        void Initialize()
        {
            // Finite CLI captures are reproducible experiments, not camera
            // sessions. Cursor capture/warping must not alter their view.
            mouseCaptured_ = !(liveConfig_.run.captureDirectory.has_value()
                && (liveConfig_.run.frameLimit > 0u || targetSamplesPerPixel_ > 0u));
            platform_->SetCursorCaptured(mouseCaptured_);
            CreateInstance();
            CreateDebugMessenger();
            CreateSurface();
            PickPhysicalDevice();
            CreateLogicalDevice();
            CreateSwapchain();
            CreateCommandPool();
            CreateDescriptorSetLayouts();
            CreatePipelineLayouts();
            CreateOutputImage();
            CreateSampler();
            CreateDescriptorPoolAndSets();
            CreateGraphicsPipeline();
            if (!activeExperimentScene_.has_value())
            {
                throw std::logic_error(
                    "GPU runtime initialization requires an owned experiment scene.");
            }
            wave2Runtime_ = std::make_unique<Renderers::Wave2Runtime>();
            wave2Runtime_->Create({
                physicalDevice_,
                device_,
                queue_,
                commandPool_,
                outputImageView_,
                swapchainExtent_,
                ExecutableDirectory()});
            wave2Runtime_->SetScene(
                activeExperimentScene_->canonical,
                &activeExperimentScene_->environment,
                activeExperimentScene_->environmentEnabled);
            // Startup --fov remains an explicit user override. Later
            // scene selections restore the complete fixed-camera preset.
            ApplyActiveSceneCamera(false);
            showcaseController_.SetSceneResourceGenerations(
                canonicalSceneGeneration_,
                wave2Runtime_->ResourceGeneration());
            std::cout << "[RuntimeConfig v2 已接入] Canonical Linear、Flattened SAH、"
                         "Vulkan Ray Query 与分离的传输/执行架构共用同一 GLFW/Vulkan 帧循环；"
                         "场景、遍历、传输、执行、直接光、选灯、环境采样、阴影和重建均为独立轴。\n";
            if (semanticInputEnabled_)
            {
                InitializeWave5ShowcaseRuntime();
            }
            AllocateCommandBuffers();
            CreateSyncObjects();
            initialized_ = true;
        }

        static void RequireWave5ShowcaseStatus(
            const Renderers::Wave5ShowcaseRuntimeStatus& status,
            const std::string_view operation)
        {
            if (!status)
            {
                throw std::runtime_error(
                    "Wave 5 showcase " + std::string(operation) + " failed: "
                    + status.reason);
            }
        }

        void ClearWave5ShowcaseTextures() noexcept
        {
            if (wave5ShowcaseRuntime_ == nullptr
                || !wave5ShowcaseRuntime_->IsInitialized())
            {
                return;
            }
            const Renderers::Wave5ShowcaseRuntimeStatus status =
                wave5ShowcaseRuntime_->ClearTextures();
            if (!status)
            {
                std::cerr << "[Wave 5 UI] 清理 Debug 纹理失败："
                    << status.reason << '\n';
            }
        }

        void RebuildWave5ShowcaseTextureRegistry()
        {
            if (wave5ShowcaseRuntime_ == nullptr
                || !wave5ShowcaseRuntime_->IsInitialized())
            {
                return;
            }
            RequireWave5ShowcaseStatus(
                wave5ShowcaseRuntime_->ClearTextures(),
                "clear debug texture registry");
            if (wave2Runtime_ == nullptr || outputImageView_ == VK_NULL_HANDLE)
            {
                return;
            }

            const std::string prefix = "wave2://resource/"
                + std::to_string(wave2Runtime_->ResourceGeneration()) + "/signal/";
            RequireWave5ShowcaseStatus(
                wave5ShowcaseRuntime_->AddTexture(
                    prefix + "raw",
                    outputImageView_,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                "register raw radiance texture");

            constexpr std::array signalTokens{
                std::string_view{"camera-emission"},
                std::string_view{"direct-diffuse"},
                std::string_view{"direct-specular"},
                std::string_view{"indirect-diffuse"},
                std::string_view{"indirect-specular"}
            };
            for (std::size_t index = 0u; index < signalTokens.size(); ++index)
            {
                const VkImageView view = wave2Runtime_->SignalImageView(index);
                if (view == VK_NULL_HANDLE)
                {
                    throw std::runtime_error(
                        "Mixed GPU runtime did not publish an expected signal image view.");
                }
                RequireWave5ShowcaseStatus(
                    wave5ShowcaseRuntime_->AddTexture(
                        prefix + std::string(signalTokens[index]),
                        view,
                        VK_IMAGE_LAYOUT_GENERAL),
                    "register mixed-runtime signal texture");
            }
        }

        void InitializeWave5ShowcaseRuntime()
        {
            IGlfwPlatformStateSource* const source =
                QueryGlfwPlatformStateSource(*platform_);
            if (source == nullptr || source->NativeWindowHandle() == nullptr)
            {
                throw std::runtime_error(
                    "Wave 5 Debug UI requires the active GLFW platform window.");
            }
            wave5ShowcaseRuntime_ =
                std::make_unique<Renderers::Wave5ShowcaseRuntime>();
            const std::uint32_t imageCount =
                static_cast<std::uint32_t>(swapchainImages_.size());
            RequireWave5ShowcaseStatus(
                wave5ShowcaseRuntime_->Initialize({
                    source->NativeWindowHandle(),
                    VK_API_VERSION_1_3,
                    instance_,
                    physicalDevice_,
                    device_,
                    queueFamilyIndex_,
                    queue_,
                    swapchainFormat_,
                    VK_SAMPLE_COUNT_1_BIT,
                    std::min(2u, imageCount),
                    imageCount,
                    512u,
                    VK_NULL_HANDLE,
                    nullptr
                }),
                "initialize");
            RebuildWave5ShowcaseTextureRegistry();
            std::cout
                << "[Wave 5 Debug 已接入] ImGui 算法/场景/Debug/Profiler/Capture 面板"
                   "使用同一 ActionQueue；按 Tab 释放鼠标，F1 查看完整键位。\n";
        }

        [[nodiscard]] Ui::ShowcaseRuntimeStatus BuildLiveShowcaseRuntimeStatus() const
        {
            Ui::ShowcaseRuntimeStatus status;
            status.providerId = "wave5.production-composition";
            status.configGeneration = showcaseController_.ConfigGeneration();
            status.sceneGeneration = showcaseController_.SceneGeneration();
            status.resourceGeneration = showcaseController_.ResourceGeneration();
            status.resolution = Ui::ShowcaseRuntimeResolution{
                swapchainExtent_.width,
                swapchainExtent_.height};
            status.seed = baseSeed_;
            status.frameIndex = totalFrames_;
            if (liveConfig_.reconstruction == ReconstructionMode::ProgressiveMean
                && liveConfig_.debugView == DebugView::Final)
                status.progressiveFilmSpp = accumulationFrame_;
            // Built GPU paths dispatch exactly one primary path per pixel;
            // this is neither total traced rays nor retained history length.
            if (totalFrames_ > 0u) status.currentFramePathsPerPixel = 1u;
            status.maximumBounce = maximumTraceDepth_;
            if (latestGpuTraceMilliseconds_.has_value()
                && latestTelemetryConfigGeneration_
                    == showcaseController_.ConfigGeneration()
                && latestTelemetrySceneGeneration_
                    == showcaseController_.SceneGeneration()
                && latestTelemetryResourceGeneration_
                    == showcaseController_.ResourceGeneration())
            {
                status.availability = Ui::TelemetryAvailability::Fresh;
                status.gpuFrameMilliseconds = latestGpuTraceMilliseconds_;
                if (liveConfig_.directLightingEstimator
                        != DirectLightingEstimator::RestirDirectIllumination)
                {
                    status.visibilityRays = latestVisibilityRays_;
                    status.totalTracedRays = latestTotalTracedRays_;
                }
                else
                {
                    status.reservoirCandidates = latestReservoirCandidates_;
                    status.visibilityRays = latestVisibilityRays_;
                    status.totalTracedRays = latestTotalTracedRays_;
                }
            }
            else
            {
                status.availability = Ui::TelemetryAvailability::Pending;
                status.reason = "Waiting for the current GPU runtime timestamp query.";
            }
            return status;
        }

        void BeginWave5ShowcaseFrame()
        {
            if (wave5ShowcaseRuntime_ == nullptr
                || !wave5ShowcaseRuntime_->IsInitialized())
            {
                return;
            }
            showcaseViewModel_ = showcaseController_.BuildViewModel(
                liveConfig_, BuildLiveShowcaseRuntimeStatus());
            Ui::ImGuiShowcaseInputs inputs;
            inputs.showcase = &showcaseViewModel_;
            inputs.program = &showcaseProgram_;
            inputs.debugProfiler = &debugProfilerModel_;
            inputs.videoShots = &showcaseProgram_.finalVideoShots;
            inputs.actionQueue = &actionQueue_;
            inputs.controller = &showcaseController_;
            inputs.fixedAbStartUnavailableReason =
                "The production split-screen A/B renderer provider is not connected.";
            inputs.actionFeedback = showcaseActionFeedback_;
            inputs.actionFeedbackIsError = showcaseActionFeedbackIsError_;
            RequireWave5ShowcaseStatus(
                wave5ShowcaseRuntime_->BeginFrame(
                    showcaseController_.Panels(), showcaseSelection_, inputs),
                "begin frame");
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
            const bool validationRequested = ResolveRuntimeToggle(validationMode_, kRequestValidation);
            validationEnabled_ = validationRequested && ValidationLayerAvailable();
            if (validationRequested && !validationEnabled_)
            {
                if (validationMode_ == RuntimeToggle::Enabled)
                {
                    throw std::runtime_error(
                        "Vulkan validation was explicitly enabled, but VK_LAYER_KHRONOS_validation is unavailable.");
                }
                std::cerr << "[Vulkan] Validation layer is unavailable; continuing without it.\n";
            }

            VkApplicationInfo applicationInfo{};
            applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            applicationInfo.pApplicationName = "Vulkan HLSL Renderer";
            applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
            applicationInfo.pEngineName = "RenderingEngine";
            applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
            applicationInfo.apiVersion = VK_API_VERSION_1_3;

            const std::vector<std::string> platformExtensions =
                platform_->RequiredVulkanInstanceExtensions();
            if (platformExtensions.empty())
            {
                throw std::runtime_error("The platform host did not provide Vulkan instance extensions.");
            }

            std::vector<const char*> extensions;
            extensions.reserve(platformExtensions.size() + 1);
            for (const std::string& extension : platformExtensions)
            {
                if (extension.empty())
                {
                    throw std::runtime_error("The platform host provided an empty Vulkan instance extension.");
                }
                extensions.push_back(extension.c_str());
            }
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
            surface_ = platform_->CreateVulkanSurface(instance_);
            if (surface_ == VK_NULL_HANDLE)
            {
                throw std::runtime_error("The platform host returned a null Vulkan surface.");
            }
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

            const std::optional<std::uint32_t> unifiedQueue =
                FindUnifiedQueueFamily(device);
            if (!unifiedQueue.has_value()
                || !HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                return -1;
            }

            // Wave2Runtime::Create already requires these features for its
            // shared Ray Query/software/linear provider set, regardless of
            // the initial backend selection. This preserves that requirement.
            std::uint32_t queueCount = 0u;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueProperties(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(
                device, &queueCount, queueProperties.data());
            if (*unifiedQueue >= queueProperties.size()
                || queueProperties[*unifiedQueue].timestampValidBits == 0u
                || !Rt::Hardware::QueryHardwareRtCapabilities(device).SupportsRayQuery())
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
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
                | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
                | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
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
                    "No Vulkan 1.3 device satisfies the GPU runtime: graphics + compute + present, timestamps, dynamic rendering, synchronization2, RGBA32F storage images, acceleration structures, buffer device address, and Ray Query.");
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

            VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{};
            VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{};
            VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{};
            std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            bufferDeviceAddress.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
            bufferDeviceAddress.bufferDeviceAddress = VK_TRUE;
            accelerationStructure.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            accelerationStructure.accelerationStructure = VK_TRUE;
            rayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            rayQuery.rayQuery = VK_TRUE;
            vulkan13Features.pNext = &bufferDeviceAddress;
            bufferDeviceAddress.pNext = &accelerationStructure;
            accelerationStructure.pNext = &rayQuery;

            const std::vector<const char*> requiredRtExtensions =
                Rt::Hardware::RequiredHardwareRtDeviceExtensions(
                    false, physicalDeviceProperties_.apiVersion);
            extensions.insert(
                extensions.end(),
                requiredRtExtensions.begin(),
                requiredRtExtensions.end());
            VkDeviceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pNext = &vulkan13Features;
            createInfo.queueCreateInfoCount = 1;
            createInfo.pQueueCreateInfos = &queueInfo;
            createInfo.enabledExtensionCount =
                static_cast<std::uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();

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
            if (vsyncMode_ == RuntimeToggle::Enabled)
            {
                return VK_PRESENT_MODE_FIFO_KHR;
            }
            if (vsyncMode_ == RuntimeToggle::Disabled
                && std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
            {
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
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

            const ClientExtent framebufferExtent = platform_->GetFramebufferExtent();
            return {
                std::clamp(framebufferExtent.width,
                    capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(framebufferExtent.height,
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
            imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
            const std::array<VkDescriptorPoolSize, 2> sizes = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1 }
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
            poolInfo.pPoolSizes = sizes.data();
            Check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_),
                "vkCreateDescriptorPool");

            VkDescriptorSetAllocateInfo presentAllocateInfo{};
            presentAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            presentAllocateInfo.descriptorPool = descriptorPool_;
            presentAllocateInfo.descriptorSetCount = 1;
            presentAllocateInfo.pSetLayouts = &presentDescriptorSetLayout_;
            Check(vkAllocateDescriptorSets(device_, &presentAllocateInfo, &presentDescriptorSet_),
                "vkAllocateDescriptorSets(present)");
            UpdatePresentDescriptor();
        }

        void UpdatePresentDescriptor()
        {
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

        [[nodiscard]] VkPipeline CreateGraphicsPipelineForCurrentSwapchain()
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

            VkPipeline pipeline = VK_NULL_HANDLE;
            const VkResult result = vkCreateGraphicsPipelines(
                device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline);
            vkDestroyShaderModule(device_, fragmentShader, nullptr);
            vkDestroyShaderModule(device_, vertexShader, nullptr);
            Check(result, "vkCreateGraphicsPipelines");
            return pipeline;
        }

        void CreateGraphicsPipeline()
        {
            presentPipeline_ = CreateGraphicsPipelineForCurrentSwapchain();
        }

        [[nodiscard]] bool ReloadShadersTransactional()
        {
            VkPipeline presentCandidate = VK_NULL_HANDLE;
            std::unique_ptr<Renderers::Wave2Runtime> wave2Candidate;
            try
            {
                presentCandidate = CreateGraphicsPipelineForCurrentSwapchain();
                if (!activeExperimentScene_.has_value() || wave2Runtime_ == nullptr)
                {
                    throw std::logic_error(
                        "GPU shader reload requires the active canonical scene.");
                }
                wave2Candidate = std::make_unique<Renderers::Wave2Runtime>();
                wave2Candidate->Create({
                    physicalDevice_,
                    device_,
                    queue_,
                    commandPool_,
                    outputImageView_,
                    swapchainExtent_,
                    ExecutableDirectory()});
                wave2Candidate->SetScene(
                    activeExperimentScene_->canonical,
                    &activeExperimentScene_->environment,
                    activeExperimentScene_->environmentEnabled);
                const std::uint64_t previousGeneration =
                    wave2Runtime_->ResourceGeneration();
                while (wave2Candidate->ResourceGeneration() <= previousGeneration)
                {
                    // Rebinding advances the provider-owned resource identity.
                    wave2Candidate->SetOutput(outputImageView_, swapchainExtent_);
                }

                Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(shader reload commit)");
                ClearWave5ShowcaseTextures();
                const VkPipeline oldPresent =
                    std::exchange(presentPipeline_, presentCandidate);
                presentCandidate = VK_NULL_HANDLE;
                wave2Runtime_.swap(wave2Candidate);
                showcaseController_.SetSceneResourceGenerations(
                    canonicalSceneGeneration_,
                    wave2Runtime_->ResourceGeneration());
                wave2TelemetryAdapter_.Clear();
                latestGpuTraceMilliseconds_.reset();
                if (oldPresent != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, oldPresent, nullptr);
                }
                try
                {
                    RebuildWave5ShowcaseTextureRegistry();
                }
                catch (const std::exception& error)
                {
                    std::cerr << "[Wave 5 UI] Shader reload 已提交，但 Debug 纹理注册失败："
                        << error.what() << '\n';
                }
                return true;
            }
            catch (const std::exception& error)
            {
                if (presentCandidate != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, presentCandidate, nullptr);
                }
                std::cerr << "[Shader Reload] 事务失败：" << error.what()
                    << "；旧 pipeline/runtime 保持有效。\n";
                return false;
            }
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

        [[nodiscard]] std::optional<std::size_t> SelectedWave2SignalAov() const
        {
            if (!showcaseController_.Panels().debug)
            {
                return std::nullopt;
            }
            const std::span<const Ui::DebugResourceView> resources =
                debugProfilerModel_.DebugResources();
            if (showcaseSelection_.selectedDebugResource >= resources.size())
            {
                return std::nullopt;
            }

            constexpr std::array signalIds{
                std::string_view{"wave2.l6.signal.camera-emission"},
                std::string_view{"wave2.l6.signal.direct-diffuse"},
                std::string_view{"wave2.l6.signal.direct-specular"},
                std::string_view{"wave2.l6.signal.indirect-diffuse"},
                std::string_view{"wave2.l6.signal.indirect-specular"}
            };
            const std::string_view selectedId =
                resources[showcaseSelection_.selectedDebugResource]
                    .descriptor.stableId;
            const auto found = std::find(signalIds.begin(), signalIds.end(), selectedId);
            return found == signalIds.end()
                ? std::nullopt
                : std::optional<std::size_t>{
                    static_cast<std::size_t>(std::distance(signalIds.begin(), found))};
        }

        [[nodiscard]] std::optional<std::size_t> SynchronizeWave2SignalSelection()
        {
            const std::optional<std::size_t> selected = SelectedWave2SignalAov();
            if (selected != activeWave2SignalAov_)
            {
                activeWave2SignalAov_ = selected;
                ResetAccumulation();
                latestGpuTraceMilliseconds_.reset();
                std::cout << "[Debug AOV] ";
                if (selected.has_value())
                {
                    std::cout << "已选择单信号生产管线 index=" << *selected
                        << "；Raw 与该 AOV 从同一 sample 0 重新积累。\n";
                }
                else
                {
                    std::cout << "已回到 Final/非辅助 AOV 资源；辅助信号追踪停止。\n";
                }
            }
            return selected;
        }

        void UpdateGpuFrame()
        {
            if (wave2Runtime_ == nullptr || !wave2Runtime_->IsReady())
            {
                throw std::logic_error(
                    "GPU frame requested before its production runtime became ready.");
            }
            UpdateTemporalStabilityExperiment();
            wave2Runtime_->UpdateFrame(
                currentFrame_,
                liveConfig_,
                {
                    camera_.Position(),
                    camera_.Forward(),
                    camera_.Right(),
                    camera_.Up(),
                    camera_.VerticalFovDegrees()
                },
                static_cast<std::uint32_t>(totalFrames_),
                showcaseController_.Panels().profiler,
                SynchronizeWave2SignalSelection());
            const auto position = camera_.Position();
            const auto forward = camera_.Forward();
            const auto right = camera_.Right();
            const auto up = camera_.Up();
            submittedCamera_ = std::array<float, 13>{
                position.x, position.y, position.z, forward.x, forward.y, forward.z,
                right.x, right.y, right.z, up.x, up.y, up.z, camera_.VerticalFovDegrees() };
        }

        void UpdateTemporalStabilityExperiment()
        {
            if (!activeExperimentScene_.has_value()
                || liveConfig_.scene != ScenePreset::TemporalStabilityCorridor)
            {
                return;
            }
            Scene::ExperimentSceneBuildOptions options =
                MakeSceneBuildOptions(liveConfig_);
            options.animationFrameIndex = static_cast<std::uint32_t>(
                animationFrameIndex_ & 0xffffffffull);
            Scene::ExperimentScene sampled =
                Scene::BuildTemporalStabilityCorridorExperimentScene(options);
            sampled.canonical.generation = canonicalSceneGeneration_;
            sampled.canonical.constants.versionFlags.y =
                static_cast<std::uint32_t>(canonicalSceneGeneration_);

            const bool movingPanel =
                sampled.activeVariantStableId == "motion-corridor"
                || sampled.activeVariantStableId == "object-only"
                || sampled.activeVariantStableId == "disocclusion-focus";
            if (movingPanel)
            {
                wave2Runtime_->UpdateRigidTransforms(sampled.canonical);
            }
            activeExperimentScene_ = std::move(sampled);

            const bool drivenCamera =
                activeExperimentScene_->activeVariantStableId == "camera-only"
                || liveConfig_.sceneVariant == "camera-cut";
            if (drivenCamera)
            {
                const Scene::CanonicalScene& canonical =
                    activeExperimentScene_->canonical;
                const auto selected = std::find_if(
                    canonical.cameras.begin(), canonical.cameras.end(),
                    [this](const Scene::CameraPreset& candidate)
                    {
                        return candidate.stableId
                            == activeExperimentScene_->activeCameraStableId;
                    });
                if (selected == canonical.cameras.end())
                {
                    throw std::logic_error(
                        "Temporal experiment sampled camera is missing.");
                }
                camera_.SetLookAt(
                    {selected->eye.x, selected->eye.y, selected->eye.z},
                    {selected->target.x, selected->target.y, selected->target.z},
                    selected->verticalFovDegrees);
                ResetProgressiveFilm();
            }
            if (activeExperimentScene_->cameraCut)
            {
                ResetAccumulation();
                std::cout << "[时序场景] frame=60 相机切换；Film/Temporal/Reservoir 历史已强制失效。\n";
            }
            if (!animationPaused_)
            {
                ++animationFrameIndex_;
            }
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

            wave2Runtime_->RecordFrame(
                frame.commandBuffer, currentFrame_, liveConfig_.backend);

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
            if (wave5ShowcaseRuntime_ != nullptr
                && wave5ShowcaseRuntime_->IsInitialized())
            {
                RequireWave5ShowcaseStatus(
                    wave5ShowcaseRuntime_->RenderDrawData(frame.commandBuffer),
                    "record draw data");
            }
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

        void PublishCompletedWave2Telemetry(const std::uint32_t frameSlot)
        {
            if (wave2Runtime_ == nullptr || !IsInteractiveGpuRuntimeConfig(liveConfig_))
            {
                return;
            }
            const Renderers::Wave2RuntimeTelemetry resolved =
                wave2Runtime_->CollectCompletedFrame(frameSlot);
            if (!resolved.available)
            {
                return;
            }
            if (resolved.sceneGeneration != showcaseController_.SceneGeneration()
                || resolved.resourceGeneration
                    != showcaseController_.ResourceGeneration())
            {
                std::cerr << "[统计丢弃] GPU readback 的场景/资源代次已过期。\n";
                return;
            }
            if (resolved.wavefrontFatalMask != 0u
                && resolved.wavefrontFatalMask != lastWavefrontFatalMask_)
            {
                std::cerr << "[Wavefront GPU failure] fatal mask=0x"
                    << std::hex << resolved.wavefrontFatalMask << std::dec
                    << "; output is an error sentinel, not a rendered image.\n";
            }
            lastWavefrontFatalMask_ = resolved.wavefrontFatalMask;
            if (resolved.restirStatisticsAvailable)
            {
                latestReservoirCandidates_ =
                    resolved.restirGeneratedCandidates;
                latestVisibilityRays_ =
                    resolved.restirSubmittedVisibilityRays;
            }

            Ui::Wave2TelemetryFrame frame{};
            frame.provenance = Ui::TelemetryProvenance::LiveRuntime;
            frame.generation = {
                resolved.frameGeneration,
                showcaseController_.ConfigGeneration(),
                showcaseController_.SceneGeneration(),
                showcaseController_.ResourceGeneration()
            };

            const auto counterValue = [&resolved](const Ui::Wave2L6Counter counter)
            {
                return static_cast<std::uint64_t>(resolved.megakernelCounters[
                    static_cast<std::size_t>(counter)]);
            };
            const auto freshCounter = [](const std::uint64_t value,
                const std::string_view source)
            {
                Ui::Wave2CounterObservation observation;
                observation.availability = Ui::TelemetryAvailability::Fresh;
                observation.value = value;
                observation.source = source;
                return observation;
            };
            const auto freshDuration = [](const double milliseconds,
                const std::string_view source)
            {
                Ui::Wave2DurationObservation observation;
                observation.availability = Ui::TelemetryAvailability::Fresh;
                observation.milliseconds = milliseconds;
                observation.source = source;
                return observation;
            };

            frame.megakernel.availability = Ui::TelemetryAvailability::Fresh;
            for (std::size_t index = 0u;
                index < Ui::kWave2L6CounterCount; ++index)
            {
                if (resolved.megakernelCountersAvailable)
                {
                    frame.megakernel.counters[index] = freshCounter(
                        resolved.megakernelCounters[index],
                        "L6 production counter readback");
                }
                else
                {
                    frame.megakernel.counters[index].reason =
                        "F3 Profiler 面板关闭；L6 原子计数已停用以避免展示帧的分析开销。";
                }
            }
            frame.megakernel.traceMilliseconds = freshDuration(
                resolved.traceMilliseconds,
                "L6 production timestamp query");

            struct SignalDescription final
            {
                std::string_view stableId;
                std::string_view label;
                std::string_view legend;
            };
            constexpr std::array signalDescriptions{
                SignalDescription{"raw", "L6 原始辐射", "线性 RGB 路径追踪结果"},
                SignalDescription{"camera-emission", "L6 相机自发光", "相机直接命中的自发光"},
                SignalDescription{"direct-diffuse", "L6 直接漫反射", "首跳直接光漫反射分量"},
                SignalDescription{"direct-specular", "L6 直接镜面", "首跳直接光镜面分量"},
                SignalDescription{"indirect-diffuse", "L6 间接漫反射", "多跳间接漫反射分量"},
                SignalDescription{"indirect-specular", "L6 间接镜面", "多跳间接镜面分量"}
            };
            frame.megakernel.debugResources.reserve(signalDescriptions.size());
            for (std::size_t signalIndex = 0u;
                signalIndex < signalDescriptions.size(); ++signalIndex)
            {
                const SignalDescription& signal = signalDescriptions[signalIndex];
                Ui::DebugResourceObservation observation;
                observation.descriptor.stableId =
                    "wave2.l6.signal." + std::string(signal.stableId);
                observation.descriptor.label = signal.label;
                observation.descriptor.opaqueUiToken =
                    "wave2://resource/" + std::to_string(resolved.resourceGeneration)
                    + "/signal/" + std::string(signal.stableId);
                observation.descriptor.format = "RGBA32F";
                observation.descriptor.extent = {
                    swapchainExtent_.width, swapchainExtent_.height, 1u};
                observation.descriptor.legend = signal.legend;
                observation.descriptor.owner = "L6 Megakernel production runtime";
                if (signalIndex == 0u)
                {
                    observation.availability = Ui::TelemetryAvailability::Fresh;
                }
                else if (resolved.signalAovIndex.has_value()
                    && *resolved.signalAovIndex == signalIndex - 1u)
                {
                    observation.availability = Ui::TelemetryAvailability::Fresh;
                }
                else
                {
                    observation.reason =
                        "选择该 Debug AOV 后才会与 Raw 同步提交；未选中信号不会后台重复追踪。";
                }
                frame.megakernel.debugResources.push_back(std::move(observation));
            }

            // PathRays already contains the depth-zero camera segment.  Adding
            // CameraRays here would double-count every primary traversal.
            const std::uint64_t tracedRays =
                counterValue(Ui::Wave2L6Counter::PathRays)
                + counterValue(Ui::Wave2L6Counter::ShadowRays);
            const std::uint64_t surfaceHits =
                counterValue(Ui::Wave2L6Counter::SurfaceHits);
            if (resolved.backend == TraversalBackend::GpuFlattenedSahBvh)
            {
                frame.flattenedSoftwareGpu.availability =
                    Ui::TelemetryAvailability::Fresh;
                if (resolved.megakernelCountersAvailable)
                {
                    frame.flattenedSoftwareGpu.counters.rays = freshCounter(
                        tracedRays, "L6 rays routed through L4 flattened SAH");
                    frame.flattenedSoftwareGpu.counters.hits = freshCounter(
                        surfaceHits, "L6 surface hits returned by L4 flattened SAH");
                }
                else
                {
                    frame.flattenedSoftwareGpu.counters.rays.reason =
                        "F3 Profiler 面板关闭；L6 路径计数未采集。";
                    frame.flattenedSoftwareGpu.counters.hits.reason =
                        "F3 Profiler 面板关闭；L6 命中计数未采集。";
                }
                frame.flattenedSoftwareGpu.counters.maximumStackDepth.reason =
                    "生产 Megakernel 使用 parent-pointer 遍历，不存在私有栈深度。";
                if (resolved.softwareTraversalCountersAvailable)
                {
                    frame.flattenedSoftwareGpu.counters.nodeTests = freshCounter(
                        resolved.softwareTraversalCounters[0u],
                        "L4 parent-pointer traversal counter");
                    frame.flattenedSoftwareGpu.counters.triangleTests = freshCounter(
                        resolved.softwareTraversalCounters[1u],
                        "L4 triangle intersection counter");
                    frame.flattenedSoftwareGpu.counters.stackOverflows = freshCounter(
                        resolved.softwareTraversalCounters[2u],
                        "L4 parent-pointer traversal has no private stack");
                    frame.flattenedSoftwareGpu.counters.invalidRays = freshCounter(
                        resolved.softwareTraversalCounters[3u],
                        "L4 traversal validation counter");
                    frame.flattenedSoftwareGpu.counters.invalidHits = freshCounter(
                        resolved.softwareTraversalCounters[4u],
                        "L4 traversal structural-validation counter");
                    frame.flattenedSoftwareGpu.counters.leafVisits = freshCounter(
                        resolved.softwareTraversalCounters[6u],
                        "L4 leaf visit counter");
                    frame.flattenedSoftwareGpu.counters.accumulatedLeafPrimitives =
                        freshCounter(
                            resolved.softwareTraversalCounters[7u],
                            "L4 accumulated leaf primitive counter");
                    frame.flattenedSoftwareGpu.counters.maximumLeafOccupancy =
                        freshCounter(
                        resolved.softwareTraversalCounters[8u],
                        "L4 maximum leaf occupancy counter");
                }
                else
                {
                    constexpr std::string_view disabledReason =
                        "F3 Profiler 面板关闭；精确 L4 遍历计数已停用以避免展示帧的分析开销。";
                    frame.flattenedSoftwareGpu.counters.nodeTests.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.triangleTests.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.stackOverflows.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.invalidRays.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.invalidHits.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.leafVisits.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.accumulatedLeafPrimitives.reason = disabledReason;
                    frame.flattenedSoftwareGpu.counters.maximumLeafOccupancy.reason = disabledReason;
                }
                frame.flattenedSoftwareGpu.buildMilliseconds.reason =
                    "Flattened SAH build is currently measured on the CPU host; it is not published as a GPU timestamp.";
                frame.flattenedSoftwareGpu.traceMilliseconds = freshDuration(
                    resolved.traceMilliseconds,
                    "L4 traversal inside the production L6 dispatch");
                frame.hardwareRayQuery.reason =
                    "本帧选择的是 GPU 展平 SAH；Ray Query 资源保持就绪但未执行。";
            }
            else
            {
                frame.hardwareRayQuery.availability =
                    Ui::TelemetryAvailability::Fresh;
                if (resolved.megakernelCountersAvailable)
                {
                    frame.hardwareRayQuery.counters.rays = freshCounter(
                        tracedRays, "L6 rays routed through L5 Ray Query");
                    frame.hardwareRayQuery.counters.hits = freshCounter(
                        surfaceHits, "L6 surface hits returned by L5 Ray Query");
                }
                else
                {
                    frame.hardwareRayQuery.counters.rays.reason =
                        "F3 Profiler 面板关闭；L6 路径计数未采集。";
                    frame.hardwareRayQuery.counters.hits.reason =
                        "F3 Profiler 面板关闭；L6 命中计数未采集。";
                }
                frame.hardwareRayQuery.accelerationStructureBuildMilliseconds =
                    freshDuration(
                        resolved.accelerationStructureBuildMilliseconds,
                        "L5 production acceleration-structure timestamp query");
                frame.hardwareRayQuery.traceMilliseconds = freshDuration(
                    resolved.traceMilliseconds,
                    "L5 Ray Query inside the production L6 dispatch");
                frame.flattenedSoftwareGpu.reason =
                    "本帧选择的是 Vulkan Ray Query；展平 SAH 资源保持就绪但未执行。";
            }

            const Ui::Wave2PublishResult publish =
                wave2TelemetryAdapter_.Publish(std::move(frame));
            if (!publish.Accepted())
            {
                std::cerr << "[统计拒绝] " << publish.reason << '\n';
                return;
            }
            const Ui::TelemetryUpdateResult refresh = debugProfilerModel_.Refresh(
                wave2TelemetryAdapter_,
                {
                    resolved.frameGeneration,
                    showcaseController_.ConfigGeneration(),
                    0u,
                    showcaseController_.SceneGeneration(),
                    showcaseController_.ResourceGeneration()
                });
            if (!refresh.Accepted())
            {
                std::cerr << "[Profiler 拒绝] " << refresh.reason << '\n';
            }
            else
            {
                latestGpuTraceMilliseconds_ = resolved.traceMilliseconds;
                const bool restir = liveConfig_.directLightingEstimator
                    == DirectLightingEstimator::RestirDirectIllumination;
                if (!restir)
                {
                    latestTotalTracedRays_.reset();
                    latestVisibilityRays_.reset();
                    latestReservoirCandidates_.reset();
                }
                if (resolved.megakernelCountersAvailable)
                {
                    if (restir && latestVisibilityRays_.has_value())
                    {
                        latestTotalTracedRays_ = counterValue(
                            Ui::Wave2L6Counter::PathRays) + *latestVisibilityRays_;
                    }
                    else if (!restir)
                    {
                        latestTotalTracedRays_ = tracedRays;
                        latestVisibilityRays_ = counterValue(
                            Ui::Wave2L6Counter::ShadowRays);
                    }
                }
                latestTelemetryConfigGeneration_ =
                    showcaseController_.ConfigGeneration();
                latestTelemetrySceneGeneration_ =
                    showcaseController_.SceneGeneration();
                latestTelemetryResourceGeneration_ =
                    showcaseController_.ResourceGeneration();
            }

            if ((resolved.frameGeneration % 120u) == 0u)
            {
                std::cout << "[运行统计] 后端="
                    << TraversalBackendName(resolved.backend)
                    << "；GPU=" << std::fixed << std::setprecision(3)
                    << resolved.traceMilliseconds << " ms；光线=" << tracedRays
                    << "；表面命中=" << surfaceHits
                    << "；无效帧="
                    << counterValue(Ui::Wave2L6Counter::InvalidFrame) << '\n';
            }
        }

        [[nodiscard]] bool DrawFrame()
        {
            FrameResources& frame = frames_[currentFrame_];
            Check(vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            PublishCompletedWave2Telemetry(currentFrame_);

            std::uint32_t imageIndex = 0;
            const VkResult acquireResult = vkAcquireNextImageKHR(
                device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
            {
                if (wave5ShowcaseRuntime_ != nullptr)
                {
                    wave5ShowcaseRuntime_->CancelFrame();
                }
                RecreateSwapchain();
                return false;
            }
            if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
            {
                Check(acquireResult, "vkAcquireNextImageKHR");
            }
            const VkSemaphore presentComplete = presentCompleteSemaphores_[imageIndex];

            Check(vkResetFences(device_, 1, &frame.inFlight), "vkResetFences");
            UpdateGpuFrame();
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
                && liveConfig_.reconstruction == ReconstructionMode::ProgressiveMean
                && accumulationFrame_ < kMaximumAccumulationSamples)
            {
                lastRenderedSampleIndex_ = totalFrames_ - 1u;
                ++accumulationFrame_;
            }
            else if (!recreatedSwapchain)
            {
                // Non-final views do not accumulate, but they still render the
                // current sample coordinate (normally zero) into the capture image.
                lastRenderedSampleIndex_ = totalFrames_ - 1u;
            }
            return !recreatedSwapchain;
        }

        void ResetAccumulation()
        {
            accumulationFrame_ = 0;
            lastWavefrontFatalMask_ = 0u;
            if (wave2Runtime_ != nullptr)
            {
                wave2Runtime_->InvalidateWave3Histories();
            }
        }

        void ResetProgressiveFilm()
        {
            accumulationFrame_ = 0u;
            if (wave2Runtime_ != nullptr)
            {
                wave2Runtime_->InvalidateProgressiveFilm();
            }
        }

        void ApplyActiveSceneCamera(const bool restorePresetFov)
        {
            if (!activeExperimentScene_.has_value())
            {
                return;
            }
            const Scene::CanonicalScene& canonical =
                activeExperimentScene_->canonical;
            const auto camera = std::find_if(
                canonical.cameras.begin(),
                canonical.cameras.end(),
                [this](const Scene::CameraPreset& candidate)
                {
                    return candidate.stableId
                        == activeExperimentScene_->activeCameraStableId;
                });
            const Scene::CameraPreset* selected = camera != canonical.cameras.end()
                ? &*camera
                : canonical.cameras.empty() ? nullptr : &canonical.cameras.front();
            if (selected == nullptr)
            {
                throw std::logic_error(
                    "Active experiment scene has no fixed camera preset.");
            }
            const float verticalFovDegrees = restorePresetFov
                ? selected->verticalFovDegrees
                : liveConfig_.render.verticalFovDegrees;
            if (restorePresetFov)
            {
                // The fixed camera is part of the scene selection, including
                // its lens. Keep RuntimeConfig aligned with the camera that
                // is actually rendered and captured.
                liveConfig_.render.verticalFovDegrees = verticalFovDegrees;
            }
            camera_.SetLookAt(
                {selected->eye.x, selected->eye.y, selected->eye.z},
                {selected->target.x, selected->target.y, selected->target.z},
                verticalFovDegrees);
        }

        void DiscardCompletedWave2Telemetry()
        {
            if (wave2Runtime_ == nullptr)
            {
                return;
            }
            for (std::uint32_t slot = 0u; slot < kFramesInFlight; ++slot)
            {
                static_cast<void>(wave2Runtime_->CollectCompletedFrame(slot));
            }
            wave2TelemetryAdapter_.Clear();
            latestGpuTraceMilliseconds_.reset();
            latestTotalTracedRays_.reset();
            latestVisibilityRays_.reset();
            latestReservoirCandidates_.reset();
        }

        void SetShadowMethod(ShadowMethod method)
        {
            if (shadowMethod_ != method)
            {
                shadowMethod_ = method;
                ResetAccumulation();
            }
        }

        void SynchronizeLiveRuntimeConfig()
        {
            const std::uint64_t incomingConfigGeneration =
                showcaseController_.ConfigGeneration();
            if (incomingConfigGeneration != appliedConfigGeneration_)
            {
                Check(vkDeviceWaitIdle(device_),
                    "vkDeviceWaitIdle(Wave 2 runtime config switch)");
                DiscardCompletedWave2Telemetry();
                debugProfilerModel_.InvalidateBeforeConfigGeneration(
                    incomingConfigGeneration);

                const bool sceneIdentityChanged =
                    liveConfig_.scene != appliedRuntimeConfig_.scene;
                const bool scenePayloadChanged =
                    sceneIdentityChanged
                    || liveConfig_.sceneVariant != appliedRuntimeConfig_.sceneVariant
                    || (liveConfig_.scene == ScenePreset::ManyLightsRestirArena
                        && (liveConfig_.restir.manyLightsTier
                                != appliedRuntimeConfig_.restir.manyLightsTier
                            || liveConfig_.restir.animateLights
                                != appliedRuntimeConfig_.restir.animateLights
                            || liveConfig_.restir.animateRigidOccluders
                                != appliedRuntimeConfig_.restir.animateRigidOccluders));
                if (scenePayloadChanged)
                {
                    if (comparisonLocked_)
                    {
                        comparisonLocked_ = false;
                        std::cout << "[A/B 锁定] 场景/变体变化使原固定锚点失效，已解除。\n";
                    }
                    Scene::ExperimentScene candidate = Scene::BuildExperimentScene(
                        static_cast<Scene::ExperimentScenePreset>(liveConfig_.scene),
                        MakeSceneBuildOptions(liveConfig_));
                    candidate.canonical.generation = canonicalSceneGeneration_ + 1u;
                    candidate.canonical.constants.versionFlags.y =
                        candidate.canonical.generation;
                    const Scene::CanonicalSceneValidation validation =
                        Scene::ValidateCanonicalScene(candidate.canonical);
                    if (!validation)
                    {
                        throw std::runtime_error(
                            "Runtime scene switch produced an invalid canonical scene: "
                            + validation.reason);
                    }
                    if (wave2Runtime_ == nullptr)
                    {
                        throw std::logic_error(
                            "Runtime scene switching requires the mixed GPU scene owner.");
                    }
                    wave2Runtime_->SetScene(
                        candidate.canonical,
                        &candidate.environment,
                        candidate.environmentEnabled);
                    activeExperimentScene_ = std::move(candidate);
                    animationFrameIndex_ = 0u;
                    canonicalSceneStableId_ = activeExperimentScene_->canonical.stableId;
                    canonicalSceneFingerprint_ = Scene::CanonicalSceneFingerprint(
                        activeExperimentScene_->canonical);
                    canonicalSceneGeneration_ =
                        activeExperimentScene_->canonical.generation;
                    // Rebuilding a variant payload inside the same scene (for
                    // example F11 restoring the Many-Lights tier/animation
                    // settings) must not teleport the user. Home remains the
                    // only explicit current-scene camera/FOV restore command.
                    if (sceneIdentityChanged)
                    {
                        ApplyActiveSceneCamera(true);
                    }
                    showcaseController_.SetSceneResourceGenerations(
                        canonicalSceneGeneration_,
                        wave2Runtime_->ResourceGeneration());
                    RebuildWave5ShowcaseTextureRegistry();
                    std::cout << "[关卡切换完成] "
                        << ScenePresetName(liveConfig_.scene)
                        << "；canonical=" << canonicalSceneStableId_
                        << "；variant="
                        << activeExperimentScene_->activeVariantStableId
                        << "；generation=" << canonicalSceneGeneration_
                        << "；资源已同步重建。\n";
                }

                const bool backendChanged =
                    liveConfig_.backend != appliedRuntimeConfig_.backend;
                if (backendChanged && !scenePayloadChanged
                    && IsInteractiveGpuRuntimeConfig(liveConfig_))
                {
                    if (!activeExperimentScene_.has_value()
                        || wave2Runtime_ == nullptr)
                    {
                        throw std::logic_error(
                            "Backend switch requires the mixed GPU scene/AS owner.");
                    }
                    wave2Runtime_->SetScene(
                        activeExperimentScene_->canonical,
                        &activeExperimentScene_->environment,
                        activeExperimentScene_->environmentEnabled);
                    showcaseController_.SetSceneResourceGenerations(
                        canonicalSceneGeneration_,
                        wave2Runtime_->ResourceGeneration());
                    RebuildWave5ShowcaseTextureRegistry();
                    std::cout << "[Backend 切换] 已重建并重新绑定对应的遍历 AS；resource generation="
                        << wave2Runtime_->ResourceGeneration() << "。\n";
                }

                if (liveConfig_.transportModel != transportModel_)
                {
                    if (IsInteractiveGpuRuntimeConfig(liveConfig_))
                    {
                        transportModel_ = liveConfig_.transportModel;
                    }
                    else
                    {
                        throw std::logic_error(
                            "CapabilityTable admitted a transport model that the mixed GPU runtime does not own.");
                    }
                }

                appliedRuntimeConfig_ = liveConfig_;
                appliedConfigGeneration_ = incomingConfigGeneration;
                std::cout << "[当前配置] "
                    << FormatRuntimeConfigStatus(liveConfig_) << '\n';
            }
            exposure_ = liveConfig_.render.exposure;
            maximumTraceDepth_ = liveConfig_.render.maximumBounce;
            targetSamplesPerPixel_ = liveConfig_.render.targetSamplesPerPixel;
            baseSeed_ = liveConfig_.render.baseSeed;
            debugView_ = liveConfig_.debugView;
            SetShadowMethod(liveConfig_.shadowMethod);
            camera_.SetVerticalFovDegrees(liveConfig_.render.verticalFovDegrees);
            if (comparisonLocked_)
            {
                camera_ = comparisonLockedCamera_;
                baseSeed_ = comparisonLockedBaseSeed_;
                liveConfig_.render.baseSeed = comparisonLockedBaseSeed_;
                animationOriginTick_ = comparisonLockedAnimationOriginTick_;
            }
        }

        void SetPointerCaptured(const bool captured)
        {
            if (mouseCaptured_ == captured)
            {
                return;
            }
            mouseCaptured_ = captured;
            platform_->SetCursorCaptured(captured);
        }

        [[nodiscard]] Demos::CaptureNamedHash ShaderHash(
            const std::string& name,
            const std::filesystem::path& path) const
        {
            const std::vector<std::uint32_t> words = ReadSpirv(path);
            std::uint64_t hash = 14695981039346656037ull;
            HashBytes(hash, words.data(), words.size() * sizeof(words.front()));
            return { name, FormatHash(hash) };
        }

        void ReadbackOutputImage(std::vector<float>& linearRgba)
        {
            const bool captureSignal = IsInteractiveGpuRuntimeConfig(liveConfig_)
                && activeWave2SignalAov_.has_value()
                && wave2Runtime_ != nullptr;
            const VkImage captureImage = captureSignal
                ? wave2Runtime_->SignalImage(*activeWave2SignalAov_)
                : outputImage_;
            if (captureImage == VK_NULL_HANDLE)
            {
                throw std::logic_error(
                    "Capture source image is unavailable for the selected debug resource.");
            }
            const VkImageLayout captureLayout = captureSignal
                ? VK_IMAGE_LAYOUT_GENERAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            const VkPipelineStageFlags2 captureSourceStage = captureSignal
                ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            const VkAccessFlags2 captureSourceAccess = captureSignal
                ? VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                    | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                    | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            const std::size_t pixelCount = static_cast<std::size_t>(swapchainExtent_.width)
                * static_cast<std::size_t>(swapchainExtent_.height);
            if (pixelCount > std::numeric_limits<std::size_t>::max() / 4u)
            {
                throw std::overflow_error("Capture RGBA component count overflows size_t.");
            }
            linearRgba.resize(pixelCount * 4u);

            Buffer readback;
            CreateBuffer(
                static_cast<VkDeviceSize>(linearRgba.size() * sizeof(float)),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                readback);
            try
            {
                ImmediateSubmit([&](VkCommandBuffer commandBuffer)
                {
                    TransitionImage(
                        commandBuffer,
                        captureImage,
                        captureLayout,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        captureSourceStage,
                        captureSourceAccess,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT);

                    VkBufferImageCopy copy{};
                    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copy.imageSubresource.layerCount = 1u;
                    copy.imageExtent = {
                        swapchainExtent_.width,
                        swapchainExtent_.height,
                        1u
                    };
                    vkCmdCopyImageToBuffer(
                        commandBuffer,
                        captureImage,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        readback.handle,
                        1u,
                        &copy);

                    TransitionImage(
                        commandBuffer,
                        captureImage,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        captureLayout,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT,
                        captureSourceStage,
                        captureSourceAccess);
                });

                Check(
                    vkMapMemory(
                        device_,
                        readback.memory,
                        0,
                        readback.size,
                        0,
                        &readback.mapped),
                    "vkMapMemory(capture readback)");
                std::memcpy(
                    linearRgba.data(),
                    readback.mapped,
                    linearRgba.size() * sizeof(float));
                if (std::any_of(
                        linearRgba.begin(),
                        linearRgba.end(),
                        [](const float value) { return !std::isfinite(value); }))
                {
                    throw std::runtime_error(
                        "Capture readback contains non-finite RGBA components.");
                }
            }
            catch (...)
            {
                DestroyBuffer(readback);
                throw;
            }
            DestroyBuffer(readback);
        }

        [[nodiscard]] Demos::CaptureMetadata BuildCaptureMetadata(
            const ArtifactLayout& layout,
            const RuntimeConfig& requestedConfig,
            const RuntimeConfig& effectiveConfig,
            const std::uint64_t configGeneration,
            const std::uint64_t sceneGeneration,
            const std::uint64_t resourceGeneration) const
        {
            Demos::CaptureMetadata metadata;
            metadata.schemaVersion = "artifact-layout-v0";
            metadata.contractVersions = {
                { "scene-frame-abi", "abi-v0-numeric-1" },
                { "gpu-traversal-abi", "abi-v1-numeric-2" },
                { "runtime-config", "runtime-config-v2-app-1" },
                { "artifact-layout", "artifact-layout-v0" }
            };
            constexpr std::array wave2SignalTokens{
                std::string_view{"camera-emission"},
                std::string_view{"direct-diffuse"},
                std::string_view{"direct-specular"},
                std::string_view{"indirect-diffuse"},
                std::string_view{"indirect-specular"}
            };
            metadata.evidenceIdentity.providerId =
                activeWave2SignalAov_.has_value()
                    && *activeWave2SignalAov_ < wave2SignalTokens.size()
                ? "capture:renderer-readback:wave2-signal-"
                    + std::string(wave2SignalTokens[*activeWave2SignalAov_])
                : "capture:renderer-readback:raw";
            metadata.evidenceIdentity.origin = Demos::CaptureEvidenceOrigin::LiveRuntime;
            metadata.evidenceIdentity.availability = Demos::CaptureEvidenceAvailability::Fresh;
            metadata.evidenceIdentity.frameIndex = totalFrames_;
            metadata.evidenceIdentity.sampleIndex = lastRenderedSampleIndex_;
            metadata.evidenceIdentity.configGeneration = configGeneration;
            metadata.evidenceIdentity.sceneGeneration = sceneGeneration;
            metadata.evidenceIdentity.resourceGeneration = resourceGeneration;
            metadata.gitCommit = "unavailable:not-embedded";
            // A build without embedded source-control facts is conservatively
            // marked dirty instead of publishing an unverifiable clean claim.
            metadata.dirtyWorktree = true;
#if defined(NDEBUG)
            metadata.executableConfiguration = "Release";
#else
            metadata.executableConfiguration = "Debug";
#endif
            if (!IsInteractiveGpuRuntimeConfig(effectiveConfig))
            {
                throw std::logic_error(
                    "Capture configuration is outside the GPU renderer's ownership.");
            }
            else if (effectiveConfig.executionArchitecture
                == ExecutionArchitecture::Wavefront)
            {
                metadata.buildIdentity = "RenderingEngine-mixed-debug-wavefront";
            }
            else
            {
                metadata.buildIdentity = wave2Runtime_ != nullptr
                        && wave2Runtime_->UsesStagedRaw(
                            effectiveConfig.executionArchitecture)
                    ? "RenderingEngine-mixed-debug-staged"
                    : "RenderingEngine-mixed-debug-monolithic";
            }
            metadata.gpuName = physicalDeviceProperties_.deviceName;
            metadata.driverVersion = "vk-driver-id:"
                + std::to_string(physicalDeviceProperties_.driverVersion);
            metadata.vulkanApiVersion = FormatVulkanVersion(
                physicalDeviceProperties_.apiVersion);
            metadata.vulkanSdkVersion = FormatVulkanVersion(VK_HEADER_VERSION_COMPLETE);
            const bool wave2Capture = IsInteractiveGpuRuntimeConfig(effectiveConfig);
            metadata.sceneId = wave2Capture && activeExperimentScene_.has_value()
                ? std::string(activeExperimentScene_->descriptor.runtimeToken)
                : "baseline";
            metadata.sceneGeneration = std::to_string(sceneGeneration);
            metadata.sceneHash = wave2Capture && activeExperimentScene_.has_value()
                ? FormatHash(canonicalSceneFingerprint_)
                : throw std::logic_error("Capture canonical scene is unavailable.");
            metadata.assetHashes = {};
            if (wave2Capture && !canonicalSceneStableId_.empty())
            {
                metadata.assetHashes.push_back({
                    "provider:" + canonicalSceneStableId_,
                    FormatHash(canonicalSceneFingerprint_)
                });
                if (activeExperimentScene_.has_value()
                    && activeExperimentScene_->environmentEnabled)
                {
                    metadata.assetHashes.push_back({
                        "provider:" + canonicalSceneStableId_ + ":environment",
                        FormatHash(ExperimentEnvironmentFingerprint(
                            activeExperimentScene_->environment))
                    });
                }
            }
            else
            {
                metadata.assetHashes.push_back({
                    "procedural-baseline", metadata.sceneHash
                });
            }
            metadata.cameraPreset = wave2Capture && activeExperimentScene_.has_value()
                ? std::string(activeExperimentScene_->activeCameraStableId)
                : "baseline-interactive-camera";
            metadata.requestedRuntimeConfig = requestedConfig;
            metadata.submittedCamera = submittedCamera_;
            metadata.effectiveRuntimeConfig = effectiveConfig;
            metadata.effectiveRuntimeConfig.render.width = swapchainExtent_.width;
            metadata.effectiveRuntimeConfig.render.height = swapchainExtent_.height;
            metadata.effectiveRuntimeConfig.run.artifactRoot = layout.artifactRoot;
            metadata.effectiveRuntimeConfig.run.runIdentifier =
                layout.runDirectory.filename().string();
            if (metadata.effectiveRuntimeConfig.run.captureDirectory.has_value())
            {
                metadata.effectiveRuntimeConfig.run.captureDirectory = layout.artifactRoot;
            }
            const std::filesystem::path executableDirectory = ExecutableDirectory();
            metadata.shaderHashes.clear();
            if (IsInteractiveGpuRuntimeConfig(effectiveConfig))
            {
                const bool softwareVariant = effectiveConfig.backend
                    != TraversalBackend::VulkanRayQuery;
                const std::string backendToken = softwareVariant
                    ? "software"
                    : "rayquery";
                const std::string traversalName = softwareVariant
                    ? "software_trace_v1.spv"
                    : "hardware_ray_query_v1.spv";
                const bool stagedRaw = wave2Runtime_ != nullptr
                    && wave2Runtime_->UsesStagedRaw(
                        effectiveConfig.executionArchitecture);
                if (stagedRaw)
                {
                    const std::array stagedRawShaders{
                        std::string{"pbr_megakernel_raw_seed.spv"},
                        "pbr_megakernel_" + backendToken + "_raw_trace.spv",
                        "pbr_megakernel_" + backendToken
                            + "_raw_shade.spv",
                        std::string{"pbr_megakernel_raw_resolve.spv"}
                    };
                    for (const std::string& shader : stagedRawShaders)
                    {
                        metadata.shaderHashes.push_back(ShaderHash(
                            shader, executableDirectory / shader));
                    }
                }
                else if (effectiveConfig.executionArchitecture
                    == ExecutionArchitecture::Megakernel)
                {
                    const std::string megakernelName = "pbr_megakernel_"
                        + backendToken
                        + "_noprofile.spv";
                    metadata.shaderHashes.push_back(ShaderHash(
                        megakernelName, executableDirectory / megakernelName));
                }
                else
                {
                    const std::array wavefrontShaders{
                        std::string{"wavefront_reset.spv"},
                        std::string{"wavefront_raygen.spv"},
                        std::string{"wavefront_prepare_dispatch.spv"},
                        "wavefront_intersect_" + backendToken + ".spv",
                        std::string{"wavefront_shade.spv"},
                        std::string{"wavefront_scan.spv"},
                        std::string{"wavefront_add_offsets.spv"},
                        std::string{"wavefront_scatter.spv"},
                        "wavefront_trace_shadow_" + backendToken + ".spv",
                        std::string{"wavefront_next_bounce.spv"},
                        std::string{"wavefront_resolve.spv"}
                    };
                    for (const std::string& shader : wavefrontShaders)
                    {
                        metadata.shaderHashes.push_back(ShaderHash(
                            shader, executableDirectory / shader));
                    }
                }
                metadata.shaderHashes.push_back(ShaderHash(
                    traversalName, executableDirectory / traversalName));
                if (stagedRaw && activeWave2SignalAov_.has_value()
                    && *activeWave2SignalAov_ < wave2SignalTokens.size())
                {
                    std::string signalSuffix =
                        std::string(wave2SignalTokens[*activeWave2SignalAov_]);
                    std::replace(
                        signalSuffix.begin(), signalSuffix.end(), '-', '_');
                    const std::string signalName = "pbr_megakernel_"
                        + backendToken + "_" + signalSuffix + ".spv";
                    metadata.shaderHashes.push_back(ShaderHash(
                        signalName, executableDirectory / signalName));
                    if (*activeWave2SignalAov_ >= 3u)
                    {
                        const std::string seedName = "pbr_megakernel_"
                            + backendToken + "_" + signalSuffix + "_seed.spv";
                        const std::string shadeName = "pbr_megakernel_"
                            + backendToken + "_" + signalSuffix + "_shade.spv";
                        const std::string resolveName = "pbr_megakernel_"
                            + signalSuffix + "_resolve.spv";
                        metadata.shaderHashes.push_back(ShaderHash(
                            seedName, executableDirectory / seedName));
                        metadata.shaderHashes.push_back(ShaderHash(
                            shadeName, executableDirectory / shadeName));
                        metadata.shaderHashes.push_back(ShaderHash(
                            resolveName, executableDirectory / resolveName));
                    }
                }
            }
            else
            {
                throw std::logic_error(
                    "No shader capture provider exists for this execution architecture.");
            }
            metadata.shaderHashes.push_back(ShaderHash(
                "Present.vert.spv", executableDirectory / "Present.vert.spv"));
            metadata.shaderHashes.push_back(ShaderHash(
                "Present.frag.spv", executableDirectory / "Present.frag.spv"));
            metadata.renderStartedAtUtc = renderStartedAtUtc_;
            metadata.renderCompletedAtUtc = FormatUtcNow();
            metadata.linearColorSpace = "linear-rec709";
            metadata.previewDisplayTransform = "pbr-neutral-plus-srgb-oetf";
            return metadata;
        }

        void WriteLiveCapture(
            const bool commandLineCapture,
            const Ui::ForwardedShowcaseRequest* const interactiveRequest = nullptr)
        {
            const RuntimeConfig& effectiveConfig = interactiveRequest != nullptr
                ? interactiveRequest->effectiveRuntimeConfig
                : liveConfig_;
            const RuntimeConfig& requestedConfig = interactiveRequest != nullptr
                ? interactiveRequest->effectiveRuntimeConfig
                : requestedRuntimeConfig_;
            const std::uint64_t configGeneration = interactiveRequest != nullptr
                ? interactiveRequest->configGeneration
                : showcaseController_.ConfigGeneration();
            const std::uint64_t sceneGeneration = interactiveRequest != nullptr
                ? interactiveRequest->sceneGeneration
                : showcaseController_.SceneGeneration();
            const std::uint64_t resourceGeneration = interactiveRequest != nullptr
                ? interactiveRequest->resourceGeneration
                : showcaseController_.ResourceGeneration();

            std::string runIdentifier = effectiveConfig.run.runIdentifier;
            if (!commandLineCapture)
            {
                std::ostringstream suffix;
                suffix << runIdentifier << "-capture-" << std::setfill('0')
                    << std::setw(4) << ++interactiveCaptureSequence_;
                runIdentifier = suffix.str();
            }
            const ArtifactLayout layout = ResolveArtifactLayout(
                effectiveConfig.run.artifactRoot,
                runIdentifier);

            std::vector<float> linearRgba;
            ReadbackOutputImage(linearRgba);
            Demos::CapturePreviewResult preview =
                Demos::BuildCapturePreviewRgba8(linearRgba, exposure_);
            if (!preview)
            {
                throw std::runtime_error(
                    "Capture preview generation failed: " + preview.error);
            }
            const Demos::CaptureMetadata metadata = BuildCaptureMetadata(
                layout,
                requestedConfig,
                effectiveConfig,
                configGeneration,
                sceneGeneration,
                resourceGeneration);
            const Demos::CaptureImageView image{
                swapchainExtent_.width,
                swapchainExtent_.height,
                linearRgba,
                preview.rgba8
            };
            const Demos::CaptureBundleResult result =
                Demos::WriteCaptureBundle(layout, image, metadata);
            if (!result)
            {
                const std::string message = "Capture failed: " + result.message;
                if (commandLineCapture)
                {
                    throw std::runtime_error(message);
                }
                std::cerr << message << '\n';
                return;
            }
            std::cout << "Capture written: " << result.runDirectory.string() << '\n';
        }

        void MaybeWritePendingCapture(
            const RuntimeConfig& config,
            const std::uint32_t completedFrames)
        {
            const bool hasFilmTarget = targetSamplesPerPixel_ > 0u
                && liveConfig_.reconstruction == ReconstructionMode::ProgressiveMean
                && debugView_ == DebugView::Final;
            const bool hasTerminalTarget = hasFilmTarget
                || config.run.frameLimit > 0u;
            const bool terminalReached =
                (hasFilmTarget
                    && accumulationFrame_ >= targetSamplesPerPixel_)
                || (config.run.frameLimit > 0u
                    && completedFrames >= config.run.frameLimit);
            if (cliCapturePending_ && (!hasTerminalTarget || terminalReached))
            {
                WriteLiveCapture(true);
                cliCapturePending_ = false;
                interactiveCaptureRequest_.reset();
                shouldClose_ = true;
                return;
            }
            if (interactiveCaptureRequest_.has_value())
            {
                Ui::ForwardedShowcaseRequest request =
                    std::move(*interactiveCaptureRequest_);
                interactiveCaptureRequest_.reset();
                if (request.configGeneration != showcaseController_.ConfigGeneration()
                    || request.sceneGeneration != showcaseController_.SceneGeneration()
                    || request.resourceGeneration != showcaseController_.ResourceGeneration())
                {
                    std::cerr
                        << "Capture request was invalidated before readback because its "
                           "config/scene/resource generation is no longer current.\n";
                    return;
                }
                WriteLiveCapture(false, &request);
            }
        }

        void ProcessForwardedShowcaseRequests(
            const std::vector<Ui::ForwardedShowcaseRequest>& requests,
            const float deltaSeconds)
        {
            float forward = 0.0f;
            float right = 0.0f;
            float vertical = 0.0f;
            bool fast = false;
            bool fine = false;
            bool cameraChanged = false;
            bool cameraCut = false;

            for (const Ui::ForwardedShowcaseRequest& request : requests)
            {
                switch (request.command)
                {
                case Ui::RoutedCommand::MoveForward: forward += 1.0f; break;
                case Ui::RoutedCommand::MoveBackward: forward -= 1.0f; break;
                case Ui::RoutedCommand::MoveLeft: right -= 1.0f; break;
                case Ui::RoutedCommand::MoveRight: right += 1.0f; break;
                case Ui::RoutedCommand::MoveDown: vertical -= 1.0f; break;
                case Ui::RoutedCommand::MoveUp: vertical += 1.0f; break;
                case Ui::RoutedCommand::FastMovementModifier: fast = true; break;
                case Ui::RoutedCommand::FineMovementModifier: fine = true; break;
                case Ui::RoutedCommand::Look:
                    if (mouseCaptured_ && !comparisonLocked_)
                    {
                        camera_.Rotate(
                            request.event.valueX,
                            -request.event.valueY);
                        cameraChanged = true;
                    }
                    break;
                case Ui::RoutedCommand::ResetShowcaseCamera:
                    ApplyActiveSceneCamera(true);
                    cameraCut = true;
                    if (comparisonLocked_)
                    {
                        comparisonLockedCamera_ = camera_;
                    }
                    cameraChanged = true;
                    std::cout << "[固定相机] 已恢复当前展示空间的 Home 预设。\n";
                    break;
                case Ui::RoutedCommand::ToggleAnimationPause:
                    animationPaused_ = !animationPaused_;
                    std::cout << "[动画] "
                        << (animationPaused_ ? "已暂停" : "已继续")
                        << "；场景 8 使用确定性 60 Hz 时间轴，其余内置场景保持当前快照。\n";
                    break;
                case Ui::RoutedCommand::StepAnimationFrame:
                    if (comparisonLocked_)
                    {
                        std::cout << "[动画单步] A/B 锚点已锁定；animation origin 保持不变。\n";
                    }
                    else if (animationPaused_)
                    {
                        ++animationOriginTick_;
                        ++animationFrameIndex_;
                        std::cout << "[动画单步] tick=" << animationOriginTick_
                            << "；场景 8 将推进一个确定性 60 Hz 样本。\n";
                    }
                    else
                    {
                        std::cout << "[动画单步] 请先按 P 暂停动画。\n";
                    }
                    break;
                case Ui::RoutedCommand::AdjustMovementSpeed:
                {
                    const float wheelDelta = request.event.valueY != 0.0f
                        ? request.event.valueY
                        : request.event.valueX;
                    cameraSpeedScale_ = std::clamp(
                        cameraSpeedScale_ * std::pow(1.2f, wheelDelta),
                        0.05f,
                        20.0f);
                    break;
                }
                case Ui::RoutedCommand::TogglePointerCapture:
                    SetPointerCaptured(!mouseCaptured_);
                    break;
                case Ui::RoutedCommand::ReleasePointerCapture:
                    SetPointerCaptured(false);
                    break;
                case Ui::RoutedCommand::RequestExit:
                    shouldClose_ = true;
                    break;
                case Ui::RoutedCommand::ResetHistories:
                    // The controller has already converted this provider request
                    // into the reset mask consumed below.
                    break;
                case Ui::RoutedCommand::ToggleComparisonLock:
                    comparisonLocked_ = !comparisonLocked_;
                    if (comparisonLocked_)
                    {
                        comparisonLockedCamera_ = camera_;
                        comparisonLockedBaseSeed_ = baseSeed_;
                        comparisonLockedAnimationOriginTick_ = animationOriginTick_;
                    }
                    std::cout << "[A/B 锁定] "
                        << (comparisonLocked_ ? "已锁定 camera/base seed/animation origin"
                                              : "已解除")
                        << "；frame/sample index 继续推进。\n";
                    break;
                case Ui::RoutedCommand::RequestCapture:
                    if (request.configGeneration != showcaseController_.ConfigGeneration()
                        || request.sceneGeneration != showcaseController_.SceneGeneration()
                        || request.resourceGeneration != showcaseController_.ResourceGeneration())
                    {
                        std::cerr
                            << "Capture request rejected because a later action in the same "
                               "frame changed its config/scene/resource generation.\n";
                    }
                    else if (interactiveCaptureRequest_.has_value())
                    {
                        std::cerr
                            << "Capture request ignored because another interactive capture "
                               "is already pending.\n";
                    }
                    else
                    {
                        // Scene application may normalize camera-preset fields
                        // (notably FOV) after the action batch commits. Store
                        // the effective live tuple that produced the frame,
                        // while retaining the request's generation identity.
                        Ui::ForwardedShowcaseRequest normalized = request;
                        normalized.effectiveRuntimeConfig = liveConfig_;
                        interactiveCaptureRequest_ = std::move(normalized);
                    }
                    break;
                case Ui::RoutedCommand::RequestShaderReload:
                    if (ReloadShadersTransactional())
                    {
                        showcaseController_.NotifyExternalReset(
                            Ui::ResetCause::ShaderReloadSucceeded);
                        std::cout << "[Shader Reload] 事务提交成功；旧资源已在新资源就绪后释放。\n";
                    }
                    break;
                default:
                    break;
                }
            }

            if (!comparisonLocked_
                && (forward != 0.0f || right != 0.0f || vertical != 0.0f))
            {
                const float fineScale = fine ? 0.25f : 1.0f;
                camera_.Move(
                    forward,
                    right,
                    vertical,
                    deltaSeconds * cameraSpeedScale_ * fineScale,
                    fast);
                cameraChanged = true;
            }
            if (cameraChanged)
            {
                if (cameraCut) ResetAccumulation();
                else ResetProgressiveFilm();
            }
        }

        void ProcessSemanticInput(const float deltaSeconds)
        {
            IGlfwPlatformStateSource* const inputSource =
                QueryGlfwPlatformStateSource(*platform_);
            if (inputSource == nullptr)
            {
                return;
            }

            const std::vector<GlfwRawInputEvent> rawEvents =
                inputSource->DrainRawInputEvents();
            const bool uiWantsKeyboard = wave5ShowcaseRuntime_ != nullptr
                && wave5ShowcaseRuntime_->WantCaptureKeyboard();
            const bool uiWantsMouse = wave5ShowcaseRuntime_ != nullptr
                && wave5ShowcaseRuntime_->WantCaptureMouse();
            const Ui::GlfwActionTranslationPolicy policy{
                uiWantsKeyboard,
                uiWantsMouse,
                mouseCaptured_
            };
            const bool finiteCapture = liveConfig_.run.captureDirectory.has_value()
                && (liveConfig_.run.frameLimit > 0u || targetSamplesPerPixel_ > 0u);
            if (!finiteCapture) static_cast<void>(Ui::QueueGlfwFrameActions(
                rawEvents,
                inputSource->PhysicalInputState(),
                policy,
                actionQueue_));

            const Ui::ActionBatchResult batch =
                Ui::ApplyQueuedActions(actionQueue_, liveConfig_, [this](const RuntimeConfig& config) {
                    std::optional<Scene::ExperimentScene> otherScene;
                    const Scene::ExperimentScene* scene = activeExperimentScene_ ? &*activeExperimentScene_ : nullptr;
                    if (scene == nullptr || config.scene != appliedRuntimeConfig_.scene
                        || config.restir.manyLightsTier != appliedRuntimeConfig_.restir.manyLightsTier)
                    {
                        otherScene = Scene::BuildExperimentScene(
                            static_cast<Scene::ExperimentScenePreset>(config.scene), MakeSceneBuildOptions(config));
                        scene = &*otherScene;
                    }
                    std::vector<std::string> ids;
                    for (const auto& variant : scene->variants) ids.emplace_back(variant.stableId);
                    return ids;
                });
            for (const Ui::ActionApplyResult& action : batch.actions)
            {
                if (action.status == Ui::ActionApplyStatus::Rejected)
                {
                    showcaseActionFeedback_ = "Command rejected: ";
                    showcaseActionFeedback_.append(action.reason);
                    showcaseActionFeedback_.append(
                        ". Current scene and complete algorithm tuple were preserved.");
                    showcaseActionFeedbackIsError_ = true;
                    std::cerr << "[指令拒绝] " << action.reason
                        << "；当前配置保持不变。\n";
                }
                else if (action.status == Ui::ActionApplyStatus::ConfigCommitted)
                {
                    const Ui::SemanticAction semanticAction =
                        action.queued.event.action;
                    if (semanticAction
                        == Ui::SemanticAction::RestoreCurrentSceneRecommendedProfile)
                    {
                        const SceneRecommendedProfile* const recommendation =
                            FindSceneRecommendedProfile(liveConfig_.scene);
                        showcaseActionFeedback_ =
                            "Current scene teaching recommendation restored atomically";
                        if (recommendation != nullptr)
                        {
                            showcaseActionFeedback_.append(": ");
                            showcaseActionFeedback_.append(recommendation->stableId);
                        }
                        showcaseActionFeedback_.append(
                            ". Camera/FOV, resolution, render scale, SPF, target SPP, exposure, seed, validation, VSync, and output paths were preserved.");
                        std::cout << "[场景适配] "
                            << ScenePresetName(liveConfig_.scene)
                            << " 已恢复教学推荐组合；按实际差异重置依赖历史/资源，"
                               "相机与运行参数保持不变。\n";
                    }
                    else if (semanticAction >= Ui::SemanticAction::SelectScene0
                        && semanticAction <= Ui::SemanticAction::SelectScene9)
                    {
                        const Scene::ExperimentSceneDescriptor* const descriptor =
                            Scene::FindExperimentScene(
                                static_cast<Scene::ExperimentScenePreset>(
                                    liveConfig_.scene));
                        showcaseActionFeedback_ = "Scene ";
                        showcaseActionFeedback_.append(
                            std::to_string(static_cast<std::uint32_t>(liveConfig_.scene)));
                        showcaseActionFeedback_.append(
                            " loaded. Algorithm tuple preserved; accumulation/history/resources reset.");
                        if (descriptor != nullptr)
                        {
                            showcaseActionFeedback_.append(" Raw preview warms toward ");
                            showcaseActionFeedback_.append(
                                std::to_string(descriptor->recommendedReferenceSpp));
                            showcaseActionFeedback_.append(" reference SPP.");
                        }
                    }
                    else if (semanticAction
                            == Ui::SemanticAction::CycleTransportModelForward
                        || semanticAction
                            == Ui::SemanticAction::CycleTransportModelBackward)
                    {
                        showcaseActionFeedback_ =
                            "Transport model committed: ";
                        showcaseActionFeedback_.append(
                            RendererTransportLabel(liveConfig_.transportModel));
                        showcaseActionFeedback_.append(". Scene=");
                        showcaseActionFeedback_.append(
                            ScenePresetName(liveConfig_.scene));
                        showcaseActionFeedback_.append(".");
                    }
                    else if (semanticAction
                            == Ui::SemanticAction::CycleExecutionArchitectureForward
                        || semanticAction
                            == Ui::SemanticAction::CycleExecutionArchitectureBackward)
                    {
                        showcaseActionFeedback_ = "Execution architecture committed: ";
                        showcaseActionFeedback_.append(ExecutionArchitectureName(
                            liveConfig_.executionArchitecture));
                        showcaseActionFeedback_.append(".");
                    }
                    else
                    {
                        showcaseActionFeedback_ =
                            "Runtime configuration committed; dependent histories were reset.";
                    }
                    showcaseActionFeedbackIsError_ = false;
                }
                else if (action.status
                    == Ui::ActionApplyStatus::AcceptedNoConfigChange
                    && !action.reason.empty())
                {
                    showcaseActionFeedback_ = action.reason;
                    showcaseActionFeedbackIsError_ = false;
                    std::cout << "[无需切换] " << action.reason << '\n';
                }
                else if (action.status == Ui::ActionApplyStatus::RoutedToOwner
                    && action.queued.event.action == Ui::SemanticAction::ToggleHelp)
                {
                    std::cout << "\n[当前配置] "
                        << FormatRuntimeConfigStatus(liveConfig_) << '\n'
                        << GlfwKeyHelpText() << "\n\n";
                }
                else if (action.status == Ui::ActionApplyStatus::RoutedToOwner
                    && action.queued.event.action
                        == Ui::SemanticAction::PrintCurrentReview)
                {
                    const Ui::ShowcaseRuntimeStatus measurement =
                        BuildLiveShowcaseRuntimeStatus();
                    const auto measured = [](const auto& value)
                    {
                        return value.has_value()
                            ? std::to_string(*value) : std::string{"--"};
                    };
                    std::cout << FormatRuntimeReview(
                        action.effectiveRuntimeConfig)
                        << "运行观测（-- 表示生产回读未提供，绝不以配置值代替）：\n"
                        << "  Current paths/pixel/frame="
                        << measured(measurement.currentFramePathsPerPixel)
                        << "；Film SPP=" << measured(measurement.progressiveFilmSpp)
                        << "；Reference SPP=" << measured(measurement.referenceSpp) << '\n'
                        << "  Temporal history=" << measured(measurement.temporalHistoryLength)
                        << "；Reservoir M/Age=" << measured(measurement.reservoirM)
                        << '/' << measured(measurement.reservoirAge)
                        << "；Candidates=" << measured(measurement.reservoirCandidates) << '\n'
                        << "  Visibility rays=" << measured(measurement.visibilityRays)
                        << "；Total traced rays=" << measured(measurement.totalTracedRays)
                        << "（精确射线计数需打开 F3；ReSTIR 独立可见性队列未回读时保持 --）\n\n";
                }
                else if (action.status == Ui::ActionApplyStatus::RoutedToOwner
                    && action.queued.event.action == Ui::SemanticAction::ResetHistories)
                {
                    std::cout << "[重置请求] 将按当前能力应用 A/T/Q/P reset mask。\n";
                }
            }
            const Ui::ShowcaseControllerUpdate controllerUpdate =
                showcaseController_.Apply(batch);
            for (const Ui::RoutedRequestResult& routed : controllerUpdate.routed)
            {
                if (routed.status == Ui::RoutedRequestStatus::ProviderUnavailable)
                {
                    showcaseActionFeedback_ = "Feature unavailable: ";
                    showcaseActionFeedback_.append(routed.reason);
                    showcaseActionFeedback_.append(" Current state was preserved.");
                    showcaseActionFeedbackIsError_ = true;
                    std::cerr << "[功能不可用] " << routed.reason
                        << "；当前状态保持不变。\n";
                }
            }
            SynchronizeLiveRuntimeConfig();

            const Ui::ResetMask requestedResets =
                showcaseController_.TakePendingResets();
            debugProfilerModel_.ApplyReset(requestedResets);
            if (Ui::HasReset(requestedResets, Ui::ResetResource::Accumulation))
            {
                ResetProgressiveFilm();
            }
            if ((Ui::HasReset(requestedResets,
                    Ui::ResetResource::TemporalHistory)
                || Ui::HasReset(requestedResets,
                    Ui::ResetResource::ReservoirHistory))
                && wave2Runtime_ != nullptr)
            {
                wave2Runtime_->InvalidateWave3Histories();
            }
            if (requestedResets != Ui::ResetResource::None)
            {
                std::string resetLabels;
                const auto appendReset = [&resetLabels](
                    const bool enabled, const std::string_view label)
                {
                    if (!enabled)
                    {
                        return;
                    }
                    if (!resetLabels.empty())
                    {
                        resetLabels += '|';
                    }
                    resetLabels += label;
                };
                appendReset(Ui::HasReset(requestedResets,
                    Ui::ResetResource::Accumulation), "A");
                appendReset(Ui::HasReset(requestedResets,
                    Ui::ResetResource::TemporalHistory), "T");
                appendReset(Ui::HasReset(requestedResets,
                    Ui::ResetResource::ReservoirHistory), "Q");
                appendReset(Ui::HasReset(requestedResets,
                    Ui::ResetResource::ProfilerStatistics), "P");
                appendReset(Ui::HasReset(requestedResets,
                    Ui::ResetResource::AccelerationStructures), "AS(rebuilt by provider)");
                std::cout << "[Reset Mask] " << resetLabels;
                if (Ui::HasReset(requestedResets,
                        Ui::ResetResource::Accumulation)
                    || Ui::HasReset(requestedResets,
                        Ui::ResetResource::TemporalHistory)
                    || Ui::HasReset(requestedResets,
                        Ui::ResetResource::ReservoirHistory))
                {
                    std::cout << "；shared reconstruction/ReSTIR histories will restart on the next frame.";
                }
                std::cout << '\n';
            }

            ProcessForwardedShowcaseRequests(
                showcaseController_.TakeForwardedRequests(),
                deltaSeconds);
        }

        void HandlePlatformEvents(
            const PlatformFrameEvents& events,
            const bool useSemanticInput)
        {
            shouldClose_ = shouldClose_ || events.closeRequested;
            framebufferResized_ = framebufferResized_ || events.framebufferResized;

            if (!useSemanticInput
                && mouseCaptured_
                && (events.input.mouseDeltaX != 0.0f || events.input.mouseDeltaY != 0.0f))
            {
                camera_.Rotate(events.input.mouseDeltaX, -events.input.mouseDeltaY);
                ResetProgressiveFilm();
            }
            if (!useSemanticInput && events.input.mouseWheelDelta != 0.0f)
            {
                camera_.Zoom(events.input.mouseWheelDelta);
                ResetAccumulation();
            }
        }

        void ProcessInput(const RawInputFrame& input, float deltaSeconds)
        {
            if (!input.focused)
            {
                tabWasPressed_ = false;
                return;
            }

            const auto keyDown = [&input](PhysicalKey key)
            {
                return input.IsKeyDown(key);
            };

            if (keyDown(PhysicalKey::Escape))
            {
                shouldClose_ = true;
            }

            const float forward = static_cast<float>(keyDown(PhysicalKey::W))
                - static_cast<float>(keyDown(PhysicalKey::S));
            const float right = static_cast<float>(keyDown(PhysicalKey::D))
                - static_cast<float>(keyDown(PhysicalKey::A));
            const bool control = keyDown(PhysicalKey::LeftControl)
                || keyDown(PhysicalKey::RightControl);
            const float vertical = static_cast<float>(keyDown(PhysicalKey::Space))
                - static_cast<float>(control);
            const bool sprint = keyDown(PhysicalKey::LeftShift)
                || keyDown(PhysicalKey::RightShift);
            if (forward != 0.0f || right != 0.0f || vertical != 0.0f)
            {
                camera_.Move(forward, right, vertical, deltaSeconds, sprint);
                ResetAccumulation();
            }

            const bool tabPressed = keyDown(PhysicalKey::Tab);
            if (tabPressed && !tabWasPressed_)
            {
                mouseCaptured_ = !mouseCaptured_;
                platform_->SetCursorCaptured(mouseCaptured_);
            }
            tabWasPressed_ = tabPressed;

            if (keyDown(PhysicalKey::Digit1))
            {
                SetShadowMethod(ShadowMethod::Pcf);
            }
            else if (keyDown(PhysicalKey::Digit2))
            {
                SetShadowMethod(ShadowMethod::Pcss);
            }
            else if (keyDown(PhysicalKey::Digit3))
            {
                SetShadowMethod(ShadowMethod::Physical);
            }
        }

        void MainLoop(const RuntimeConfig& config)
        {
            using Clock = std::chrono::steady_clock;
            auto previousTime = Clock::now();
            const auto benchmarkStart = previousTime;
            auto titleUpdateTime = previousTime;
            std::uint32_t titleFrameCount = 0;
            const std::uint32_t firstFrame = totalFrames_;

            while (!shouldClose_)
            {
                const PlatformFrameEvents events = platform_->PumpEvents(EventPumpMode::Poll);
                HandlePlatformEvents(events, semanticInputEnabled_);
                if (shouldClose_)
                {
                    break;
                }
                const auto now = Clock::now();
                const float deltaSeconds = std::min(
                    std::chrono::duration<float>(now - previousTime).count(), 0.1f);
                previousTime = now;
                BeginWave5ShowcaseFrame();
                if (semanticInputEnabled_)
                {
                    ProcessSemanticInput(deltaSeconds);
                }
                else
                {
                    ProcessInput(events.input, deltaSeconds);
                }
                if (shouldClose_)
                {
                    if (wave5ShowcaseRuntime_ != nullptr)
                    {
                        wave5ShowcaseRuntime_->CancelFrame();
                    }
                    break;
                }
                const bool renderedDrawableFrame = DrawFrame();
                ++titleFrameCount;

                const std::uint32_t completedFrames = totalFrames_ - firstFrame;
                if (renderedDrawableFrame)
                {
                    MaybeWritePendingCapture(config, completedFrames);
                }
                if (config.run.resizeTest && completedFrames == 30)
                {
                    platform_->RequestClientArea({ 960, 540 });
                }
                else if (config.run.resizeTest && completedFrames == 60)
                {
                    platform_->RequestClientArea({ config.render.width, config.render.height });
                }

                const float titleInterval = std::chrono::duration<float>(now - titleUpdateTime).count();
                if (titleInterval >= 0.5f)
                {
                    const float framesPerSecond = static_cast<float>(titleFrameCount) / titleInterval;
                    std::ostringstream title;
                    title << "Vulkan RT Wave 5 Debug | Scene="
                        << ScenePresetName(liveConfig_.scene)
                        << " | Backend=" << TraversalBackendName(liveConfig_.backend)
                        << " | Transport=" << TransportModelName(
                            liveConfig_.transportModel)
                        << " | Execution=" << ExecutionArchitectureName(
                            liveConfig_.executionArchitecture)
                        << " | DirectEstimator="
                        << DirectLightingEstimatorName(liveConfig_.directLightingEstimator)
                        << " | LightSelection="
                        << LightSelectionStrategyName(liveConfig_.lightSelection)
                        << " | EnvironmentSampler="
                        << EnvironmentDirectionSamplerName(
                            liveConfig_.environmentSampler)
                        << " | Shadow=" << ShadowMethodName(liveConfig_.shadowMethod)
                        << " | Reconstruction="
                        << ReconstructionModeName(liveConfig_.reconstruction)
                        << " | Debug=" << DebugViewName(debugView_)
                        << " | Resolution=" << swapchainExtent_.width << 'x'
                        << swapchainExtent_.height
                        << " | Seed=" << baseSeed_
                        << " | Frame=" << totalFrames_
                        << " | Film SPP=" << (liveConfig_.reconstruction == ReconstructionMode::ProgressiveMean
                            && debugView_ == DebugView::Final ? std::to_string(accumulationFrame_) : "n/a")
                        << " | Paths/pixel/frame=1"
                        << " | Bounce=" << maximumTraceDepth_;
                    if (latestGpuTraceMilliseconds_.has_value())
                    {
                        title << " | GPU trace=" << std::fixed << std::setprecision(3)
                            << *latestGpuTraceMilliseconds_ << " ms";
                    }
                    else
                    {
                        title << " | GPU trace=pending";
                    }
                    title << " | CPU present=" << std::fixed << std::setprecision(1)
                        << framesPerSecond << " FPS";
                    platform_->SetTitle(title.str());
                    titleFrameCount = 0;
                    titleUpdateTime = now;
                }

                if (config.run.frameLimit > 0 && completedFrames >= config.run.frameLimit)
                {
                    break;
                }
                if (targetSamplesPerPixel_ > 0
                    && liveConfig_.reconstruction == ReconstructionMode::ProgressiveMean
                    && debugView_ == DebugView::Final
                    && accumulationFrame_ >= targetSamplesPerPixel_)
                {
                    break;
                }
            }

            Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
            if (config.run.frameLimit > 0 || targetSamplesPerPixel_ > 0)
            {
                // Finite runs can end before either frame slot is reused. The
                // device is idle here, so publish each still-pending readback
                // once before writing the terminal observation record.
                for (std::uint32_t offset = 0u; offset < kFramesInFlight; ++offset)
                {
                    const std::uint32_t slot =
                        (currentFrame_ + offset) % kFramesInFlight;
                    PublishCompletedWave2Telemetry(slot);
                }
                const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
                    Clock::now() - benchmarkStart).count();
                const auto observed = [](const auto& value)
                {
                    return value.has_value()
                        ? std::to_string(*value) : std::string{"--"};
                };
                std::cout << "Rendered " << (totalFrames_ - firstFrame) << " frames in "
                    << std::fixed << std::setprecision(2) << elapsedMilliseconds << " ms ("
                    << elapsedMilliseconds / std::max<std::uint32_t>(totalFrames_ - firstFrame, 1u)
                    << " ms/frame including presentation, Film SPP="
                    << (liveConfig_.reconstruction == ReconstructionMode::ProgressiveMean
                        && debugView_ == DebugView::Final ? std::to_string(accumulationFrame_) : "n/a")
                    << ", paths/pixel/frame=1, "
                    << RendererTransportLabel(transportModel_) << ", "
                    << ShadowMethodName(shadowMethod_) << ", debug view "
                    << RendererDebugViewLabel(debugView_) << ", seed " << baseSeed_
                    << ", candidates=" << observed(latestReservoirCandidates_)
                    << ", visibility rays=" << observed(latestVisibilityRays_)
                    << ", total traced rays=" << observed(latestTotalTracedRays_)
                    << ").\n";
            }
        }

        void RecreateSwapchain()
        {
            ClientExtent framebufferExtent = platform_->GetFramebufferExtent();
            while (!framebufferExtent.IsDrawable() && !shouldClose_)
            {
                const PlatformFrameEvents events = platform_->PumpEvents(EventPumpMode::Wait);
                HandlePlatformEvents(events, semanticInputEnabled_);
                framebufferExtent = events.framebufferExtent;
            }
            if (shouldClose_)
            {
                return;
            }

            Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(swapchain recreation)");
            DiscardCompletedWave2Telemetry();
            ClearWave5ShowcaseTextures();
            CleanupSwapchainResources();
            CreateSwapchain();
            if (wave5ShowcaseRuntime_ != nullptr
                && wave5ShowcaseRuntime_->IsInitialized())
            {
                RequireWave5ShowcaseStatus(
                    wave5ShowcaseRuntime_->SetMinImageCount(
                        std::min(2u, static_cast<std::uint32_t>(swapchainImages_.size()))),
                    "update swapchain image count");
            }
            CreatePresentSemaphores();
            CreateOutputImage();
            if (wave2Runtime_ != nullptr)
            {
                wave2Runtime_->SetOutput(outputImageView_, swapchainExtent_);
                showcaseController_.SetSceneResourceGenerations(
                    canonicalSceneGeneration_,
                    wave2Runtime_->ResourceGeneration());
                wave2TelemetryAdapter_.Clear();
                debugProfilerModel_.InvalidateBeforeGenerationTuple(
                    showcaseController_.ConfigGeneration(),
                    showcaseController_.SceneGeneration(),
                    showcaseController_.ResourceGeneration());
            }
            RebuildWave5ShowcaseTextureRegistry();
            CreateGraphicsPipeline();
            UpdatePresentDescriptor();
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
                // L4/L5/L6 own descriptors, pipelines, AS objects and buffers
                // that reference the renderer device, command pool and output
                // view. Release them before any of those owners are destroyed.
                if (wave5ShowcaseRuntime_ != nullptr)
                {
                    ClearWave5ShowcaseTextures();
                    const Renderers::Wave5ShowcaseRuntimeStatus status =
                        wave5ShowcaseRuntime_->Shutdown();
                    if (!status)
                    {
                        std::cerr << "[Wave 5 UI] Shutdown 失败："
                            << status.reason << '\n';
                    }
                    wave5ShowcaseRuntime_.reset();
                }
                wave2Runtime_.reset();
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
                }

                if (descriptorPool_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
                }
                if (sampler_ != VK_NULL_HANDLE)
                {
                    vkDestroySampler(device_, sampler_, nullptr);
                }
                CleanupSwapchainResources();
                if (presentPipelineLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device_, presentPipelineLayout_, nullptr);
                }
                if (presentDescriptorSetLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(device_, presentDescriptorSetLayout_, nullptr);
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
                debugMessenger_ = VK_NULL_HANDLE;
            }
            if (surface_ != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(instance_, surface_, nullptr);
                surface_ = VK_NULL_HANDLE;
            }
            if (instance_ != VK_NULL_HANDLE)
            {
                vkDestroyInstance(instance_, nullptr);
                instance_ = VK_NULL_HANDLE;
            }
            platform_.reset();
        }

        std::unique_ptr<IPlatformHost> platform_;
        bool initialized_ = false;
        bool validationEnabled_ = false;
        bool framebufferResized_ = false;
        bool mouseCaptured_ = true;
        bool tabWasPressed_ = false;
        bool shouldClose_ = false;
        bool semanticInputEnabled_ = false;

        Camera camera_;
        RuntimeConfig requestedRuntimeConfig_;
        RuntimeConfig liveConfig_;
        RuntimeConfig appliedRuntimeConfig_;
        std::uint64_t appliedConfigGeneration_ = 0u;
        Ui::ActionQueue actionQueue_;
        Ui::ShowcaseController showcaseController_;
        Ui::ShowcaseViewModel showcaseViewModel_;
        Ui::ImGuiShowcaseSelectionState showcaseSelection_;
        std::string showcaseActionFeedback_;
        bool showcaseActionFeedbackIsError_ = false;
        std::optional<std::size_t> activeWave2SignalAov_;
        Ui::Wave2TelemetryAdapter wave2TelemetryAdapter_;
        Ui::DebugProfilerModel debugProfilerModel_;
        Demos::ShowcaseProgramModel showcaseProgram_;
        std::unique_ptr<Renderers::Wave2Runtime> wave2Runtime_;
        std::unique_ptr<Renderers::Wave5ShowcaseRuntime> wave5ShowcaseRuntime_;
        std::optional<Scene::ExperimentScene> activeExperimentScene_;
        std::string canonicalSceneStableId_;
        std::uint64_t canonicalSceneFingerprint_ = 0u;
        std::uint32_t canonicalSceneGeneration_ = 1u;
        std::string renderStartedAtUtc_;
        std::uint32_t interactiveCaptureSequence_ = 0u;
        bool cliCapturePending_ = false;
        std::optional<Ui::ForwardedShowcaseRequest> interactiveCaptureRequest_;
        std::optional<double> latestGpuTraceMilliseconds_;
        std::optional<std::uint64_t> latestVisibilityRays_;
        std::optional<std::uint64_t> latestTotalTracedRays_;
        std::optional<std::uint64_t> latestReservoirCandidates_;
        std::uint64_t latestTelemetryConfigGeneration_ = 0u;
        std::uint64_t latestTelemetrySceneGeneration_ = 0u;
        std::uint64_t latestTelemetryResourceGeneration_ = 0u;
        bool animationPaused_ = false;
        std::uint64_t animationOriginTick_ = 0u;
        std::uint64_t animationFrameIndex_ = 0u;
        bool comparisonLocked_ = false;
        Camera comparisonLockedCamera_;
        std::uint64_t comparisonLockedBaseSeed_ = 0u;
        std::uint64_t comparisonLockedAnimationOriginTick_ = 0u;
        float cameraSpeedScale_ = 1.0f;
        float exposure_ = 1.0f;
        std::uint32_t maximumTraceDepth_ = 8;
        std::uint32_t targetSamplesPerPixel_ = 0;
        std::uint64_t baseSeed_ = 0;
        RuntimeToggle vsyncMode_ = RuntimeToggle::RendererDefault;
        RuntimeToggle validationMode_ = RuntimeToggle::RendererDefault;
        TransportModel transportModel_ = TransportModel::Pbr;
        ShadowMethod shadowMethod_ = ShadowMethod::Physical;
        DebugView debugView_ = DebugView::Final;
        std::uint32_t accumulationFrame_ = 0;
        std::optional<std::array<float, 13>> submittedCamera_;
        std::uint32_t totalFrames_ = 0;
        std::uint32_t currentFrame_ = 0;
        std::uint64_t lastRenderedSampleIndex_ = 0u;
        std::uint32_t lastWavefrontFatalMask_ = 0u;

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
        VkDescriptorSetLayout presentDescriptorSetLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout presentPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline presentPipeline_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet presentDescriptorSet_ = VK_NULL_HANDLE;

        VkImage outputImage_ = VK_NULL_HANDLE;
        VkDeviceMemory outputImageMemory_ = VK_NULL_HANDLE;
        VkImageView outputImageView_ = VK_NULL_HANDLE;
        VkSampler sampler_ = VK_NULL_HANDLE;
    };

    VulkanWhittedRenderer::VulkanWhittedRenderer(std::unique_ptr<IPlatformHost> platform)
        : impl_(std::make_unique<Impl>(std::move(platform)))
    {
    }

    VulkanWhittedRenderer::~VulkanWhittedRenderer() = default;
    VulkanWhittedRenderer::VulkanWhittedRenderer(VulkanWhittedRenderer&&) noexcept = default;
    VulkanWhittedRenderer& VulkanWhittedRenderer::operator=(VulkanWhittedRenderer&&) noexcept = default;

    void VulkanWhittedRenderer::Run(const RuntimeConfig& config)
    {
        impl_->Run(config);
    }

    void VulkanWhittedRenderer::Run(
        const RuntimeConfig& config,
        const Scene::CanonicalScene& canonicalScene)
    {
        impl_->Run(config, &canonicalScene);
    }
}
