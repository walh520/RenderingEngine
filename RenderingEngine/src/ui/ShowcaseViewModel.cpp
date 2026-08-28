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
                && status.samplesPerPixel.has_value()
                && *status.samplesPerPixel == 0u)
            {
                model.availability = TelemetryAvailability::Invalid;
                model.reason = "fresh runtime status contains zero accumulated SPP";
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
            model.samplesPerPixel = observationIsFresh && status.samplesPerPixel.has_value()
                ? IntegerText(*status.samplesPerPixel)
                : "--";
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
            model.text.append(" | SPP: ").append(model.samplesPerPixel);
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
            { TraversalBackend::LegacyAnalyticGpu, "legacy-analytic-gpu", "Legacy Analytic GPU", "L0" },
            { TraversalBackend::CpuBruteForce, "cpu-brute-force", "CPU Brute Force", "L3" },
            { TraversalBackend::CpuSahBvh, "cpu-sah", "CPU SAH BVH", "L3" },
            { TraversalBackend::GpuFlattenedSahBvh, "gpu-flattened-sah", "GPU Flattened SAH BVH", "L4" },
            { TraversalBackend::GpuLbvh, "gpu-lbvh", "GPU LBVH", "L4" },
            { TraversalBackend::VulkanRayQuery, "ray-query", "Vulkan Ray Query", "L5" },
            { TraversalBackend::VulkanRayTracingPipeline, "rt-pipeline", "Vulkan RT Pipeline", "L5" }
        });

        constexpr auto kIntegrators = std::to_array<EnumOption<Integrator>>({
            { Integrator::Whitted, "whitted", "Whitted", "L0" },
            { Integrator::Pbr, "pbr", "Legacy PBR Comparison", "L0" },
            { Integrator::CpuReferencePathTracer, "cpu-reference", "CPU Reference Path Tracer", "L3" },
            { Integrator::GpuMegakernelPathTracer, "megakernel", "GPU Megakernel Path Tracer", "L6" },
            { Integrator::GpuWavefrontPathTracer, "wavefront", "GPU Wavefront Path Tracer", "L7" }
        });

        constexpr auto kDirectEstimators = std::to_array<EnumOption<DirectLightingEstimator>>({
            { DirectLightingEstimator::LegacyAnalyticDirect, "legacy-analytic-direct", "Legacy Analytic Direct", "L0" },
            { DirectLightingEstimator::BsdfOnly, "bsdf-only", "BSDF Only", "L6" },
            { DirectLightingEstimator::NextEventEstimation, "nee", "Next-event Estimation", "L6" },
            { DirectLightingEstimator::MultipleImportanceSampling, "mis", "Multiple Importance Sampling", "L6" },
            { DirectLightingEstimator::RestirDirectIllumination, "restir-di", "ReSTIR Direct Illumination", "L9" }
        });

        constexpr auto kLightProposals = std::to_array<EnumOption<LightProposalDistribution>>({
            { LightProposalDistribution::LegacyAnalyticLights, "legacy-analytic", "Legacy Analytic Lights", "L0" },
            { LightProposalDistribution::UniformLights, "uniform", "Uniform Lights", "L6" },
            { LightProposalDistribution::PowerWeightedLights, "power", "Power-weighted Lights", "L6" },
            { LightProposalDistribution::EnvironmentImportance, "environment", "Environment Importance", "L6" }
        });

        constexpr auto kReconstructions = std::to_array<EnumOption<ReconstructionMode>>({
            { ReconstructionMode::Raw, "raw", "Raw", "L0" },
            { ReconstructionMode::TemporalAccumulation, "temporal", "Temporal Accumulation", "L8" },
            { ReconstructionMode::TemporalFixedAtrous, "temporal-atrous", "Temporal + Fixed A-Trous", "L8" },
            { ReconstructionMode::Svgf, "svgf", "SVGF", "L8" }
        });

        constexpr auto kDebugViews = std::to_array<EnumOption<DebugView>>({
            { DebugView::Final, "final", "Final", "L0" },
            { DebugView::BaseColor, "base-color", "Base Color", "L0" },
            { DebugView::Normal, "normal", "Normal", "L0" },
            { DebugView::Roughness, "roughness", "Roughness", "L0" },
            { DebugView::Metallic, "metallic", "Metallic", "L0" },
            { DebugView::Emissive, "emissive", "Emissive", "L0" }
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

        [[nodiscard]] CapabilityOptionViewModel BuildLightPresetOption(
            const RuntimeConfig& config,
            const LightSamplingPresetDescriptor& preset)
        {
            RuntimeConfig candidate = config;
            candidate.directLightingEstimator = preset.estimator;
            candidate.lightProposalDistribution = preset.proposal;
            const CapabilityDecision decision = CapabilityTable::Evaluate(candidate);
            const bool built = CapabilityTable::IsBuilt(preset.estimator)
                && CapabilityTable::IsBuilt(preset.proposal);

            CapabilityOptionViewModel option;
            option.value = static_cast<std::uint32_t>(preset.preset);
            option.token = preset.label;
            option.label = preset.label;
            option.owner = preset.owner;
            option.selected = config.directLightingEstimator == preset.estimator
                && config.lightProposalDistribution == preset.proposal;
            option.built = built;
            option.enabled = built && decision.IsSupported();
            option.status = decision.status;
            option.reason = option.enabled ? std::string_view{} : decision.reason;
            return option;
        }

        [[nodiscard]] bool IsConfigAction(SemanticAction action) noexcept
        {
            return action == SemanticAction::AdjustVerticalFov
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
    }

    ModeTupleViewModel BuildModeTupleViewModel(const RuntimeConfig& config) noexcept
    {
        return {
            LabelFor(config.scene, kScenes),
            LabelFor(config.backend, kBackends),
            LabelFor(config.integrator, kIntegrators),
            LabelFor(config.directLightingEstimator, kDirectEstimators),
            LabelFor(config.lightProposalDistribution, kLightProposals),
            LabelFor(config.reconstruction, kReconstructions),
            LabelFor(config.debugView, kDebugViews)
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
        append(tuple.integrator);
        append(tuple.directEstimator);
        append(tuple.lightProposal);
        append(tuple.reconstruction);
        append(tuple.debugView);
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
        model.dimensions.reserve(7);
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
            "integrator", "Integrator", kIntegrators, config,
            [](const RuntimeConfig& value) { return value.integrator; },
            [](RuntimeConfig& value, Integrator option) { value.integrator = option; },
            [](Integrator option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "direct-lighting", "Direct-lighting Estimator", kDirectEstimators, config,
            [](const RuntimeConfig& value) { return value.directLightingEstimator; },
            [](RuntimeConfig& value, DirectLightingEstimator option)
            {
                value.directLightingEstimator = option;
            },
            [](DirectLightingEstimator option) { return CapabilityTable::IsBuilt(option); }));
        model.dimensions.push_back(BuildDimension(
            "light-proposal", "Light Proposal Distribution", kLightProposals, config,
            [](const RuntimeConfig& value) { return value.lightProposalDistribution; },
            [](RuntimeConfig& value, LightProposalDistribution option)
            {
                value.lightProposalDistribution = option;
            },
            [](LightProposalDistribution option) { return CapabilityTable::IsBuilt(option); }));
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

        const std::span<const LightSamplingPresetDescriptor> presets =
            GetLightSamplingPresetCatalog();
        model.lightSamplingPresets.reserve(presets.size());
        for (const LightSamplingPresetDescriptor& preset : presets)
        {
            model.lightSamplingPresets.push_back(BuildLightPresetOption(config, preset));
        }

        const std::span<const ActionBinding> bindings = GetActionCatalog();
        model.help.reserve(bindings.size());
        for (const ActionBinding& binding : bindings)
        {
            model.help.push_back(BuildHelpEntry(binding, config, availability));
        }
        return model;
    }
}
