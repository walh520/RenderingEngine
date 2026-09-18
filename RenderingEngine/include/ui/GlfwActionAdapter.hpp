#pragma once

#include "platform/glfw/GlfwInput.hpp"
#include "ui/RuntimeConfigHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace RenderingEngine::Ui
{
    struct GlfwActionTranslationPolicy
    {
        bool keyboardCapturedByUi = false;
        bool mouseCapturedByUi = false;
        bool pointerCaptured = false;
    };

    struct GlfwActionTranslationResult
    {
        std::size_t queued = 0;
        std::size_t suppressedByUi = 0;
        std::size_t ignoredRepeat = 0;
        std::size_t unmapped = 0;
    };

    [[nodiscard]] std::optional<InputKey> TranslateGlfwPhysicalKey(
        std::int32_t physicalCode) noexcept;
    [[nodiscard]] InputModifier TranslateGlfwModifiers(
        std::uint32_t glfwModifiers) noexcept;

    // Translates one GLFW callback batch plus the current held state. Semantic
    // bindings remain in ActionMap: this adapter only converts physical codes,
    // applies ImGui capture policy, and queues resolved ActionEvents.
    [[nodiscard]] GlfwActionTranslationResult QueueGlfwFrameActions(
        std::span<const GlfwRawInputEvent> rawEvents,
        const GlfwPhysicalInputState& physicalState,
        const GlfwActionTranslationPolicy& policy,
        ActionQueue& queue);
}
