#include "app/RuntimeConfig.hpp"
#include "ui/RuntimeConfigHarness.hpp"
#include "ui/ShowcaseController.hpp"

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    [[nodiscard]] RenderingEngine::Demos::ShowcaseWorkflowStart MakeAbStart()
    {
        using namespace RenderingEngine::Demos;
        ShowcaseWorkflowStart start;
        start.sceneStableId = "cornell";
        start.sceneGeneration = 5u;
        start.resourceGeneration = 8u;
        start.anchor = { "camera:cornell-reference", 123u, 456u };
        start.variantAStableId = "mis-off";
        start.variantBStableId = "mis-on";
        start.configGeneration = 3u;
        start.captureProvider = {
            "capture:renderer-readback", Availability::Available, {}
        };
        return start;
    }

    [[nodiscard]] RenderingEngine::Demos::ShowcaseProviderArtifactRecord MakeAbArtifact(
        const RenderingEngine::Demos::ShowcaseCaptureWorkRequest& request,
        std::string runId)
    {
        using namespace RenderingEngine::Demos;
        ShowcaseProviderArtifactRecord artifact;
        artifact.workflowIdentity = request.workflowIdentity;
        artifact.requestIdentity = request.requestIdentity;
        artifact.variant = request.variant;
        artifact.configGeneration = request.configGeneration;
        artifact.sceneGeneration = request.sceneGeneration;
        artifact.resourceGeneration = request.resourceGeneration;
        artifact.runId = std::move(runId);
        artifact.exrPath = "captures/image.exr";
        artifact.pngPath = "captures/preview.png";
        artifact.metadataPath = "metadata.json";
        artifact.provenance = {
            EvidenceSource::ProviderReported,
            request.providerToken,
            "controller test provider artifact"
        };
        return artifact;
    }
}

