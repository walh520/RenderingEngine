#include "app/RuntimeConfig.hpp"

#include <array>

namespace RenderingEngine
{
    namespace
    {
        constexpr RestirSettings kManyLightsRecommendedRestir{
            ManyLightsTier::Lights100,
            RestirReuseStage::TemporalSpatial,
            RestirBiasMode::ExplicitlyBiased,
            1u,
            5u,
            32u,
            20u,
            8u,
            1u,
            false,
            false
        };

        constexpr auto kSceneRecommendedProfiles =
            std::to_array<SceneRecommendedProfile>({
                {
                    ScenePreset::BaselineGallery,
                    "scene-recommended.baseline.v2",
                    TraversalBackend::CanonicalLinearGpu,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Staged,
                    DirectLightingEstimator::NextEventEstimation,
                    LightSelectionStrategy::Uniform,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    8u,
                    std::nullopt
                },
                {
                    ScenePreset::IntersectionBvhLab,
                    "scene-recommended.intersection-bvh.v2",
                    TraversalBackend::GpuFlattenedSahBvh,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Staged,
                    DirectLightingEstimator::NextEventEstimation,
                    LightSelectionStrategy::Uniform,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    1u,
                    std::nullopt
                },
                {
                    ScenePreset::WhittedOpticsRoom,
                    "scene-recommended.whitted-optics.v2",
                    TraversalBackend::VulkanRayQuery,
                    TransportModel::Whitted,
                    ExecutionArchitecture::Staged,
                    DirectLightingEstimator::NextEventEstimation,
                    LightSelectionStrategy::Uniform,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    12u,
                    std::nullopt
                },
                {
                    ScenePreset::CornellBox,
                    "scene-recommended.cornell.v2",
                    TraversalBackend::GpuFlattenedSahBvh,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Staged,
                    DirectLightingEstimator::MultipleImportanceSampling,
                    LightSelectionStrategy::Uniform,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    8u,
                    std::nullopt
                },
                {
                    ScenePreset::GgxMisMaterialLab,
                    "scene-recommended.ggx-mis.v2",
                    TraversalBackend::VulkanRayQuery,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Megakernel,
                    DirectLightingEstimator::MultipleImportanceSampling,
                    LightSelectionStrategy::PowerWeighted,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    8u,
                    std::nullopt
                },
                {
                    ScenePreset::EnvironmentSamplingDome,
                    "scene-recommended.environment-dome.v2",
                    TraversalBackend::VulkanRayQuery,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Staged,
                    DirectLightingEstimator::MultipleImportanceSampling,
                    LightSelectionStrategy::PowerWeighted,
                    EnvironmentDirectionSampler::ImportanceMap,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    8u,
                    std::nullopt
                },
                {
                    ScenePreset::SponzaTraversalHall,
                    "scene-recommended.sponza.v2",
                    TraversalBackend::VulkanRayQuery,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Wavefront,
                    DirectLightingEstimator::MultipleImportanceSampling,
                    LightSelectionStrategy::PowerWeighted,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    8u,
                    std::nullopt
                },
                {
                    ScenePreset::BackendParityBenchmark,
                    "scene-recommended.backend-parity.v2",
                    TraversalBackend::CanonicalLinearGpu,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Staged,
                    DirectLightingEstimator::NextEventEstimation,
                    LightSelectionStrategy::Uniform,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::ProgressiveMean,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    4u,
                    std::nullopt
                },
                {
                    ScenePreset::TemporalStabilityCorridor,
                    "scene-recommended.temporal-stability.v2",
                    TraversalBackend::VulkanRayQuery,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Wavefront,
                    DirectLightingEstimator::MultipleImportanceSampling,
                    LightSelectionStrategy::PowerWeighted,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::Svgf,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    6u,
                    std::nullopt
                },
                {
                    ScenePreset::ManyLightsRestirArena,
                    "scene-recommended.many-lights.v2",
                    TraversalBackend::VulkanRayQuery,
                    TransportModel::Pbr,
                    ExecutionArchitecture::Wavefront,
                    DirectLightingEstimator::RestirDirectIllumination,
                    LightSelectionStrategy::PowerWeighted,
                    EnvironmentDirectionSampler::UniformSphere,
                    ReconstructionMode::Svgf,
                    DebugView::Final,
                    ShadowMethod::Physical,
                    4u,
                    kManyLightsRecommendedRestir
                }
            });

