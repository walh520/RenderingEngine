#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR

#include "platform/win32/Win32PlatformHost.hpp"

#include <Windows.h>
#include <windowsx.h>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace RenderingEngine
{
    namespace
    {
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int sourceLength = static_cast<int>(text.size());
            const int requiredLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.data(),
                sourceLength,
                nullptr,
                0);
            if (requiredLength == 0)
            {
                throw std::runtime_error("Invalid UTF-8 text passed to the Win32 platform host.");
            }

            std::wstring result(static_cast<std::size_t>(requiredLength), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    sourceLength,
                    result.data(),
                    requiredLength) == 0)
            {
                throw std::runtime_error("MultiByteToWideChar failed.");
            }
            return result;
        }

        class Win32PlatformHost final : public IPlatformHost
        {
        public:
            explicit Win32PlatformHost(const PlatformCreateInfo& createInfo)
            {
                try
                {
                    CreateNativeWindow(createInfo);
                }
                catch (...)
                {
                    Cleanup();
                    throw;
                }
            }

            ~Win32PlatformHost() override
            {
                Cleanup();
            }

            [[nodiscard]] std::vector<std::string> RequiredVulkanInstanceExtensions() const override
            {
                return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
            }

            [[nodiscard]] VkSurfaceKHR CreateVulkanSurface(VkInstance instance) override
            {
                if (instance == VK_NULL_HANDLE || window_ == nullptr || instance_ == nullptr)
                {
                    throw std::invalid_argument("Cannot create a Win32 Vulkan surface without a window and instance.");
                }

                VkWin32SurfaceCreateInfoKHR createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                createInfo.hinstance = instance_;
                createInfo.hwnd = window_;

                VkSurfaceKHR surface = VK_NULL_HANDLE;
                const VkResult result = vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface);
                if (result != VK_SUCCESS)
                {
                    throw std::runtime_error(
                        "vkCreateWin32SurfaceKHR failed with VkResult "
                        + std::to_string(static_cast<int>(result)) + '.');
                }
                return surface;
            }

            [[nodiscard]] PlatformFrameEvents PumpEvents(EventPumpMode mode) override
            {
                if (mode == EventPumpMode::Wait && !closeRequested_)
                {
                    WaitMessage();
                }

                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
                {
                    if (message.message == WM_QUIT)
                    {
                        closeRequested_ = true;
                        continue;
                    }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }

                PlatformFrameEvents events{};
                events.framebufferExtent = GetFramebufferExtent();
                events.framebufferResized = std::exchange(framebufferResized_, false);
                events.closeRequested = closeRequested_;
                events.input.focused = focused_ && GetForegroundWindow() == window_;
                events.input.mouseDeltaX = std::exchange(mouseDeltaX_, 0.0f);
                events.input.mouseDeltaY = std::exchange(mouseDeltaY_, 0.0f);
                events.input.mouseWheelDelta = std::exchange(mouseWheelDelta_, 0.0f);

                if (events.input.focused)
                {
                    static constexpr std::array<int, kPhysicalKeyCount> virtualKeys{
                        VK_ESCAPE,
                        VK_TAB,
                        'W',
                        'A',
                        'S',
                        'D',
                        VK_SPACE,
                        VK_LCONTROL,
                        VK_RCONTROL,
                        VK_LSHIFT,
                        VK_RSHIFT,
                        '1',
                        '2',
                        '3'
                    };
                    for (std::size_t index = 0; index < virtualKeys.size(); ++index)
                    {
                        events.input.keysDown[index] =
                            (GetAsyncKeyState(virtualKeys[index]) & 0x8000) != 0;
                    }
                }
                return events;
            }

            [[nodiscard]] ClientExtent GetFramebufferExtent() const noexcept override
            {
                if (window_ == nullptr)
                {
                    return {};
                }

                RECT rectangle{};
                if (GetClientRect(window_, &rectangle) == FALSE)
                {
                    return {};
                }
                return {
                    static_cast<std::uint32_t>(rectangle.right - rectangle.left),
                    static_cast<std::uint32_t>(rectangle.bottom - rectangle.top)
                };
            }

            void SetTitle(std::string_view utf8Title) override
            {
                if (window_ != nullptr)
                {
                    const std::wstring title = Utf8ToWide(utf8Title);
                    if (SetWindowTextW(window_, title.c_str()) == FALSE)
                    {
                        throw std::runtime_error("SetWindowTextW failed.");
                    }
                }
            }

            void SetCursorCaptured(bool captured) override
            {
                if (cursorCaptured_ == captured)
                {
                    return;
                }
                cursorCaptured_ = captured;

                if (captured && window_ != nullptr)
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

            void RequestClientArea(ClientExtent extent) override
            {
                if (!extent.IsDrawable())
                {
                    throw std::invalid_argument("The Win32 client area must be drawable.");
                }

                RECT rectangle{
                    0,
                    0,
                    static_cast<LONG>(extent.width),
                    static_cast<LONG>(extent.height)
                };
                if (AdjustWindowRectEx(&rectangle, kWindowStyle, FALSE, 0) == FALSE)
                {
                    throw std::runtime_error("AdjustWindowRectEx failed.");
                }
                if (SetWindowPos(
                        window_,
                        nullptr,
                        0,
                        0,
                        rectangle.right - rectangle.left,
                        rectangle.bottom - rectangle.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
                {
                    throw std::runtime_error("SetWindowPos failed while resizing the client area.");
                }
            }

        private:
            void CreateNativeWindow(const PlatformCreateInfo& createInfo)
            {
                if (!createInfo.clientExtent.IsDrawable())
                {
                    throw std::invalid_argument("The initial Win32 client area must be drawable.");
                }

                instance_ = GetModuleHandleW(nullptr);
                if (instance_ == nullptr)
                {
                    throw std::runtime_error("GetModuleHandleW failed.");
                }

                WNDCLASSEXW windowClass{};
                windowClass.cbSize = sizeof(windowClass);
                windowClass.style = CS_HREDRAW | CS_VREDRAW;
                windowClass.lpfnWndProc = WindowProcedure;
                windowClass.hInstance = instance_;
                windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                windowClass.lpszClassName = kWindowClassName;
                classAtom_ = RegisterClassExW(&windowClass);
                if (classAtom_ == 0)
                {
                    throw std::runtime_error(
                        "RegisterClassExW failed with Win32 error "
                        + std::to_string(GetLastError()) + '.');
                }

                RECT rectangle{
                    0,
                    0,
                    static_cast<LONG>(createInfo.clientExtent.width),
                    static_cast<LONG>(createInfo.clientExtent.height)
                };
                if (AdjustWindowRectEx(&rectangle, kWindowStyle, FALSE, 0) == FALSE)
                {
                    throw std::runtime_error("AdjustWindowRectEx failed.");
                }

                const std::wstring title = Utf8ToWide(createInfo.title);
                window_ = CreateWindowExW(
                    0,
                    kWindowClassName,
                    title.c_str(),
                    kWindowStyle,
                    CW_USEDEFAULT,
                    CW_USEDEFAULT,
                    rectangle.right - rectangle.left,
                    rectangle.bottom - rectangle.top,
                    nullptr,
                    nullptr,
                    instance_,
                    this);
                if (window_ == nullptr)
                {
                    throw std::runtime_error(
                        "CreateWindowExW failed with Win32 error "
                        + std::to_string(GetLastError()) + '.');
                }

                RAWINPUTDEVICE rawMouse{};
                rawMouse.usUsagePage = 0x01;
                rawMouse.usUsage = 0x02;
                rawMouse.hwndTarget = window_;
                if (RegisterRawInputDevices(&rawMouse, 1, sizeof(rawMouse)) == FALSE)
                {
                    throw std::runtime_error("RegisterRawInputDevices failed.");
                }

                ShowWindow(window_, SW_SHOW);
                UpdateWindow(window_);
                // WM_SIZE is delivered while CreateWindowExW/ShowWindow establish
                // the requested client area. The renderer queries that final
                // extent before creating its first swapchain, so this is not a
                // runtime resize and must not discard the first rendered sample.
                framebufferResized_ = false;
                SetCursorCaptured(createInfo.captureCursorOnStart);
            }

            void Cleanup() noexcept
            {
                if (cursorCaptured_)
                {
                    SetCursorCaptured(false);
                }
                if (window_ != nullptr)
                {
                    DestroyWindow(window_);
                    window_ = nullptr;
                }
                if (classAtom_ != 0 && instance_ != nullptr)
                {
                    UnregisterClassW(kWindowClassName, instance_);
                    classAtom_ = 0;
                }
            }

            void UpdateCursorClip() const noexcept
            {
                if (!cursorCaptured_ || !focused_ || window_ == nullptr)
                {
                    return;
                }

                RECT rectangle{};
                if (GetClientRect(window_, &rectangle) == FALSE)
                {
                    return;
                }
                POINT upperLeft{ rectangle.left, rectangle.top };
                POINT lowerRight{ rectangle.right, rectangle.bottom };
                if (ClientToScreen(window_, &upperLeft) == FALSE
                    || ClientToScreen(window_, &lowerRight) == FALSE)
                {
                    return;
                }
                rectangle = { upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y };
                ClipCursor(&rectangle);
            }

            static LRESULT CALLBACK WindowProcedure(
                HWND window,
                UINT message,
                WPARAM wordParameter,
                LPARAM longParameter)
            {
                Win32PlatformHost* self = reinterpret_cast<Win32PlatformHost*>(
                    GetWindowLongPtrW(window, GWLP_USERDATA));
                if (message == WM_NCCREATE)
                {
                    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(longParameter);
                    self = static_cast<Win32PlatformHost*>(create->lpCreateParams);
                    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }
                if (self != nullptr)
                {
                    return self->HandleWindowMessage(window, message, wordParameter, longParameter);
                }
                return DefWindowProcW(window, message, wordParameter, longParameter);
            }

            LRESULT HandleWindowMessage(
                HWND window,
                UINT message,
                WPARAM wordParameter,
                LPARAM longParameter) noexcept
            {
                switch (message)
                {
                case WM_CLOSE:
                    closeRequested_ = true;
                    return 0;
                case WM_DESTROY:
                    closeRequested_ = true;
                    PostQuitMessage(0);
                    return 0;
                case WM_SIZE:
                    framebufferResized_ = true;
                    UpdateCursorClip();
                    return 0;
                case WM_INPUT:
                    if (cursorCaptured_)
                    {
                        RAWINPUT rawInput{};
                        UINT byteCount = sizeof(rawInput);
                        const UINT copiedBytes = GetRawInputData(
                            reinterpret_cast<HRAWINPUT>(longParameter),
                            RID_INPUT,
                            &rawInput,
                            &byteCount,
                            sizeof(RAWINPUTHEADER));
                        if (copiedBytes != UINT_MAX
                            && copiedBytes == byteCount
                            && rawInput.header.dwType == RIM_TYPEMOUSE)
                        {
                            mouseDeltaX_ += static_cast<float>(rawInput.data.mouse.lLastX);
                            mouseDeltaY_ += static_cast<float>(rawInput.data.mouse.lLastY);
                        }
                    }
                    return 0;
                case WM_MOUSEWHEEL:
                    mouseWheelDelta_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wordParameter))
                        / static_cast<float>(WHEEL_DELTA);
                    return 0;
                case WM_ACTIVATE:
                    focused_ = LOWORD(wordParameter) != WA_INACTIVE;
                    if (focused_)
                    {
                        UpdateCursorClip();
                    }
                    else
                    {
                        ClipCursor(nullptr);
                    }
                    return 0;
                case WM_SETCURSOR:
                    if (cursorCaptured_ && LOWORD(longParameter) == HTCLIENT)
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

            static constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
            static constexpr const wchar_t* kWindowClassName = L"RenderingEngineVulkanWindow";

            HWND window_ = nullptr;
            HINSTANCE instance_ = nullptr;
            ATOM classAtom_ = 0;
            bool cursorCaptured_ = false;
            bool focused_ = true;
            bool framebufferResized_ = false;
            bool closeRequested_ = false;
            float mouseDeltaX_ = 0.0f;
            float mouseDeltaY_ = 0.0f;
            float mouseWheelDelta_ = 0.0f;
        };
    }

    std::unique_ptr<IPlatformHost> CreateWin32PlatformHost(const PlatformCreateInfo& createInfo)
    {
        return std::make_unique<Win32PlatformHost>(createInfo);
    }
}
