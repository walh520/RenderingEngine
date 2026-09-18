#include "ui/GlfwActionAdapter.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>

namespace RenderingEngine::Ui
{
    namespace
    {
        struct KeyTranslation
        {
            std::int32_t glfwKey = GLFW_KEY_UNKNOWN;
            InputKey key = InputKey::W;
        };

        constexpr auto kKeyTranslations = std::to_array<KeyTranslation>({
            { GLFW_KEY_W, InputKey::W },
            { GLFW_KEY_A, InputKey::A },
            { GLFW_KEY_S, InputKey::S },
            { GLFW_KEY_D, InputKey::D },
            { GLFW_KEY_Q, InputKey::Q },
            { GLFW_KEY_E, InputKey::E },
            { GLFW_KEY_LEFT_SHIFT, InputKey::LeftShift },
            { GLFW_KEY_LEFT_CONTROL, InputKey::LeftControl },
            { GLFW_KEY_TAB, InputKey::Tab },
            { GLFW_KEY_ESCAPE, InputKey::Escape },
            { GLFW_KEY_HOME, InputKey::Home },
            { GLFW_KEY_P, InputKey::P },
            { GLFW_KEY_O, InputKey::O },
            { GLFW_KEY_B, InputKey::B },
            { GLFW_KEY_I, InputKey::I },
            { GLFW_KEY_L, InputKey::L },
            { GLFW_KEY_N, InputKey::N },
            { GLFW_KEY_V, InputKey::V },
            { GLFW_KEY_R, InputKey::R },
            { GLFW_KEY_K, InputKey::K },
            { GLFW_KEY_LEFT_BRACKET, InputKey::LeftBracket },
            { GLFW_KEY_RIGHT_BRACKET, InputKey::RightBracket },
            { GLFW_KEY_MINUS, InputKey::Minus },
            { GLFW_KEY_EQUAL, InputKey::Equal },
            { GLFW_KEY_PAGE_DOWN, InputKey::PageDown },
            { GLFW_KEY_PAGE_UP, InputKey::PageUp },
            { GLFW_KEY_F1, InputKey::F1 },
            { GLFW_KEY_F2, InputKey::F2 },
            { GLFW_KEY_F3, InputKey::F3 },
            { GLFW_KEY_F4, InputKey::F4 },
            { GLFW_KEY_F5, InputKey::F5 },
            { GLFW_KEY_F6, InputKey::F6 },
            { GLFW_KEY_F7, InputKey::F7 },
            { GLFW_KEY_F8, InputKey::F8 },
            { GLFW_KEY_F9, InputKey::F9 },
            { GLFW_KEY_F10, InputKey::F10 },
            { GLFW_KEY_F11, InputKey::F11 },
            { GLFW_KEY_F12, InputKey::F12 },
            { GLFW_KEY_0, InputKey::Digit0 },
            { GLFW_KEY_1, InputKey::Digit1 },
            { GLFW_KEY_2, InputKey::Digit2 },
            { GLFW_KEY_3, InputKey::Digit3 },
            { GLFW_KEY_4, InputKey::Digit4 },
            { GLFW_KEY_5, InputKey::Digit5 },
            { GLFW_KEY_6, InputKey::Digit6 },
            { GLFW_KEY_7, InputKey::Digit7 },
            { GLFW_KEY_8, InputKey::Digit8 },
            { GLFW_KEY_9, InputKey::Digit9 },
            { GLFW_KEY_KP_0, InputKey::Digit0 },
            { GLFW_KEY_KP_1, InputKey::Digit1 },
            { GLFW_KEY_KP_2, InputKey::Digit2 },
            { GLFW_KEY_KP_3, InputKey::Digit3 },
            { GLFW_KEY_KP_4, InputKey::Digit4 },
            { GLFW_KEY_KP_5, InputKey::Digit5 },
            { GLFW_KEY_KP_6, InputKey::Digit6 },
            { GLFW_KEY_KP_7, InputKey::Digit7 },
            { GLFW_KEY_KP_8, InputKey::Digit8 },
            { GLFW_KEY_KP_9, InputKey::Digit9 }
        });

        [[nodiscard]] std::optional<std::int32_t> GlfwCodeFor(
            const InputKey key) noexcept
        {
            for (const KeyTranslation& translation : kKeyTranslations)
            {
                if (translation.key == key)
                {
                    return translation.glfwKey;
                }
            }
            return std::nullopt;
        }

        void QueueAxis(
            ActionQueue& queue,
            const ActionBinding& binding,
            const GlfwRawInputEvent& event,
            GlfwActionTranslationResult& result)
        {
            if (!std::isfinite(event.valueX) || !std::isfinite(event.valueY))
            {
                ++result.unmapped;
                return;
            }
            queue.Push({
                binding.action,
                ActionPhase::Axis,
                static_cast<float>(event.valueX),
                static_cast<float>(event.valueY)
            });
            ++result.queued;
        }
    }

