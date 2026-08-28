#include "ui/RuntimeConfigHarness.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace RenderingEngine::Ui
{
    namespace
    {
        constexpr ResetMask kManualHistoryReset = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory;

        constexpr auto kLightSamplingPresets = std::to_array<LightSamplingPresetDescriptor>({
            {
                LightSamplingPreset::LegacyAnalytic,
                DirectLightingEstimator::LegacyAnalyticDirect,
                LightProposalDistribution::LegacyAnalyticLights,
                "Legacy Analytic Direct + Legacy Analytic Lights",
                "L0"
            },
            {
                LightSamplingPreset::BsdfOnly,
                DirectLightingEstimator::BsdfOnly,
                LightProposalDistribution::UniformLights,
                "BSDF Only + Uniform Lights",
                "L6"
            },
            {
                LightSamplingPreset::NeeUniform,
                DirectLightingEstimator::NextEventEstimation,
                LightProposalDistribution::UniformLights,
                "NEE + Uniform Light Proposal",
                "L6"
            },
            {
                LightSamplingPreset::NeePowerWeighted,
                DirectLightingEstimator::NextEventEstimation,
                LightProposalDistribution::PowerWeightedLights,
                "NEE + Power-weighted Light Proposal",
                "L6"
            },
            {
                LightSamplingPreset::MisPowerWeighted,
                DirectLightingEstimator::MultipleImportanceSampling,
                LightProposalDistribution::PowerWeightedLights,
                "NEE + MIS + Power-weighted Proposal",
                "L6"
            },
            {
                LightSamplingPreset::RestirDirectIllumination,
                DirectLightingEstimator::RestirDirectIllumination,
                LightProposalDistribution::PowerWeightedLights,
                "ReSTIR DI + Power-weighted Proposal",
                "L9"
            }
        });

        constexpr auto kBackends = std::to_array({
            TraversalBackend::LegacyAnalyticGpu,
            TraversalBackend::CpuBruteForce,
            TraversalBackend::CpuSahBvh,
            TraversalBackend::GpuFlattenedSahBvh,
            TraversalBackend::GpuLbvh,
            TraversalBackend::VulkanRayQuery,
            TraversalBackend::VulkanRayTracingPipeline
        });

        constexpr auto kIntegrators = std::to_array({
            Integrator::Whitted,
            Integrator::Pbr,
            Integrator::CpuReferencePathTracer,
            Integrator::GpuMegakernelPathTracer,
            Integrator::GpuWavefrontPathTracer
        });

        constexpr auto kReconstructions = std::to_array({
            ReconstructionMode::Raw,
            ReconstructionMode::TemporalAccumulation,
            ReconstructionMode::TemporalFixedAtrous,
            ReconstructionMode::Svgf
        });

        constexpr auto kDebugViews = std::to_array({
            DebugView::Final,
            DebugView::BaseColor,
            DebugView::Normal,
            DebugView::Roughness,
            DebugView::Metallic,
            DebugView::Emissive
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

        [[nodiscard]] ActionApplyResult ApplyLightSamplingCycle(
            const QueuedAction& queued,
            RuntimeConfig& liveConfig,
            bool forward)
        {
            const auto matchesCurrent = [&liveConfig](const LightSamplingPresetDescriptor& preset)
            {
                return preset.estimator == liveConfig.directLightingEstimator
                    && preset.proposal == liveConfig.lightProposalDistribution;
            };

            std::size_t currentIndex = forward ? kLightSamplingPresets.size() - 1u : 0u;
            for (std::size_t index = 0; index < kLightSamplingPresets.size(); ++index)
            {
                if (matchesCurrent(kLightSamplingPresets[index]))
                {
                    currentIndex = index;
                    break;
                }
            }

            for (std::size_t step = 1; step <= kLightSamplingPresets.size(); ++step)
            {
                const std::size_t offset = step % kLightSamplingPresets.size();
                const std::size_t index = forward
                    ? (currentIndex + offset) % kLightSamplingPresets.size()
                    : (currentIndex + kLightSamplingPresets.size() - offset)
                        % kLightSamplingPresets.size();
                const LightSamplingPresetDescriptor& preset = kLightSamplingPresets[index];
                if (!CapabilityTable::IsBuilt(preset.estimator)
                    || !CapabilityTable::IsBuilt(preset.proposal))
                {
                    continue;
                }

                RuntimeConfig candidate = liveConfig;
                candidate.directLightingEstimator = preset.estimator;
                candidate.lightProposalDistribution = preset.proposal;
                if (!CapabilityTable::Evaluate(candidate).IsSupported())
                {
                    continue;
                }

                const bool changed = !matchesCurrent(preset);
                return CommitCandidate(
                    queued,
                    liveConfig,
                    std::move(candidate),
                    changed,
                    ResetMaskFor(ResetCause::IntegratorOrLightSamplingChanged));
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
            RuntimeConfig& liveConfig)
        {
            const SemanticAction action = queued.event.action;
            if (queued.event.phase == ActionPhase::Repeated
                || (RequiresPressedPhase(action) && queued.event.phase != ActionPhase::Pressed))
            {
                return IgnoredPhase(queued);
            }

            const bool forward = action == SemanticAction::CycleBackendForward
                || action == SemanticAction::CycleIntegratorForward
                || action == SemanticAction::CycleLightSamplingForward
                || action == SemanticAction::CycleReconstructionForward
                || action == SemanticAction::CycleDebugViewForward;

            switch (action)
            {
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
            case SemanticAction::CycleIntegratorForward:
            case SemanticAction::CycleIntegratorBackward:
                return ApplyBuiltCycle(
                    queued,
                    liveConfig,
                    kIntegrators,
                    forward,
                    [](const RuntimeConfig& config) { return config.integrator; },
                    [](RuntimeConfig& config, Integrator value) { config.integrator = value; },
                    [](Integrator value) { return CapabilityTable::IsBuilt(value); },
                    ResetMaskFor(ResetCause::IntegratorOrLightSamplingChanged));
            case SemanticAction::CycleLightSamplingForward:
            case SemanticAction::CycleLightSamplingBackward:
                return ApplyLightSamplingCycle(queued, liveConfig, forward);
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
                candidate.scene = *scene;
                const bool changed = *scene != liveConfig.scene;
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

    std::span<const LightSamplingPresetDescriptor> GetLightSamplingPresetCatalog() noexcept
    {
        return kLightSamplingPresets;
    }

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

    ActionBatchResult ApplyQueuedActions(ActionQueue& queue, RuntimeConfig& liveConfig)
    {
        ActionBatchResult batch;
        batch.actions.reserve(queue.Size());
        while (!queue.Empty())
        {
            ActionApplyResult result = ApplyOne(ActionQueueAccess::PopFront(queue), liveConfig);
            result.effectiveRuntimeConfig = liveConfig;
            batch.requestedResets |= result.requestedResets;
            batch.actions.push_back(result);
        }
        return batch;
    }
}
