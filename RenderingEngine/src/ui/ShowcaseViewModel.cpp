#include "ui/ShowcaseViewModel.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>

namespace RenderingEngine::Ui
{
    namespace
    {
        template <typename Enum>
        struct EnumOption
        {
            Enum value;
            std::string_view token;
            std::string_view label;
            std::string_view owner;
        };

        [[nodiscard]] std::string_view CapabilityStatusLabel(
            CapabilityStatus status) noexcept
        {
            switch (status)
            {
            case CapabilityStatus::Supported: return "supported";
            case CapabilityStatus::Unsupported: return "unsupported";
            case CapabilityStatus::InvalidConfiguration: return "invalid-configuration";
            }
            return "invalid-configuration";
        }

        template <typename Integer>
        [[nodiscard]] std::string IntegerText(Integer value)
        {
            char buffer[32]{};
            const auto conversion = std::to_chars(
                std::begin(buffer), std::end(buffer), value);
            return std::string(buffer, conversion.ptr);
        }

        [[nodiscard]] std::string RuntimeDoubleText(double value)
        {
            if (!std::isfinite(value) || value < 0.0)
            {
                return "--";
            }
            if (value == 0.0)
            {
                return "0";
            }

            char buffer[64]{};
            const auto conversion = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value,
                std::chars_format::general,
                std::numeric_limits<double>::max_digits10);
            return std::string(buffer, conversion.ptr);
        }

