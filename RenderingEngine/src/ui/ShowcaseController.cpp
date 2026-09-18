#include "ui/ShowcaseController.hpp"

#include <string>
#include <utility>

namespace RenderingEngine::Ui
{
    ShowcaseController::ShowcaseController(ShowcaseFeatureAvailability availability)
        : availability_(availability)
    {
    }

    void ShowcaseController::SetFeatureAvailability(
        ShowcaseFeatureAvailability availability) noexcept
    {
        availability_ = availability;
    }

    void ShowcaseController::SetConfigGeneration(std::uint64_t configGeneration)
    {
        if (configGeneration != configGeneration_)
        {
            abWorkflow_.InvalidateGenerations(
                "The active A/B pair was invalidated by a config generation change.");
        }
        configGeneration_ = configGeneration;
    }

    void ShowcaseController::SetSceneResourceGenerations(
        std::uint64_t sceneGeneration,
        std::uint64_t resourceGeneration)
    {
        if (sceneGeneration != sceneGeneration_
            || resourceGeneration != resourceGeneration_)
        {
            abWorkflow_.InvalidateGenerations(
                "The active A/B pair was invalidated by a scene/resource generation change.");
        }
        sceneGeneration_ = sceneGeneration;
        resourceGeneration_ = resourceGeneration;
    }

    const ShowcaseFeatureAvailability& ShowcaseController::FeatureAvailability() const noexcept
    {
        return availability_;
    }

    ShowcasePanelState& ShowcaseController::Panels() noexcept
    {
        return panels_;
    }

    const ShowcasePanelState& ShowcaseController::Panels() const noexcept
    {
        return panels_;
    }

    std::uint64_t ShowcaseController::ConfigGeneration() const noexcept
    {
        return configGeneration_;
    }

    std::uint64_t ShowcaseController::SceneGeneration() const noexcept
    {
        return sceneGeneration_;
    }

    std::uint64_t ShowcaseController::ResourceGeneration() const noexcept
    {
        return resourceGeneration_;
    }

    bool ShowcaseController::ProviderAvailable(RoutedCommand command) const noexcept
    {
        switch (command)
        {
        case RoutedCommand::MoveForward:
        case RoutedCommand::MoveBackward:
        case RoutedCommand::MoveLeft:
        case RoutedCommand::MoveRight:
        case RoutedCommand::MoveDown:
        case RoutedCommand::MoveUp:
        case RoutedCommand::FastMovementModifier:
        case RoutedCommand::FineMovementModifier:
        case RoutedCommand::Look:
        case RoutedCommand::AdjustMovementSpeed:
        case RoutedCommand::TogglePointerCapture:
        case RoutedCommand::ReleasePointerCapture:
        case RoutedCommand::RequestExit:
            return availability_.platformCommands;
        case RoutedCommand::ResetShowcaseCamera:
        case RoutedCommand::ToggleAnimationPause:
        case RoutedCommand::StepAnimationFrame:
        case RoutedCommand::ToggleComparisonLock:
            return availability_.sceneCommands;
        case RoutedCommand::ResetHistories:
            return availability_.historyResetConsumer;
        case RoutedCommand::RequestCapture:
            return availability_.captureProvider;
        case RoutedCommand::RequestShaderReload:
            return availability_.shaderReloadProvider;
        case RoutedCommand::ToggleSplitScreenComparison:
            return availability_.splitScreenProvider;
        case RoutedCommand::RequestBenchmark:
            return availability_.benchmarkProvider;
        case RoutedCommand::RequestReferenceComparison:
            return availability_.referenceProvider;
        case RoutedCommand::ToggleHelp:
        case RoutedCommand::ToggleAlgorithmPanel:
        case RoutedCommand::ToggleProfilerPanel:
        case RoutedCommand::ToggleDebugLegend:
        case RoutedCommand::PrintCurrentReview:
        case RoutedCommand::None:
            return false;
        }
        return false;
    }

