#pragma once

#include "app/RuntimeConfig.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace RenderingEngine
{
    enum class IntegratedModule : std::uint32_t
    {
        L0Foundation = 0,
        L1Platform,
        L2SceneAssets,
        L3CpuReference,
        L4SoftwareGpu,
        L5HardwareRt,
        L6Megakernel,
        L7Wavefront,
        L8Reconstruction,
        L9RestirDi,
        L10Showcase
    };

    // This is repository-composition state, not runtime or acceptance evidence.
    enum class ModuleCompositionStage : std::uint32_t
    {
        Missing = 0,
        SourceOnly,
        CentralBuild,
        ProductionRuntime
    };

    struct IntegratedModuleStatus final
    {
        IntegratedModule module{};
        std::string_view lane;
        std::string_view name;
        ModuleCompositionStage stage{};
        std::string_view boundary;

        [[nodiscard]] constexpr bool IsProductionAttached() const noexcept
        {
            return stage == ModuleCompositionStage::ProductionRuntime;
        }
    };

    [[nodiscard]] std::span<const IntegratedModuleStatus> IntegratedModules() noexcept;
    [[nodiscard]] const IntegratedModuleStatus& GetIntegratedModuleStatus(
        IntegratedModule module) noexcept;
    [[nodiscard]] std::string_view ToString(ModuleCompositionStage stage) noexcept;
    [[nodiscard]] std::string IntegrationStatusText();

    [[nodiscard]] IntegratedModule ProviderOwner(ScenePreset value) noexcept;
    [[nodiscard]] IntegratedModule ProviderOwner(TraversalBackend value) noexcept;
    [[nodiscard]] IntegratedModule ProviderOwner(Integrator value) noexcept;
    [[nodiscard]] IntegratedModule ProviderOwner(DirectLightingEstimator value) noexcept;
    [[nodiscard]] IntegratedModule ProviderOwner(LightProposalDistribution value) noexcept;
    [[nodiscard]] IntegratedModule ProviderOwner(ReconstructionMode value) noexcept;

    [[nodiscard]] std::string_view ProductionAttachmentReason(
        IntegratedModule module) noexcept;
}
