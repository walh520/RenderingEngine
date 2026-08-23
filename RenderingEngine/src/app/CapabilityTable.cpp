#include "app/CapabilityTable.hpp"

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
            return Invalid("target samples per pixel may not exceed 4096 in the Wave 0 renderer");
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
        if (!IsDeclared(config.integrator, Integrator::GpuWavefrontPathTracer))
        {
            return Invalid("integrator is invalid");
        }
        if (!IsDeclared(
                config.directLightingEstimator,
                DirectLightingEstimator::RestirDirectIllumination))
        {
            return Invalid("direct-lighting estimator is invalid");
        }
        if (!IsDeclared(
                config.lightProposalDistribution,
                LightProposalDistribution::EnvironmentImportance))
        {
            return Invalid("light proposal distribution is invalid");
        }
        if (!IsDeclared(config.reconstruction, ReconstructionMode::Svgf))
        {
            return Invalid("reconstruction mode is invalid");
        }
        if (!IsDeclared(config.shadowMethod, ShadowMethod::Physical))
        {
            return Invalid("shadow method is invalid");
        }
        if (!IsDeclared(config.debugView, DebugView::Emissive))
        {
            return Invalid("debug view is invalid");
        }

        if (!IsBuilt(config.scene))
        {
            return Unsupported("only the baseline-gallery scene is built in Wave 0");
        }
        if (!IsBuilt(config.backend))
        {
            return Unsupported("only the legacy-analytic-gpu backend is built in Wave 0");
        }
        if (!IsBuilt(config.integrator))
        {
            return Unsupported("the selected integrator is declared but not built in Wave 0");
        }
        if (!IsBuilt(config.directLightingEstimator))
        {
            return Unsupported("only the legacy-analytic-direct estimator is built in Wave 0");
        }
        if (!IsBuilt(config.lightProposalDistribution))
        {
            return Unsupported("only the legacy-analytic light proposal is built in Wave 0");
        }
        if (!IsBuilt(config.reconstruction))
        {
            return Unsupported("only raw reconstruction is built in Wave 0");
        }

        if (!IsBuilt(config.shadowMethod))
        {
            return Invalid("shadow method is invalid");
        }
        if (!IsBuilt(config.debugView))
        {
            return Invalid("debug view is invalid");
        }
        if (config.render.targetSamplesPerPixel > 0 && config.debugView != DebugView::Final)
        {
            return Invalid("target SPP termination is only defined for the final debug view");
        }

        if (config.render.renderScale != 1.0f)
        {
            return Unsupported("render scaling is declared but not connected to the Wave 0 renderer");
        }
        if (config.render.samplesPerFrame != 1)
        {
            return Unsupported("configurable samples-per-frame is not built in Wave 0");
        }
        if (config.run.headless)
        {
            return Unsupported("headless rendering is not built in Wave 0; use the mock config only in tests");
        }
        if (config.run.captureDirectory.has_value())
        {
            return Unsupported("capture output is planned but not built in Wave 0");
        }
        if (config.run.benchmarkPreset.has_value())
        {
            return Unsupported("benchmark output is planned but not built in Wave 0");
        }
        if (config.run.referenceImage.has_value())
        {
            return Unsupported("reference comparison is planned but not built in Wave 0");
        }

        return Supported();
    }
}
