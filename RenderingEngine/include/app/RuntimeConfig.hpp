#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace RenderingEngine
{
    inline constexpr std::uint32_t kRuntimeConfigVersion = 0;

    enum class ScenePreset : std::uint32_t
    {
        BaselineGallery = 0,
        IntersectionBvhLab,
        WhittedOpticsRoom,
        CornellBox,
        GgxMisMaterialLab,
        EnvironmentSamplingDome,
        SponzaTraversalHall,
        BackendParityBenchmark,
        TemporalStabilityCorridor,
        ManyLightsRestirArena
    };

    enum class TraversalBackend : std::uint32_t
    {
        LegacyAnalyticGpu = 0,
        CpuBruteForce,
        CpuSahBvh,
        GpuFlattenedSahBvh,
        GpuLbvh,
        VulkanRayQuery,
        VulkanRayTracingPipeline
    };

    // Pbr and Whitted retain their legacy numeric values because the current renderer
    // copies them into its existing shader-facing control path.
    enum class Integrator : std::uint32_t
    {
        Pbr = 0,
        Whitted = 1,
        CpuReferencePathTracer,
        GpuMegakernelPathTracer,
        GpuWavefrontPathTracer
    };

    enum class DirectLightingEstimator : std::uint32_t
    {
        LegacyAnalyticDirect = 0,
        BsdfOnly,
        NextEventEstimation,
        MultipleImportanceSampling,
        RestirDirectIllumination
    };

    enum class LightProposalDistribution : std::uint32_t
    {
        LegacyAnalyticLights = 0,
        UniformLights,
        PowerWeightedLights,
        EnvironmentImportance
    };

    enum class ReconstructionMode : std::uint32_t
    {
        Raw = 0,
        TemporalAccumulation,
        TemporalFixedAtrous,
        Svgf
    };

    enum class ShadowMethod : std::uint32_t
    {
        Pcf = 0,
        Pcss = 1,
        Physical = 2
    };

    enum class DebugView : std::uint32_t
    {
        Final = 0,
        BaseColor = 1,
        Normal = 2,
        Roughness = 3,
        Metallic = 4,
        Emissive = 5
    };

    enum class RuntimeToggle : std::uint8_t
    {
        RendererDefault = 0,
        Enabled,
        Disabled
    };

    struct RenderSettings
    {
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        float renderScale = 1.0f;
        std::uint32_t samplesPerFrame = 1;
        std::uint32_t targetSamplesPerPixel = 0;
        std::uint32_t maximumBounce = 8;
        std::uint64_t baseSeed = 0;
        float exposure = 1.0f;
        float verticalFovDegrees = 52.0f;
        RuntimeToggle vsync = RuntimeToggle::RendererDefault;
    };

    struct RunSettings
    {
        std::uint32_t frameLimit = 0; // Zero keeps an interactive run open until exit.
        bool resizeTest = false;
        bool headless = false;
        RuntimeToggle validation = RuntimeToggle::RendererDefault;
        std::optional<std::filesystem::path> captureDirectory;
        std::optional<std::string> benchmarkPreset;
        std::optional<std::filesystem::path> referenceImage;
        std::filesystem::path artifactRoot = ".artifacts";
        std::string runIdentifier = "manual";
    };

    struct RuntimeConfig
    {
        std::uint32_t version = kRuntimeConfigVersion;
        ScenePreset scene = ScenePreset::BaselineGallery;
        TraversalBackend backend = TraversalBackend::LegacyAnalyticGpu;
        Integrator integrator = Integrator::Whitted;
        DirectLightingEstimator directLightingEstimator = DirectLightingEstimator::LegacyAnalyticDirect;
        LightProposalDistribution lightProposalDistribution = LightProposalDistribution::LegacyAnalyticLights;
        ReconstructionMode reconstruction = ReconstructionMode::Raw;
        DebugView debugView = DebugView::Final;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
        RenderSettings render;
        RunSettings run;
    };

    // Compatibility payload for the legacy monolithic renderer. Application composition
    // is the only layer that projects canonical RuntimeConfig into this shape.
    struct RunOptions
    {
        std::uint32_t initialWidth = 1280;
        std::uint32_t initialHeight = 720;
        std::uint32_t frameLimit = 0;
        bool resizeTest = false;
        float exposure = 1.0f;
        std::uint32_t maximumTraceDepth = 8;
        std::uint32_t targetSamplesPerPixel = 0;
        std::uint64_t baseSeed = 0;
        float verticalFovDegrees = 52.0f;
        RuntimeToggle vsync = RuntimeToggle::RendererDefault;
        RuntimeToggle validation = RuntimeToggle::RendererDefault;
        Integrator integrator = Integrator::Whitted;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
        DebugView debugView = DebugView::Final;
    };

    [[nodiscard]] RunOptions MakeLegacyRunOptions(const RuntimeConfig& config) noexcept;

    // Test fixture only: constructing this value never creates a platform window.
    // The Wave 0 production capability table intentionally rejects headless execution.
    [[nodiscard]] RuntimeConfig MakeHeadlessMockRuntimeConfig();
}
