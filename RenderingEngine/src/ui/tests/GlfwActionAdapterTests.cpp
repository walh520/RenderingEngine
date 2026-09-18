#include "ui/GlfwActionAdapter.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <ostream>
#include <string_view>
#include <vector>

bool RunGlfwActionAdapterTests(std::ostream& errors)
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Ui;

    int failureCount = 0;
    const auto expect = [&errors, &failureCount](const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            errors << "L1/L10 GLFW action adapter test failed: " << message << '\n';
            ++failureCount;
        }
    };

    GlfwInputAccumulator input;
    input.OnKey(GLFW_KEY_W, PhysicalInputAction::Pressed, 0u);
    input.OnKey(GLFW_KEY_B, PhysicalInputAction::Pressed, 0u);
    input.OnKey(GLFW_KEY_B, PhysicalInputAction::Repeated, 0u);
    input.OnScroll(0.0, 1.0, GLFW_MOD_ALT);
    input.OnCursor(10.0, 10.0);
    input.OnCursor(12.0, 7.0);
    const std::vector<GlfwRawInputEvent> events = input.DrainEvents();

    ActionQueue queue;
    const GlfwActionTranslationResult translated = QueueGlfwFrameActions(
        events,
        input.State(),
        { false, false, true },
        queue);
    expect(translated.ignoredRepeat == 1u,
        "GLFW repeat must be consumed before a discrete semantic action is queued");
    expect(translated.queued == 4u && queue.Size() == 4u,
        "held W, pressed B, Alt+Wheel, and captured cursor delta must queue exactly once");

    RuntimeConfig config;
    const ActionBatchResult batch = ApplyQueuedActions(queue, config);
    expect(batch.actions.size() == 4u
            && batch.actions[0].queued.event.action == SemanticAction::MoveForward
            && batch.actions[1].queued.event.action == SemanticAction::CycleBackendForward
            && batch.actions[2].queued.event.action == SemanticAction::AdjustVerticalFov
            && batch.actions[3].queued.event.action == SemanticAction::Look,
        "the adapter must preserve held, discrete, wheel-modifier, and cursor ordering");

    ActionQueue keyboardCapturedQueue;
    const GlfwActionTranslationResult keyboardCaptured = QueueGlfwFrameActions(
        events,
        input.State(),
        { true, false, true },
        keyboardCapturedQueue);
    expect(keyboardCaptured.suppressedByUi >= 1u
            && keyboardCapturedQueue.Size() == 3u,
        "ImGui keyboard capture must suppress continuous movement while preserving global press-only shortcuts and mouse axes");
    const ActionBatchResult keyboardCapturedBatch = ApplyQueuedActions(
        keyboardCapturedQueue, config);
    expect(!keyboardCapturedBatch.actions.empty()
            && keyboardCapturedBatch.actions[0].queued.event.action
                == SemanticAction::CycleBackendForward,
        "global showcase shortcuts must remain usable while an ImGui panel owns keyboard focus");

    ActionQueue mouseCapturedQueue;
    const GlfwActionTranslationResult mouseCaptured = QueueGlfwFrameActions(
        events,
        input.State(),
        { false, true, true },
        mouseCapturedQueue);
    expect(mouseCaptured.suppressedByUi >= 2u
            && mouseCapturedQueue.Size() == 2u,
        "ImGui mouse capture must suppress wheel/cursor without suppressing keyboard actions");

    const std::optional<InputKey> unknown = TranslateGlfwPhysicalKey(GLFW_KEY_UNKNOWN);
    expect(!unknown.has_value(), "unknown GLFW key codes must remain unmapped");
    expect(TranslateGlfwPhysicalKey(GLFW_KEY_F10) == InputKey::F10,
        "GLFW F10 must map to the current scene and algorithm review action");
    expect(TranslateGlfwPhysicalKey(GLFW_KEY_F11) == InputKey::F11,
        "GLFW F11 must map to the current scene recommendation restore action");
    expect(TranslateGlfwPhysicalKey(GLFW_KEY_F12) == InputKey::F12,
        "GLFW F12 must map to scene variant cycling");
    const ActionBinding* variantForward = FindActionBinding(InputKey::F12, InputModifier::None);
    const ActionBinding* variantBackward = FindActionBinding(InputKey::F12, InputModifier::Shift);
    expect(variantForward && variantForward->action == SemanticAction::CycleSceneVariantForward
            && variantForward->activation == ActionActivation::PressOnly
            && variantBackward && variantBackward->action == SemanticAction::CycleSceneVariantBackward,
        "F12 and Shift+F12 must select opposite press-only variant cycles");

    GlfwInputAccumulator recommendationInput;
    GlfwInputAccumulator variantInput;
    variantInput.OnKey(GLFW_KEY_F12, PhysicalInputAction::Pressed, 0u);
    variantInput.OnKey(GLFW_KEY_F12, PhysicalInputAction::Repeated, 0u);
    ActionQueue variantInputQueue;
    const auto variantTranslation = QueueGlfwFrameActions(variantInput.DrainEvents(),
        variantInput.State(), { true, false, true }, variantInputQueue);
    expect(variantTranslation.queued == 1u && variantTranslation.ignoredRepeat == 1u,
        "F12 must remain global during UI capture and ignore key repeat");
    recommendationInput.OnKey(GLFW_KEY_F11, PhysicalInputAction::Pressed, 0u);
    recommendationInput.OnKey(GLFW_KEY_F11, PhysicalInputAction::Repeated, 0u);
    ActionQueue recommendationQueue;
    const GlfwActionTranslationResult recommendationTranslation =
        QueueGlfwFrameActions(
            recommendationInput.DrainEvents(),
            recommendationInput.State(),
            { true, false, true },
            recommendationQueue);
    expect(recommendationTranslation.queued == 1u
            && recommendationTranslation.ignoredRepeat == 1u
            && recommendationQueue.Size() == 1u,
        "F11 must stay global under ImGui capture and ignore GLFW repeat");
    RuntimeConfig recommendationConfig;
    recommendationConfig.debugView = DebugView::Emissive;
    const ActionBatchResult recommendationBatch =
        ApplyQueuedActions(recommendationQueue, recommendationConfig);
    expect(recommendationBatch.actions.size() == 1u
            && recommendationBatch.actions[0].queued.event.action
                == SemanticAction::RestoreCurrentSceneRecommendedProfile
            && recommendationBatch.actions[0].status
                == ActionApplyStatus::ConfigCommitted
            && recommendationConfig.debugView == DebugView::Final,
        "translated F11 must reach the atomic RuntimeConfig harness");

    GlfwInputAccumulator unfocusedRecommendation;
    unfocusedRecommendation.OnFocus(false);
    unfocusedRecommendation.OnKey(
        GLFW_KEY_F11, PhysicalInputAction::Pressed, 0u);
    ActionQueue unfocusedQueue;
    const GlfwActionTranslationResult unfocusedTranslation =
        QueueGlfwFrameActions(
            unfocusedRecommendation.DrainEvents(),
            unfocusedRecommendation.State(),
            { false, false, true },
            unfocusedQueue);
    expect(unfocusedQueue.Empty() && unfocusedTranslation.suppressedByUi >= 1u,
        "F11 must not cross a GLFW focus-loss boundary");

    GlfwInputAccumulator transportCycles;
    transportCycles.OnKey(GLFW_KEY_I, PhysicalInputAction::Pressed, 0u);
    transportCycles.OnKey(
        GLFW_KEY_I, PhysicalInputAction::Pressed, GLFW_MOD_SHIFT);
    ActionQueue transportCycleQueue;
    static_cast<void>(QueueGlfwFrameActions(
        transportCycles.DrainEvents(),
        transportCycles.State(),
        { false, false, true },
        transportCycleQueue));
    RuntimeConfig transportCycleConfig;
    const ActionBatchResult transportCycleBatch = ApplyQueuedActions(
        transportCycleQueue, transportCycleConfig);
    expect(transportCycleBatch.actions.size() == 2u
            && transportCycleBatch.actions[0].queued.event.action
                == SemanticAction::CycleTransportModelForward
            && transportCycleBatch.actions[0].status
                == ActionApplyStatus::ConfigCommitted
            && transportCycleBatch.actions[1].queued.event.action
                == SemanticAction::CycleTransportModelBackward
            && transportCycleBatch.actions[1].status
                == ActionApplyStatus::ConfigCommitted
            && transportCycleConfig.transportModel == TransportModel::Pbr,
        "GLFW I/Shift+I must execute the independent forward/reverse transport route");

    constexpr std::array keypadCodes{
        GLFW_KEY_KP_0, GLFW_KEY_KP_1, GLFW_KEY_KP_2, GLFW_KEY_KP_3,
        GLFW_KEY_KP_4, GLFW_KEY_KP_5, GLFW_KEY_KP_6, GLFW_KEY_KP_7,
        GLFW_KEY_KP_8, GLFW_KEY_KP_9
    };
    constexpr std::array digitKeys{
        InputKey::Digit0, InputKey::Digit1, InputKey::Digit2, InputKey::Digit3,
        InputKey::Digit4, InputKey::Digit5, InputKey::Digit6, InputKey::Digit7,
        InputKey::Digit8, InputKey::Digit9
    };
    for (std::size_t index = 0u; index < keypadCodes.size(); ++index)
    {
        expect(TranslateGlfwPhysicalKey(keypadCodes[index]) == digitKeys[index],
            "numeric-keypad scene keys must match the top-row digit actions");
    }
    expect((TranslateGlfwModifiers(GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT)
            & InputModifier::Alt) == InputModifier::Alt,
        "GLFW modifier bits must preserve Alt independently of Shift and Control");

    GlfwInputAccumulator platformCommands;
    platformCommands.OnKey(GLFW_KEY_ESCAPE, PhysicalInputAction::Pressed, 0u);
    platformCommands.OnKey(GLFW_KEY_TAB, PhysicalInputAction::Pressed, 0u);
    platformCommands.OnKey(
        GLFW_KEY_F4,
        PhysicalInputAction::Pressed,
        GLFW_MOD_ALT);
    ActionQueue platformCommandQueue;
    const std::vector<GlfwRawInputEvent> platformCommandEvents =
        platformCommands.DrainEvents();
    static_cast<void>(QueueGlfwFrameActions(
        platformCommandEvents,
        platformCommands.State(),
        { true, false, true },
        platformCommandQueue));
    RuntimeConfig platformConfig;
    const ActionBatchResult platformBatch =
        ApplyQueuedActions(platformCommandQueue, platformConfig);
    expect(platformBatch.actions.size() == 3u
            && platformBatch.actions[0].routedCommand
                == RoutedCommand::ReleasePointerCapture
            && platformBatch.actions[1].routedCommand
                == RoutedCommand::TogglePointerCapture
            && platformBatch.actions[2].routedCommand
                == RoutedCommand::RequestExit,
        "Esc, Tab, and Alt+F4 must remain global even while ImGui owns keyboard focus");

    if (failureCount == 0)
    {
        errors << "L1/L10 GLFW action adapter checks passed.\n";
    }
    return failureCount == 0;
}