    std::string_view ShowcaseController::ProviderUnavailableReason(
        RoutedCommand command) const noexcept
    {
        switch (command)
        {
        case RoutedCommand::RequestCapture:
            return "renderer readback/capture provider is unavailable";
        case RoutedCommand::RequestBenchmark:
            return "live timing/counter provider is unavailable";
        case RoutedCommand::RequestReferenceComparison:
            return "render/reference image provider is unavailable";
        case RoutedCommand::RequestShaderReload:
            return "transactional shader-reload provider is unavailable";
        case RoutedCommand::ToggleSplitScreenComparison:
            return "split-screen renderer provider is unavailable";
        case RoutedCommand::ResetHistories:
            return "renderer history-reset consumer is unavailable";
        case RoutedCommand::ResetShowcaseCamera:
        case RoutedCommand::ToggleAnimationPause:
        case RoutedCommand::StepAnimationFrame:
        case RoutedCommand::ToggleComparisonLock:
            return "scene/camera provider is unavailable";
        default:
            return "platform or renderer provider is unavailable";
        }
    }

    bool ShowcaseController::ConsumeLocal(RoutedCommand command) noexcept
    {
        switch (command)
        {
        case RoutedCommand::ToggleHelp:
            panels_.help = !panels_.help;
            return true;
        case RoutedCommand::ToggleAlgorithmPanel:
            panels_.algorithm = !panels_.algorithm;
            return true;
        case RoutedCommand::ToggleProfilerPanel:
            panels_.profiler = !panels_.profiler;
            return true;
        case RoutedCommand::ToggleDebugLegend:
            panels_.debugLegend = !panels_.debugLegend;
            return true;
        case RoutedCommand::PrintCurrentReview:
            return true;
        default:
            return false;
        }
    }

    ShowcaseControllerUpdate ShowcaseController::Apply(const ActionBatchResult& batch)
    {
        ShowcaseControllerUpdate update;
        ResetMask acceptedResets = ResetResource::None;

        for (const ActionApplyResult& action : batch.actions)
        {
            if (action.status == ActionApplyStatus::ConfigCommitted)
            {
                abWorkflow_.InvalidateGenerations(
                    "The active A/B pair was invalidated by a RuntimeConfig commit.");
                ++configGeneration_;
                acceptedResets |= action.requestedResets;
            }
            if (action.status != ActionApplyStatus::RoutedToOwner
                || action.routedCommand == RoutedCommand::None)
            {
                continue;
            }

            RoutedRequestResult routed;
            routed.actionSequence = action.queued.sequence;
            routed.command = action.routedCommand;
            if (ConsumeLocal(action.routedCommand))
            {
                routed.status = RoutedRequestStatus::ConsumedLocally;
            }
            else if (ProviderAvailable(action.routedCommand))
            {
                routed.status = RoutedRequestStatus::ForwardedToProvider;
                forwarded_.push_back({
                    action.queued.sequence,
                    action.routedCommand,
                    action.queued.event,
                    configGeneration_,
                    sceneGeneration_,
                    resourceGeneration_,
                    action.effectiveRuntimeConfig
                });
                acceptedResets |= action.requestedResets;
                if (action.routedCommand == RoutedCommand::RequestCapture)
                {
                    panels_.capture = true;
                }
            }
            else
            {
                routed.status = RoutedRequestStatus::ProviderUnavailable;
                routed.reason = ProviderUnavailableReason(action.routedCommand);
            }
            update.routed.push_back(routed);
        }

        update.requestedResets = acceptedResets;
        pendingResets_ |= acceptedResets;
        update.configGeneration = configGeneration_;
        return update;
    }

    ResetMask ShowcaseController::TakePendingResets() noexcept
    {
        const ResetMask result = pendingResets_;
        pendingResets_ = ResetResource::None;
        return result;
    }

