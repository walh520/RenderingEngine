#include "app/IntegratedModuleRegistry.hpp"

#include <array>
#include <sstream>

namespace RenderingEngine
{
    namespace
    {
        constexpr std::array kModules{
            IntegratedModuleStatus{
                IntegratedModule::L0Foundation, "L0", "foundation / analytic Vulkan runtime",
                ModuleCompositionStage::ProductionRuntime,
                "attached to the production application" },
            IntegratedModuleStatus{
                IntegratedModule::L1Platform, "L1", "GLFW / Vulkan Core",
                ModuleCompositionStage::SourceOnly,
                "Win32 platform seam is present; GLFW/Vulkan Core delivery is incomplete" },
            IntegratedModuleStatus{
                IntegratedModule::L2SceneAssets, "L2", "scene and asset pipeline",
                ModuleCompositionStage::Missing,
                "canonical mesh/scene/light providers were not delivered" },
            IntegratedModuleStatus{
                IntegratedModule::L3CpuReference, "L3", "CPU reference path tracer",
                ModuleCompositionStage::ProductionRuntime,
                "headless Cornell CPU reference and deterministic artifact output are attached" },
            IntegratedModuleStatus{
                IntegratedModule::L4SoftwareGpu, "L4", "software GPU traversal",
                ModuleCompositionStage::CentralBuild,
                "library/tests/Vulkan smoke are composed; no production scene/upload adapter" },
            IntegratedModuleStatus{
                IntegratedModule::L5HardwareRt, "L5", "Ray Query / RT Pipeline",
                ModuleCompositionStage::CentralBuild,
                "module/tests are composed; production AS build and dispatch are not attached" },
            IntegratedModuleStatus{
                IntegratedModule::L6Megakernel, "L6", "PBR megakernel path tracer",
                ModuleCompositionStage::CentralBuild,
                "shader/tests are composed; production Vulkan dispatch is not attached" },
            IntegratedModuleStatus{
                IntegratedModule::L7Wavefront, "L7", "wavefront path tracer",
                ModuleCompositionStage::CentralBuild,
                "stages/tests are composed; production queues and dispatch are not attached" },
            IntegratedModuleStatus{
                IntegratedModule::L8Reconstruction, "L8", "temporal / SVGF reconstruction",
                ModuleCompositionStage::CentralBuild,
                "shader/tests are composed; production history/GBuffer adapter is not attached" },
            IntegratedModuleStatus{
                IntegratedModule::L9RestirDi, "L9", "ReSTIR DI",
                ModuleCompositionStage::CentralBuild,
                "library/tests/Vulkan smoke are composed; production light/GBuffer adapter is not attached" },
            IntegratedModuleStatus{
                IntegratedModule::L10Showcase, "L10", "showcase / QA",
                ModuleCompositionStage::CentralBuild,
                "library/tests are composed; production UI and renderer providers are not attached" }
        };

        [[nodiscard]] constexpr std::size_t ModuleIndex(
            const IntegratedModule module) noexcept
        {
            return static_cast<std::size_t>(module);
        }
    }

    std::span<const IntegratedModuleStatus> IntegratedModules() noexcept
    {
        return kModules;
    }

    const IntegratedModuleStatus& GetIntegratedModuleStatus(
        const IntegratedModule module) noexcept
    {
        const std::size_t index = ModuleIndex(module);
        return index < kModules.size() ? kModules[index] : kModules.front();
    }

    std::string_view ToString(const ModuleCompositionStage stage) noexcept
    {
        switch (stage)
        {
        case ModuleCompositionStage::Missing: return "missing";
        case ModuleCompositionStage::SourceOnly: return "source-only";
        case ModuleCompositionStage::CentralBuild: return "central-build";
        case ModuleCompositionStage::ProductionRuntime: return "production-runtime";
        default: return "invalid";
        }
    }

    std::string IntegrationStatusText()
    {
        std::ostringstream output;
        output << "RenderingEngine integration status\n";
        for (const IntegratedModuleStatus& module : kModules)
        {
            output << module.lane << "  " << ToString(module.stage) << "  "
                << module.name << " -- " << module.boundary << '\n';
        }
        output << "central-build means solution composition only; it is not GPU, numerical, visual, or production-runtime acceptance\n";
        return output.str();
    }

    IntegratedModule ProviderOwner(const ScenePreset value) noexcept
    {
        return value == ScenePreset::BaselineGallery
            ? IntegratedModule::L0Foundation
            : IntegratedModule::L2SceneAssets;
    }

    IntegratedModule ProviderOwner(const TraversalBackend value) noexcept
    {
        switch (value)
        {
        case TraversalBackend::LegacyAnalyticGpu: return IntegratedModule::L0Foundation;
        case TraversalBackend::CpuBruteForce:
        case TraversalBackend::CpuSahBvh: return IntegratedModule::L3CpuReference;
        case TraversalBackend::GpuFlattenedSahBvh:
        case TraversalBackend::GpuLbvh: return IntegratedModule::L4SoftwareGpu;
        case TraversalBackend::VulkanRayQuery:
        case TraversalBackend::VulkanRayTracingPipeline: return IntegratedModule::L5HardwareRt;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const Integrator value) noexcept
    {
        switch (value)
        {
        case Integrator::Pbr:
        case Integrator::Whitted: return IntegratedModule::L0Foundation;
        case Integrator::CpuReferencePathTracer: return IntegratedModule::L3CpuReference;
        case Integrator::GpuMegakernelPathTracer: return IntegratedModule::L6Megakernel;
        case Integrator::GpuWavefrontPathTracer: return IntegratedModule::L7Wavefront;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const DirectLightingEstimator value) noexcept
    {
        switch (value)
        {
        case DirectLightingEstimator::LegacyAnalyticDirect: return IntegratedModule::L0Foundation;
        case DirectLightingEstimator::BsdfOnly:
        case DirectLightingEstimator::NextEventEstimation:
        case DirectLightingEstimator::MultipleImportanceSampling:
            return IntegratedModule::L6Megakernel;
        case DirectLightingEstimator::RestirDirectIllumination:
            return IntegratedModule::L9RestirDi;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const LightProposalDistribution value) noexcept
    {
        return value == LightProposalDistribution::LegacyAnalyticLights
            ? IntegratedModule::L0Foundation
            : IntegratedModule::L6Megakernel;
    }

    IntegratedModule ProviderOwner(const ReconstructionMode value) noexcept
    {
        return value == ReconstructionMode::Raw
            ? IntegratedModule::L0Foundation
            : IntegratedModule::L8Reconstruction;
    }

    std::string_view ProductionAttachmentReason(const IntegratedModule module) noexcept
    {
        return GetIntegratedModuleStatus(module).boundary;
    }
}
