#include "ui/RuntimeConfigHarness.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <utility>

namespace RenderingEngine::Ui
{
    namespace
    {
        constexpr ResetMask kManualHistoryReset = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory;
        constexpr std::string_view kScene6UnavailableReason =
            "Scene 6 unavailable: Sponza asset/provider gate";
        constexpr std::string_view kAlreadyRecommendedReason =
            "current scene already uses its recommended teaching profile";
        constexpr std::string_view kMissingRecommendedReason =
            "current scene has no registered recommended teaching profile";

        constexpr auto kBackends = std::to_array({
            TraversalBackend::CanonicalLinearGpu,
            TraversalBackend::GpuFlattenedSahBvh,
            TraversalBackend::VulkanRayQuery
        });

        constexpr auto kTransportModels = std::to_array({
            TransportModel::Pbr,
            TransportModel::Whitted
        });

        constexpr auto kExecutionArchitectures = std::to_array({
            ExecutionArchitecture::Staged,
            ExecutionArchitecture::Megakernel,
            ExecutionArchitecture::Wavefront
        });

        constexpr auto kDirectEstimators = std::to_array({
            DirectLightingEstimator::BsdfOnly,
            DirectLightingEstimator::NextEventEstimation,
            DirectLightingEstimator::MultipleImportanceSampling,
            DirectLightingEstimator::RestirDirectIllumination
        });

        constexpr auto kLightSelections = std::to_array({
            LightSelectionStrategy::Uniform,
            LightSelectionStrategy::PowerWeighted
        });

        constexpr auto kEnvironmentSamplers = std::to_array({
            EnvironmentDirectionSampler::UniformSphere,
            EnvironmentDirectionSampler::ImportanceMap
        });

        constexpr auto kReconstructions = std::to_array({
            ReconstructionMode::CurrentFrame,
            ReconstructionMode::ProgressiveMean,
            ReconstructionMode::TemporalAccumulation,
            ReconstructionMode::SpatialFixedAtrous,
            ReconstructionMode::Svgf
        });

        constexpr auto kDebugViews = std::to_array({
            DebugView::Final,
            DebugView::BaseColor,
            DebugView::Normal,
            DebugView::Roughness,
            DebugView::Metallic,
            DebugView::Emissive,
            DebugView::Motion,
            DebugView::HistoryLength,
            DebugView::Moments,
            DebugView::Variance,
            DebugView::TemporalAcceptance,
            DebugView::TemporalRejectReasons,
            DebugView::ReservoirM,
            DebugView::ReservoirWeight,
            DebugView::ReservoirLightId,
            DebugView::ReservoirSource,
            DebugView::ReservoirReuse,
            DebugView::ReservoirRejection,
            DebugView::WinnerVisibility
        });

        constexpr auto kShadowMethods = std::to_array({
            ShadowMethod::Pcf,
            ShadowMethod::Pcss,
            ShadowMethod::Physical
        });

        constexpr auto kRenderScales = std::to_array({
            0.25f,
            0.5f,
            0.75f,
            1.0f,
            1.5f,
            2.0f
        });

        constexpr float kExposureQuarterStop = 1.189207115f;
        constexpr float kFovDegreesPerWheelStep = 2.0f;

        [[nodiscard]] bool RestirSettingsDiffer(
            const RestirSettings& left,
            const RestirSettings& right) noexcept
        {
            return left.manyLightsTier != right.manyLightsTier
                || left.reuseStage != right.reuseStage
                || left.biasMode != right.biasMode
                || left.initialCandidatesPerPixel != right.initialCandidatesPerPixel
                || left.spatialNeighbors != right.spatialNeighbors
                || left.maximumReservoirM != right.maximumReservoirM
                || left.maximumHistoryAge != right.maximumHistoryAge
                || left.comparisonCandidateBudgetPerPixel
                    != right.comparisonCandidateBudgetPerPixel
                || left.comparisonVisibilityBudgetPerPixel
                    != right.comparisonVisibilityBudgetPerPixel
                || left.animateLights != right.animateLights
                || left.animateRigidOccluders != right.animateRigidOccluders;
        }

