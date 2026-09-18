#include "platform/glfw/GlfwInput.hpp"

#include <iostream>
#include <string_view>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "L1 GLFW input test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] int ExitCode() const noexcept
        {
            return failures_ == 0 ? 0 : 1;
        }

    private:
        int failures_ = 0;
    };
}

int main()
{
    using namespace RenderingEngine;

    TestContext tests;
    GlfwInputAccumulator input;
    input.OnKey(66, PhysicalInputAction::Pressed, 0u);
    input.OnKey(66, PhysicalInputAction::Repeated, 0u);
    input.OnKey(66, PhysicalInputAction::Released, 0u);
    const std::vector<GlfwRawInputEvent> keyEvents = input.DrainEvents();
    tests.Expect(keyEvents.size() == 3u
            && keyEvents[0].sequence < keyEvents[1].sequence
            && keyEvents[1].sequence < keyEvents[2].sequence,
        "raw key events must preserve callback FIFO order");
    tests.Expect(keyEvents.size() == 3u
            && keyEvents[0].action == PhysicalInputAction::Pressed
            && keyEvents[1].action == PhysicalInputAction::Repeated
            && keyEvents[2].action == PhysicalInputAction::Released,
        "press, repeat, and release must remain distinguishable for the L10 adapter");
    tests.Expect(!input.State().IsKeyDown(66),
        "a released physical key must not remain latched");

    input.OnCursor(10.0, 20.0);
    input.OnCursor(13.5, 18.0);
    tests.Expect(input.State().frameMouseDeltaX == 3.5
            && input.State().frameMouseDeltaY == -2.0,
        "cursor callbacks must accumulate frame-local relative motion");
    input.OnScroll(1.0, -2.0, 4u);
    tests.Expect(input.State().frameScrollX == 1.0
            && input.State().frameScrollY == -2.0,
        "scroll callbacks must retain both axes");
    const std::vector<GlfwRawInputEvent> pointerEvents = input.DrainEvents();
    tests.Expect(!pointerEvents.empty() && pointerEvents.back().modifiers == 4u,
        "scroll events must retain callback-time modifiers for semantic chords");
    input.ResetFrameDeltas();
    tests.Expect(input.State().frameMouseDeltaX == 0.0
            && input.State().frameMouseDeltaY == 0.0
            && input.State().frameScrollX == 0.0
            && input.State().frameScrollY == 0.0,
        "frame consumption must clear deltas without discarding held state");

    input.OnKey(87, PhysicalInputAction::Pressed, 0u);
    input.OnMouseButton(0, PhysicalInputAction::Pressed, 0u);
    input.OnFocus(false);
    tests.Expect(!input.State().focused
            && !input.State().IsKeyDown(87)
            && !input.State().IsMouseButtonDown(0),
        "focus loss must clear held keyboard and mouse state");
    input.OnScroll(0.0, 4.0);
    tests.Expect(input.State().frameScrollY == 0.0,
        "unfocused scroll callbacks must not leak into a later frame");
    input.OnFocus(true);
    input.OnCursor(100.0, 100.0);
    tests.Expect(input.State().frameMouseDeltaX == 0.0
            && input.State().frameMouseDeltaY == 0.0,
        "the first cursor sample after focus regain must establish a reference only");

    input.OnKey(-1, PhysicalInputAction::Pressed, 0u);
    input.OnKey(
        static_cast<std::int32_t>(kGlfwPhysicalKeyCapacity),
        PhysicalInputAction::Pressed,
        0u);
    tests.Expect(!input.State().IsKeyDown(-1)
            && !input.State().IsKeyDown(
                static_cast<std::int32_t>(kGlfwPhysicalKeyCapacity)),
        "out-of-range physical codes must be ignored safely");

    if (tests.ExitCode() == 0)
    {
        std::cout << "L1 GLFW raw-input Wave 1 checks passed.\n";
    }
    return tests.ExitCode();
}