    std::optional<InputKey> TranslateGlfwPhysicalKey(
        const std::int32_t physicalCode) noexcept
    {
        for (const KeyTranslation& translation : kKeyTranslations)
        {
            if (translation.glfwKey == physicalCode)
            {
                return translation.key;
            }
        }
        return std::nullopt;
    }

    InputModifier TranslateGlfwModifiers(const std::uint32_t glfwModifiers) noexcept
    {
        InputModifier modifiers = InputModifier::None;
        if ((glfwModifiers & GLFW_MOD_SHIFT) != 0u)
        {
            modifiers = modifiers | InputModifier::Shift;
        }
        if ((glfwModifiers & GLFW_MOD_CONTROL) != 0u)
        {
            modifiers = modifiers | InputModifier::Control;
        }
        if ((glfwModifiers & GLFW_MOD_ALT) != 0u)
        {
            modifiers = modifiers | InputModifier::Alt;
        }
        return modifiers;
    }

    GlfwActionTranslationResult QueueGlfwFrameActions(
        const std::span<const GlfwRawInputEvent> rawEvents,
        const GlfwPhysicalInputState& physicalState,
        const GlfwActionTranslationPolicy& policy,
        ActionQueue& queue)
    {
        GlfwActionTranslationResult result;
        if (physicalState.focused && !policy.keyboardCapturedByUi)
        {
            for (const ActionBinding& binding : GetActionCatalog())
            {
                if (binding.activation != ActionActivation::WhileHeld)
                {
                    continue;
                }
                const std::optional<std::int32_t> glfwCode = GlfwCodeFor(binding.key);
                if (glfwCode.has_value() && physicalState.IsKeyDown(*glfwCode))
                {
                    queue.Push(binding.action, ActionPhase::Pressed);
                    ++result.queued;
                }
            }
        }

        for (const GlfwRawInputEvent& event : rawEvents)
        {
            if (event.device == PhysicalInputDevice::Keyboard)
            {
                const std::optional<InputKey> key = TranslateGlfwPhysicalKey(event.physicalCode);
                if (!key.has_value())
                {
                    ++result.unmapped;
                    continue;
                }
                if (!physicalState.focused)
                {
                    ++result.suppressedByUi;
                    continue;
                }
                if (event.action == PhysicalInputAction::Repeated)
                {
                    ++result.ignoredRepeat;
                    continue;
                }
                if (event.action != PhysicalInputAction::Pressed)
                {
                    continue;
                }
                const ActionBinding* const binding = FindActionBinding(
                    *key,
                    TranslateGlfwModifiers(event.modifiers));
                if (binding == nullptr)
                {
                    ++result.unmapped;
                    continue;
                }
                // ImGui capture owns text/navigation and continuous camera input,
                // but press-only showcase commands are application-global. In
                // particular Tab/Esc must remain able to release a pointer that
                // was captured before an ImGui window acquired keyboard focus.
                if (policy.keyboardCapturedByUi
                    && binding->activation != ActionActivation::PressOnly)
                {
                    ++result.suppressedByUi;
                    continue;
                }
                if (binding->activation == ActionActivation::PressOnly)
                {
                    queue.Push(binding->action, ActionPhase::Pressed);
                    ++result.queued;
                }
                continue;
            }

            if (event.device == PhysicalInputDevice::Cursor
                && event.action == PhysicalInputAction::Moved)
            {
                if (policy.mouseCapturedByUi
                    || !policy.pointerCaptured
                    || !physicalState.focused)
                {
                    ++result.suppressedByUi;
                    continue;
                }
                const ActionBinding* const binding = FindActionBinding(
                    InputKey::MouseMove,
                    InputModifier::None);
                if (binding != nullptr
                    && (event.valueX != 0.0 || event.valueY != 0.0))
                {
                    QueueAxis(queue, *binding, event, result);
                }
                continue;
            }

            if (event.device == PhysicalInputDevice::Scroll
                && event.action == PhysicalInputAction::Scrolled)
            {
                if (policy.mouseCapturedByUi || !physicalState.focused)
                {
                    ++result.suppressedByUi;
                    continue;
                }
                const ActionBinding* const binding = FindActionBinding(
                    InputKey::MouseWheel,
                    TranslateGlfwModifiers(event.modifiers));
                if (binding != nullptr
                    && (event.valueX != 0.0 || event.valueY != 0.0))
                {
                    QueueAxis(queue, *binding, event, result);
                }
                else
                {
                    ++result.unmapped;
                }
            }
        }
        return result;
    }
}