        [[nodiscard]] bool IsKnown(TelemetryProvenance value) noexcept
        {
            switch (value)
            {
            case TelemetryProvenance::LiveRuntime:
            case TelemetryProvenance::ImportedArtifact:
            case TelemetryProvenance::SyntheticTest:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(TelemetryAvailability value) noexcept
        {
            switch (value)
            {
            case TelemetryAvailability::Unavailable:
            case TelemetryAvailability::Pending:
            case TelemetryAvailability::Fresh:
            case TelemetryAvailability::Stale:
            case TelemetryAvailability::Invalid:
                return true;
            }
            return false;
        }

        [[nodiscard]] RuntimeStatusLineViewModel BuildRuntimeStatusLine(
            const RuntimeConfig& config,
            const ShowcaseRuntimeStatus& status)
        {
            RuntimeStatusLineViewModel model;
            model.providerId = status.providerId;
            model.provenance = status.provenance;
            model.availability = status.availability;
            model.configGeneration = status.configGeneration;
            model.sceneGeneration = status.sceneGeneration;
            model.resourceGeneration = status.resourceGeneration;
            if (!IsKnown(status.provenance) || !IsKnown(status.availability))
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "runtime status has invalid provenance or availability";
            }
            else if (status.availability == TelemetryAvailability::Fresh
                && status.providerId.empty())
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "fresh runtime status requires a provider ID";
            }
            else if (status.availability == TelemetryAvailability::Fresh
                && !status.reason.empty())
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "fresh runtime status cannot carry a stale/unavailable reason";
            }
            else if (status.availability == TelemetryAvailability::Fresh
                && status.resolution.has_value()
                && (status.resolution->width == 0u
                    || status.resolution->height == 0u))
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "fresh runtime status contains an invalid resolution";
            }
            else if (status.availability == TelemetryAvailability::Fresh
                && status.maximumBounce.has_value()
                && *status.maximumBounce == 0u)
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "fresh runtime status contains zero maximum bounce";
            }
            else if (status.availability == TelemetryAvailability::Fresh
                && status.gpuFrameMilliseconds.has_value()
                && (!std::isfinite(*status.gpuFrameMilliseconds)
                    || *status.gpuFrameMilliseconds < 0.0))
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "fresh runtime status contains an invalid GPU duration";
            }
            else if (!status.reason.empty())
            {
                model.reason = status.reason;
            }
            else if (status.availability != TelemetryAvailability::Fresh)
            {
                model.reason = "runtime frame/SPP/GPU observations are unavailable";
            }

            const bool observationIsFresh =
                model.availability == TelemetryAvailability::Fresh
                && model.reason.empty();

            const ShowcaseRuntimeResolution configuredResolution = {
                config.render.width,
                config.render.height
            };
            const ShowcaseRuntimeResolution* resolution = observationIsFresh
                    && status.resolution.has_value()
                ? &*status.resolution
                : &configuredResolution;
            if (resolution->width > 0u && resolution->height > 0u)
            {
                model.resolution = IntegerText(resolution->width);
                model.resolution.push_back('x');
                model.resolution.append(IntegerText(resolution->height));
            }
            else
            {
                model.resolution = "--";
            }
            model.seed = IntegerText(observationIsFresh
                ? status.seed.value_or(config.render.baseSeed)
                : config.render.baseSeed);
            model.frame = observationIsFresh && status.frameIndex.has_value()
                ? IntegerText(*status.frameIndex)
                : "--";
            model.progressiveFilmSpp = observationIsFresh && status.progressiveFilmSpp.has_value()
                ? IntegerText(*status.progressiveFilmSpp)
                : "--";
            model.currentFramePathsPerPixel = observationIsFresh && status.currentFramePathsPerPixel.has_value()
                ? IntegerText(*status.currentFramePathsPerPixel) : "--";
            model.referenceSpp = observationIsFresh && status.referenceSpp.has_value()
                ? IntegerText(*status.referenceSpp) : "--";
            model.temporalHistoryLength = observationIsFresh
                    && status.temporalHistoryLength.has_value()
                ? IntegerText(*status.temporalHistoryLength) : "--";
            model.reservoirM = observationIsFresh && status.reservoirM.has_value()
                ? IntegerText(*status.reservoirM) : "--";
            model.reservoirAge = observationIsFresh && status.reservoirAge.has_value()
                ? IntegerText(*status.reservoirAge) : "--";
            model.reservoirCandidates = observationIsFresh
                    && status.reservoirCandidates.has_value()
                ? IntegerText(*status.reservoirCandidates) : "--";
            model.visibilityRays = observationIsFresh && status.visibilityRays.has_value()
                ? IntegerText(*status.visibilityRays) : "--";
            model.totalTracedRays = observationIsFresh && status.totalTracedRays.has_value()
                ? IntegerText(*status.totalTracedRays) : "--";
            const std::uint32_t bounce =
                observationIsFresh
                    ? status.maximumBounce.value_or(config.render.maximumBounce)
                    : config.render.maximumBounce;
            model.bounce = bounce > 0u ? IntegerText(bounce) : "--";
            model.gpuMilliseconds = observationIsFresh
                    && status.gpuFrameMilliseconds.has_value()
                ? RuntimeDoubleText(*status.gpuFrameMilliseconds)
                : "--";

            model.text.reserve(128u);
            model.text.append("Resolution: ").append(model.resolution);
            model.text.append(" | Seed: ").append(model.seed);
            model.text.append(" | Frame: ").append(model.frame);
            model.text.append(" | Film SPP: ").append(model.progressiveFilmSpp);
            model.text.append(" | Paths/pixel/frame: ").append(model.currentFramePathsPerPixel);
            model.text.append(" | Reference SPP: ").append(model.referenceSpp);
            model.text.append(" | Temporal H: ").append(model.temporalHistoryLength);
            model.text.append(" | Reservoir M/Age: ").append(model.reservoirM)
                .append("/").append(model.reservoirAge);
            model.text.append(" | Candidates: ").append(model.reservoirCandidates);
            model.text.append(" | Visibility/Total rays: ").append(model.visibilityRays)
                .append("/").append(model.totalTracedRays);
            model.text.append(" | Bounce: ").append(model.bounce);
            model.text.append(" | GPU ms: ").append(model.gpuMilliseconds);
            return model;
        }

        [[nodiscard]] CurrentTupleCapabilityViewModel BuildCurrentTupleCapability(
            const RuntimeConfig& config)
        {
            const CapabilityDecision decision = CapabilityTable::Evaluate(config);
            CurrentTupleCapabilityViewModel model;
            model.status = decision.status;
            model.statusLabel = CapabilityStatusLabel(decision.status);
            model.reason = decision.reason;
            model.text.reserve(96u + decision.reason.size());
            model.text.append("Capability: ").append(model.statusLabel);
            model.text.append(" | Reason: ");
            model.text.append(model.reason.empty() ? std::string_view("--") : model.reason);
            return model;
        }

        constexpr auto kScenes = std::to_array<EnumOption<ScenePreset>>({
            { ScenePreset::BaselineGallery, "baseline", "Baseline Gallery", "L0" },
            { ScenePreset::IntersectionBvhLab, "intersection-bvh", "Intersection & BVH Lab", "L2" },
            { ScenePreset::WhittedOpticsRoom, "whitted-optics", "Whitted Optics Room", "L2" },
            { ScenePreset::CornellBox, "cornell", "Cornell Box", "L2" },
            { ScenePreset::GgxMisMaterialLab, "ggx-mis", "GGX & MIS Material Lab", "L2" },
            { ScenePreset::EnvironmentSamplingDome, "environment-dome", "Environment Sampling Dome", "L2" },
            { ScenePreset::SponzaTraversalHall, "sponza", "Sponza Traversal Hall", "L2" },
            { ScenePreset::BackendParityBenchmark, "backend-parity", "Backend Parity Benchmark", "L2" },
            { ScenePreset::TemporalStabilityCorridor, "temporal-stability", "Temporal Stability Corridor", "L2" },
            { ScenePreset::ManyLightsRestirArena, "many-lights", "Many Lights / ReSTIR Arena", "L2" }
        });

        constexpr auto kBackends = std::to_array<EnumOption<TraversalBackend>>({
            { TraversalBackend::CanonicalLinearGpu, "canonical-linear-gpu", "Canonical Linear GPU", "L0/L4" },
            { TraversalBackend::CpuBruteForce, "cpu-brute-force", "CPU Brute Force", "L3" },
            { TraversalBackend::CpuSahBvh, "cpu-sah", "CPU SAH BVH", "L3" },
            { TraversalBackend::GpuFlattenedSahBvh, "gpu-flattened-sah", "GPU Flattened SAH BVH", "L4" },
            { TraversalBackend::GpuLbvh, "gpu-lbvh", "GPU LBVH", "L4" },
            { TraversalBackend::VulkanRayQuery, "ray-query", "Vulkan Ray Query", "L5" },
            { TraversalBackend::VulkanRayTracingPipeline, "rt-pipeline", "Vulkan RT Pipeline", "L5" }
        });

        constexpr auto kTransportModels = std::to_array<EnumOption<TransportModel>>({
            { TransportModel::Pbr, "pbr", "PBR Path Transport", "L6" },
            { TransportModel::Whitted, "whitted", "Whitted Specular Transport", "L0" }
        });

        constexpr auto kExecutionArchitectures =
            std::to_array<EnumOption<ExecutionArchitecture>>({
                { ExecutionArchitecture::Staged, "staged", "GPU Staged", "L6" },
                { ExecutionArchitecture::CpuReference, "cpu-reference", "CPU Reference", "L3" },
                { ExecutionArchitecture::Megakernel, "megakernel", "GPU Megakernel", "L6" },
                { ExecutionArchitecture::Wavefront, "wavefront", "GPU Wavefront", "L7" }
        });

        constexpr auto kDirectEstimators = std::to_array<EnumOption<DirectLightingEstimator>>({
            { DirectLightingEstimator::BsdfOnly, "bsdf-only", "BSDF Only", "L6" },
            { DirectLightingEstimator::NextEventEstimation, "nee", "Next-event Estimation", "L6" },
            { DirectLightingEstimator::MultipleImportanceSampling, "mis", "Multiple Importance Sampling", "L6" },
            { DirectLightingEstimator::RestirDirectIllumination, "restir-di", "ReSTIR Direct Illumination", "L9" }
        });

        constexpr auto kLightSelections =
            std::to_array<EnumOption<LightSelectionStrategy>>({
                { LightSelectionStrategy::Uniform, "uniform", "Uniform Light Selection", "L6" },
                { LightSelectionStrategy::PowerWeighted, "power", "Power-weighted Light Selection", "L6" }
        });

        constexpr auto kEnvironmentSamplers =
            std::to_array<EnumOption<EnvironmentDirectionSampler>>({
                { EnvironmentDirectionSampler::UniformSphere, "uniform-sphere", "Uniform Sphere", "L6" },
                { EnvironmentDirectionSampler::ImportanceMap, "importance-map", "Environment Importance Map", "L6" }
        });

        constexpr auto kReconstructions = std::to_array<EnumOption<ReconstructionMode>>({
            { ReconstructionMode::CurrentFrame, "current-frame", "Current Frame Noisy", "L8" },
            { ReconstructionMode::ProgressiveMean, "progressive-mean", "Progressive Mean (static film)", "L8" },
            { ReconstructionMode::TemporalAccumulation, "temporal", "Temporal Accumulation", "L8" },
            { ReconstructionMode::SpatialFixedAtrous, "atrous-spatial", "Spatial A-Trous (no temporal history)", "L8" },
            { ReconstructionMode::Svgf, "svgf", "SVGF", "L8" }
        });

        constexpr auto kDebugViews = std::to_array<EnumOption<DebugView>>({
            { DebugView::Final, "final", "Final", "L0" },
            { DebugView::BaseColor, "base-color", "Base Color", "L0" },
            { DebugView::Normal, "normal", "Normal", "L0" },
            { DebugView::Roughness, "roughness", "Roughness", "L0" },
            { DebugView::Metallic, "metallic", "Metallic", "L0" },
            { DebugView::Emissive, "emissive", "Emissive", "L0" },
            { DebugView::Motion, "motion", "Motion Vectors", "L8" },
            { DebugView::HistoryLength, "history-length", "History Length", "L8" },
            { DebugView::Moments, "moments", "Luminance Moments", "L8" },
            { DebugView::Variance, "variance", "Variance", "L8" },
            { DebugView::TemporalAcceptance, "temporal-acceptance", "Temporal Acceptance", "L8" },
            { DebugView::TemporalRejectReasons, "temporal-reject-reasons", "Temporal Reject Reasons", "L8" },
            { DebugView::ReservoirM, "reservoir-m", "Reservoir M", "L9" },
            { DebugView::ReservoirWeight, "reservoir-weight", "Reservoir Weight", "L9" },
            { DebugView::ReservoirLightId, "reservoir-light-id", "Reservoir Light ID", "L9" },
            { DebugView::ReservoirSource, "reservoir-source", "Candidate Source", "L9" },
            { DebugView::ReservoirReuse, "reservoir-reuse", "Reuse Source", "L9" },
            { DebugView::ReservoirRejection, "reservoir-rejection", "Reuse Rejection", "L9" },
            { DebugView::WinnerVisibility, "winner-visibility", "Winner Visibility", "L9" }
        });

        constexpr auto kShadowMethods = std::to_array<EnumOption<ShadowMethod>>({
            { ShadowMethod::Pcf, "pcf", "Percentage-closer Filtering", "L0" },
            { ShadowMethod::Pcss, "pcss", "Percentage-closer Soft Shadows", "L0" },
            { ShadowMethod::Physical, "physical", "Physical Shadows", "L0" }
        });

        template <typename Enum, std::size_t Size>
        [[nodiscard]] std::string_view LabelFor(
            Enum value,
            const std::array<EnumOption<Enum>, Size>& options) noexcept
        {
            for (const EnumOption<Enum>& option : options)
            {
                if (option.value == value)
                {
                    return option.label;
                }
            }
            return "Invalid";
        }

        template <typename Enum, std::size_t Size, typename Getter, typename Setter, typename BuiltQuery>
        [[nodiscard]] CapabilityDimensionViewModel BuildDimension(
            std::string_view id,
            std::string_view label,
            const std::array<EnumOption<Enum>, Size>& source,
            const RuntimeConfig& config,
            Getter getter,
            Setter setter,
            BuiltQuery isBuilt)
        {
            CapabilityDimensionViewModel dimension;
            dimension.id = id;
            dimension.label = label;
            dimension.options.reserve(Size);
            const Enum current = getter(config);
            for (const EnumOption<Enum>& sourceOption : source)
            {
                RuntimeConfig candidate = config;
                setter(candidate, sourceOption.value);
                const CapabilityDecision decision = CapabilityTable::Evaluate(candidate);
                const bool built = isBuilt(sourceOption.value);

                CapabilityOptionViewModel option;
                option.value = static_cast<std::uint32_t>(sourceOption.value);
                option.token = sourceOption.token;
                option.label = sourceOption.label;
                option.owner = sourceOption.owner;
                option.selected = sourceOption.value == current;
                option.built = built;
                option.enabled = built && decision.IsSupported();
                option.status = decision.status;
                option.reason = option.enabled ? std::string_view{} : decision.reason;
                dimension.options.push_back(option);
            }
            return dimension;
        }

        [[nodiscard]] bool IsConfigAction(SemanticAction action) noexcept
        {
            return action == SemanticAction::AdjustVerticalFov
                || action == SemanticAction::CycleSceneVariantForward
                || action == SemanticAction::CycleSceneVariantBackward
                || action
                    == SemanticAction::RestoreCurrentSceneRecommendedProfile
                || (action >= SemanticAction::CycleBackendForward
                    && action <= SemanticAction::CycleDebugViewBackward)
                || (action >= SemanticAction::DecreaseMaximumBounce
                    && action <= SemanticAction::IncreaseRenderScale)
                || (action >= SemanticAction::SelectScene0
                    && action <= SemanticAction::SelectScene9);
        }

        [[nodiscard]] std::string_view RoutedOwner(SemanticAction action) noexcept
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
            case SemanticAction::TogglePointerCapture:
            case SemanticAction::ReleasePointerCapture:
            case SemanticAction::RequestExit:
            case SemanticAction::RequestShaderReload:
                return "L1/L0";
            case SemanticAction::ResetShowcaseCamera:
            case SemanticAction::ToggleAnimationPause:
            case SemanticAction::StepAnimationFrame:
                return "L2/L0";
            default:
                return "L10/L0";
            }
        }

        [[nodiscard]] bool IsLocallyAvailable(SemanticAction action) noexcept
        {
            switch (action)
            {
            case SemanticAction::ToggleHelp:
            case SemanticAction::ToggleAlgorithmPanel:
            case SemanticAction::ToggleProfilerPanel:
            case SemanticAction::ToggleDebugLegend:
            case SemanticAction::PrintCurrentReview:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool HasProvider(
            SemanticAction action,
            const ShowcaseFeatureAvailability& availability) noexcept
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
            case SemanticAction::TogglePointerCapture:
            case SemanticAction::ReleasePointerCapture:
            case SemanticAction::RequestExit:
                return availability.platformCommands;
            case SemanticAction::ResetShowcaseCamera:
            case SemanticAction::ToggleAnimationPause:
            case SemanticAction::StepAnimationFrame:
            case SemanticAction::ToggleComparisonLock:
                return availability.sceneCommands;
            case SemanticAction::ResetHistories:
                return availability.historyResetConsumer;
            case SemanticAction::RequestCapture:
                return availability.captureProvider;
            case SemanticAction::RequestShaderReload:
                return availability.shaderReloadProvider;
            case SemanticAction::ToggleSplitScreenComparison:
                return availability.splitScreenProvider;
            case SemanticAction::RequestBenchmark:
                return availability.benchmarkProvider;
            case SemanticAction::RequestReferenceComparison:
                return availability.referenceProvider;
            default:
                return IsLocallyAvailable(action);
            }
        }

        [[nodiscard]] std::string_view RoutedReason(SemanticAction action) noexcept
        {
            switch (action)
            {
            case SemanticAction::RequestCapture:
                return "capture orchestration is built; attach a renderer readback provider to enable it";
            case SemanticAction::RequestBenchmark:
                return "benchmark orchestration is built; attach a live timing/counter provider to enable it";
            case SemanticAction::RequestReferenceComparison:
                return "reference metrics are built; attach render and reference image providers to enable them";
            case SemanticAction::RequestShaderReload:
                return "attach the platform transactional shader-reload provider";
            case SemanticAction::ToggleSplitScreenComparison:
                return "A/B state orchestration is built; attach a split-screen renderer provider";
            case SemanticAction::ToggleHelp:
            case SemanticAction::ToggleAlgorithmPanel:
            case SemanticAction::ToggleProfilerPanel:
            case SemanticAction::ToggleDebugLegend:
            case SemanticAction::PrintCurrentReview:
                return {};
            case SemanticAction::ResetHistories:
                return "attach the renderer history-reset consumer";
            default:
                return "attach the owning platform, scene, or renderer provider";
            }
        }

        [[nodiscard]] HelpEntryViewModel BuildHelpEntry(
            const ActionBinding& binding,
            const RuntimeConfig& config,
            const ShowcaseFeatureAvailability& availability)
        {
            HelpEntryViewModel entry;
            entry.binding = &binding;
            if (!IsConfigAction(binding.action))
            {
                entry.owner = RoutedOwner(binding.action);
                entry.enabled = HasProvider(binding.action, availability);
                entry.reason = entry.enabled ? std::string_view{} : RoutedReason(binding.action);
                return entry;
            }

            // Algorithm cycles are capability-filtered again at application time.
            // At least the supported current value is a legal (possibly no-op)
            // cycle target, so the help entry itself remains enabled.
            if (binding.action >= SemanticAction::CycleBackendForward
                && binding.action <= SemanticAction::CycleDebugViewBackward)
            {
                entry.enabled = CapabilityTable::Evaluate(config).IsSupported();
                entry.owner = "L10";
                if (!entry.enabled)
                {
                    entry.reason = CapabilityTable::Evaluate(config).reason;
                }
                return entry;
            }

            RuntimeConfig candidate = config;
            switch (binding.action)
            {
            case SemanticAction::RestoreCurrentSceneRecommendedProfile:
            {
                const SceneRecommendedProfile* const profile =
                    FindSceneRecommendedProfile(config.scene);
                if (profile == nullptr)
                {
                    entry.owner = "L10";
                    entry.enabled = false;
                    entry.reason = "current scene has no registered recommended teaching profile";
                    return entry;
                }
                candidate = MakeSceneRecommendedConfig(config, *profile);
                entry.owner = "L10";
                break;
            }
            case SemanticAction::AdjustVerticalFov:
                candidate.render.verticalFovDegrees += 2.0f;
                break;
            case SemanticAction::DecreaseMaximumBounce:
                if (candidate.render.maximumBounce > 0) --candidate.render.maximumBounce;
                break;
            case SemanticAction::IncreaseMaximumBounce:
                ++candidate.render.maximumBounce;
                break;
            case SemanticAction::DecreaseExposure:
                candidate.render.exposure /= 1.189207115f;
                break;
            case SemanticAction::IncreaseExposure:
                candidate.render.exposure *= 1.189207115f;
                break;
            case SemanticAction::DecreaseRenderScale:
                candidate.render.renderScale = 0.75f;
                break;
            case SemanticAction::IncreaseRenderScale:
                candidate.render.renderScale = 1.5f;
                break;
            default:
                if (binding.action >= SemanticAction::SelectScene0
                    && binding.action <= SemanticAction::SelectScene9)
                {
                    const auto scene = static_cast<std::uint32_t>(binding.action)
                        - static_cast<std::uint32_t>(SemanticAction::SelectScene0);
                    candidate.scene = static_cast<ScenePreset>(scene);
                    entry.owner = kScenes[scene].owner;
                }
                break;
            }

            const CapabilityDecision decision = CapabilityTable::Evaluate(candidate);
            entry.enabled = decision.IsSupported();
            if (entry.owner.empty())
            {
                entry.owner = "L10/L0";
            }
            entry.reason = entry.enabled ? std::string_view{} : decision.reason;
            return entry;
        }

        [[nodiscard]] SceneRecommendationViewModel BuildSceneRecommendation(
            const RuntimeConfig& config)
        {
            SceneRecommendationViewModel model;
            const SceneRecommendedProfile* const profile =
                FindSceneRecommendedProfile(config.scene);
            if (profile == nullptr)
            {
                model.text = "Teaching recommendation: unregistered";
                return model;
            }

            model.registered = true;
            model.matches = MatchesSceneRecommendedProfile(config, *profile);
            model.stableId = profile->stableId;
            model.text.reserve(320u);
            model.text.append("Teaching recommendation (F11): ");
            model.text.append(model.matches ? "MATCH" : "DIFFERS");
            model.text.append(" | ").append(profile->stableId);
            model.text.append(" | ").append(LabelFor(profile->backend, kBackends));
            model.text.append(" | ").append(LabelFor(
                profile->transportModel, kTransportModels));
            model.text.append(" | ").append(LabelFor(
                profile->executionArchitecture, kExecutionArchitectures));
            model.text.append(" | ").append(LabelFor(
                profile->directLightingEstimator, kDirectEstimators));
            model.text.append(" | ").append(LabelFor(
                profile->lightSelection, kLightSelections));
            model.text.append(" | ").append(LabelFor(
                profile->environmentSampler, kEnvironmentSamplers));
            model.text.append(" | ").append(LabelFor(
                profile->reconstruction, kReconstructions));
            model.text.append(" | ").append(LabelFor(profile->debugView, kDebugViews));
            model.text.append(" | ").append(LabelFor(
                profile->shadowMethod, kShadowMethods));
            model.text.append(" | Bounce ").append(IntegerText(profile->maximumBounce));
            if (profile->restir.has_value())
            {
                model.text.append(" | ReSTIR 100 / temporal-spatial / explicitly-biased"
                    " / C1 / N5 / M32 / H20 / Compare 8:1 / animation off");
            }
            return model;
        }
    }

    ModeTupleViewModel BuildModeTupleViewModel(const RuntimeConfig& config) noexcept
    {
        return {
            LabelFor(config.scene, kScenes),
            LabelFor(config.backend, kBackends),
            LabelFor(config.transportModel, kTransportModels),
            LabelFor(config.executionArchitecture, kExecutionArchitectures),
            LabelFor(config.directLightingEstimator, kDirectEstimators),
            LabelFor(config.lightSelection, kLightSelections),
            LabelFor(config.environmentSampler, kEnvironmentSamplers),
            LabelFor(config.reconstruction, kReconstructions),
            LabelFor(config.debugView, kDebugViews),
            LabelFor(config.shadowMethod, kShadowMethods)
        };
    }

    std::string FormatModeTuple(const ModeTupleViewModel& tuple)
    {
        std::string text;
        text.reserve(192);
        const auto append = [&text](std::string_view value)
        {
            if (!text.empty())
            {
                text += " | ";
            }
            text.append(value);
        };
        append(tuple.scene);
        append(tuple.backend);
        append(tuple.transportModel);
        append(tuple.executionArchitecture);
        append(tuple.directEstimator);
        append(tuple.lightSelection);
        append(tuple.environmentSampler);
        append(tuple.reconstruction);
        append(tuple.debugView);
        append(tuple.shadowMethod);
        return text;
    }

    ShowcaseViewModel BuildShowcaseViewModel(const RuntimeConfig& config)
    {
        return BuildShowcaseViewModel(config, {}, {});
    }

    ShowcaseViewModel BuildShowcaseViewModel(
        const RuntimeConfig& config,
        const ShowcaseFeatureAvailability& availability)
    {
        return BuildShowcaseViewModel(config, availability, {});
    }

    ShowcaseViewModel BuildShowcaseViewModel(
        const RuntimeConfig& config,
        const ShowcaseFeatureAvailability& availability,
        const ShowcaseRuntimeStatus& runtimeStatus)
    {
        ShowcaseRuntimeStatus untrustedStatus = runtimeStatus;
        std::string reason;
        if (untrustedStatus.availability == TelemetryAvailability::Fresh)
        {
            untrustedStatus.availability = TelemetryAvailability::Invalid;
            reason = "fresh runtime status requires an expected generation tuple";
            untrustedStatus.reason = reason;
        }
        return BuildShowcaseViewModel(
            config,
            availability,
            untrustedStatus,
            {
                untrustedStatus.configGeneration,
                untrustedStatus.sceneGeneration,
                untrustedStatus.resourceGeneration
            });
    }

    ShowcaseViewModel BuildShowcaseViewModel(
        const RuntimeConfig& config,
        const ShowcaseFeatureAvailability& availability,
        const ShowcaseRuntimeStatus& runtimeStatus,
        const ShowcaseRuntimeGenerationTuple& expectedGenerations)
    {
        ShowcaseRuntimeStatus effectiveStatus = runtimeStatus;
        std::string generationReason;
        if (effectiveStatus.availability == TelemetryAvailability::Fresh
            && (effectiveStatus.configGeneration
                    != expectedGenerations.configGeneration
                || effectiveStatus.sceneGeneration
                    != expectedGenerations.sceneGeneration
                || effectiveStatus.resourceGeneration
                    != expectedGenerations.resourceGeneration))
        {
            effectiveStatus.availability = TelemetryAvailability::Stale;
            generationReason = "runtime status generation mismatch";
            effectiveStatus.reason = generationReason;
        }
        ShowcaseViewModel model;
        model.tuple = BuildModeTupleViewModel(config);
        model.tupleText = FormatModeTuple(model.tuple);
        model.runtimeStatus = BuildRuntimeStatusLine(config, effectiveStatus);
        model.currentTupleCapability = BuildCurrentTupleCapability(config);
        model.sceneRecommendation = BuildSceneRecommendation(config);
        model.dimensions.reserve(10);
        model.dimensions.push_back(BuildDimension(
            "scene", "Scene", kScenes, config,
            [](const RuntimeConfig& value) { return value.scene; },
            [](RuntimeConfig& value, ScenePreset option) { value.scene = option; },
            [](ScenePreset option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "backend", "Traversal Backend", kBackends, config,
            [](const RuntimeConfig& value) { return value.backend; },
            [](RuntimeConfig& value, TraversalBackend option) { value.backend = option; },
            [](TraversalBackend option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "transport", "Transport Model", kTransportModels, config,
            [](const RuntimeConfig& value) { return value.transportModel; },
            [](RuntimeConfig& value, TransportModel option)
            {
                value.transportModel = option;
            },
            [](TransportModel option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "execution", "Execution Architecture", kExecutionArchitectures, config,
            [](const RuntimeConfig& value) { return value.executionArchitecture; },
            [](RuntimeConfig& value, ExecutionArchitecture option)
            {
                value.executionArchitecture = option;
            },
            [](ExecutionArchitecture option)
            {
                return CapabilityTable::IsBuilt(option);
            }));
        model.dimensions.push_back(BuildDimension(
            "direct-lighting", "Direct-lighting Estimator", kDirectEstimators, config,
            [](const RuntimeConfig& value) { return value.directLightingEstimator; },
            [](RuntimeConfig& value, DirectLightingEstimator option)
            {
                value.directLightingEstimator = option;
            },
            [](DirectLightingEstimator option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "light-selection", "Discrete Light Selection", kLightSelections, config,
            [](const RuntimeConfig& value) { return value.lightSelection; },
            [](RuntimeConfig& value, LightSelectionStrategy option)
            {
                value.lightSelection = option;
            },
            [](LightSelectionStrategy option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "environment-sampler", "Environment Direction Sampler",
            kEnvironmentSamplers, config,
            [](const RuntimeConfig& value) { return value.environmentSampler; },
            [](RuntimeConfig& value, EnvironmentDirectionSampler option)
            {
                value.environmentSampler = option;
            },
            [](EnvironmentDirectionSampler option)
            {
                return CapabilityTable::IsBuilt(option);
            }));
        model.dimensions.push_back(BuildDimension(
            "reconstruction", "Reconstruction", kReconstructions, config,
            [](const RuntimeConfig& value) { return value.reconstruction; },
            [](RuntimeConfig& value, ReconstructionMode option) { value.reconstruction = option; },
            [](ReconstructionMode option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "debug-view", "Debug View", kDebugViews, config,
            [](const RuntimeConfig& value) { return value.debugView; },
            [](RuntimeConfig& value, DebugView option) { value.debugView = option; },
            [](DebugView option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "shadow-method", "Shadow Method", kShadowMethods, config,
            [](const RuntimeConfig& value) { return value.shadowMethod; },
            [](RuntimeConfig& value, ShadowMethod option) { value.shadowMethod = option; },
            [](ShadowMethod option) { return CapabilityTable::IsBuilt(option); }));

        const std::span<const ActionBinding> bindings = GetActionCatalog();
        model.help.reserve(bindings.size());
        for (const ActionBinding& binding : bindings)
        {
            model.help.push_back(BuildHelpEntry(binding, config, availability));
        }
        return model;
    }
}
