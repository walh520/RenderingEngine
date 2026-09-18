#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace RenderingEngine
{
    inline constexpr std::uint32_t kRuntimeConfigVersion = 2;

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
        CanonicalLinearGpu = 0,
        CpuBruteForce,
        CpuSahBvh,
        GpuFlattenedSahBvh,
        GpuLbvh,
        VulkanRayQuery,
        VulkanRayTracingPipeline
    };

    enum class TransportModel : std::uint32_t
    {
        Pbr = 0,
        Whitted
    };

    enum class ExecutionArchitecture : std::uint32_t
    {
        Staged = 0,
        CpuReference,
        Megakernel,
        Wavefront
    };

    enum class DirectLightingEstimator : std::uint32_t
    {
        BsdfOnly = 0,
        NextEventEstimation,
        MultipleImportanceSampling,
        RestirDirectIllumination
    };

    enum class LightSelectionStrategy : std::uint32_t
    {
        Uniform = 0,
        PowerWeighted
    };

    enum class EnvironmentDirectionSampler : std::uint32_t
    {
        UniformSphere = 0,
        ImportanceMap
    };

    enum class ReconstructionMode : std::uint32_t
    {
        ProgressiveMean = 0,
        TemporalAccumulation,
        SpatialFixedAtrous,
        Svgf,
        CurrentFrame
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
        Emissive = 5,
        Motion,
        HistoryLength,
        Moments,
        Variance,
        TemporalAcceptance,
        TemporalRejectReasons,
        ReservoirM,
        ReservoirWeight,
        ReservoirLightId,
        ReservoirSource,
        ReservoirReuse,
        ReservoirRejection,
        WinnerVisibility
    };

    enum class ManyLightsTier : std::uint32_t
    {
        Lights100 = 0u,
        Lights1000,
        Lights10000
    };

    enum class RestirReuseStage : std::uint32_t
    {
        Initial = 0u,
        Temporal,
        TemporalSpatial,
        Spatial
    };

    [[nodiscard]] constexpr bool UsesRestirTemporalReuse(RestirReuseStage stage) noexcept
    {
        return stage == RestirReuseStage::Temporal || stage == RestirReuseStage::TemporalSpatial;
    }

    [[nodiscard]] constexpr bool UsesRestirSpatialReuse(RestirReuseStage stage) noexcept
    {
        return stage == RestirReuseStage::Spatial || stage == RestirReuseStage::TemporalSpatial;
    }

    enum class RestirBiasMode : std::uint32_t
    {
        ExplicitlyBiased = 0u,
        ReferenceCorrection
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

    // Wave 4 settings remain host-side control state. They never alter the
    // binary abi-v3 records, so UI and CLI can share this tuple without making
    // descriptor layouts depend on application policy.
    struct RestirSettings
    {
        ManyLightsTier manyLightsTier = ManyLightsTier::Lights100;
        RestirReuseStage reuseStage = RestirReuseStage::TemporalSpatial;
        RestirBiasMode biasMode = RestirBiasMode::ExplicitlyBiased;
        std::uint32_t initialCandidatesPerPixel = 8u;
        std::uint32_t spatialNeighbors = 5u;
        std::uint32_t maximumReservoirM = 32u;
        std::uint32_t maximumHistoryAge = 20u;
        std::uint32_t comparisonCandidateBudgetPerPixel = 8u;
        std::uint32_t comparisonVisibilityBudgetPerPixel = 1u;
        bool animateLights = true;
        bool animateRigidOccluders = true;
    };

    struct RuntimeConfig
    {
        std::uint32_t version = kRuntimeConfigVersion;
        ScenePreset scene = ScenePreset::BaselineGallery;
        TraversalBackend backend = TraversalBackend::CanonicalLinearGpu;
        TransportModel transportModel = TransportModel::Pbr;
        ExecutionArchitecture executionArchitecture = ExecutionArchitecture::Staged;
        DirectLightingEstimator directLightingEstimator =
            DirectLightingEstimator::NextEventEstimation;
        LightSelectionStrategy lightSelection = LightSelectionStrategy::Uniform;
        EnvironmentDirectionSampler environmentSampler =
            EnvironmentDirectionSampler::UniformSphere;
        ReconstructionMode reconstruction = ReconstructionMode::ProgressiveMean;
        DebugView debugView = DebugView::Final;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
        RenderSettings render;
        RunSettings run;
        RestirSettings restir;
        // Scene-local experiment ID; empty chooses the presentation variant.
        std::string sceneVariant;
    };

    // Versioned teaching recommendation for one experiment scene. This is
    // application policy only: it does not change RuntimeConfig's layout or
    // the renderer/shader ABI version.
    struct SceneRecommendedProfile
    {
        ScenePreset scene = ScenePreset::BaselineGallery;
        std::string_view stableId;
        TraversalBackend backend = TraversalBackend::CanonicalLinearGpu;
        TransportModel transportModel = TransportModel::Pbr;
        ExecutionArchitecture executionArchitecture = ExecutionArchitecture::Staged;
        DirectLightingEstimator directLightingEstimator =
            DirectLightingEstimator::NextEventEstimation;
        LightSelectionStrategy lightSelection = LightSelectionStrategy::Uniform;
        EnvironmentDirectionSampler environmentSampler =
            EnvironmentDirectionSampler::UniformSphere;
        ReconstructionMode reconstruction = ReconstructionMode::ProgressiveMean;
        DebugView debugView = DebugView::Final;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
        std::uint32_t maximumBounce = 8u;
        std::optional<RestirSettings> restir;
    };

    [[nodiscard]] std::span<const SceneRecommendedProfile>
        GetSceneRecommendedProfiles() noexcept;

    [[nodiscard]] const SceneRecommendedProfile* FindSceneRecommendedProfile(
        ScenePreset scene) noexcept;

    // Copies source and overwrites only the fields owned by the recommendation:
    // the eight algorithm axes, Final debug view, maximum bounce, and (only when
    // present) the scene-specific ReSTIR teaching settings.
    [[nodiscard]] RuntimeConfig MakeSceneRecommendedConfig(
        const RuntimeConfig& source,
        const SceneRecommendedProfile& profile) noexcept;

    [[nodiscard]] bool MatchesSceneRecommendedProfile(
        const RuntimeConfig& config,
        const SceneRecommendedProfile& profile) noexcept;

    [[nodiscard]] constexpr std::uint32_t ResolveManyLightsCount(
        const ManyLightsTier tier) noexcept
    {
        switch (tier)
        {
        case ManyLightsTier::Lights100: return 100u;
        case ManyLightsTier::Lights1000: return 1000u;
        case ManyLightsTier::Lights10000: return 10000u;
        }
        return 0u;
    }

    // Test fixture only: constructing this value never creates a platform window.
    // Production headless execution is restricted to the explicit L3 CPU-reference tuple.
    [[nodiscard]] RuntimeConfig MakeHeadlessMockRuntimeConfig();
}
