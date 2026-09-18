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
                "the application owns one final mixed runtime; historical Wave boundaries remain ABI provenance only and no longer constrain interactive tuples" },
            IntegratedModuleStatus{
                IntegratedModule::L1Platform, "L1", "GLFW / Vulkan Core",
                ModuleCompositionStage::ProductionRuntime,
                "GLFW window/input/Vulkan-surface host is the production platform factory; runtime validation remains separate" },
            IntegratedModuleStatus{
                IntegratedModule::L2SceneAssets, "L2", "scene and asset pipeline",
                ModuleCompositionStage::ProductionRuntime,
                "nine canonical experiment spaces are production-selectable (Sponza remains asset-gated); every built scene feeds the same traversal/transport/execution/reconstruction graph" },
            IntegratedModuleStatus{
                IntegratedModule::L3CpuReference, "L3", "CPU reference path tracer",
                ModuleCompositionStage::ProductionRuntime,
                "headless Cornell CPU reference and deterministic artifact output are attached" },
            IntegratedModuleStatus{
                IntegratedModule::L4SoftwareGpu, "L4", "software GPU traversal",
                ModuleCompositionStage::ProductionRuntime,
                "canonical linear and CPU binned-SAH flattened traversal consume the shared triangle stream; GPU LBVH remains separately gated" },
            IntegratedModuleStatus{
                IntegratedModule::L5HardwareRt, "L5", "Ray Query / RT Pipeline",
                ModuleCompositionStage::ProductionRuntime,
                "Ray Query owns production BLAS/TLAS traversal for every interactive execution architecture; RT Pipeline/SBT remains separately gated" },
            IntegratedModuleStatus{
                IntegratedModule::L6Megakernel, "L6", "PBR megakernel path tracer",
                ModuleCompositionStage::ProductionRuntime,
                "the staged PBR/Whitted/Megakernel path owns conventional direct lighting and exports the shared primary surface; ReSTIR owns primary direct lighting when selected" },
            IntegratedModuleStatus{
                IntegratedModule::L7Wavefront, "L7", "wavefront path tracer",
                ModuleCompositionStage::ProductionRuntime,
                "queue reset, ray generation, indirect intersect/shade/shadow/next-bounce, and resolve dispatch through the mixed runtime" },
            IntegratedModuleStatus{
                IntegratedModule::L8Reconstruction, "L8", "temporal / SVGF reconstruction",
                ModuleCompositionStage::ProductionRuntime,
                "one shared primary-surface/signal export drives Raw, Temporal, fixed A-Trous, and SVGF for every interactive transport/execution tuple" },
            IntegratedModuleStatus{
                IntegratedModule::L9RestirDi, "L9", "ReSTIR DI",
                ModuleCompositionStage::ProductionRuntime,
                "abi-v3 candidate, reservoir, temporal/spatial reuse, visibility, split-direct publication, history, and reconstruction are attached to the mixed runtime" },
            IntegratedModuleStatus{
                IntegratedModule::L10Showcase, "L10", "showcase / QA",
                ModuleCompositionStage::ProductionRuntime,
                "the Debug ImGui composition exposes eight independent algorithm axes, current provider status, GPU timing, debug textures, capture, and transactional shader reload" }
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
        output << "final mixed GPU runtime: abi-v1 traversal, abi-v2 reconstruction, and abi-v3 ReSTIR are stage contracts rather than tuple partitions\n";
        for (const IntegratedModuleStatus& module : kModules)
        {
            output << module.lane << "  " << ToString(module.stage) << "  "
                << module.name << " -- " << module.boundary << '\n';
        }
        output << "central-build means solution composition only for explicitly tested configurations; it is not GPU runtime, Vulkan validation, numerical convergence, visual, performance, or production-runtime acceptance\n";
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
        case TraversalBackend::CanonicalLinearGpu: return IntegratedModule::L0Foundation;
        case TraversalBackend::CpuBruteForce:
        case TraversalBackend::CpuSahBvh: return IntegratedModule::L3CpuReference;
        case TraversalBackend::GpuFlattenedSahBvh:
        case TraversalBackend::GpuLbvh: return IntegratedModule::L4SoftwareGpu;
        case TraversalBackend::VulkanRayQuery:
        case TraversalBackend::VulkanRayTracingPipeline: return IntegratedModule::L5HardwareRt;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const TransportModel value) noexcept
    {
        switch (value)
        {
        case TransportModel::Pbr: return IntegratedModule::L6Megakernel;
        case TransportModel::Whitted: return IntegratedModule::L0Foundation;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const ExecutionArchitecture value) noexcept
    {
        switch (value)
        {
        case ExecutionArchitecture::Staged: return IntegratedModule::L6Megakernel;
        case ExecutionArchitecture::CpuReference: return IntegratedModule::L3CpuReference;
        case ExecutionArchitecture::Megakernel: return IntegratedModule::L6Megakernel;
        case ExecutionArchitecture::Wavefront: return IntegratedModule::L7Wavefront;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const DirectLightingEstimator value) noexcept
    {
        switch (value)
        {
        case DirectLightingEstimator::BsdfOnly:
        case DirectLightingEstimator::NextEventEstimation:
        case DirectLightingEstimator::MultipleImportanceSampling:
            return IntegratedModule::L6Megakernel;
        case DirectLightingEstimator::RestirDirectIllumination:
            return IntegratedModule::L9RestirDi;
        default: return IntegratedModule::L0Foundation;
        }
    }

    IntegratedModule ProviderOwner(const LightSelectionStrategy) noexcept
    {
        return IntegratedModule::L6Megakernel;
    }

    IntegratedModule ProviderOwner(const EnvironmentDirectionSampler) noexcept
    {
        return IntegratedModule::L6Megakernel;
    }

    IntegratedModule ProviderOwner(const ReconstructionMode) noexcept
    {
        return IntegratedModule::L8Reconstruction;
    }

    std::string_view ProductionAttachmentReason(const IntegratedModule module) noexcept
    {
        return GetIntegratedModuleStatus(module).boundary;
    }
}
