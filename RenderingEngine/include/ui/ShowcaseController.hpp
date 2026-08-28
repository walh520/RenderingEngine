#pragma once

#include "demos/ShowcaseWorkflow.hpp"
#include "ui/RuntimeConfigHarness.hpp"
#include "ui/ShowcaseViewModel.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Ui
{
    struct ShowcasePanelState
    {
        bool help = false;
        bool algorithm = false;
        bool scene = false;
        bool debug = false;
        bool profiler = false;
        bool debugLegend = false;
        bool capture = false;
    };

    enum class RoutedRequestStatus : std::uint8_t
    {
        ConsumedLocally,
        ForwardedToProvider,
        ProviderUnavailable
    };

    struct RoutedRequestResult
    {
        std::uint64_t actionSequence = 0;
        RoutedCommand command = RoutedCommand::None;
        RoutedRequestStatus status = RoutedRequestStatus::ProviderUnavailable;
        std::string_view reason;
    };

    struct ForwardedShowcaseRequest
    {
        std::uint64_t actionSequence = 0;
        RoutedCommand command = RoutedCommand::None;
        ActionEvent event;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        RuntimeConfig effectiveRuntimeConfig;
    };

    struct ShowcaseControllerUpdate
    {
        std::vector<RoutedRequestResult> routed;
        ResetMask requestedResets = ResetResource::None;
        std::uint64_t configGeneration = 0;
    };

    // Consumes the lane-local routed portion of an ActionBatchResult.  It owns
    // only UI visibility and request queues; platform, scene, renderer, and GPU
    // work remains behind explicitly published provider availability.
    class ShowcaseController final
    {
    public:
        explicit ShowcaseController(ShowcaseFeatureAvailability availability = {});

        void SetFeatureAvailability(ShowcaseFeatureAvailability availability) noexcept;
        void SetConfigGeneration(std::uint64_t configGeneration);
        void SetSceneResourceGenerations(
            std::uint64_t sceneGeneration,
            std::uint64_t resourceGeneration);
        [[nodiscard]] const ShowcaseFeatureAvailability& FeatureAvailability() const noexcept;
        [[nodiscard]] ShowcasePanelState& Panels() noexcept;
        [[nodiscard]] const ShowcasePanelState& Panels() const noexcept;
        [[nodiscard]] std::uint64_t ConfigGeneration() const noexcept;
        [[nodiscard]] std::uint64_t SceneGeneration() const noexcept;
        [[nodiscard]] std::uint64_t ResourceGeneration() const noexcept;

        [[nodiscard]] ShowcaseControllerUpdate Apply(const ActionBatchResult& batch);
        // Called only after an owning provider reports that an external event
        // succeeded. Merely forwarding (for example) F5 never queues resets.
        void NotifyExternalReset(ResetCause cause) noexcept;
        [[nodiscard]] ResetMask TakePendingResets() noexcept;
        [[nodiscard]] std::vector<ForwardedShowcaseRequest> TakeForwardedRequests();

        [[nodiscard]] ShowcaseViewModel BuildViewModel(const RuntimeConfig& config) const;
        [[nodiscard]] ShowcaseViewModel BuildViewModel(
            const RuntimeConfig& config,
            const ShowcaseRuntimeStatus& runtimeStatus) const;

        [[nodiscard]] Demos::ShowcaseWorkflowStatus StartAbComparison(
            const Demos::ShowcaseWorkflowStart& start);
        [[nodiscard]] Demos::ShowcaseWorkflowStatus SubmitAbArtifact(
            const Demos::ShowcaseProviderArtifactRecord& artifact);
        void CancelAbComparison() noexcept;
        [[nodiscard]] Demos::ShowcaseWorkflowState AbWorkflowState() const noexcept;
        [[nodiscard]] std::span<const Demos::ShowcaseCaptureWorkRequest>
        AbRequests() const noexcept;
        [[nodiscard]] const Demos::ShowcaseCaptureWorkRequest*
        PendingAbRequest() const noexcept;
        [[nodiscard]] const Demos::ShowcaseAbManifest*
        CompletedAbManifest() const noexcept;
        [[nodiscard]] std::string CompletedAbManifestJson() const;
        [[nodiscard]] const Demos::ShowcaseWorkflowStatus&
        AbWorkflowStatus() const noexcept;

    private:
        [[nodiscard]] bool ProviderAvailable(RoutedCommand command) const noexcept;
        [[nodiscard]] std::string_view ProviderUnavailableReason(RoutedCommand command) const noexcept;
        [[nodiscard]] bool ConsumeLocal(RoutedCommand command) noexcept;

        ShowcaseFeatureAvailability availability_;
        ShowcasePanelState panels_;
        std::uint64_t configGeneration_ = 0;
        std::uint64_t sceneGeneration_ = 0;
        std::uint64_t resourceGeneration_ = 0;
        ResetMask pendingResets_ = ResetResource::None;
        std::vector<ForwardedShowcaseRequest> forwarded_;
        Demos::ShowcaseWorkflow abWorkflow_;
    };
}
