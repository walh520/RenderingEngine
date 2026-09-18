#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine
{
    inline constexpr std::size_t kGlfwPhysicalKeyCapacity = 512u;
    inline constexpr std::size_t kGlfwMouseButtonCapacity = 16u;

    enum class PhysicalInputDevice : std::uint8_t
    {
        Keyboard,
        MouseButton,
        Cursor,
        Scroll,
        Focus
    };

    enum class PhysicalInputAction : std::uint8_t
    {
        Pressed,
        Released,
        Repeated,
        Moved,
        Scrolled,
        FocusGained,
        FocusLost
    };

    struct GlfwRawInputEvent
    {
        std::uint64_t sequence = 0;
        PhysicalInputDevice device = PhysicalInputDevice::Keyboard;
        PhysicalInputAction action = PhysicalInputAction::Released;
        std::int32_t physicalCode = -1;
        std::uint32_t modifiers = 0;
        double valueX = 0.0;
        double valueY = 0.0;
    };

    struct GlfwPhysicalInputState
    {
        std::array<bool, kGlfwPhysicalKeyCapacity> keysDown{};
        std::array<bool, kGlfwMouseButtonCapacity> mouseButtonsDown{};
        double cursorX = 0.0;
        double cursorY = 0.0;
        double frameMouseDeltaX = 0.0;
        double frameMouseDeltaY = 0.0;
        double frameScrollX = 0.0;
        double frameScrollY = 0.0;
        bool focused = true;

        [[nodiscard]] bool IsKeyDown(std::int32_t physicalCode) const noexcept;
        [[nodiscard]] bool IsMouseButtonDown(std::int32_t physicalCode) const noexcept;
    };

    // Callback-facing physical input accumulator. It deliberately retains GLFW
    // integer codes and never binds them to L10 semantic actions.
    class GlfwInputAccumulator final
    {
    public:
        void OnKey(
            std::int32_t physicalCode,
            PhysicalInputAction action,
            std::uint32_t modifiers);
        void OnMouseButton(
            std::int32_t physicalCode,
            PhysicalInputAction action,
            std::uint32_t modifiers);
        void OnCursor(double x, double y);
        void OnScroll(double x, double y, std::uint32_t modifiers = 0);
        void OnFocus(bool focused);
        void ResetCursorReference() noexcept;
        void ResetFrameDeltas() noexcept;

        [[nodiscard]] const GlfwPhysicalInputState& State() const noexcept;
        [[nodiscard]] std::vector<GlfwRawInputEvent> DrainEvents();

    private:
        void Push(GlfwRawInputEvent event);

        GlfwPhysicalInputState state_;
        std::vector<GlfwRawInputEvent> events_;
        std::uint64_t nextSequence_ = 1;
        bool hasCursorReference_ = false;
    };
}
