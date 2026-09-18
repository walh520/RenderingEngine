#include "renderers/Wave5ShowcaseRuntime.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace RenderingEngine::Renderers
{
    namespace
    {
        thread_local VkResult gImGuiVulkanFailure = VK_SUCCESS;

        void RecordImGuiVulkanResult(const VkResult result) noexcept
        {
            if (result < VK_SUCCESS && gImGuiVulkanFailure == VK_SUCCESS)
            {
                gImGuiVulkanFailure = result;
            }
        }

        void ResetImGuiVulkanFailure() noexcept
        {
            gImGuiVulkanFailure = VK_SUCCESS;
        }

        [[nodiscard]] std::filesystem::path WindowsFontDirectory()
        {
#if defined(_WIN32)
            char* windowsDirectory = nullptr;
            std::size_t length = 0u;
            if (_dupenv_s(&windowsDirectory, &length, "WINDIR") == 0
                && windowsDirectory != nullptr)
            {
                const std::filesystem::path result =
                    std::filesystem::path(windowsDirectory) / "Fonts";
                std::free(windowsDirectory);
                return result;
            }
            std::free(windowsDirectory);
#endif
            return std::filesystem::path("C:/Windows/Fonts");
        }

        [[nodiscard]] ImFont* LoadShowcaseFont(ImGuiIO& io)
        {
            const std::filesystem::path fontDirectory = WindowsFontDirectory();
            const std::array candidates{
                fontDirectory / "msyh.ttc",
                fontDirectory / "msyhbd.ttc",
                fontDirectory / "simhei.ttf"};

            ImFontConfig fontConfig{};
            fontConfig.FontNo = 0u;
            fontConfig.OversampleH = 2;
            fontConfig.OversampleV = 1;
            fontConfig.RasterizerMultiply = 1.05f;

            for (const std::filesystem::path& candidate : candidates)
            {
                std::error_code error;
                if (!std::filesystem::is_regular_file(candidate, error))
                {
                    continue;
                }
                const std::string utf8Path = candidate.string();
                if (ImFont* const font = io.Fonts->AddFontFromFileTTF(
                    utf8Path.c_str(),
                    17.0f,
                    &fontConfig,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon()))
                {
                    return font;
                }
            }
            return io.Fonts->AddFontDefault();
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus Fail(
            const Wave5ShowcaseRuntimeStatusCode code,
            std::string reason)
        {
            return {code, std::move(reason)};
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus BackendResult(
            const std::string_view operation)
        {
            if (gImGuiVulkanFailure == VK_SUCCESS)
            {
                return {};
            }
            return Fail(
                Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                std::string(operation) + " failed with VkResult "
                    + std::to_string(static_cast<int>(gImGuiVulkanFailure)));
        }

        [[nodiscard]] bool IsVulkan13OrNewer(const std::uint32_t version) noexcept
        {
            const std::uint32_t major = VK_API_VERSION_MAJOR(version);
            const std::uint32_t minor = VK_API_VERSION_MINOR(version);
            return major > 1u || (major == 1u && minor >= 3u);
        }

        [[nodiscard]] bool IsSingleSampleCountBit(
            const VkSampleCountFlagBits samples) noexcept
        {
            const auto value = static_cast<std::uint32_t>(samples);
            return value != 0u && (value & (value - 1u)) == 0u;
        }

        [[nodiscard]] bool IsShaderReadableLayout(
            const VkImageLayout layout) noexcept
        {
            switch (layout)
            {
            case VK_IMAGE_LAYOUT_GENERAL:
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] std::uint64_t DescriptorSetTextureId(
            const VkDescriptorSet descriptorSet) noexcept
        {
            static_assert(std::is_trivially_copyable_v<VkDescriptorSet>);
            static_assert(sizeof(VkDescriptorSet) <= sizeof(std::uint64_t));
            std::uint64_t result = 0u;
            std::memcpy(&result, &descriptorSet, sizeof(descriptorSet));
            return result;
        }
    }

    class Wave5ShowcaseRuntime::Impl final
    {
    public:
        [[nodiscard]] Wave5ShowcaseRuntimeStatus Initialize(
            const Wave5ShowcaseRuntimeCreateInfo& createInfo)
        {
            if (initialized_ || context_ != nullptr)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase runtime is already initialized");
            }
            if (createInfo.nativeWindowHandle == nullptr
                || createInfo.instance == VK_NULL_HANDLE
                || createInfo.physicalDevice == VK_NULL_HANDLE
                || createInfo.device == VK_NULL_HANDLE
                || createInfo.queue == VK_NULL_HANDLE)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase initialization requires a GLFW window and complete Vulkan handles");
            }
            if (!IsVulkan13OrNewer(createInfo.apiVersion))
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase requires Vulkan API version 1.3 or newer");
            }
            if (createInfo.queueFamilyIndex
                == std::numeric_limits<std::uint32_t>::max())
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase queue-family index is invalid");
            }
            if (createInfo.colorAttachmentFormat == VK_FORMAT_UNDEFINED
                || !IsSingleSampleCountBit(createInfo.rasterizationSamples))
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase requires a color format and one valid sample-count bit");
            }
            if (createInfo.minimumImageCount < 2u
                || createInfo.imageCount < createInfo.minimumImageCount)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase image count must be at least the minimum count, and both must be at least two");
            }
            if (createInfo.descriptorPoolSize
                < IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase descriptor pool is smaller than the ImGui Vulkan backend minimum");
            }
            if (ImGui::GetCurrentContext() != nullptr)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase owns the process ImGui context; an external context is already current");
            }

            window_ = static_cast<GLFWwindow*>(createInfo.nativeWindowHandle);
            colorAttachmentFormat_ = createInfo.colorAttachmentFormat;
            minimumImageCount_ = createInfo.minimumImageCount;
            try
            {
                IMGUI_CHECKVERSION();
                context_ = ImGui::CreateContext();
                if (context_ == nullptr)
                {
                    ResetState();
                    return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                        "ImGui::CreateContext returned null");
                }
                ImGui::SetCurrentContext(context_);
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                io.FontDefault = LoadShowcaseFont(io);
                ImGui::StyleColorsDark();
                ImGuiStyle& style = ImGui::GetStyle();
                style.WindowPadding = ImVec2(10.0f, 10.0f);
                style.FramePadding = ImVec2(6.0f, 4.0f);
                style.ItemSpacing = ImVec2(8.0f, 6.0f);
                style.WindowRounding = 5.0f;
                style.FrameRounding = 3.0f;
                style.ScrollbarRounding = 4.0f;

                if (!ImGui_ImplGlfw_InitForVulkan(window_, true))
                {
                    const std::string reason =
                        "ImGui GLFW backend initialization failed";
                    CleanupAfterFailedInitialize();
                    return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                        reason);
                }
                glfwBackendInitialized_ = true;

                ImGui_ImplVulkan_InitInfo initInfo{};
                initInfo.ApiVersion = createInfo.apiVersion;
                initInfo.Instance = createInfo.instance;
                initInfo.PhysicalDevice = createInfo.physicalDevice;
                initInfo.Device = createInfo.device;
                initInfo.QueueFamily = createInfo.queueFamilyIndex;
                initInfo.Queue = createInfo.queue;
                initInfo.DescriptorPoolSize = createInfo.descriptorPoolSize;
                initInfo.MinImageCount = createInfo.minimumImageCount;
                initInfo.ImageCount = createInfo.imageCount;
                initInfo.PipelineCache = createInfo.pipelineCache;
                initInfo.PipelineInfoMain.MSAASamples =
                    createInfo.rasterizationSamples;
                initInfo.UseDynamicRendering = true;
                initInfo.Allocator = createInfo.allocator;
                initInfo.CheckVkResultFn = RecordImGuiVulkanResult;
                initInfo.MinAllocationSize = 1024u * 1024u;

                VkPipelineRenderingCreateInfoKHR pipelineRendering{
                    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
                pipelineRendering.colorAttachmentCount = 1u;
                pipelineRendering.pColorAttachmentFormats =
                    &colorAttachmentFormat_;
                initInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
                    pipelineRendering;

                ResetImGuiVulkanFailure();
                if (!ImGui_ImplVulkan_Init(&initInfo))
                {
                    const Wave5ShowcaseRuntimeStatus backendStatus =
                        BackendResult("ImGui Vulkan backend initialization");
                    const std::string reason = backendStatus
                        ? "ImGui Vulkan backend initialization returned false"
                        : backendStatus.reason;
                    CleanupAfterFailedInitialize();
                    return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                        reason);
                }
                vulkanBackendInitialized_ = true;
                if (Wave5ShowcaseRuntimeStatus backendStatus =
                    BackendResult("ImGui Vulkan backend initialization");
                    !backendStatus)
                {
                    const std::string reason = backendStatus.reason;
                    CleanupAfterFailedInitialize();
                    return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                        reason);
                }

                initialized_ = true;
                return {};
            }
            catch (const std::exception& exception)
            {
                const std::string reason =
                    std::string("Wave 5 showcase initialization threw: ")
                    + exception.what();
                CleanupAfterFailedInitialize();
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    reason);
            }
            catch (...)
            {
                CleanupAfterFailedInitialize();
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    "Wave 5 showcase initialization threw an unknown exception");
            }
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus Shutdown()
        {
            if (context_ == nullptr && !initialized_)
            {
                ResetState();
                return {};
            }

            std::string firstFailure;
            try
            {
                ImGui::SetCurrentContext(context_);
                if (frameOpen_)
                {
                    ImGui::EndFrame();
                    frameOpen_ = false;
                }
                if (Wave5ShowcaseRuntimeStatus textureStatus = ClearTextures();
                    !textureStatus)
                {
                    firstFailure = textureStatus.reason;
                }
                ResetImGuiVulkanFailure();
                if (vulkanBackendInitialized_)
                {
                    ImGui_ImplVulkan_Shutdown();
                    vulkanBackendInitialized_ = false;
                }
                if (firstFailure.empty())
                {
                    if (Wave5ShowcaseRuntimeStatus backendStatus =
                        BackendResult("ImGui Vulkan backend shutdown");
                        !backendStatus)
                    {
                        firstFailure = backendStatus.reason;
                    }
                }
                if (glfwBackendInitialized_)
                {
                    ImGui_ImplGlfw_Shutdown();
                    glfwBackendInitialized_ = false;
                }
                ImGui::DestroyContext(context_);
            }
            catch (const std::exception& exception)
            {
                if (firstFailure.empty())
                {
                    firstFailure = std::string("Wave 5 showcase shutdown threw: ")
                        + exception.what();
                }
            }
            catch (...)
            {
                if (firstFailure.empty())
                {
                    firstFailure =
                        "Wave 5 showcase shutdown threw an unknown exception";
                }
            }
            ResetState();
            ImGui::SetCurrentContext(nullptr);
            return firstFailure.empty()
                ? Wave5ShowcaseRuntimeStatus{}
                : Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    std::move(firstFailure));
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus BeginFrame(
            Ui::ShowcasePanelState& panels,
            Ui::ImGuiShowcaseSelectionState& selection,
            const Ui::ImGuiShowcaseInputs& inputs,
            const Ui::IImGuiDebugTextureResolver& textureResolver)
        {
            if (!initialized_ || context_ == nullptr
                || !glfwBackendInitialized_ || !vulkanBackendInitialized_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase BeginFrame requires initialized backends");
            }
            if (frameOpen_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase frame is already open");
            }
            if (inputs.showcase == nullptr || inputs.actionQueue == nullptr)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase panels require a view model and action queue");
            }

            bool imguiFrameStarted = false;
            try
            {
                ImGui::SetCurrentContext(context_);
                ResetImGuiVulkanFailure();
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                imguiFrameStarted = true;

                Ui::ImGuiShowcaseInputs resolvedInputs = inputs;
                resolvedInputs.textureResolver = &textureResolver;
                const Ui::ImGuiShowcaseDrawStatus drawStatus =
                    Ui::DrawImGuiShowcasePanels(
                        panels, selection, resolvedInputs);
                if (drawStatus != Ui::ImGuiShowcaseDrawStatus::Drawn)
                {
                    ImGui::EndFrame();
                    const char* const reason = drawStatus
                        == Ui::ImGuiShowcaseDrawStatus::NoImGuiContext
                        ? "Wave 5 showcase lost its ImGui context"
                        : "Wave 5 showcase panel inputs are invalid";
                    return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                        reason);
                }
                if (Wave5ShowcaseRuntimeStatus backendStatus =
                    BackendResult("ImGui NewFrame"); !backendStatus)
                {
                    ImGui::EndFrame();
                    return backendStatus;
                }

                const ImGuiIO& io = ImGui::GetIO();
                wantCaptureKeyboard_ = io.WantCaptureKeyboard;
                wantCaptureMouse_ = io.WantCaptureMouse;
                frameOpen_ = true;
                return {};
            }
            catch (const std::exception& exception)
            {
                if (imguiFrameStarted)
                {
                    try
                    {
                        ImGui::EndFrame();
                    }
                    catch (...)
                    {
                    }
                }
                frameOpen_ = false;
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    std::string("Wave 5 showcase BeginFrame threw: ")
                        + exception.what());
            }
            catch (...)
            {
                if (imguiFrameStarted)
                {
                    try
                    {
                        ImGui::EndFrame();
                    }
                    catch (...)
                    {
                    }
                }
                frameOpen_ = false;
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    "Wave 5 showcase BeginFrame threw an unknown exception");
            }
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus RenderDrawData(
            const VkCommandBuffer commandBuffer)
        {
            if (!initialized_ || !frameOpen_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase RenderDrawData requires an open frame");
            }
            if (commandBuffer == VK_NULL_HANDLE)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase cannot record into a null command buffer");
            }

            bool rendered = false;
            try
            {
                ImGui::SetCurrentContext(context_);
                ImGui::Render();
                rendered = true;
                frameOpen_ = false;
                ResetImGuiVulkanFailure();
                ImGui_ImplVulkan_RenderDrawData(
                    ImGui::GetDrawData(), commandBuffer);
                return BackendResult("ImGui Vulkan draw recording");
            }
            catch (const std::exception& exception)
            {
                if (!rendered)
                {
                    try
                    {
                        ImGui::EndFrame();
                    }
                    catch (...)
                    {
                    }
                }
                frameOpen_ = false;
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    std::string("Wave 5 showcase draw recording threw: ")
                        + exception.what());
            }
            catch (...)
            {
                if (!rendered)
                {
                    try
                    {
                        ImGui::EndFrame();
                    }
                    catch (...)
                    {
                    }
                }
                frameOpen_ = false;
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    "Wave 5 showcase draw recording threw an unknown exception");
            }
        }

        void CancelFrame() noexcept
        {
            if (!frameOpen_ || context_ == nullptr)
            {
                return;
            }
            try
            {
                ImGui::SetCurrentContext(context_);
                ImGui::EndFrame();
            }
            catch (...)
            {
            }
            frameOpen_ = false;
            wantCaptureKeyboard_ = false;
            wantCaptureMouse_ = false;
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus SetMinImageCount(
            const std::uint32_t minimumImageCount)
        {
            if (!initialized_ || !vulkanBackendInitialized_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase SetMinImageCount requires the Vulkan backend");
            }
            if (frameOpen_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase cannot change image count during an open frame");
            }
            if (minimumImageCount < 2u)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "ImGui Vulkan minimum image count must be at least two");
            }

            ImGui::SetCurrentContext(context_);
            ResetImGuiVulkanFailure();
            ImGui_ImplVulkan_SetMinImageCount(minimumImageCount);
            if (Wave5ShowcaseRuntimeStatus backendStatus =
                BackendResult("ImGui Vulkan minimum-image-count update");
                !backendStatus)
            {
                return backendStatus;
            }
            minimumImageCount_ = minimumImageCount;
            return {};
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus AddTexture(
            const std::string_view opaqueUiToken,
            const VkImageView imageView,
            const VkImageLayout imageLayout)
        {
            if (!initialized_ || !vulkanBackendInitialized_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase AddTexture requires the Vulkan backend");
            }
            if (opaqueUiToken.empty() || imageView == VK_NULL_HANDLE
                || !IsShaderReadableLayout(imageLayout))
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                    "Wave 5 showcase texture requires a token, image view, and shader-readable layout");
            }
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            try
            {
                const std::string token(opaqueUiToken);
                if (textures_.contains(token))
                {
                    return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                        "Wave 5 showcase texture token is already registered: "
                            + token);
                }

                ImGui::SetCurrentContext(context_);
                ResetImGuiVulkanFailure();
                descriptorSet =
                    ImGui_ImplVulkan_AddTexture(imageView, imageLayout);
                if (descriptorSet == VK_NULL_HANDLE)
                {
                    if (Wave5ShowcaseRuntimeStatus backendStatus =
                        BackendResult("ImGui Vulkan texture registration");
                        !backendStatus)
                    {
                        return backendStatus;
                    }
                    return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                        "ImGui Vulkan texture registration returned a null descriptor set");
                }
                if (Wave5ShowcaseRuntimeStatus backendStatus =
                    BackendResult("ImGui Vulkan texture registration");
                    !backendStatus)
                {
                    ResetImGuiVulkanFailure();
                    ImGui_ImplVulkan_RemoveTexture(descriptorSet);
                    return backendStatus;
                }
                textures_.emplace(token, descriptorSet);
            }
            catch (const std::exception& exception)
            {
                if (descriptorSet != VK_NULL_HANDLE)
                {
                    ResetImGuiVulkanFailure();
                    ImGui_ImplVulkan_RemoveTexture(descriptorSet);
                }
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    std::string("Wave 5 showcase texture registration threw: ")
                        + exception.what());
            }
            catch (...)
            {
                if (descriptorSet != VK_NULL_HANDLE)
                {
                    ResetImGuiVulkanFailure();
                    ImGui_ImplVulkan_RemoveTexture(descriptorSet);
                }
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    "Wave 5 showcase texture registration threw an unknown exception");
            }
            return {};
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus RemoveTexture(
            const std::string_view opaqueUiToken)
        {
            if (!initialized_ || !vulkanBackendInitialized_)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase RemoveTexture requires the Vulkan backend");
            }
            try
            {
                const auto found = textures_.find(std::string(opaqueUiToken));
                if (found == textures_.end())
                {
                    return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidRequest,
                        "Wave 5 showcase texture token is not registered: "
                            + std::string(opaqueUiToken));
                }

                ImGui::SetCurrentContext(context_);
                ResetImGuiVulkanFailure();
                ImGui_ImplVulkan_RemoveTexture(found->second);
                textures_.erase(found);
                return BackendResult("ImGui Vulkan texture removal");
            }
            catch (const std::exception& exception)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    std::string("Wave 5 showcase texture removal threw: ")
                        + exception.what());
            }
            catch (...)
            {
                return Fail(Wave5ShowcaseRuntimeStatusCode::BackendFailure,
                    "Wave 5 showcase texture removal threw an unknown exception");
            }
        }

        [[nodiscard]] Wave5ShowcaseRuntimeStatus ClearTextures()
        {
            if (textures_.empty())
            {
                return {};
            }
            if (!vulkanBackendInitialized_ || context_ == nullptr)
            {
                textures_.clear();
                return Fail(Wave5ShowcaseRuntimeStatusCode::InvalidState,
                    "Wave 5 showcase texture registry outlived its Vulkan backend");
            }

            ImGui::SetCurrentContext(context_);
            ResetImGuiVulkanFailure();
            for (const auto& [token, descriptorSet] : textures_)
            {
                (void)token;
                ImGui_ImplVulkan_RemoveTexture(descriptorSet);
            }
            textures_.clear();
            return BackendResult("ImGui Vulkan texture registry clear");
        }

        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return initialized_;
        }

        [[nodiscard]] bool IsFrameOpen() const noexcept
        {
            return frameOpen_;
        }

        [[nodiscard]] bool WantCaptureKeyboard() const noexcept
        {
            return initialized_ && wantCaptureKeyboard_;
        }

        [[nodiscard]] bool WantCaptureMouse() const noexcept
        {
            return initialized_ && wantCaptureMouse_;
        }

        [[nodiscard]] std::optional<std::uint64_t> ResolveTextureId(
            const std::string_view opaqueUiToken) const noexcept
        {
            for (const auto& [token, descriptorSet] : textures_)
            {
                if (token == opaqueUiToken)
                {
                    const std::uint64_t textureId =
                        DescriptorSetTextureId(descriptorSet);
                    return textureId == 0u
                        ? std::nullopt
                        : std::optional<std::uint64_t>(textureId);
                }
            }
            return std::nullopt;
        }

    private:
        void CleanupAfterFailedInitialize() noexcept
        {
            try
            {
                if (context_ != nullptr)
                {
                    ImGui::SetCurrentContext(context_);
                }
                if (vulkanBackendInitialized_)
                {
                    ImGui_ImplVulkan_Shutdown();
                }
                if (glfwBackendInitialized_)
                {
                    ImGui_ImplGlfw_Shutdown();
                }
                if (context_ != nullptr)
                {
                    ImGui::DestroyContext(context_);
                }
            }
            catch (...)
            {
            }
            ResetState();
            ImGui::SetCurrentContext(nullptr);
        }

        void ResetState() noexcept
        {
            textures_.clear();
            context_ = nullptr;
            window_ = nullptr;
            colorAttachmentFormat_ = VK_FORMAT_UNDEFINED;
            minimumImageCount_ = 0u;
            initialized_ = false;
            glfwBackendInitialized_ = false;
            vulkanBackendInitialized_ = false;
            frameOpen_ = false;
            wantCaptureKeyboard_ = false;
            wantCaptureMouse_ = false;
            ResetImGuiVulkanFailure();
        }

        ImGuiContext* context_ = nullptr;
        GLFWwindow* window_ = nullptr;
        VkFormat colorAttachmentFormat_ = VK_FORMAT_UNDEFINED;
        std::uint32_t minimumImageCount_ = 0u;
        std::unordered_map<std::string, VkDescriptorSet> textures_;
        bool initialized_ = false;
        bool glfwBackendInitialized_ = false;
        bool vulkanBackendInitialized_ = false;
        bool frameOpen_ = false;
        bool wantCaptureKeyboard_ = false;
        bool wantCaptureMouse_ = false;
    };

    Wave5ShowcaseRuntime::Wave5ShowcaseRuntime()
        : impl_(std::make_unique<Impl>())
    {
    }

    Wave5ShowcaseRuntime::~Wave5ShowcaseRuntime()
    {
        if (impl_ != nullptr)
        {
            try
            {
                (void)impl_->Shutdown();
            }
            catch (...)
            {
            }
        }
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::Initialize(
        const Wave5ShowcaseRuntimeCreateInfo& createInfo)
    {
        return impl_->Initialize(createInfo);
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::Shutdown()
    {
        return impl_->Shutdown();
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::BeginFrame(
        Ui::ShowcasePanelState& panels,
        Ui::ImGuiShowcaseSelectionState& selection,
        const Ui::ImGuiShowcaseInputs& inputs)
    {
        return impl_->BeginFrame(panels, selection, inputs, *this);
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::RenderDrawData(
        const VkCommandBuffer commandBuffer)
    {
        return impl_->RenderDrawData(commandBuffer);
    }

    void Wave5ShowcaseRuntime::CancelFrame() noexcept
    {
        impl_->CancelFrame();
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::SetMinImageCount(
        const std::uint32_t minimumImageCount)
    {
        return impl_->SetMinImageCount(minimumImageCount);
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::AddTexture(
        const std::string_view opaqueUiToken,
        const VkImageView imageView,
        const VkImageLayout imageLayout)
    {
        return impl_->AddTexture(opaqueUiToken, imageView, imageLayout);
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::RemoveTexture(
        const std::string_view opaqueUiToken)
    {
        return impl_->RemoveTexture(opaqueUiToken);
    }

    Wave5ShowcaseRuntimeStatus Wave5ShowcaseRuntime::ClearTextures()
    {
        return impl_->ClearTextures();
    }

    bool Wave5ShowcaseRuntime::IsInitialized() const noexcept
    {
        return impl_->IsInitialized();
    }

    bool Wave5ShowcaseRuntime::IsFrameOpen() const noexcept
    {
        return impl_->IsFrameOpen();
    }

    bool Wave5ShowcaseRuntime::WantCaptureKeyboard() const noexcept
    {
        return impl_->WantCaptureKeyboard();
    }

    bool Wave5ShowcaseRuntime::WantCaptureMouse() const noexcept
    {
        return impl_->WantCaptureMouse();
    }

    std::optional<std::uint64_t> Wave5ShowcaseRuntime::ResolveTextureId(
        const std::string_view opaqueUiToken) const noexcept
    {
        return impl_->ResolveTextureId(opaqueUiToken);
    }
}