        [[nodiscard]] ResetMask RecommendedProfileResetMask(
            const RuntimeConfig& current,
            const RuntimeConfig& candidate,
            const SceneRecommendedProfile& profile) noexcept
        {
            ResetMask resets = ResetResource::None;
            if (current.backend != candidate.backend)
            {
                resets |= ResetMaskFor(ResetCause::BackendChanged);
            }
            if (current.transportModel != candidate.transportModel
                || current.executionArchitecture
                    != candidate.executionArchitecture
                || current.directLightingEstimator
                    != candidate.directLightingEstimator
                || current.lightSelection != candidate.lightSelection
                || current.environmentSampler != candidate.environmentSampler)
            {
                resets |= ResetMaskFor(
                    ResetCause::TransportExecutionOrSamplingChanged);
            }
            if (current.reconstruction != candidate.reconstruction)
            {
                resets |= ResetMaskFor(ResetCause::ReconstructionChanged);
            }
            if (current.shadowMethod != candidate.shadowMethod)
            {
                resets |= ResetMaskFor(ResetCause::ShadingParameterChanged);
            }
            if (current.render.maximumBounce
                != candidate.render.maximumBounce)
            {
                resets |= ResetMaskFor(ResetCause::SamplingParameterChanged);
            }

            // DebugView is display-only. Scene identity is never changed by
            // F11. Only scene 9 owns ReSTIR fields in the recommendation.
            if (profile.restir.has_value()
                && RestirSettingsDiffer(current.restir, candidate.restir))
            {
                resets |= ResetMaskFor(ResetCause::SamplingParameterChanged);
                if (current.restir.manyLightsTier
                        != candidate.restir.manyLightsTier
                    || current.restir.animateLights
                        != candidate.restir.animateLights
                    || current.restir.animateRigidOccluders
                        != candidate.restir.animateRigidOccluders)
                {
                    resets |= ResetMaskFor(ResetCause::TopologyOrStableIdChanged);
                }
            }
            return resets;
        }

        [[nodiscard]] bool RequiresPressedPhase(SemanticAction action) noexcept
        {
            switch (action)
            {
            case SemanticAction::MoveForward:
            case SemanticAction::MoveBackward:
            case SemanticAction::MoveLeft:
            case SemanticAction::MoveRight:
            case SemanticAction::MoveDown:
            case SemanticAction::MoveUp:
            case SemanticAction::FastMovementModifier:
            case SemanticAction::FineMovementModifier:
            case SemanticAction::Look:
            case SemanticAction::AdjustMovementSpeed:
            case SemanticAction::AdjustVerticalFov:
                return false;
            default:
                return true;
            }
        }

        [[nodiscard]] ActionApplyResult IgnoredPhase(const QueuedAction& queued) noexcept
        {
            ActionApplyResult result;
            result.queued = queued;
            result.status = ActionApplyStatus::IgnoredInputPhase;
            result.reason = "discrete semantic actions apply only on press, never repeat";
            return result;
        }

        [[nodiscard]] ActionApplyResult Reject(
            const QueuedAction& queued,
            CapabilityDecision decision) noexcept
        {
            ActionApplyResult result;
            result.queued = queued;
            result.status = ActionApplyStatus::Rejected;
            result.capabilityStatus = decision.status;
            result.reason = decision.reason;
            return result;
        }

        [[nodiscard]] ActionApplyResult CommitCandidate(
            const QueuedAction& queued,
            RuntimeConfig& liveConfig,
            RuntimeConfig candidate,
            bool changed,
            ResetMask resets)
        {
            const CapabilityDecision decision = CapabilityTable::Evaluate(candidate);
            if (!decision.IsSupported())
            {
                if ((queued.event.action == SemanticAction::SelectScene6
                        || queued.event.action
                            == SemanticAction::RestoreCurrentSceneRecommendedProfile)
                    && candidate.scene == ScenePreset::SponzaTraversalHall)
                {
                    return Reject(queued, {
                        decision.status,
                        kScene6UnavailableReason
                    });
                }
                return Reject(queued, decision);
            }

            ActionApplyResult result;
            result.queued = queued;
            if (!changed)
            {
                result.status = ActionApplyStatus::AcceptedNoConfigChange;
                return result;
            }

            // The live object is replaced only after the complete candidate has
            // passed the shared capability table.  No individual field is ever
            // written into liveConfig before that decision.
            using std::swap;
            swap(liveConfig, candidate);
            result.status = ActionApplyStatus::ConfigCommitted;
            result.requestedResets = resets;
            return result;
        }

