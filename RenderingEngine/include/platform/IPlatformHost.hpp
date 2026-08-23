#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine
{
    struct ClientExtent
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        [[nodiscard]] constexpr bool IsDrawable() const noexcept
        {
            return width != 0 && height != 0;
        }
    };

    struct PlatformCreateInfo
    {
        ClientExtent clientExtent{ 1280, 720 };
        std::string title = "Vulkan HLSL Renderer";
        bool captureCursorOnStart = true;
    };

    enum class EventPumpMode : std::uint8_t
    {
        Poll,
        Wait
    };

    enum class PhysicalKey : std::uint8_t
    {
        Escape,
        Tab,
        W,
        A,
        S,
        D,
        Space,
        LeftControl,
        RightControl,
        LeftShift,
        RightShift,
        Digit1,
        Digit2,
        Digit3,
        Count
    };

    inline constexpr std::size_t kPhysicalKeyCount = static_cast<std::size_t>(PhysicalKey::Count);

    struct RawInputFrame
    {
        std::array<bool, kPhysicalKeyCount> keysDown{};
        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        float mouseWheelDelta = 0.0f;
        bool focused = true;

        [[nodiscard]] constexpr bool IsKeyDown(PhysicalKey key) const noexcept
        {
            const std::size_t index = static_cast<std::size_t>(key);
            return index < keysDown.size() && keysDown[index];
        }
    };

    struct PlatformFrameEvents
    {
        RawInputFrame input;
        ClientExtent framebufferExtent;
        bool framebufferResized = false;
        bool closeRequested = false;
    };

    class IPlatformHost
    {
    public:
        virtual ~IPlatformHost() = default;

        IPlatformHost(const IPlatformHost&) = delete;
        IPlatformHost& operator=(const IPlatformHost&) = delete;
        IPlatformHost(IPlatformHost&&) = delete;
        IPlatformHost& operator=(IPlatformHost&&) = delete;

        [[nodiscard]] virtual std::vector<std::string> RequiredVulkanInstanceExtensions() const = 0;
        [[nodiscard]] virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) = 0;
        [[nodiscard]] virtual PlatformFrameEvents PumpEvents(EventPumpMode mode) = 0;
        [[nodiscard]] virtual ClientExtent GetFramebufferExtent() const noexcept = 0;

        virtual void SetTitle(std::string_view utf8Title) = 0;
        virtual void SetCursorCaptured(bool captured) = 0;
        virtual void RequestClientArea(ClientExtent extent) = 0;

    protected:
        IPlatformHost() = default;
    };

    using PlatformHostFactory = std::unique_ptr<IPlatformHost> (*)(const PlatformCreateInfo& createInfo);
}
