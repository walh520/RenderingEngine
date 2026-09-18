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
            return value == ScenePreset::BaselineGallery
                || value == ScenePreset::IntersectionBvhLab
                || value == ScenePreset::WhittedOpticsRoom
                || value == ScenePreset::CornellBox
                || value == ScenePreset::GgxMisMaterialLab
                || value == ScenePreset::EnvironmentSamplingDome
                || value == ScenePreset::BackendParityBenchmark
                || value == ScenePreset::TemporalStabilityCorridor
                || value == ScenePreset::ManyLightsRestirArena;
        }

        [[nodiscard]] static constexpr bool IsBuilt(TraversalBackend value) noexcept
        {
            return value == TraversalBackend::CanonicalLinearGpu
                || value == TraversalBackend::GpuFlattenedSahBvh
                || value == TraversalBackend::VulkanRayQuery;
        }

        [[nodiscard]] static constexpr bool IsBuilt(TransportModel value) noexcept
        {
            return value == TransportModel::Pbr
                || value == TransportModel::Whitted;
        }

        [[nodiscard]] static constexpr bool IsBuilt(
            ExecutionArchitecture value) noexcept
        {
            return value == ExecutionArchitecture::Staged
                || value == ExecutionArchitecture::Megakernel
                || value == ExecutionArchitecture::Wavefront;
        }

        [[nodiscard]] static constexpr bool IsBuilt(DirectLightingEstimator value) noexcept
        {
            return value == DirectLightingEstimator::BsdfOnly
                || value == DirectLightingEstimator::NextEventEstimation
                || value == DirectLightingEstimator::MultipleImportanceSampling
                || value == DirectLightingEstimator::RestirDirectIllumination;
        }

        [[nodiscard]] static constexpr bool IsBuilt(
            LightSelectionStrategy value) noexcept
        {
            return value == LightSelectionStrategy::Uniform
                || value == LightSelectionStrategy::PowerWeighted;
        }

        [[nodiscard]] static constexpr bool IsBuilt(
            EnvironmentDirectionSampler value) noexcept
        {
            return value == EnvironmentDirectionSampler::UniformSphere
                || value == EnvironmentDirectionSampler::ImportanceMap;
        }

        [[nodiscard]] static constexpr bool IsBuilt(ReconstructionMode value) noexcept
        {
            return value == ReconstructionMode::ProgressiveMean
                || value == ReconstructionMode::CurrentFrame
                || value == ReconstructionMode::TemporalAccumulation
                || value == ReconstructionMode::SpatialFixedAtrous
                || value == ReconstructionMode::Svgf;
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
                || value == DebugView::Emissive
                || (value >= DebugView::Motion
                    && value <= DebugView::TemporalRejectReasons)
                || (value >= DebugView::ReservoirM && value <= DebugView::WinnerVisibility);
        }

        [[nodiscard]] static CapabilityDecision Evaluate(const RuntimeConfig& config) noexcept;
    };
}
