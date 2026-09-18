#pragma once

#include "platform/IPlatformHost.hpp"
#include "platform/glfw/GlfwInput.hpp"

namespace RenderingEngine
{
    struct GlfwWindowState
    {
        ClientExtent framebufferExtent;
        float contentScaleX = 1.0f;
        float contentScaleY = 1.0f;
        bool focused = true;
        bool iconified = false;
        bool cursorCaptured = false;
        bool rawMouseMotion = false;
    };

    class IGlfwPlatformStateSource
    {
    public:
        virtual ~IGlfwPlatformStateSource() = default;
        [[nodiscard]] virtual std::vector<GlfwRawInputEvent> DrainRawInputEvents() = 0;
        [[nodiscard]] virtual const GlfwPhysicalInputState& PhysicalInputState() const noexcept = 0;
        [[nodiscard]] virtual GlfwWindowState WindowState() const noexcept = 0;
        // Opaque GLFWwindow pointer for composition-owned integrations such as
        // the ImGui GLFW backend. Consumers must not destroy the window and
        // must release dependent integrations before the platform host.
        [[nodiscard]] virtual void* NativeWindowHandle() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IPlatformHost> CreateGlfwPlatformHost(
        const PlatformCreateInfo& createInfo);

    // Returns non-null only for a host produced by CreateGlfwPlatformHost.
    [[nodiscard]] IGlfwPlatformStateSource* QueryGlfwPlatformStateSource(
        IPlatformHost& host) noexcept;
}
