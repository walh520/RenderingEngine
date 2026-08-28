#include "ui/ActionMap.hpp"

#include <array>

namespace RenderingEngine::Ui
{
    namespace
    {
        using enum ActionActivation;
        using enum InputKey;
        using enum InputModifier;
        using enum SemanticAction;

        constexpr auto kBindings = std::to_array<ActionBinding>({
            { W, None, None, MoveForward, WhileHeld, "W", "Move camera forward" },
            { A, None, None, MoveLeft, WhileHeld, "A", "Move camera left" },
            { S, None, None, MoveBackward, WhileHeld, "S", "Move camera backward" },
            { D, None, None, MoveRight, WhileHeld, "D", "Move camera right" },
            { Q, None, None, MoveDown, WhileHeld, "Q", "Move camera down" },
            { E, None, None, MoveUp, WhileHeld, "E", "Move camera up" },
            { LeftShift, None, None, FastMovementModifier, WhileHeld,
                "Left Shift", "Use fast camera movement" },
            { LeftControl, None, None, FineMovementModifier, WhileHeld,
                "Left Ctrl", "Use fine camera movement" },
            { MouseMove, None, None, Look, Axis2D, "Mouse", "Look while the pointer is captured" },
            { MouseWheel, Alt, None, AdjustVerticalFov, Axis1D,
                "Alt + Wheel", "Adjust vertical field of view" },
            { MouseWheel, None, Alt, AdjustMovementSpeed, Axis1D,
                "Wheel", "Adjust camera movement speed" },
            { Tab, None, None, TogglePointerCapture, PressOnly,
                "Tab", "Capture or release the pointer" },
            { Escape, None, None, ReleasePointerCapture, PressOnly,
                "Esc", "Release the pointer without exiting" },
            { F4, Alt, None, RequestExit, PressOnly, "Alt + F4", "Exit the application" },
            { Home, None, None, ResetShowcaseCamera, PressOnly,
                "Home", "Restore the current showcase camera" },
            { P, None, None, ToggleAnimationPause, PressOnly,
                "P", "Pause or resume scene animation" },
            { O, None, None, StepAnimationFrame, PressOnly,
                "O", "Advance one animation frame while paused" },

            { B, None, Shift, CycleBackendForward, PressOnly,
                "B", "Cycle traversal backend forward" },
            { B, Shift, None, CycleBackendBackward, PressOnly,
                "Shift + B", "Cycle traversal backend backward" },
            { I, None, Shift, CycleIntegratorForward, PressOnly,
                "I", "Cycle integrator forward" },
            { I, Shift, None, CycleIntegratorBackward, PressOnly,
                "Shift + I", "Cycle integrator backward" },
            { L, None, Shift, CycleLightSamplingForward, PressOnly,
                "L", "Cycle explicit light-sampling presets forward" },
            { L, Shift, None, CycleLightSamplingBackward, PressOnly,
                "Shift + L", "Cycle explicit light-sampling presets backward" },
            { N, None, Shift, CycleReconstructionForward, PressOnly,
                "N", "Cycle reconstruction forward" },
            { N, Shift, None, CycleReconstructionBackward, PressOnly,
                "Shift + N", "Cycle reconstruction backward" },
            { V, None, Shift, CycleDebugViewForward, PressOnly,
                "V", "Cycle debug view forward" },
            { V, Shift, None, CycleDebugViewBackward, PressOnly,
                "Shift + V", "Cycle debug view backward" },

            { R, None, None, ResetHistories, PressOnly,
                "R", "Request accumulation, temporal, and reservoir resets" },
            { K, None, None, ToggleComparisonLock, PressOnly,
                "K", "Toggle fixed camera, seed, and animation origin" },
            { LeftBracket, None, None, DecreaseMaximumBounce, PressOnly,
                "[", "Decrease maximum bounce" },
            { RightBracket, None, None, IncreaseMaximumBounce, PressOnly,
                "]", "Increase maximum bounce" },
            { Minus, None, None, DecreaseExposure, PressOnly,
                "-", "Decrease exposure" },
            { Equal, None, None, IncreaseExposure, PressOnly,
                "=", "Increase exposure" },
            { PageDown, None, None, DecreaseRenderScale, PressOnly,
                "PageDown", "Decrease internal render scale" },
            { PageUp, None, None, IncreaseRenderScale, PressOnly,
                "PageUp", "Increase internal render scale" },
            { F1, None, None, ToggleHelp, PressOnly, "F1", "Toggle help and algorithm explanation" },
            { F2, None, None, ToggleAlgorithmPanel, PressOnly, "F2", "Toggle algorithm and mode panel" },
            { F3, None, None, ToggleProfilerPanel, PressOnly, "F3", "Toggle profiler panel shell" },
            { F4, None, Alt, RequestCapture, PressOnly, "F4", "Request PNG, EXR, and metadata capture" },
            { F5, None, None, RequestShaderReload, PressOnly, "F5", "Request transactional shader reload" },
            { F6, None, None, ToggleSplitScreenComparison, PressOnly,
                "F6", "Toggle fixed-seed split-screen comparison" },
            { F7, None, None, ToggleDebugLegend, PressOnly, "F7", "Toggle debug overlay legend" },
            { F8, None, None, RequestBenchmark, PressOnly, "F8", "Request the short scene benchmark" },
            { F9, None, None, RequestReferenceComparison, PressOnly,
                "F9", "Request a reference comparison" },

            { Digit0, None, None, SelectScene0, PressOnly, "0", "Select Baseline Gallery" },
            { Digit1, None, None, SelectScene1, PressOnly, "1", "Select Intersection & BVH Lab" },
            { Digit2, None, None, SelectScene2, PressOnly, "2", "Select Whitted Optics Room" },
            { Digit3, None, None, SelectScene3, PressOnly, "3", "Select Cornell Box" },
            { Digit4, None, None, SelectScene4, PressOnly, "4", "Select GGX & MIS Material Lab" },
            { Digit5, None, None, SelectScene5, PressOnly, "5", "Select Environment Sampling Dome" },
            { Digit6, None, None, SelectScene6, PressOnly, "6", "Select Sponza Traversal Hall" },
            { Digit7, None, None, SelectScene7, PressOnly, "7", "Select Backend Parity Benchmark" },
            { Digit8, None, None, SelectScene8, PressOnly, "8", "Select Temporal Stability Corridor" },
            { Digit9, None, None, SelectScene9, PressOnly, "9", "Select Many Lights / ReSTIR Arena" }
        });

        [[nodiscard]] constexpr bool ContainsAll(
            InputModifier value,
            InputModifier required) noexcept
        {
            return (value & required) == required;
        }

        [[nodiscard]] constexpr bool ContainsNone(
            InputModifier value,
            InputModifier excluded) noexcept
        {
            return (value & excluded) == InputModifier::None;
        }
    }

    std::span<const ActionBinding> GetActionCatalog() noexcept
    {
        return kBindings;
    }

    const ActionBinding* FindActionBinding(InputKey key, InputModifier modifiers) noexcept
    {
        for (const ActionBinding& binding : kBindings)
        {
            if (binding.key == key
                && ContainsAll(modifiers, binding.requiredModifiers)
                && ContainsNone(modifiers, binding.excludedModifiers))
            {
                return &binding;
            }
        }
        return nullptr;
    }
}
