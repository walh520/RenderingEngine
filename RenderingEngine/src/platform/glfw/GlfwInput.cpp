#include "platform/glfw/GlfwInput.hpp"

#include <algorithm>
#include <utility>

namespace RenderingEngine
{
    namespace
    {
        template <std::size_t Size>
        [[nodiscard]] bool IsValidCode(std::int32_t code) noexcept
        {
            return code >= 0 && static_cast<std::size_t>(code) < Size;
        }
    }

    bool GlfwPhysicalInputState::IsKeyDown(const std::int32_t physicalCode) const noexcept
    {
        return IsValidCode<kGlfwPhysicalKeyCapacity>(physicalCode)
            && keysDown[static_cast<std::size_t>(physicalCode)];
    }

    bool GlfwPhysicalInputState::IsMouseButtonDown(
        const std::int32_t physicalCode) const noexcept
    {
        return IsValidCode<kGlfwMouseButtonCapacity>(physicalCode)
            && mouseButtonsDown[static_cast<std::size_t>(physicalCode)];
    }

    void GlfwInputAccumulator::Push(GlfwRawInputEvent event)
    {
        event.sequence = nextSequence_++;
        events_.push_back(event);
    }

    void GlfwInputAccumulator::OnKey(
        const std::int32_t physicalCode,
        const PhysicalInputAction action,
        const std::uint32_t modifiers)
    {
        if (!IsValidCode<kGlfwPhysicalKeyCapacity>(physicalCode)
            || (action != PhysicalInputAction::Pressed
                && action != PhysicalInputAction::Released
                && action != PhysicalInputAction::Repeated))
        {
            return;
        }

        state_.keysDown[static_cast<std::size_t>(physicalCode)] =
            action != PhysicalInputAction::Released;
        Push({
            0,
            PhysicalInputDevice::Keyboard,
            action,
            physicalCode,
            modifiers,
            0.0,
            0.0
        });
    }

    void GlfwInputAccumulator::OnMouseButton(
        const std::int32_t physicalCode,
        const PhysicalInputAction action,
        const std::uint32_t modifiers)
    {
        if (!IsValidCode<kGlfwMouseButtonCapacity>(physicalCode)
            || (action != PhysicalInputAction::Pressed
                && action != PhysicalInputAction::Released))
        {
            return;
        }

        state_.mouseButtonsDown[static_cast<std::size_t>(physicalCode)] =
            action == PhysicalInputAction::Pressed;
        Push({
            0,
            PhysicalInputDevice::MouseButton,
            action,
            physicalCode,
            modifiers,
            0.0,
            0.0
        });
    }

    void GlfwInputAccumulator::OnCursor(const double x, const double y)
    {
        double deltaX = 0.0;
        double deltaY = 0.0;
        if (state_.focused && hasCursorReference_)
        {
            deltaX = x - state_.cursorX;
            deltaY = y - state_.cursorY;
            state_.frameMouseDeltaX += deltaX;
            state_.frameMouseDeltaY += deltaY;
        }
        state_.cursorX = x;
        state_.cursorY = y;
        hasCursorReference_ = true;

        Push({
            0,
            PhysicalInputDevice::Cursor,
            PhysicalInputAction::Moved,
            -1,
            0,
            deltaX,
            deltaY
        });
    }

    void GlfwInputAccumulator::OnScroll(
        const double x,
        const double y,
        const std::uint32_t modifiers)
    {
        if (!state_.focused)
        {
            return;
        }
        state_.frameScrollX += x;
        state_.frameScrollY += y;
        Push({
            0,
            PhysicalInputDevice::Scroll,
            PhysicalInputAction::Scrolled,
            -1,
            modifiers,
            x,
            y
        });
    }

    void GlfwInputAccumulator::OnFocus(const bool focused)
    {
        state_.focused = focused;
        hasCursorReference_ = false;
        state_.frameMouseDeltaX = 0.0;
        state_.frameMouseDeltaY = 0.0;
        state_.frameScrollX = 0.0;
        state_.frameScrollY = 0.0;
        if (!focused)
        {
            state_.keysDown.fill(false);
            state_.mouseButtonsDown.fill(false);
        }
        Push({
            0,
            PhysicalInputDevice::Focus,
            focused ? PhysicalInputAction::FocusGained : PhysicalInputAction::FocusLost,
            -1,
            0,
            0.0,
            0.0
        });
    }

    void GlfwInputAccumulator::ResetCursorReference() noexcept
    {
        hasCursorReference_ = false;
        state_.frameMouseDeltaX = 0.0;
        state_.frameMouseDeltaY = 0.0;
    }

    void GlfwInputAccumulator::ResetFrameDeltas() noexcept
    {
        state_.frameMouseDeltaX = 0.0;
        state_.frameMouseDeltaY = 0.0;
        state_.frameScrollX = 0.0;
        state_.frameScrollY = 0.0;
    }

    const GlfwPhysicalInputState& GlfwInputAccumulator::State() const noexcept
    {
        return state_;
    }

    std::vector<GlfwRawInputEvent> GlfwInputAccumulator::DrainEvents()
    {
        std::vector<GlfwRawInputEvent> drained;
        drained.swap(events_);
        return drained;
    }
}
