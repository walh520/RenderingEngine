#include "app/RuntimeConfig.hpp"
#include "ui/ImGuiShowcasePanels.hpp"

#include <imgui.h>

#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace
{
    class SyntheticProfilerProvider final
        : public RenderingEngine::Ui::IDebugProfilerProvider
    {
    public:
        [[nodiscard]] RenderingEngine::Ui::DebugProfilerSnapshot ReadSnapshot() override
        {
            using namespace RenderingEngine::Ui;
            DebugProfilerSnapshot snapshot;
            snapshot.providerId = "synthetic.imgui-smoke";
            snapshot.provenance = TelemetryProvenance::SyntheticTest;
            snapshot.availability = TelemetryAvailability::Fresh;
            snapshot.metrics.push_back({
                { "synthetic.zero", "Synthetic zero", MetricDomain::Cpu,
                    MetricUnit::Milliseconds, "test fixture" },
                TelemetryAvailability::Fresh,
                0.0,
                {}
            });
            snapshot.debugResources.push_back({
                { "synthetic.image", "Synthetic image", "fixture-texture", "RGBA8",
                    { 16u, 8u, 1u }, "synthetic-test checker", "L10 test" },
                TelemetryAvailability::Fresh,
                {}
            });
            return snapshot;
        }
    };

    class SyntheticTextureResolver final
        : public RenderingEngine::Ui::IImGuiDebugTextureResolver
    {
    public:
        [[nodiscard]] std::optional<std::uint64_t> ResolveTextureId(
            std::string_view opaqueUiToken) const noexcept override
        {
            return opaqueUiToken == "fixture-texture"
                ? std::optional<std::uint64_t>(1u)
                : std::nullopt;
        }
    };

    [[nodiscard]] RenderingEngine::Demos::ShowcaseWorkflowStart MakeFixedAbStart()
    {
        using namespace RenderingEngine::Demos;
        ShowcaseWorkflowStart start;
        start.sceneStableId = "baseline";
        start.sceneGeneration = 5u;
        start.resourceGeneration = 8u;
        start.anchor = { "camera:baseline-fixed", 0u, 100u };
        start.variantAStableId = "whitted-a";
        start.variantBStableId = "whitted-b";
        start.configGeneration = 3u;
        start.captureProvider = {
            "capture:renderer-readback", Availability::Available, {}
        };
        return start;
    }

    [[nodiscard]] RenderingEngine::Demos::ShowcaseProviderArtifactRecord MakeArtifact(
        const RenderingEngine::Demos::ShowcaseCaptureWorkRequest& request,
        std::string_view runId)
    {
        using namespace RenderingEngine::Demos;
        ShowcaseProviderArtifactRecord artifact;
        artifact.workflowIdentity = request.workflowIdentity;
        artifact.requestIdentity = request.requestIdentity;
        artifact.variant = request.variant;
        artifact.configGeneration = request.configGeneration;
        artifact.sceneGeneration = request.sceneGeneration;
        artifact.resourceGeneration = request.resourceGeneration;
        artifact.runId = runId;
        artifact.exrPath = "captures/image.exr";
        artifact.pngPath = "captures/preview.png";
        artifact.metadataPath = "metadata.json";
        artifact.provenance = {
            EvidenceSource::ProviderReported,
            request.providerToken,
            "ImGui composition fixture"
        };
        return artifact;
    }
}