    void ShowcaseController::NotifyExternalReset(ResetCause cause) noexcept
    {
        pendingResets_ |= ResetMaskFor(cause);
    }

    std::vector<ForwardedShowcaseRequest> ShowcaseController::TakeForwardedRequests()
    {
        std::vector<ForwardedShowcaseRequest> result = std::move(forwarded_);
        forwarded_.clear();
        return result;
    }

    ShowcaseViewModel ShowcaseController::BuildViewModel(const RuntimeConfig& config) const
    {
        return BuildShowcaseViewModel(config, availability_);
    }

    ShowcaseViewModel ShowcaseController::BuildViewModel(
        const RuntimeConfig& config,
        const ShowcaseRuntimeStatus& runtimeStatus) const
    {
        return BuildShowcaseViewModel(
            config,
            availability_,
            runtimeStatus,
            { configGeneration_, sceneGeneration_, resourceGeneration_ });
    }

    Demos::ShowcaseWorkflowStatus ShowcaseController::StartAbComparison(
        const Demos::ShowcaseWorkflowStart& start)
    {
        if (abWorkflow_.State() != Demos::ShowcaseWorkflowState::Idle)
        {
            return {
                Demos::ShowcaseWorkflowError::InvalidState,
                "A/B workflow is already active; cancel it before starting another pair."
            };
        }
        if (!availability_.captureProvider
            || start.captureProvider.state != Demos::Availability::Available)
        {
            return {
                Demos::ShowcaseWorkflowError::ProviderUnavailable,
                "A/B capture provider is unavailable."
            };
        }
        if (start.configGeneration != configGeneration_
            || start.sceneGeneration != sceneGeneration_
            || start.resourceGeneration != resourceGeneration_)
        {
            return {
                Demos::ShowcaseWorkflowError::GenerationMismatch,
                "A/B start generations do not match the controller's current tuple."
            };
        }
        return abWorkflow_.Start(start);
    }

    Demos::ShowcaseWorkflowStatus ShowcaseController::SubmitAbArtifact(
        const Demos::ShowcaseProviderArtifactRecord& artifact)
    {
        const Demos::ShowcaseCaptureWorkRequest* const pending =
            abWorkflow_.PendingRequest();
        if (pending != nullptr
            && (pending->configGeneration != configGeneration_
                || pending->sceneGeneration != sceneGeneration_
                || pending->resourceGeneration != resourceGeneration_))
        {
            abWorkflow_.InvalidateGenerations(
                "The active A/B pair no longer matches the controller's current tuple.");
            return abWorkflow_.LastStatus();
        }
        return abWorkflow_.Submit(artifact);
    }

    void ShowcaseController::CancelAbComparison() noexcept
    {
        abWorkflow_.Cancel();
    }

    Demos::ShowcaseWorkflowState ShowcaseController::AbWorkflowState() const noexcept
    {
        return abWorkflow_.State();
    }

    std::span<const Demos::ShowcaseCaptureWorkRequest>
    ShowcaseController::AbRequests() const noexcept
    {
        return abWorkflow_.Requests();
    }

    const Demos::ShowcaseCaptureWorkRequest*
    ShowcaseController::PendingAbRequest() const noexcept
    {
        return abWorkflow_.PendingRequest();
    }

    const Demos::ShowcaseAbManifest*
    ShowcaseController::CompletedAbManifest() const noexcept
    {
        return abWorkflow_.CompletedManifest();
    }

    std::string ShowcaseController::CompletedAbManifestJson() const
    {
        const Demos::ShowcaseAbManifest* const manifest = CompletedAbManifest();
        return manifest == nullptr
            ? std::string{}
            : Demos::BuildShowcaseAbManifestJson(*manifest);
    }

    const Demos::ShowcaseWorkflowStatus&
    ShowcaseController::AbWorkflowStatus() const noexcept
    {
        return abWorkflow_.LastStatus();
    }
}
