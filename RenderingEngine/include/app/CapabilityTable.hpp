#pragma once

#include "app/RuntimeConfig.hpp"

#include <string_view>

namespace RenderingEngine
{
    enum class CapabilityStatus
    {
        Supported,
        Unsupported,
        InvalidConfiguration
    };

    struct CapabilityDecision
    {
        CapabilityStatus status = CapabilityStatus::Supported;
        std::string_view reason;

        [[nodiscard]] constexpr bool IsSupported() const noexcept
        {
            return status == CapabilityStatus::Supported;
        }
    };

    class CapabilityTable final
    {
    public:
        [[nodiscard]] static constexpr bool IsBuilt(ScenePreset value) noexcept
        {
            return value == ScenePreset::BaselineGallery;
        }

        [[nodiscard]] static constexpr bool IsBuilt(TraversalBackend value) noexcept
        {
            return value == TraversalBackend::LegacyAnalyticGpu;
        }

        [[nodiscard]] static constexpr bool IsBuilt(Integrator value) noexcept
        {
            return value == Integrator::Whitted || value == Integrator::Pbr;
        }

        [[nodiscard]] static constexpr bool IsBuilt(DirectLightingEstimator value) noexcept
        {
            return value == DirectLightingEstimator::LegacyAnalyticDirect;
        }

        [[nodiscard]] static constexpr bool IsBuilt(LightProposalDistribution value) noexcept
        {
            return value == LightProposalDistribution::LegacyAnalyticLights;
        }

        [[nodiscard]] static constexpr bool IsBuilt(ReconstructionMode value) noexcept
        {
            return value == ReconstructionMode::Raw;
        }

        [[nodiscard]] static constexpr bool IsBuilt(ShadowMethod value) noexcept
        {
            return value == ShadowMethod::Pcf
                || value == ShadowMethod::Pcss
                || value == ShadowMethod::Physical;
        }

        [[nodiscard]] static constexpr bool IsBuilt(DebugView value) noexcept
        {
            return value == DebugView::Final
                || value == DebugView::BaseColor
                || value == DebugView::Normal
                || value == DebugView::Roughness
                || value == DebugView::Metallic
                || value == DebugView::Emissive;
        }

        [[nodiscard]] static CapabilityDecision Evaluate(const RuntimeConfig& config) noexcept;
    };
}