bool RunImGuiShowcasePanelsTests(std::ostream& errors)
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Ui;

    int failureCount = 0;
    const auto expect = [&errors, &failureCount](bool condition, std::string_view message)
    {
        if (!condition)
        {
            errors << "L10 ImGui panel test failed: " << message << '\n';
            ++failureCount;
        }
    };

    RuntimeConfig config;
    ActionQueue queue;
    expect(!QueueShowcaseSceneSelection(queue, 10u) && queue.Empty(),
        "out-of-range scene cards must not enqueue an action");
    expect(QueueShowcaseSceneSelection(queue, 3u),
        "a valid scene card must enqueue its semantic action");
    const ActionBatchResult sceneBatch = ApplyQueuedActions(queue, config);
    expect(sceneBatch.actions.size() == 1u
        && sceneBatch.actions[0].queued.event.action == SemanticAction::SelectScene3,
        "scene UI must use the shared SelectScene action path");

    expect(QueueShowcaseDimensionCycle(queue, "debug-view", false),
        "a known algorithm dimension must enqueue a cycle action");
    const ActionBatchResult debugBatch = ApplyQueuedActions(queue, config);
    expect(debugBatch.actions.size() == 1u
        && debugBatch.actions[0].queued.event.action == SemanticAction::CycleDebugViewBackward,
        "algorithm UI must use the shared RuntimeConfig cycle path");
    expect(QueueShowcaseDimensionCycle(queue, "direct-lighting", true),
        "the direct-lighting view-model dimension must map to the shared L cycle");
    const ActionBatchResult directLightingBatch = ApplyQueuedActions(queue, config);
    expect(directLightingBatch.actions.size() == 1u
        && directLightingBatch.actions[0].queued.event.action
            == SemanticAction::CycleLightSamplingForward,
        "direct-lighting and proposal panels must preserve the paired semantic action");
    expect(!QueueShowcaseDimensionCycle(queue, "scene", true) && queue.Empty(),
        "dimensions without a cycle action must fail without queue mutation");

    const ShowcaseViewModel unavailableRuntimeModel = BuildShowcaseViewModel(config);
    expect(unavailableRuntimeModel.runtimeStatus.text
            == "Resolution: 1280x720 | Seed: 0 | Frame: -- | SPP: -- | Bounce: 8 | GPU ms: --",
        "config-owned status must remain visible while absent runtime-only observations use placeholders");
    expect(unavailableRuntimeModel.currentTupleCapability.status
            == CapabilityStatus::Supported
        && unavailableRuntimeModel.currentTupleCapability.statusLabel == "supported"
        && unavailableRuntimeModel.currentTupleCapability.reason.empty()
        && unavailableRuntimeModel.currentTupleCapability.text
            == "Capability: supported | Reason: --",
        "the complete current tuple must expose its shared supported capability decision");

    ShowcaseRuntimeStatus runtimeStatus;
    runtimeStatus.providerId = "renderer.frame-status";
    runtimeStatus.provenance = TelemetryProvenance::SyntheticTest;
    runtimeStatus.availability = TelemetryAvailability::Fresh;
    runtimeStatus.configGeneration = 3u;
    runtimeStatus.sceneGeneration = 5u;
    runtimeStatus.resourceGeneration = 8u;
    runtimeStatus.resolution = ShowcaseRuntimeResolution{ 1920u, 1080u };
    runtimeStatus.seed = 0u;
    runtimeStatus.frameIndex = 77u;
    runtimeStatus.samplesPerPixel = 64u;
    runtimeStatus.maximumBounce = 8u;
    runtimeStatus.gpuFrameMilliseconds = 2.5;
    const ShowcaseRuntimeGenerationTuple runtimeGenerations = { 3u, 5u, 8u };
    const ShowcaseViewModel viewModel = BuildShowcaseViewModel(
        config, {}, runtimeStatus, runtimeGenerations);
    expect(viewModel.tupleText == unavailableRuntimeModel.tupleText,
        "runtime observations must not alter the first-line algorithm/lighting/scene tuple");
    expect(viewModel.runtimeStatus.resolution == "1920x1080"
        && viewModel.runtimeStatus.seed == "0"
        && viewModel.runtimeStatus.frame == "77"
        && viewModel.runtimeStatus.samplesPerPixel == "64"
        && viewModel.runtimeStatus.bounce == "8"
        && viewModel.runtimeStatus.gpuMilliseconds == "2.5"
        && viewModel.runtimeStatus.providerId == "renderer.frame-status"
        && viewModel.runtimeStatus.reason.empty()
        && viewModel.runtimeStatus.provenance == TelemetryProvenance::SyntheticTest
        && viewModel.runtimeStatus.availability == TelemetryAvailability::Fresh
        && viewModel.runtimeStatus.configGeneration == 3u
        && viewModel.runtimeStatus.sceneGeneration == 5u
        && viewModel.runtimeStatus.resourceGeneration == 8u
        && viewModel.runtimeStatus.text
            == "Resolution: 1920x1080 | Seed: 0 | Frame: 77 | SPP: 64 | Bounce: 8 | GPU ms: 2.5",
        "provider runtime observations must populate every explicitly labelled second-line field");

    ShowcaseRuntimeStatus invalidRuntimeStatus = runtimeStatus;
    invalidRuntimeStatus.resolution = ShowcaseRuntimeResolution{ 0u, 1080u };
    invalidRuntimeStatus.gpuFrameMilliseconds =
        std::numeric_limits<double>::quiet_NaN();
    const ShowcaseViewModel invalidRuntimeModel =
        BuildShowcaseViewModel(config, {}, invalidRuntimeStatus, runtimeGenerations);
    expect(invalidRuntimeModel.runtimeStatus.resolution == "1280x720"
            && invalidRuntimeModel.runtimeStatus.gpuMilliseconds == "--"
            && invalidRuntimeModel.runtimeStatus.availability
                == TelemetryAvailability::Invalid
            && !invalidRuntimeModel.runtimeStatus.reason.empty(),
        "invalid provider resolution or GPU timing must revoke Fresh and explain the failure");

    const ShowcaseViewModel unverifiedRuntimeModel =
        BuildShowcaseViewModel(config, {}, runtimeStatus);
    expect(unverifiedRuntimeModel.runtimeStatus.availability
                == TelemetryAvailability::Invalid
            && unverifiedRuntimeModel.runtimeStatus.frame == "--"
            && unverifiedRuntimeModel.runtimeStatus.reason
                == "fresh runtime status requires an expected generation tuple",
        "the public builder without an expected tuple must not bypass generation validation");

    ShowcaseRuntimeStatus staleRuntimeStatus = runtimeStatus;
    staleRuntimeStatus.availability = TelemetryAvailability::Stale;
    staleRuntimeStatus.reason = "provider generation is stale";
    const ShowcaseViewModel staleRuntimeModel =
        BuildShowcaseViewModel(config, {}, staleRuntimeStatus, runtimeGenerations);
    expect(staleRuntimeModel.runtimeStatus.frame == "--"
            && staleRuntimeModel.runtimeStatus.samplesPerPixel == "--"
            && staleRuntimeModel.runtimeStatus.gpuMilliseconds == "--"
            && staleRuntimeModel.runtimeStatus.reason == "provider generation is stale",
        "stale runtime observations must retain their reason without displaying old frame/SPP/GPU values");

    ShowcaseRuntimeStatus unavailableRuntimeStatus = runtimeStatus;
    unavailableRuntimeStatus.availability = TelemetryAvailability::Unavailable;
    unavailableRuntimeStatus.reason = "provider is offline";
    const ShowcaseViewModel unavailableObservationModel =
        BuildShowcaseViewModel(config, {}, unavailableRuntimeStatus, runtimeGenerations);
    expect(unavailableObservationModel.runtimeStatus.frame == "--"
            && unavailableObservationModel.runtimeStatus.samplesPerPixel == "--"
            && unavailableObservationModel.runtimeStatus.gpuMilliseconds == "--",
        "unavailable runtime observations must never display provider frame/SPP/GPU values");

    RuntimeConfig unsupportedConfig = config;
    unsupportedConfig.scene = ScenePreset::CornellBox;
    const CapabilityDecision unsupportedDecision =
        CapabilityTable::Evaluate(unsupportedConfig);
    const ShowcaseViewModel unsupportedModel = BuildShowcaseViewModel(unsupportedConfig);
    expect(unsupportedModel.currentTupleCapability.status
            == unsupportedDecision.status
        && unsupportedModel.currentTupleCapability.statusLabel == "unsupported"
        && unsupportedModel.currentTupleCapability.reason == unsupportedDecision.reason
        && !unsupportedModel.currentTupleCapability.reason.empty()
        && unsupportedModel.currentTupleCapability.text.find(unsupportedDecision.reason)
            != std::string::npos,
        "the view model must expose CapabilityTable status and reason for the complete tuple");

    ShowcasePanelState panels;
    ImGuiShowcaseSelectionState selection;
    ImGuiShowcaseInputs invalidInputs;
    expect(DrawImGuiShowcasePanels(panels, selection, invalidInputs)
            == ImGuiShowcaseDrawStatus::InvalidInput,
        "missing view model and queue must be reported without calling ImGui");

    ImGuiShowcaseInputs headlessInputs;
    headlessInputs.showcase = &viewModel;
    headlessInputs.actionQueue = &queue;
    expect(DrawImGuiShowcasePanels(panels, selection, headlessInputs)
            == ImGuiShowcaseDrawStatus::NoImGuiContext,
        "headless use must fail closed before any frame drawing call");

    ShowcaseController controller;
    ShowcaseFeatureAvailability workflowAvailability;
    workflowAvailability.captureProvider = true;
    controller.SetFeatureAvailability(workflowAvailability);
    controller.SetConfigGeneration(3u);
    controller.SetSceneResourceGenerations(5u, 8u);
    Demos::ShowcaseWorkflowStart fixedAbStart = MakeFixedAbStart();
    ImGuiShowcaseInputs unavailableAbInputs;
    unavailableAbInputs.fixedAbStartUnavailableReason =
        "composition is waiting for scene/camera identities";
    const ImGuiShowcaseActionAvailability unavailableAb =
        EvaluateFixedAbStartAvailability(unavailableAbInputs);
    expect(!unavailableAb.enabled
            && unavailableAb.reason == "showcase workflow controller is unavailable",
        "fixed A/B start must be disabled with a reason when no controller is composed");

    unavailableAbInputs.controller = &controller;
    const ImGuiShowcaseActionAvailability missingStart =
        EvaluateFixedAbStartAvailability(unavailableAbInputs);
    expect(!missingStart.enabled
            && missingStart.reason == "composition is waiting for scene/camera identities",
        "fixed A/B start must preserve the composition's unavailable-input reason");

    unavailableAbInputs.fixedAbStart = &fixedAbStart;
    expect(EvaluateFixedAbStartAvailability(unavailableAbInputs).enabled,
        "a complete, current composition-authored A/B input must enable one-click start");
    Demos::ShowcaseWorkflowStart unavailableProviderStart = fixedAbStart;
    unavailableProviderStart.captureProvider.state = Demos::Availability::Unavailable;
    unavailableProviderStart.captureProvider.reason = "readback queue is offline";
    unavailableAbInputs.fixedAbStart = &unavailableProviderStart;
    const ImGuiShowcaseActionAvailability unavailableProvider =
        EvaluateFixedAbStartAvailability(unavailableAbInputs);
    expect(!unavailableProvider.enabled
            && unavailableProvider.reason == "readback queue is offline",
        "provider-unavailable A/B input must remain disabled with its provider reason");

    SyntheticProfilerProvider profilerProvider;
    DebugProfilerModel profiler;
    expect(profiler.Refresh(profilerProvider, {}) .Accepted(),
        "synthetic ImGui smoke telemetry must pass the normal provenance validator");
    const Demos::ShowcaseProgramModel program = Demos::BuildShowcaseProgramModel({}, {}, {});
    Demos::FinalVideoShotList shots = Demos::BuildFinalVideoShotList(
        program.captureShots,
        program.licenseGate);
    Demos::BenchmarkSequence benchmark;
    SyntheticTextureResolver textureResolver;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    expect(fontPixels != nullptr && fontWidth > 0 && fontHeight > 0,
        "backend-independent smoke must build the default ImGui font atlas");

    ImGuiShowcaseInputs statusOnlyInputs;
    statusOnlyInputs.showcase = &viewModel;
    statusOnlyInputs.actionQueue = &queue;
    ImGui::NewFrame();
    expect(DrawImGuiShowcasePanels(panels, selection, statusOnlyInputs)
            == ImGuiShowcaseDrawStatus::Drawn,
        "a valid draw must succeed when every optional panel is closed");
    ImGui::Render();
    const ImDrawData* const statusOnlyDrawData = ImGui::GetDrawData();
    expect(statusOnlyDrawData != nullptr
            && statusOnlyDrawData->CmdListsCount > 0
            && statusOnlyDrawData->TotalVtxCount > 0,
        "the unconditional top status window must render independently of Algorithm visibility");

    panels.algorithm = true;
    panels.scene = true;
    panels.debug = true;
    panels.profiler = true;
    panels.capture = true;
    panels.help = true;
    panels.debugLegend = true;
    ImGuiShowcaseInputs drawInputs;
    drawInputs.showcase = &viewModel;
    drawInputs.program = &program;
    drawInputs.debugProfiler = &profiler;
    drawInputs.benchmark = &benchmark;
    drawInputs.videoShots = &shots;
    drawInputs.actionQueue = &queue;
    drawInputs.textureResolver = &textureResolver;
    drawInputs.controller = &controller;
    drawInputs.fixedAbStart = &fixedAbStart;
    expect(static_cast<bool>(controller.StartAbComparison(fixedAbStart))
            && controller.PendingAbRequest() != nullptr,
        "ImGui smoke must enter the same controller-owned one-click A/B path");
    ImGui::NewFrame();
    expect(DrawImGuiShowcasePanels(panels, selection, drawInputs)
            == ImGuiShowcaseDrawStatus::Drawn,
        "all five ImGui panels and help must execute inside a valid frame");
    ImGui::Render();
    const ImDrawData* const drawData = ImGui::GetDrawData();
    expect(drawData != nullptr && drawData->CmdListsCount > 0
            && drawData->TotalVtxCount > 0,
        "panel smoke must produce backend-independent ImGui draw data");

    const Demos::ShowcaseCaptureWorkRequest requestA = controller.AbRequests()[0];
    const Demos::ShowcaseCaptureWorkRequest requestB = controller.AbRequests()[1];
    expect(static_cast<bool>(controller.SubmitAbArtifact(MakeArtifact(requestA, "imgui-run-a")))
            && static_cast<bool>(controller.SubmitAbArtifact(MakeArtifact(requestB, "imgui-run-b")))
            && controller.CompletedAbManifest() != nullptr,
        "provider artifacts must complete the same A/B workflow displayed by Capture and QA");
    ImGui::NewFrame();
    expect(DrawImGuiShowcasePanels(panels, selection, drawInputs)
            == ImGuiShowcaseDrawStatus::Drawn,
        "Capture and QA must draw the persisted completed A/B manifest state");
    ImGui::Render();
    ImGui::DestroyContext();

    controller.Panels().scene = true;
    controller.Panels().debug = true;
    expect(controller.Panels().scene && controller.Panels().debug,
        "L0 must be able to keep menu-driven Scene and Debug visibility in controller state");

    return failureCount == 0;
}