bool RunShowcaseControllerTests(std::ostream& errors)
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Ui;

    int failureCount = 0;
    const auto expect = [&errors, &failureCount](bool condition, std::string_view message)
    {
        if (!condition)
        {
            errors << "L10 controller test failed: " << message << '\n';
            ++failureCount;
        }
    };

    RuntimeConfig config;
    ActionQueue queue;
    ShowcaseController controller;

    queue.Push(SemanticAction::ToggleProfilerPanel);
    const ActionBatchResult panelBatch = ApplyQueuedActions(queue, config);
    const ShowcaseControllerUpdate panelUpdate = controller.Apply(panelBatch);
    expect(panelUpdate.routed.size() == 1u
        && panelUpdate.routed[0].status == RoutedRequestStatus::ConsumedLocally
        && controller.Panels().profiler,
        "F3 must toggle the lane-local profiler panel without a runtime provider");

    queue.Push(SemanticAction::RequestCapture);
    const ShowcaseControllerUpdate unavailableCapture =
        controller.Apply(ApplyQueuedActions(queue, config));
    expect(unavailableCapture.routed.size() == 1u
        && unavailableCapture.routed[0].status == RoutedRequestStatus::ProviderUnavailable
        && !unavailableCapture.routed[0].reason.empty()
        && controller.TakeForwardedRequests().empty(),
        "capture must distinguish an unavailable provider from a successful zero-work request");

    queue.Push(SemanticAction::ResetHistories);
    const ShowcaseControllerUpdate unavailableReset =
        controller.Apply(ApplyQueuedActions(queue, config));
    expect(unavailableReset.routed.size() == 1u
        && unavailableReset.routed[0].status == RoutedRequestStatus::ProviderUnavailable
        && unavailableReset.requestedResets == ResetResource::None
        && controller.TakePendingResets() == ResetResource::None,
        "a reset that was not delivered to its owner must not leak into the pending reset queue");

    queue.Push(SemanticAction::RequestShaderReload);
    const ShowcaseControllerUpdate unavailableReload =
        controller.Apply(ApplyQueuedActions(queue, config));
    expect(unavailableReload.routed.size() == 1u
            && unavailableReload.routed[0].status == RoutedRequestStatus::ProviderUnavailable
            && controller.TakePendingResets() == ResetResource::None,
        "an unavailable F5 request must not reset renderer-owned histories");

    ShowcaseFeatureAvailability availability;
    availability.captureProvider = true;
    availability.shaderReloadProvider = true;
    controller.SetFeatureAvailability(availability);
    controller.SetSceneResourceGenerations(2u, 4u);
    queue.Push(SemanticAction::RequestCapture);
    const ShowcaseControllerUpdate availableCapture =
        controller.Apply(ApplyQueuedActions(queue, config));
    controller.SetSceneResourceGenerations(3u, 5u);
    std::vector<ForwardedShowcaseRequest> forwarded = controller.TakeForwardedRequests();
    expect(availableCapture.routed.size() == 1u
        && availableCapture.routed[0].status == RoutedRequestStatus::ForwardedToProvider
        && forwarded.size() == 1u
        && forwarded[0].command == RoutedCommand::RequestCapture
        && forwarded[0].configGeneration == 0u
        && forwarded[0].sceneGeneration == 2u
        && forwarded[0].resourceGeneration == 4u
        && controller.SceneGeneration() == 3u
        && controller.ResourceGeneration() == 5u
        && forwarded[0].effectiveRuntimeConfig.render.exposure == config.render.exposure
        && controller.Panels().capture,
        "queued capture identity must freeze config/scene/resource generations even after a later generation bump");

    queue.Push(SemanticAction::RequestShaderReload);
    const ShowcaseControllerUpdate forwardedReload =
        controller.Apply(ApplyQueuedActions(queue, config));
    forwarded = controller.TakeForwardedRequests();
    expect(forwardedReload.routed.size() == 1u
            && forwardedReload.routed[0].status == RoutedRequestStatus::ForwardedToProvider
            && forwarded.size() == 1u
            && forwarded[0].command == RoutedCommand::RequestShaderReload
            && controller.TakePendingResets() == ResetResource::None,
        "forwarding F5 must wait for provider success before requesting resets");
    controller.NotifyExternalReset(ResetCause::ShaderReloadSucceeded);
    const ResetMask reloadSucceededResets = controller.TakePendingResets();
    expect(HasReset(reloadSucceededResets, ResetResource::Accumulation)
            && HasReset(reloadSucceededResets, ResetResource::TemporalHistory)
            && HasReset(reloadSucceededResets, ResetResource::ReservoirHistory)
            && HasReset(reloadSucceededResets, ResetResource::ProfilerStatistics)
            && !HasReset(reloadSucceededResets, ResetResource::AccelerationStructures)
            && controller.TakePendingResets() == ResetResource::None,
        "a provider-reported shader reload success must queue one-shot A/T/Q/P resets");

    queue.Push(SemanticAction::CycleIntegratorForward);
    const ActionBatchResult configBatch = ApplyQueuedActions(queue, config);
    const ShowcaseControllerUpdate configUpdate = controller.Apply(configBatch);
    const ResetMask pendingResets = controller.TakePendingResets();
    expect(configUpdate.configGeneration == 1u
        && HasReset(pendingResets, ResetResource::ProfilerStatistics)
        && controller.TakePendingResets() == ResetResource::None,
        "a committed tuple change must advance generation and expose a one-shot P reset request");

    const float exposureBeforeMixedBatch = config.render.exposure;
    queue.Push(SemanticAction::RequestCapture);
    queue.Push(SemanticAction::IncreaseExposure);
    const ActionBatchResult mixedBatch = ApplyQueuedActions(queue, config);
    const ShowcaseControllerUpdate mixedUpdate = controller.Apply(mixedBatch);
    forwarded = controller.TakeForwardedRequests();
    expect(mixedUpdate.configGeneration == 2u
        && forwarded.size() == 1u
        && forwarded[0].configGeneration == 1u
        && forwarded[0].effectiveRuntimeConfig.render.exposure == exposureBeforeMixedBatch
        && config.render.exposure > exposureBeforeMixedBatch,
        "an earlier capture in a mixed FIFO batch must freeze its pre-mutation config generation and tuple");

    const ShowcaseViewModel model = controller.BuildViewModel(config);
    const auto captureHelp = std::find_if(
        model.help.begin(),
        model.help.end(),
        [](const HelpEntryViewModel& entry)
        {
            return entry.binding != nullptr
                && entry.binding->action == SemanticAction::RequestCapture;
        });
    const auto profilerHelp = std::find_if(
        model.help.begin(),
        model.help.end(),
        [](const HelpEntryViewModel& entry)
        {
            return entry.binding != nullptr
                && entry.binding->action == SemanticAction::ToggleProfilerPanel;
        });
    expect(captureHelp != model.help.end() && captureHelp->enabled
        && profilerHelp != model.help.end() && profilerHelp->enabled,
        "help availability must reflect attached providers while keeping local panels enabled");

    controller.SetConfigGeneration(3u);
    controller.SetSceneResourceGenerations(5u, 8u);
    ShowcaseRuntimeStatus runtimeStatus;
    runtimeStatus.providerId = "renderer.status";
    runtimeStatus.provenance = TelemetryProvenance::SyntheticTest;
    runtimeStatus.availability = TelemetryAvailability::Fresh;
    runtimeStatus.configGeneration = 3u;
    runtimeStatus.sceneGeneration = 5u;
    runtimeStatus.resourceGeneration = 8u;
    runtimeStatus.frameIndex = 77u;
    runtimeStatus.samplesPerPixel = 64u;
    runtimeStatus.gpuFrameMilliseconds = 2.5;
    const ShowcaseViewModel freshRuntime = controller.BuildViewModel(config, runtimeStatus);
    expect(freshRuntime.runtimeStatus.frame == "77"
            && freshRuntime.runtimeStatus.samplesPerPixel == "64"
            && freshRuntime.runtimeStatus.gpuMilliseconds == "2.5"
            && freshRuntime.runtimeStatus.availability == TelemetryAvailability::Fresh,
        "matching config/scene/resource generations must expose fresh runtime observations");

    ++runtimeStatus.resourceGeneration;
    const ShowcaseViewModel staleRuntime = controller.BuildViewModel(config, runtimeStatus);
    expect(staleRuntime.runtimeStatus.availability == TelemetryAvailability::Stale
            && staleRuntime.runtimeStatus.frame == "--"
            && staleRuntime.runtimeStatus.samplesPerPixel == "--"
            && staleRuntime.runtimeStatus.gpuMilliseconds == "--"
            && staleRuntime.runtimeStatus.reason.find("generation mismatch")
                != std::string::npos,
        "a stale runtime generation must expose its reason without displaying old frame/SPP/GPU values");

    Demos::ShowcaseWorkflowStart abStart = MakeAbStart();
    Demos::ShowcaseWorkflowStart staleAbStart = abStart;
    ++staleAbStart.sceneGeneration;
    expect(controller.StartAbComparison(staleAbStart).error
            == Demos::ShowcaseWorkflowError::GenerationMismatch
            && controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::Idle,
        "controller must reject a composition-authored A/B start for stale generations");
    expect(static_cast<bool>(controller.StartAbComparison(abStart))
            && controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::AwaitingA
            && controller.AbRequests().size() == 2u
            && controller.PendingAbRequest() != nullptr,
        "controller must own and expose a valid one-click fixed A/B workflow");
    const Demos::ShowcaseCaptureWorkRequest requestA = controller.AbRequests()[0];
    const Demos::ShowcaseCaptureWorkRequest requestB = controller.AbRequests()[1];
    expect(static_cast<bool>(controller.SubmitAbArtifact(MakeAbArtifact(requestA, "ui-run-a")))
            && controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::AwaitingB,
        "controller must route provider artifact A to the owned workflow");
    expect(static_cast<bool>(controller.SubmitAbArtifact(MakeAbArtifact(requestB, "ui-run-b")))
            && controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::Complete
            && controller.CompletedAbManifest() != nullptr
            && controller.CompletedAbManifestJson().find("showcase-ab-manifest-v1")
                != std::string::npos,
        "controller must preserve and serialize the completed A/B manifest");
    controller.CancelAbComparison();
    expect(controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::Idle
            && controller.CompletedAbManifest() == nullptr
            && controller.CompletedAbManifestJson().empty(),
        "controller cancel must restore a reusable A/B workflow and clear its manifest");

    expect(static_cast<bool>(controller.StartAbComparison(abStart)),
        "scene/resource invalidation fixture must restart");
    const Demos::ShowcaseProviderArtifactRecord oldSceneArtifact =
        MakeAbArtifact(controller.AbRequests()[0], "stale-scene-run");
    controller.SetSceneResourceGenerations(6u, 8u);
    expect(controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::Failed
            && controller.AbWorkflowStatus().error
                == Demos::ShowcaseWorkflowError::GenerationMismatch
            && controller.SubmitAbArtifact(oldSceneArtifact).error
                == Demos::ShowcaseWorkflowError::InvalidState
            && controller.CompletedAbManifest() == nullptr,
        "a scene/resource generation change must visibly invalidate an active pair");
    controller.CancelAbComparison();

    abStart.sceneGeneration = 6u;
    expect(static_cast<bool>(controller.StartAbComparison(abStart)),
        "config invalidation fixture must restart at the current tuple");
    const Demos::ShowcaseProviderArtifactRecord oldConfigArtifact =
        MakeAbArtifact(controller.AbRequests()[0], "stale-config-run");
    queue.Push(SemanticAction::IncreaseExposure);
    static_cast<void>(controller.Apply(ApplyQueuedActions(queue, config)));
    expect(controller.AbWorkflowState() == Demos::ShowcaseWorkflowState::Failed
            && controller.AbWorkflowStatus().error
                == Demos::ShowcaseWorkflowError::GenerationMismatch
            && controller.SubmitAbArtifact(oldConfigArtifact).error
                == Demos::ShowcaseWorkflowError::InvalidState
            && controller.CompletedAbManifest() == nullptr,
        "a RuntimeConfig commit must invalidate an active pair before old artifacts can complete");
    static_cast<void>(controller.TakePendingResets());
    controller.CancelAbComparison();

    return failureCount == 0;
}
