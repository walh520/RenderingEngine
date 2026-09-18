#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "platform/glfw/GlfwPlatformHost.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace RenderingEngine
{
    namespace
    {
        [[nodiscard]] PhysicalInputAction TranslateAction(const int action)
        {
            switch (action)
            {
            case GLFW_PRESS: return PhysicalInputAction::Pressed;
            case GLFW_RELEASE: return PhysicalInputAction::Released;
            case GLFW_REPEAT: return PhysicalInputAction::Repeated;
            default: throw std::invalid_argument("Unsupported GLFW input action.");
            }
        }

        class GlfwPlatformHost final
            : public IPlatformHost
            , public IGlfwPlatformStateSource
        {
        public:
            explicit GlfwPlatformHost(const PlatformCreateInfo& createInfo)
            {
                if (!createInfo.clientExtent.IsDrawable())
                {
                    throw std::invalid_argument("The initial GLFW client area must be drawable.");
                }
                if (glfwInit() != GLFW_TRUE)
                {
                    throw std::runtime_error("glfwInit failed.");
                }
                ownsGlfw_ = true;
                if (glfwVulkanSupported() != GLFW_TRUE)
                {
                    Cleanup();
                    throw std::runtime_error(
                        "GLFW reports that the Vulkan loader and required platform support are unavailable.");
                }

                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
                glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
                glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
                window_ = glfwCreateWindow(
                    static_cast<int>(createInfo.clientExtent.width),
                    static_cast<int>(createInfo.clientExtent.height),
                    createInfo.title.c_str(),
                    nullptr,
                    nullptr);
                if (window_ == nullptr)
                {
                    Cleanup();
                    throw std::runtime_error("glfwCreateWindow failed.");
                }

                glfwSetWindowUserPointer(window_, this);
                glfwSetKeyCallback(window_, KeyCallback);
                glfwSetMouseButtonCallback(window_, MouseButtonCallback);
                glfwSetCursorPosCallback(window_, CursorCallback);
                glfwSetScrollCallback(window_, ScrollCallback);
                glfwSetWindowFocusCallback(window_, FocusCallback);
                glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
                glfwSetWindowContentScaleCallback(window_, ContentScaleCallback);
                glfwSetWindowIconifyCallback(window_, IconifyCallback);

                glfwGetWindowContentScale(window_, &contentScaleX_, &contentScaleY_);
                focused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
                iconified_ = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
                input_.OnFocus(focused_);
                (void)input_.DrainEvents();
                SetCursorCaptured(createInfo.captureCursorOnStart);
            }

            ~GlfwPlatformHost() override
            {
                Cleanup();
            }

            [[nodiscard]] std::vector<std::string> RequiredVulkanInstanceExtensions() const override
            {
                std::uint32_t count = 0;
                const char** names = glfwGetRequiredInstanceExtensions(&count);
                if (names == nullptr || count == 0)
                {
                    throw std::runtime_error("GLFW did not publish Vulkan instance extensions.");
                }
                std::vector<std::string> extensions;
                extensions.reserve(count);
                for (std::uint32_t index = 0; index < count; ++index)
                {
                    extensions.emplace_back(names[index]);
                }
                return extensions;
            }

            [[nodiscard]] VkSurfaceKHR CreateVulkanSurface(const VkInstance instance) override
            {
                if (instance == VK_NULL_HANDLE || window_ == nullptr)
                {
                    throw std::invalid_argument("Cannot create a GLFW Vulkan surface without a window and instance.");
                }
                VkSurfaceKHR surface = VK_NULL_HANDLE;
                const VkResult result = glfwCreateWindowSurface(instance, window_, nullptr, &surface);
                if (result != VK_SUCCESS)
                {
                    throw std::runtime_error(
                        "glfwCreateWindowSurface failed with VkResult "
                        + std::to_string(static_cast<int>(result)) + '.');
                }
                return surface;
            }

            [[nodiscard]] PlatformFrameEvents PumpEvents(const EventPumpMode mode) override
            {
                if (mode == EventPumpMode::Wait && glfwWindowShouldClose(window_) == GLFW_FALSE)
                {
                    glfwWaitEvents();
                }
                else
                {
                    glfwPollEvents();
                }

                PlatformFrameEvents events;
                events.framebufferExtent = GetFramebufferExtent();
                events.framebufferResized = std::exchange(framebufferResized_, false);
                events.closeRequested = glfwWindowShouldClose(window_) == GLFW_TRUE;

                const GlfwPhysicalInputState& state = input_.State();
                events.input.focused = focused_;
                events.input.mouseDeltaX = static_cast<float>(state.frameMouseDeltaX);
                events.input.mouseDeltaY = static_cast<float>(state.frameMouseDeltaY);
                events.input.mouseWheelDelta = static_cast<float>(state.frameScrollY);
                static constexpr std::array<int, kPhysicalKeyCount> keys{
                    GLFW_KEY_ESCAPE,
                    GLFW_KEY_TAB,
                    GLFW_KEY_W,
                    GLFW_KEY_A,
                    GLFW_KEY_S,
                    GLFW_KEY_D,
                    GLFW_KEY_SPACE,
                    GLFW_KEY_LEFT_CONTROL,
                    GLFW_KEY_RIGHT_CONTROL,
                    GLFW_KEY_LEFT_SHIFT,
                    GLFW_KEY_RIGHT_SHIFT,
                    GLFW_KEY_1,
                    GLFW_KEY_2,
                    GLFW_KEY_3
                };
                for (std::size_t index = 0; index < keys.size(); ++index)
                {
                    events.input.keysDown[index] = focused_ && state.IsKeyDown(keys[index]);
                }
                input_.ResetFrameDeltas();
                return events;
            }

            [[nodiscard]] ClientExtent GetFramebufferExtent() const noexcept override
            {
                if (window_ == nullptr || iconified_)
                {
                    return {};
                }
                int width = 0;
                int height = 0;
                glfwGetFramebufferSize(window_, &width, &height);
                return {
                    width > 0 ? static_cast<std::uint32_t>(width) : 0u,
                    height > 0 ? static_cast<std::uint32_t>(height) : 0u
                };
            }

            void SetTitle(const std::string_view utf8Title) override
            {
                if (window_ != nullptr)
                {
                    const std::string title(utf8Title);
                    glfwSetWindowTitle(window_, title.c_str());
                }
            }

            void SetCursorCaptured(const bool captured) override
            {
                if (window_ == nullptr)
                {
                    return;
                }
                cursorCaptured_ = captured;
                glfwSetInputMode(
                    window_,
                    GLFW_CURSOR,
                    captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
                const bool rawMouseSupported = glfwRawMouseMotionSupported() == GLFW_TRUE;
                rawMouseMotion_ = captured && rawMouseSupported;
                if (rawMouseSupported)
                {
                    glfwSetInputMode(
                        window_,
                        GLFW_RAW_MOUSE_MOTION,
                        rawMouseMotion_ ? GLFW_TRUE : GLFW_FALSE);
                }
                input_.ResetCursorReference();
            }

            void RequestClientArea(const ClientExtent extent) override
            {
                if (!extent.IsDrawable()
                    || extent.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                    || extent.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
                {
                    throw std::invalid_argument("The requested GLFW client area is invalid.");
                }
                glfwSetWindowSize(
                    window_,
                    static_cast<int>(extent.width),
                    static_cast<int>(extent.height));
            }

            [[nodiscard]] std::vector<GlfwRawInputEvent> DrainRawInputEvents() override
            {
                return input_.DrainEvents();
            }

            [[nodiscard]] const GlfwPhysicalInputState& PhysicalInputState() const noexcept override
            {
                return input_.State();
            }

            [[nodiscard]] GlfwWindowState WindowState() const noexcept override
            {
                return {
                    GetFramebufferExtent(),
                    contentScaleX_,
                    contentScaleY_,
                    focused_,
                    iconified_,
                    cursorCaptured_,
                    rawMouseMotion_
                };
            }

            [[nodiscard]] void* NativeWindowHandle() const noexcept override
            {
                return window_;
            }

        private:
            [[nodiscard]] static GlfwPlatformHost* From(GLFWwindow* window) noexcept
            {
                return static_cast<GlfwPlatformHost*>(glfwGetWindowUserPointer(window));
            }

            static void KeyCallback(GLFWwindow* window, int key, int, int action, int modifiers)
            {
                if (GlfwPlatformHost* host = From(window);
                    host != nullptr && action >= GLFW_RELEASE && action <= GLFW_REPEAT)
                {
                    host->input_.OnKey(key, TranslateAction(action), static_cast<std::uint32_t>(modifiers));
                }
            }

            static void MouseButtonCallback(GLFWwindow* window, int button, int action, int modifiers)
            {
                if (GlfwPlatformHost* host = From(window);
                    host != nullptr && (action == GLFW_PRESS || action == GLFW_RELEASE))
                {
                    host->input_.OnMouseButton(
                        button,
                        TranslateAction(action),
                        static_cast<std::uint32_t>(modifiers));
                }
            }

            static void CursorCallback(GLFWwindow* window, double x, double y)
            {
                if (GlfwPlatformHost* host = From(window); host != nullptr)
                {
                    host->input_.OnCursor(x, y);
                }
            }

            static void ScrollCallback(GLFWwindow* window, double x, double y)
            {
                if (GlfwPlatformHost* host = From(window); host != nullptr)
                {
                    std::uint32_t modifiers = 0u;
                    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                        || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
                    {
                        modifiers |= GLFW_MOD_SHIFT;
                    }
                    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                        || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
                    {
                        modifiers |= GLFW_MOD_CONTROL;
                    }
                    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                        || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
                    {
                        modifiers |= GLFW_MOD_ALT;
                    }
                    host->input_.OnScroll(x, y, modifiers);
                }
            }

            static void FocusCallback(GLFWwindow* window, int focused)
            {
                if (GlfwPlatformHost* host = From(window); host != nullptr)
                {
                    host->focused_ = focused == GLFW_TRUE;
                    host->input_.OnFocus(host->focused_);
                    if (!host->focused_ && host->cursorCaptured_)
                    {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    }
                    else if (host->focused_ && host->cursorCaptured_)
                    {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        host->input_.ResetCursorReference();
                    }
                }
            }

            static void FramebufferSizeCallback(GLFWwindow* window, int, int)
            {
                if (GlfwPlatformHost* host = From(window); host != nullptr)
                {
                    host->framebufferResized_ = true;
                }
            }

            static void ContentScaleCallback(GLFWwindow* window, float x, float y)
            {
                if (GlfwPlatformHost* host = From(window); host != nullptr)
                {
                    host->contentScaleX_ = x;
                    host->contentScaleY_ = y;
                }
            }

            static void IconifyCallback(GLFWwindow* window, int iconified)
            {
                if (GlfwPlatformHost* host = From(window); host != nullptr)
                {
                    host->iconified_ = iconified == GLFW_TRUE;
                    host->framebufferResized_ = true;
                }
            }

            void Cleanup() noexcept
            {
                if (window_ != nullptr)
                {
                    glfwDestroyWindow(window_);
                    window_ = nullptr;
                }
                if (ownsGlfw_)
                {
                    glfwTerminate();
                    ownsGlfw_ = false;
                }
            }

            GLFWwindow* window_ = nullptr;
            GlfwInputAccumulator input_;
            float contentScaleX_ = 1.0f;
            float contentScaleY_ = 1.0f;
            bool ownsGlfw_ = false;
            bool focused_ = true;
            bool iconified_ = false;
            bool cursorCaptured_ = false;
            bool rawMouseMotion_ = false;
            bool framebufferResized_ = false;
        };
    }

    std::unique_ptr<IPlatformHost> CreateGlfwPlatformHost(
        const PlatformCreateInfo& createInfo)
    {
        return std::make_unique<GlfwPlatformHost>(createInfo);
    }

    IGlfwPlatformStateSource* QueryGlfwPlatformStateSource(IPlatformHost& host) noexcept
    {
        return dynamic_cast<IGlfwPlatformStateSource*>(&host);
    }
}
