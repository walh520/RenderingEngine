#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace RenderingEngine::Ui
{
    // Portable physical names used only at the L1 -> L10 translation seam.  The
    // catalog deliberately does not expose GLFW key values or a platform host.
    enum class InputKey : std::uint16_t
    {
        W,
        A,
        S,
        D,
        Q,
        E,
        LeftShift,
        LeftControl,
        MouseMove,
        MouseWheel,
        Tab,
        Escape,
        Home,
        P,
        O,
        B,
        I,
        L,
        N,
        V,
        R,
        K,
        LeftBracket,
        RightBracket,
        Minus,
        Equal,
        PageDown,
        PageUp,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        Digit0,
        Digit1,
        Digit2,
        Digit3,
        Digit4,
        Digit5,
        Digit6,
        Digit7,
        Digit8,
        Digit9
    };

    enum class InputModifier : std::uint8_t
    {
        None = 0,
        Shift = 1u << 0u,
        Control = 1u << 1u,
        Alt = 1u << 2u
    };

    [[nodiscard]] constexpr InputModifier operator|(InputModifier left, InputModifier right) noexcept
    {
        return static_cast<InputModifier>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr InputModifier operator&(InputModifier left, InputModifier right) noexcept
    {
        return static_cast<InputModifier>(
            static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    enum class ActionActivation : std::uint8_t
    {
        PressOnly,
        WhileHeld,
        Axis1D,
        Axis2D
    };

    enum class ActionPhase : std::uint8_t
    {
        Pressed,
        Released,
        Repeated,
        Axis
    };

    enum class SemanticAction : std::uint16_t
    {
        MoveForward,
        MoveBackward,
        MoveLeft,
        MoveRight,
        MoveDown,
        MoveUp,
        FastMovementModifier,
        FineMovementModifier,
        Look,
        AdjustMovementSpeed,
        AdjustVerticalFov,
        TogglePointerCapture,
        ReleasePointerCapture,
        RequestExit,
        ResetShowcaseCamera,
        ToggleAnimationPause,
        StepAnimationFrame,
        CycleBackendForward,
        CycleBackendBackward,
        CycleIntegratorForward,
        CycleIntegratorBackward,
        CycleLightSamplingForward,
        CycleLightSamplingBackward,
        CycleReconstructionForward,
        CycleReconstructionBackward,
        CycleDebugViewForward,
        CycleDebugViewBackward,
        ResetHistories,
        ToggleComparisonLock,
        DecreaseMaximumBounce,
        IncreaseMaximumBounce,
        DecreaseExposure,
        IncreaseExposure,
        DecreaseRenderScale,
        IncreaseRenderScale,
        ToggleHelp,
        ToggleAlgorithmPanel,
        ToggleProfilerPanel,
        RequestCapture,
        RequestShaderReload,
        ToggleSplitScreenComparison,
        ToggleDebugLegend,
        RequestBenchmark,
        RequestReferenceComparison,
        SelectScene0,
        SelectScene1,
        SelectScene2,
        SelectScene3,
        SelectScene4,
        SelectScene5,
        SelectScene6,
        SelectScene7,
        SelectScene8,
        SelectScene9
    };

    struct ActionBinding
    {
        InputKey key;
        InputModifier requiredModifiers = InputModifier::None;
        InputModifier excludedModifiers = InputModifier::None;
        SemanticAction action;
        ActionActivation activation = ActionActivation::PressOnly;
        std::string_view chordLabel;
        std::string_view description;
    };

    struct ActionEvent
    {
        SemanticAction action = SemanticAction::ToggleHelp;
        ActionPhase phase = ActionPhase::Pressed;
        float valueX = 0.0f;
        float valueY = 0.0f;
    };

    // Complete section-10 binding catalog.  Callers may use it for translation,
    // tests, or help text; it owns no input state and invokes no callbacks.
    [[nodiscard]] std::span<const ActionBinding> GetActionCatalog() noexcept;

    // Returns the most-specific matching chord.  Extra modifiers are tolerated
    // unless a binding explicitly excludes them (Shift cycles and Alt+wheel).
    [[nodiscard]] const ActionBinding* FindActionBinding(
        InputKey key,
        InputModifier modifiers) noexcept;

    [[nodiscard]] constexpr bool IsDiscreteCycleAction(SemanticAction action) noexcept
    {
        return action >= SemanticAction::CycleBackendForward
            && action <= SemanticAction::CycleDebugViewBackward;
    }
}