        template <typename Enum, std::size_t Size, typename Getter, typename Setter, typename BuiltQuery>
        [[nodiscard]] ActionApplyResult ApplyBuiltCycle(
            const QueuedAction& queued,
            RuntimeConfig& liveConfig,
            const std::array<Enum, Size>& values,
            bool forward,
            Getter getter,
            Setter setter,
            BuiltQuery isBuilt,
            ResetMask resets)
        {
            const Enum current = getter(liveConfig);
            std::size_t currentIndex = forward ? Size - 1u : 0u;
            for (std::size_t index = 0; index < Size; ++index)
            {
                if (values[index] == current)
                {
                    currentIndex = index;
                    break;
                }
            }

            for (std::size_t step = 1; step <= Size; ++step)
            {
                const std::size_t offset = step % Size;
                const std::size_t index = forward
                    ? (currentIndex + offset) % Size
                    : (currentIndex + Size - offset) % Size;
                const Enum selected = values[index];
                if (!isBuilt(selected))
                {
                    continue;
                }

                RuntimeConfig candidate = liveConfig;
                setter(candidate, selected);
                if (!CapabilityTable::Evaluate(candidate).IsSupported())
                {
                    continue;
                }
                const bool changed = selected != current;
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    resets);
            }

            return Reject(queued, CapabilityTable::Evaluate(liveConfig));
        }

        [[nodiscard]] std::optional<ScenePreset> SceneForAction(SemanticAction action) noexcept
        {
            if (action < SemanticAction::SelectScene0 || action > SemanticAction::SelectScene9)
            {
                return std::nullopt;
            }
            const auto index = static_cast<std::uint32_t>(action)
                - static_cast<std::uint32_t>(SemanticAction::SelectScene0);
            return static_cast<ScenePreset>(index);
        }

        [[nodiscard]] float AdjacentRenderScale(float current, bool increase) noexcept
        {
            if (increase)
            {
                const auto found = std::find_if(
                    kRenderScales.begin(),
                    kRenderScales.end(),
                    [current](float value) { return value > current; });
                return found == kRenderScales.end() ? kRenderScales.back() : *found;
            }

            const auto found = std::find_if(
                kRenderScales.rbegin(),
                kRenderScales.rend(),
                [current](float value) { return value < current; });
            return found == kRenderScales.rend() ? kRenderScales.front() : *found;
        }

        [[nodiscard]] RoutedCommand CommandFor(SemanticAction action) noexcept
        {
            switch (action)
            {
            case SemanticAction::MoveForward: return RoutedCommand::MoveForward;
            case SemanticAction::MoveBackward: return RoutedCommand::MoveBackward;
            case SemanticAction::MoveLeft: return RoutedCommand::MoveLeft;
            case SemanticAction::MoveRight: return RoutedCommand::MoveRight;
            case SemanticAction::MoveDown: return RoutedCommand::MoveDown;
            case SemanticAction::MoveUp: return RoutedCommand::MoveUp;
            case SemanticAction::FastMovementModifier: return RoutedCommand::FastMovementModifier;
            case SemanticAction::FineMovementModifier: return RoutedCommand::FineMovementModifier;
            case SemanticAction::Look: return RoutedCommand::Look;
            case SemanticAction::AdjustMovementSpeed: return RoutedCommand::AdjustMovementSpeed;
            case SemanticAction::TogglePointerCapture: return RoutedCommand::TogglePointerCapture;
            case SemanticAction::ReleasePointerCapture: return RoutedCommand::ReleasePointerCapture;
            case SemanticAction::RequestExit: return RoutedCommand::RequestExit;
            case SemanticAction::ResetShowcaseCamera: return RoutedCommand::ResetShowcaseCamera;
            case SemanticAction::ToggleAnimationPause: return RoutedCommand::ToggleAnimationPause;
            case SemanticAction::StepAnimationFrame: return RoutedCommand::StepAnimationFrame;
            case SemanticAction::ResetHistories: return RoutedCommand::ResetHistories;
            case SemanticAction::ToggleComparisonLock: return RoutedCommand::ToggleComparisonLock;
            case SemanticAction::ToggleHelp: return RoutedCommand::ToggleHelp;
            case SemanticAction::ToggleAlgorithmPanel: return RoutedCommand::ToggleAlgorithmPanel;
            case SemanticAction::ToggleProfilerPanel: return RoutedCommand::ToggleProfilerPanel;
            case SemanticAction::RequestCapture: return RoutedCommand::RequestCapture;
            case SemanticAction::RequestShaderReload: return RoutedCommand::RequestShaderReload;
            case SemanticAction::ToggleSplitScreenComparison: return RoutedCommand::ToggleSplitScreenComparison;
            case SemanticAction::ToggleDebugLegend: return RoutedCommand::ToggleDebugLegend;
            case SemanticAction::RequestBenchmark: return RoutedCommand::RequestBenchmark;
            case SemanticAction::RequestReferenceComparison: return RoutedCommand::RequestReferenceComparison;
            case SemanticAction::PrintCurrentReview: return RoutedCommand::PrintCurrentReview;
            default: return RoutedCommand::None;
            }
        }

