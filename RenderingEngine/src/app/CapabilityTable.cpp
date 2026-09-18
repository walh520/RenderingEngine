#include "app/CapabilityTable.hpp"
#include "app/IntegratedModuleRegistry.hpp"

#include <cmath>
#include <type_traits>

namespace RenderingEngine
{
    namespace
    {
        [[nodiscard]] constexpr CapabilityDecision Supported() noexcept
        {
            return {};
        }

        [[nodiscard]] constexpr CapabilityDecision Unsupported(std::string_view reason) noexcept
        {
            return { CapabilityStatus::Unsupported, reason };
        }

        [[nodiscard]] constexpr CapabilityDecision Invalid(std::string_view reason) noexcept
        {
            return { CapabilityStatus::InvalidConfiguration, reason };
        }

        template <typename Enum>
        [[nodiscard]] constexpr bool IsDeclared(Enum value, Enum lastValue) noexcept
        {
            using Underlying = std::underlying_type_t<Enum>;
            return static_cast<Underlying>(value) <= static_cast<Underlying>(lastValue);
        }

    }

    CapabilityDecision CapabilityTable::Evaluate(const RuntimeConfig& config) noexcept
    {
        if (config.version != kRuntimeConfigVersion)
        {
            return Invalid("runtime config version is not supported");
        }
        if (config.render.width < 64 || config.render.width > 16384
            || config.render.height < 64 || config.render.height > 16384)
        {
            return Invalid("resolution must be between 64x64 and 16384x16384");
        }
        if (!std::isfinite(config.render.renderScale)
            || config.render.renderScale < 0.0625f || config.render.renderScale > 2.0f)
        {
            return Invalid("render scale must be from 0.0625 to 2");
        }
        if (config.render.samplesPerFrame == 0)
        {
            return Invalid("samples per frame must be positive");
        }
        if (config.render.targetSamplesPerPixel > 4096)
        {
            return Invalid("target samples per pixel may not exceed 4096 in the interactive renderer");
        }
        if (config.render.maximumBounce < 1 || config.render.maximumBounce > 12)
        {
            return Invalid("maximum bounce must be from 1 to 12");
        }
        if (!std::isfinite(config.render.exposure)
            || config.render.exposure < 0.01f || config.render.exposure > 64.0f)
        {
            return Invalid("exposure must be from 0.01 to 64");
        }
        if (!std::isfinite(config.render.verticalFovDegrees)
            || config.render.verticalFovDegrees < 25.0f || config.render.verticalFovDegrees > 80.0f)
        {
            return Invalid("vertical FOV must be from 25 to 80 degrees");
        }
        if (config.run.artifactRoot.empty())
        {
            return Invalid("artifact root may not be empty");
        }
        if (config.run.runIdentifier.empty())
        {
            return Invalid("run identifier may not be empty");
        }
        switch (config.render.vsync)
        {
        case RuntimeToggle::RendererDefault:
        case RuntimeToggle::Enabled:
        case RuntimeToggle::Disabled:
            break;
        default:
            return Invalid("vsync selection is invalid");
        }
        switch (config.run.validation)
        {
        case RuntimeToggle::RendererDefault:
        case RuntimeToggle::Enabled:
        case RuntimeToggle::Disabled:
            break;
        default:
            return Invalid("validation selection is invalid");
        }

        if (!IsDeclared(config.scene, ScenePreset::ManyLightsRestirArena))
        {
            return Invalid("scene selection is invalid");
        }
        if (!IsDeclared(config.backend, TraversalBackend::VulkanRayTracingPipeline))
        {
            return Invalid("traversal backend is invalid");
        }
        if (!IsDeclared(config.transportModel, TransportModel::Whitted))
        {
            return Invalid("transport model is invalid");
        }
        if (!IsDeclared(
                config.executionArchitecture,
                ExecutionArchitecture::Wavefront))
        {
            return Invalid("execution architecture is invalid");
        }
        if (!IsDeclared(
                config.directLightingEstimator,
                DirectLightingEstimator::RestirDirectIllumination))
        {
            return Invalid("direct-lighting estimator is invalid");
        }
        if (!IsDeclared(
                config.lightSelection,
                LightSelectionStrategy::PowerWeighted))
        {
            return Invalid("light-selection strategy is invalid");
        }
        if (!IsDeclared(
                config.environmentSampler,
                EnvironmentDirectionSampler::ImportanceMap))
        {
            return Invalid("environment direction sampler is invalid");
        }
        if (!IsDeclared(config.reconstruction, ReconstructionMode::CurrentFrame))
        {
            return Invalid("reconstruction mode is invalid");
        }
        if (!IsDeclared(config.shadowMethod, ShadowMethod::Physical))
        {
            return Invalid("shadow method is invalid");
        }
        if (!IsDeclared(config.debugView, DebugView::WinnerVisibility))
        {
            return Invalid("debug view is invalid");
        }

        if (!IsDeclared(config.restir.manyLightsTier, ManyLightsTier::Lights10000)
            || !IsDeclared(config.restir.reuseStage, RestirReuseStage::Spatial)
            || !IsDeclared(config.restir.biasMode, RestirBiasMode::ReferenceCorrection))
        {
            return Invalid("ReSTIR selection is invalid");
        }
        if (config.restir.initialCandidatesPerPixel == 0u
            || config.restir.initialCandidatesPerPixel > 64u
            || config.restir.spatialNeighbors > 30u
            || config.restir.maximumReservoirM == 0u
            || config.restir.maximumReservoirM > 4096u
            || config.restir.maximumHistoryAge == 0u
            || config.restir.maximumHistoryAge > 4096u
            || config.restir.comparisonCandidateBudgetPerPixel == 0u
            || config.restir.comparisonCandidateBudgetPerPixel > 64u
            || config.restir.comparisonVisibilityBudgetPerPixel == 0u
            || config.restir.comparisonVisibilityBudgetPerPixel > 64u)
        {
            return Invalid("ReSTIR budgets are outside their published bounds");
        }
        if (!UsesRestirSpatialReuse(config.restir.reuseStage)
            && config.restir.spatialNeighbors != 0u)
        {
            return Invalid(
                "spatial-neighbor budget must be zero unless spatial or temporal-spatial reuse is selected");
        }

        if (config.executionArchitecture == ExecutionArchitecture::CpuReference)
        {
            if (!config.sceneVariant.empty())
                return Unsupported("CPU reference does not consume experiment scene variants");
            if (config.transportModel != TransportModel::Pbr
                || config.scene != ScenePreset::CornellBox
                || config.backend != TraversalBackend::CpuSahBvh
                || config.directLightingEstimator != DirectLightingEstimator::MultipleImportanceSampling
                || config.lightSelection != LightSelectionStrategy::Uniform
                || config.environmentSampler
                    != EnvironmentDirectionSampler::UniformSphere
                || config.reconstruction != ReconstructionMode::ProgressiveMean)
            {
                return Unsupported(
                    "L3 runtime requires PBR + CPU reference + cornell + cpu-sah + MIS + uniform light selection + uniform-sphere environment sampling + progressive-mean");
            }
            if (!config.run.headless)
            {
                return Unsupported("L3 CPU reference runtime requires --headless");
            }
            if (config.render.targetSamplesPerPixel == 0u)
            {
                return Invalid("L3 CPU reference runtime requires a non-zero --spp target");
            }
            if (config.run.captureDirectory.has_value()
                || config.run.benchmarkPreset.has_value()
                || config.run.referenceImage.has_value())
            {
                return Unsupported(
                    "L3 CPU reference writes its fixed artifact bundle and does not consume capture/benchmark/reference requests");
            }
            if (config.debugView != DebugView::Final
                || config.render.renderScale != 1.0f
                || config.render.samplesPerFrame != 1u)
            {
                return Unsupported(
                    "L3 CPU reference supports final output, render-scale 1, and one scheduling sample per frame");
            }
            return Supported();
        }

        if (!IsBuilt(config.scene))
        {
            return Unsupported(
                "the requested experiment scene has no production scene provider; Sponza remains fail-closed until its pinned licensed asset and texture-capable glTF path are present");
        }
        if (!IsBuilt(config.backend))
        {
            return Unsupported(
                "the mixed renderer exposes canonical-linear, flattened SAH, and Vulkan Ray Query; other traversal providers remain gated");
        }
        if (!IsBuilt(config.transportModel))
        {
            return Unsupported(
                "the selected transport model has no production implementation");
        }
        if (!IsBuilt(config.executionArchitecture))
        {
            return Unsupported(
                "the mixed renderer exposes staged, GPU megakernel, and GPU wavefront execution; CPU reference is headless-only");
        }
        if (!IsBuilt(config.directLightingEstimator))
        {
            return Unsupported(
                "the selected direct-lighting estimator has no mixed-runtime provider");
        }
        if (!IsBuilt(config.lightSelection))
        {
            return Unsupported(
                "the selected light-selection strategy has no mixed-runtime provider");
        }
        if (!IsBuilt(config.environmentSampler))
        {
            return Unsupported(
                "the selected environment direction sampler has no mixed-runtime provider");
        }
        if (!IsBuilt(config.reconstruction))
        {
            return Unsupported(
                "the requested reconstruction mode has no production attachment");
        }

        if (!IsBuilt(config.shadowMethod))
        {
            return Invalid("shadow method is invalid");
        }
        if (!IsBuilt(config.debugView))
        {
            return Unsupported(
                "reservoir debug resources are declared but not attached to the production renderer");
        }
        if (config.debugView >= DebugView::ReservoirM
            && config.directLightingEstimator != DirectLightingEstimator::RestirDirectIllumination)
            return Unsupported("reservoir debug views require the ReSTIR DI producer");
        if (config.transportModel == TransportModel::Whitted
            && config.executionArchitecture != ExecutionArchitecture::Staged)
        {
            return Unsupported(
                "Whitted transport is implemented only by the staged execution architecture");
        }
        if (config.transportModel == TransportModel::Whitted
            && config.directLightingEstimator
                == DirectLightingEstimator::RestirDirectIllumination)
        {
            return Unsupported(
                "ReSTIR DI is not implemented for Whitted transport");
        }
        if (config.render.renderScale != 1.0f)
        {
            return Unsupported("render scaling is declared but not connected to the mixed GPU renderer");
        }
        if (config.render.samplesPerFrame != 1)
        {
            return Unsupported("configurable samples-per-frame is not built in the mixed GPU renderer");
        }
        if (config.run.headless)
        {
            return Unsupported("GPU headless rendering is not attached; use the supported CPU-reference headless path");
        }
        if (config.run.benchmarkPreset.has_value())
        {
            return Unsupported("benchmark orchestration is composed but not attached to the production renderer");
        }
        if (config.run.referenceImage.has_value())
        {
            return Unsupported("reference comparison is composed but not attached to the production renderer");
        }

        return Supported();
    }
}