        [[nodiscard]] constexpr bool MatchesRestirSettings(
            const RestirSettings& left,
            const RestirSettings& right) noexcept
        {
            return left.manyLightsTier == right.manyLightsTier
                && left.reuseStage == right.reuseStage
                && left.biasMode == right.biasMode
                && left.initialCandidatesPerPixel == right.initialCandidatesPerPixel
                && left.spatialNeighbors == right.spatialNeighbors
                && left.maximumReservoirM == right.maximumReservoirM
                && left.maximumHistoryAge == right.maximumHistoryAge
                && left.comparisonCandidateBudgetPerPixel
                    == right.comparisonCandidateBudgetPerPixel
                && left.comparisonVisibilityBudgetPerPixel
                    == right.comparisonVisibilityBudgetPerPixel
                && left.animateLights == right.animateLights
                && left.animateRigidOccluders == right.animateRigidOccluders;
        }
    }

    std::span<const SceneRecommendedProfile> GetSceneRecommendedProfiles() noexcept
    {
        return kSceneRecommendedProfiles;
    }

    const SceneRecommendedProfile* FindSceneRecommendedProfile(
        const ScenePreset scene) noexcept
    {
        for (const SceneRecommendedProfile& profile : kSceneRecommendedProfiles)
        {
            if (profile.scene == scene)
            {
                return &profile;
            }
        }
        return nullptr;
    }

    RuntimeConfig MakeSceneRecommendedConfig(
        const RuntimeConfig& source,
        const SceneRecommendedProfile& profile) noexcept
    {
        RuntimeConfig candidate = source;
        candidate.backend = profile.backend;
        candidate.transportModel = profile.transportModel;
        candidate.executionArchitecture = profile.executionArchitecture;
        candidate.directLightingEstimator = profile.directLightingEstimator;
        candidate.lightSelection = profile.lightSelection;
        candidate.environmentSampler = profile.environmentSampler;
        candidate.reconstruction = profile.reconstruction;
        candidate.debugView = profile.debugView;
        candidate.shadowMethod = profile.shadowMethod;
        candidate.render.maximumBounce = profile.maximumBounce;
        if (profile.restir.has_value())
        {
            candidate.restir = *profile.restir;
        }
        return candidate;
    }

    bool MatchesSceneRecommendedProfile(
        const RuntimeConfig& config,
        const SceneRecommendedProfile& profile) noexcept
    {
        return config.scene == profile.scene
            && config.backend == profile.backend
            && config.transportModel == profile.transportModel
            && config.executionArchitecture == profile.executionArchitecture
            && config.directLightingEstimator == profile.directLightingEstimator
            && config.lightSelection == profile.lightSelection
            && config.environmentSampler == profile.environmentSampler
            && config.reconstruction == profile.reconstruction
            && config.debugView == profile.debugView
            && config.shadowMethod == profile.shadowMethod
            && config.render.maximumBounce == profile.maximumBounce
            && (!profile.restir.has_value()
                || MatchesRestirSettings(config.restir, *profile.restir));
    }

    RuntimeConfig MakeHeadlessMockRuntimeConfig()
    {
        RuntimeConfig config;
        config.render.width = 64;
        config.render.height = 64;
        config.run.frameLimit = 1;
        config.run.headless = true;
        config.run.validation = RuntimeToggle::Disabled;
        config.run.runIdentifier = "headless-mock";
        return config;
    }
}