        [[nodiscard]] ActionApplyResult Route(const QueuedAction& queued) noexcept
        {
            ActionApplyResult result;
            result.queued = queued;
            result.status = ActionApplyStatus::RoutedToOwner;
            result.routedCommand = CommandFor(queued.event.action);
            if (queued.event.action == SemanticAction::ResetHistories)
            {
                result.requestedResets = kManualHistoryReset;
            }
            else if (queued.event.action == SemanticAction::ResetShowcaseCamera)
            {
                result.requestedResets = ResetMaskFor(ResetCause::CameraDiscontinuity);
            }
            return result;
        }

        [[nodiscard]] ActionApplyResult ApplyOne(
            const QueuedAction& queued,
            RuntimeConfig& liveConfig,
            const SceneVariantCatalog& sceneVariants)
        {
            const SemanticAction action = queued.event.action;
            if (queued.event.phase == ActionPhase::Repeated
                || (RequiresPressedPhase(action) && queued.event.phase != ActionPhase::Pressed))
            {
                return IgnoredPhase(queued);
            }

            if (action == SemanticAction::CycleSceneVariantForward
                || action == SemanticAction::CycleSceneVariantBackward)
            {
                if (!sceneVariants)
                    return Reject(queued, { CapabilityStatus::Unsupported, "scene variant provider is unavailable" });
                std::vector<std::string> variants;
                try { variants = sceneVariants(liveConfig); }
                catch (const std::exception&)
                {
                    return Reject(queued, { CapabilityStatus::Unsupported, "scene variant provider failed; configuration unchanged" });
                }
                if (variants.empty())
                    return Reject(queued, { CapabilityStatus::Unsupported, "current scene has no available variants" });
                const auto found = std::find(variants.begin(), variants.end(), liveConfig.sceneVariant);
                if (!liveConfig.sceneVariant.empty() && found == variants.end())
                    return Reject(queued, { CapabilityStatus::InvalidConfiguration, "current scene variant is not registered" });
                const std::size_t current = liveConfig.sceneVariant.empty() ? 0u
                    : static_cast<std::size_t>(found - variants.begin());
                const std::size_t selected = action == SemanticAction::CycleSceneVariantForward
                    ? (current + 1u) % variants.size()
                    : (current + variants.size() - 1u) % variants.size();
                RuntimeConfig candidate = liveConfig;
                candidate.sceneVariant = variants[selected];
                return CommitCandidate(queued, liveConfig, std::move(candidate),
                    selected != current, ResetMaskFor(ResetCause::SceneChanged));
            }
            const bool forward = action == SemanticAction::CycleBackendForward
                || action == SemanticAction::CycleTransportModelForward
                || action == SemanticAction::CycleExecutionArchitectureForward
                || action == SemanticAction::CycleDirectLightingForward
                || action == SemanticAction::CycleLightSelectionForward
                || action == SemanticAction::CycleEnvironmentSamplerForward
                || action == SemanticAction::CycleShadowForward
                || action == SemanticAction::CycleReconstructionForward
                || action == SemanticAction::CycleDebugViewForward;

            switch (action)
            {
            case SemanticAction::RestoreCurrentSceneRecommendedProfile:
            {
                const SceneRecommendedProfile* const profile =
                    FindSceneRecommendedProfile(liveConfig.scene);
                if (profile == nullptr)
                {
                    return Reject(queued, {
                        CapabilityStatus::InvalidConfiguration,
                        kMissingRecommendedReason
                    });
                }

                RuntimeConfig candidate = MakeSceneRecommendedConfig(
                    liveConfig,
                    *profile);
                const bool changed = !MatchesSceneRecommendedProfile(
                    liveConfig,
                    *profile);
                const ResetMask resets = RecommendedProfileResetMask(
                    liveConfig,
                    candidate,
                    *profile);
                ActionApplyResult result = CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    resets);
                if (result.status == ActionApplyStatus::AcceptedNoConfigChange)
                {
                    result.reason = kAlreadyRecommendedReason;
                }
                return result;
            }
            case SemanticAction::CycleBackendForward:
            case SemanticAction::CycleBackendBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kBackends,
                    forward,
                    [](const RuntimeConfig& config) { return config.backend; },
                    [](RuntimeConfig& config, TraversalBackend value) { config.backend = value; },
                    [](TraversalBackend value) { return CapabilityTable::IsBuilt(value); },
                    ResetMaskFor(ResetCause::BackendChanged));
            case SemanticAction::CycleTransportModelForward:
            case SemanticAction::CycleTransportModelBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kTransportModels,
                    forward,
                    [](const RuntimeConfig& config) { return config.transportModel; },
                    [](RuntimeConfig& config, TransportModel value)
                    {
                        config.transportModel = value;
                    },
                    [](TransportModel value) { return CapabilityTable::IsBuilt(value); },
                    ResetMaskFor(ResetCause::TransportExecutionOrSamplingChanged));
            case SemanticAction::CycleExecutionArchitectureForward:
            case SemanticAction::CycleExecutionArchitectureBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kExecutionArchitectures,
                    forward,
                    [](const RuntimeConfig& config)
                    {
                        return config.executionArchitecture;
                    },
                    [](RuntimeConfig& config, ExecutionArchitecture value)
                    {
                        config.executionArchitecture = value;
                    },
                    [](ExecutionArchitecture value)
                    {
                        return CapabilityTable::IsBuilt(value);
                    },
                    ResetMaskFor(ResetCause::TransportExecutionOrSamplingChanged));
            case SemanticAction::CycleDirectLightingForward:
            case SemanticAction::CycleDirectLightingBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kDirectEstimators,
                    forward,
                    [](const RuntimeConfig& config)
                    {
                        return config.directLightingEstimator;
                    },
                    [](RuntimeConfig& config, DirectLightingEstimator value)
                    {
                        config.directLightingEstimator = value;
                    },
                    [](DirectLightingEstimator value)
                    {
                        return CapabilityTable::IsBuilt(value);
                    },
                    ResetMaskFor(ResetCause::TransportExecutionOrSamplingChanged));
            case SemanticAction::CycleLightSelectionForward:
            case SemanticAction::CycleLightSelectionBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kLightSelections,
                    forward,
                    [](const RuntimeConfig& config)
                    {
                        return config.lightSelection;
                    },
                    [](RuntimeConfig& config, LightSelectionStrategy value)
                    {
                        config.lightSelection = value;
                    },
                    [](LightSelectionStrategy value)
                    {
                        return CapabilityTable::IsBuilt(value);
                    },
                    ResetMaskFor(ResetCause::TransportExecutionOrSamplingChanged));
            case SemanticAction::CycleEnvironmentSamplerForward:
            case SemanticAction::CycleEnvironmentSamplerBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kEnvironmentSamplers,
                    forward,
                    [](const RuntimeConfig& config)
                    {
                        return config.environmentSampler;
                    },
                    [](RuntimeConfig& config, EnvironmentDirectionSampler value)
                    {
                        config.environmentSampler = value;
                    },
                    [](EnvironmentDirectionSampler value)
                    {
                        return CapabilityTable::IsBuilt(value);
                    },
                    ResetMaskFor(ResetCause::TransportExecutionOrSamplingChanged));
            case SemanticAction::CycleShadowForward:
            case SemanticAction::CycleShadowBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kShadowMethods,
                    forward,
                    [](const RuntimeConfig& config) { return config.shadowMethod; },
                    [](RuntimeConfig& config, ShadowMethod value) { config.shadowMethod = value; },
                    [](ShadowMethod value) { return CapabilityTable::IsBuilt(value); },
                    ResetMaskFor(ResetCause::ShadingParameterChanged));
            case SemanticAction::CycleReconstructionForward:
            case SemanticAction::CycleReconstructionBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kReconstructions,
                    forward,
                    [](const RuntimeConfig& config) { return config.reconstruction; },
                    [](RuntimeConfig& config, ReconstructionMode value) { config.reconstruction = value; },
                    [](ReconstructionMode value) { return CapabilityTable::IsBuilt(value); },
                    ResetMaskFor(ResetCause::ReconstructionChanged));
            case SemanticAction::CycleDebugViewForward:
            case SemanticAction::CycleDebugViewBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kDebugViews,
                    forward,
                    [](const RuntimeConfig& config) { return config.debugView; },
                    [](RuntimeConfig& config, DebugView value) { config.debugView = value; },
                    [](DebugView value) { return CapabilityTable::IsBuilt(value); },
                    ResetMaskFor(ResetCause::DisplayOnly));
            case SemanticAction::DecreaseMaximumBounce:
            case SemanticAction::IncreaseMaximumBounce:
            {
                RuntimeConfig candidate = liveConfig;
                if (action == SemanticAction::IncreaseMaximumBounce)
                {
                    ++candidate.render.maximumBounce;
                }
                else if (candidate.render.maximumBounce > 0)
                {
                    --candidate.render.maximumBounce;
                }
                const bool changed = candidate.render.maximumBounce
                    != liveConfig.render.maximumBounce;
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    ResetMaskFor(ResetCause::SamplingParameterChanged));
            }
            case SemanticAction::DecreaseExposure:
            case SemanticAction::IncreaseExposure:
            {
                RuntimeConfig candidate = liveConfig;
                candidate.render.exposure = action == SemanticAction::IncreaseExposure
                    ? candidate.render.exposure * kExposureQuarterStop
                    : candidate.render.exposure / kExposureQuarterStop;
                const bool changed = candidate.render.exposure != liveConfig.render.exposure;
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    ResetMaskFor(ResetCause::DisplayOnly));
            }
            case SemanticAction::DecreaseRenderScale:
            case SemanticAction::IncreaseRenderScale:
            {
                RuntimeConfig candidate = liveConfig;
                candidate.render.renderScale = AdjacentRenderScale(
                    candidate.render.renderScale,
                    action == SemanticAction::IncreaseRenderScale);
                const bool changed = candidate.render.renderScale
                    != liveConfig.render.renderScale;
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    ResetMaskFor(ResetCause::ResolutionRenderScaleOrFovChanged));
            }
            case SemanticAction::AdjustVerticalFov:
            {
                if (queued.event.phase != ActionPhase::Axis)
                {
                    return IgnoredPhase(queued);
                }
                const float wheelDelta = queued.event.valueY != 0.0f
                    ? queued.event.valueY
                    : queued.event.valueX;
                RuntimeConfig candidate = liveConfig;
                candidate.render.verticalFovDegrees += wheelDelta * kFovDegreesPerWheelStep;
                const bool changed = wheelDelta != 0.0f;
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    ResetMaskFor(ResetCause::ResolutionRenderScaleOrFovChanged));
            }
            default:
                break;
            }

            if (const std::optional<ScenePreset> scene = SceneForAction(action); scene.has_value())
            {
                RuntimeConfig candidate = liveConfig;
                // Scene digits never mutate the algorithm tuple. If the
                // current backend/transport/execution tuple cannot consume the selected
                // scene, CommitCandidate rejects the complete candidate and
                // preserves the old scene and algorithms.
                candidate.scene = *scene;
                const bool changed = *scene != liveConfig.scene;
                if (changed) candidate.sceneVariant.clear();
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    ResetMaskFor(ResetCause::SceneChanged));
            }

            const RoutedCommand command = CommandFor(action);
            if (command != RoutedCommand::None)
            {
                return Route(queued);
            }

            ActionApplyResult result;
            result.queued = queued;
            result.status = ActionApplyStatus::Rejected;
            result.capabilityStatus = CapabilityStatus::InvalidConfiguration;
            result.reason = "semantic action value is invalid";
            return result;
        }
    }

    struct ActionQueueAccess
    {
        [[nodiscard]] static QueuedAction PopFront(ActionQueue& queue)
        {
            QueuedAction queued = queue.actions_.front();
            queue.actions_.pop_front();
            return queued;
        }
    };

    std::uint64_t ActionQueue::Push(ActionEvent event)
    {
        const std::uint64_t sequence = nextSequence_++;
        actions_.push_back({ sequence, event });
        return sequence;
    }

    std::uint64_t ActionQueue::Push(
        SemanticAction action,
        ActionPhase phase,
        float valueX,
        float valueY)
    {
        return Push({ action, phase, valueX, valueY });
    }

    bool ActionQueue::Empty() const noexcept
    {
        return actions_.empty();
    }

    std::size_t ActionQueue::Size() const noexcept
    {
        return actions_.size();
    }

    ActionBatchResult ApplyQueuedActions(ActionQueue& queue, RuntimeConfig& liveConfig,
        const SceneVariantCatalog& sceneVariants)
    {
        ActionBatchResult batch;
        batch.actions.reserve(queue.Size());
        while (!queue.Empty())
        {
            ActionApplyResult result = ApplyOne(ActionQueueAccess::PopFront(queue), liveConfig, sceneVariants);
            result.effectiveRuntimeConfig = liveConfig;
            batch.requestedResets |= result.requestedResets;
            batch.actions.push_back(result);
        }
        return batch;
    }
}
