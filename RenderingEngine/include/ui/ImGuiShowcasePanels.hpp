#pragma once

#include "demos/ShowcaseEvidence.hpp"
#include "demos/ShowcaseProgram.hpp"
#include "ui/DebugProfilerModel.hpp"
#include "ui/ShowcaseController.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RenderingEngine::Ui
{
    // L0/L1 may translate an opaque provider token into the texture identifier
    // registered with their ImGui backend. L10 never sees a Vulkan handle,
    // descriptor set, image layout, or resource lifetime through this seam.
    class IImGuiDebugTextureResolver
    {
    public:
        virtual ~IImGuiDebugTextureResolver() = default;

        [[nodiscard]] virtual std::optional<std::uint64_t> ResolveTextureId(
            std::string_view opaqueUiToken) const noexcept = 0;
    };

    struct ImGuiShowcaseSelectionState
    {
        std::size_t selectedDebugResource = 0;
        std::size_t selectedManyLightsPreset = 0;
    };

    struct ImGuiShowcaseInputs
    {
        const ShowcaseViewModel* showcase = nullptr;
        const Demos::ShowcaseProgramModel* program = nullptr;
        const DebugProfilerModel* debugProfiler = nullptr;
        const Demos::BenchmarkSequence* benchmark = nullptr;
        const Demos::FinalVideoShotList* videoShots = nullptr;
        ActionQueue* actionQueue = nullptr;
        const IImGuiDebugTextureResolver* textureResolver = nullptr;

        // The composition layer owns these fixed provider inputs. The panel
        // never derives a scene/camera/seed/generation or provider token.
        ShowcaseController* controller = nullptr;
        const Demos::ShowcaseWorkflowStart* fixedAbStart = nullptr;
        std::string_view fixedAbStartUnavailableReason;

        // L0 publishes the most recent semantic-command result so a rejected
        // hotkey is visible in the Vulkan window instead of only on stderr.
        // The renderer owns the backing storage for the duration of the frame.
        std::string_view actionFeedback;
        bool actionFeedbackIsError = false;
    };

    enum class ImGuiShowcaseDrawStatus : std::uint8_t
    {
        Drawn,
        NoImGuiContext,
        InvalidInput
    };

    struct ImGuiShowcaseActionAvailability
    {
        bool enabled = false;
        std::string reason;
    };

    // These helpers deliberately enqueue the same semantic actions consumed by
    // ApplyQueuedActions. ImGui therefore has no separate RuntimeConfig path.
    [[nodiscard]] bool QueueShowcaseSceneSelection(
        ActionQueue& queue,
        std::size_t sceneIndex);
    [[nodiscard]] bool QueueShowcaseDimensionCycle(
        ActionQueue& queue,
        std::string_view dimensionId,
        bool forward);
    [[nodiscard]] ImGuiShowcaseActionAvailability EvaluateFixedAbStartAvailability(
        const ImGuiShowcaseInputs& inputs);

    // L0 calls this only between ImGui NewFrame/Render. Headless executables use
    // the models above and never call an ImGui function.
    [[nodiscard]] ImGuiShowcaseDrawStatus DrawImGuiShowcasePanels(
        ShowcasePanelState& panels,
        ImGuiShowcaseSelectionState& selection,
        const ImGuiShowcaseInputs& inputs);
}
