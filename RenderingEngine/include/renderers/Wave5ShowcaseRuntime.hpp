#pragma once

#include "ui/ImGuiShowcasePanels.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RenderingEngine::Renderers
{
    enum class Wave5ShowcaseRuntimeStatusCode : std::uint32_t
    {
        Ready = 0u,
        InvalidRequest,
        InvalidState,
        BackendFailure
    };

    struct Wave5ShowcaseRuntimeStatus final
    {
        Wave5ShowcaseRuntimeStatusCode code =
            Wave5ShowcaseRuntimeStatusCode::Ready;
        std::string reason;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return code == Wave5ShowcaseRuntimeStatusCode::Ready;
        }
    };

    // The caller owns every Vulkan handle and the GLFW window. The runtime
    // owns only its ImGui context, GLFW/Vulkan backends, backend-created
    // descriptor pool, and texture descriptor sets. Shutdown must run before
    // destroying the VkDevice or the GLFW platform host.
    struct Wave5ShowcaseRuntimeCreateInfo final
    {
        void* nativeWindowHandle = nullptr;
        std::uint32_t apiVersion = VK_API_VERSION_1_3;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t queueFamilyIndex = 0u;
        VkQueue queue = VK_NULL_HANDLE;
        VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        std::uint32_t minimumImageCount = 2u;
        std::uint32_t imageCount = 2u;
        std::uint32_t descriptorPoolSize = 256u;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        const VkAllocationCallbacks* allocator = nullptr;
    };

    // Single-context, render-thread owner for the Wave 5 showcase UI. The
    // renderer must already be inside a compatible Vulkan dynamic-rendering
    // scope before calling RenderDrawData; this class never begins or ends the
    // caller's rendering scope.
    class Wave5ShowcaseRuntime final
        : public Ui::IImGuiDebugTextureResolver
    {
    public:
        Wave5ShowcaseRuntime();
        ~Wave5ShowcaseRuntime() override;
        Wave5ShowcaseRuntime(const Wave5ShowcaseRuntime&) = delete;
        Wave5ShowcaseRuntime& operator=(const Wave5ShowcaseRuntime&) = delete;
        Wave5ShowcaseRuntime(Wave5ShowcaseRuntime&&) = delete;
        Wave5ShowcaseRuntime& operator=(Wave5ShowcaseRuntime&&) = delete;

        [[nodiscard]] Wave5ShowcaseRuntimeStatus Initialize(
            const Wave5ShowcaseRuntimeCreateInfo& createInfo);
        [[nodiscard]] Wave5ShowcaseRuntimeStatus Shutdown();

        // Starts both backends and ImGui, then draws all L10 showcase panels.
        // The supplied textureResolver is intentionally replaced with this
        // owner so opaque provider tokens cannot bypass its lifetime registry.
        [[nodiscard]] Wave5ShowcaseRuntimeStatus BeginFrame(
            Ui::ShowcasePanelState& panels,
            Ui::ImGuiShowcaseSelectionState& selection,
            const Ui::ImGuiShowcaseInputs& inputs);

        // Calls ImGui::Render and records only ImGui draw commands. The command
        // buffer must be recording inside dynamic rendering with the format and
        // sample count supplied to Initialize.
        [[nodiscard]] Wave5ShowcaseRuntimeStatus RenderDrawData(
            VkCommandBuffer commandBuffer);

        // Safe no-op when no frame is open. This is the acquire-out-of-date
        // path when no command buffer/dynamic-rendering scope is available.
        void CancelFrame() noexcept;

        [[nodiscard]] Wave5ShowcaseRuntimeStatus SetMinImageCount(
            std::uint32_t minimumImageCount);

        [[nodiscard]] Wave5ShowcaseRuntimeStatus AddTexture(
            std::string_view opaqueUiToken,
            VkImageView imageView,
            VkImageLayout imageLayout);
        [[nodiscard]] Wave5ShowcaseRuntimeStatus RemoveTexture(
            std::string_view opaqueUiToken);
        [[nodiscard]] Wave5ShowcaseRuntimeStatus ClearTextures();

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsFrameOpen() const noexcept;
        [[nodiscard]] bool WantCaptureKeyboard() const noexcept;
        [[nodiscard]] bool WantCaptureMouse() const noexcept;

        [[nodiscard]] std::optional<std::uint64_t> ResolveTextureId(
            std::string_view opaqueUiToken) const noexcept override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
