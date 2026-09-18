#pragma once

#include "contracts/GpuRecordsAbiV1.hpp"
#include "contracts/RayHitAbiV0.hpp"
#include "scene/CanonicalScene.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace RenderingEngine::Scene
{
    enum class ExperimentScenePreset : std::uint32_t
    {
        BaselineGallery = 0u,
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

    enum ExperimentAlgorithmFlags : std::uint64_t
    {
        ExperimentAlgorithmNone = 0ull,
        ExperimentAlgorithmClosestAnyHit = 1ull << 0u,
        ExperimentAlgorithmBackendParity = 1ull << 1u,
        ExperimentAlgorithmCpuSah = 1ull << 2u,
        ExperimentAlgorithmFlattenedSah = 1ull << 3u,
        ExperimentAlgorithmGpuLbvh = 1ull << 4u,
        ExperimentAlgorithmWhittedReflection = 1ull << 5u,
        ExperimentAlgorithmWhittedRefraction = 1ull << 6u,
        ExperimentAlgorithmFresnelTir = 1ull << 7u,
        ExperimentAlgorithmShadowRay = 1ull << 8u,
        ExperimentAlgorithmBeerLambert = 1ull << 9u,
        ExperimentAlgorithmDiffuseGi = 1ull << 10u,
        ExperimentAlgorithmNextEventEstimation = 1ull << 11u,
        ExperimentAlgorithmMultipleImportanceSampling = 1ull << 12u,
        ExperimentAlgorithmRussianRoulette = 1ull << 13u,
        ExperimentAlgorithmGgxSmithFresnel = 1ull << 14u,
        ExperimentAlgorithmGgxVndf = 1ull << 15u,
        ExperimentAlgorithmWhiteFurnace = 1ull << 16u,
        ExperimentAlgorithmAccumulation = 1ull << 17u,
        ExperimentAlgorithmEnvironmentImportance = 1ull << 18u,
        ExperimentAlgorithmTemporalReconstruction = 1ull << 19u,
        ExperimentAlgorithmAtrous = 1ull << 20u,
        ExperimentAlgorithmSvgf = 1ull << 21u,
        ExperimentAlgorithmRestirDi = 1ull << 22u,
        ExperimentAlgorithmStableLightHistory = 1ull << 23u
    };

    enum class ExperimentAnyHitExpectation : std::uint32_t
    {
        Unoccluded = 0u,
        Occluded,
        Invalid
    };

    enum class ExperimentEnvironmentProfile : std::uint32_t
    {
        Original = 0u,
        PolarSun,
        SeamSun
    };

#if defined(_MSC_VER)
#pragma warning(push)
    // ExperimentRayCase intentionally combines a 16-byte ABI record with
    // host-only labels and expectations. This host layout is not serialized.
#pragma warning(disable: 4324)
#endif
    struct ExperimentRayCase
    {
        std::string_view stableId;
        Contracts::AbiV1::GpuRayQueueRecordV1 ray{};
        std::uint32_t expectedHitKind = Contracts::AbiV0::HitKindMiss;
        std::array<std::uint32_t, 2> acceptablePrimitiveIds{
            Contracts::AbiV0::kInvalidId,
            Contracts::AbiV0::kInvalidId
        };
        std::uint32_t acceptablePrimitiveCount = 0u;
        float expectedDistance = 0.0f;
        float relativeDistanceTolerance = 1.0e-5f;
        ExperimentAnyHitExpectation anyHit = ExperimentAnyHitExpectation::Unoccluded;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    struct ExperimentEnvironment
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::vector<Contracts::AbiV0::AbiFloat4> linearRgba;
        ExperimentEnvironmentProfile profile = ExperimentEnvironmentProfile::Original;

        [[nodiscard]] bool Empty() const noexcept
        {
            return width == 0u || height == 0u || linearRgba.empty();
        }
    };

    // Light bits address stable GpuLightV0 identity.y values. Variants let the
    // future UI switch experimental lighting without changing scene topology.
    struct ExperimentSceneVariant
    {
        std::string_view stableId;
        std::string_view cameraStableId;
        std::uint64_t enabledLightMask = ~0ull;
        bool enableEnvironment = false;
        // A camera cut is an explicit history event, not a property of the
        // camera pose. It is consumed by the host to reset temporal state.
        bool cameraCut = false;
        ExperimentEnvironmentProfile environmentProfile =
            ExperimentEnvironmentProfile::Original;
    };

    struct ExperimentSceneDescriptor
    {
        ExperimentScenePreset preset = ExperimentScenePreset::BaselineGallery;
        std::uint32_t showcaseIndex = 0u;
        std::string_view runtimeToken;
        std::string_view canonicalStableId;
        std::string_view displayName;
        std::string_view fixedCameraStableId;
        std::uint64_t algorithmMask = ExperimentAlgorithmNone;
        std::uint32_t recommendedMaximumBounces = 1u;
        std::uint32_t recommendedReferenceSpp = 1u;
    };

    struct ExperimentSceneBuildOptions
    {
        std::uint32_t manyLightsCount = 100u;
        std::uint32_t animationFrameIndex = 1u;
        bool animateLights = true;
        bool animateCamera = true;
        bool animateRigidOccluders = true;
        // Empty selects the scene's first (presentation/default) variant.
        // A non-empty ID is resolved transactionally by BuildExperimentScene.
        // Kept last so existing aggregate initialization remains source-safe.
        std::string_view variantStableId{};
    };

#if defined(_MSC_VER)
#pragma warning(push)
    // CanonicalScene owns 16-byte ABI constants; ExperimentScene is a
    // host-only aggregate and is never copied to a contract buffer.
#pragma warning(disable: 4324)
#endif
    struct ExperimentScene
    {
        ExperimentSceneDescriptor descriptor{};
        CanonicalScene canonical{};
        std::vector<ExperimentRayCase> traversalCorpus;
        std::vector<ExperimentSceneVariant> variants;
        ExperimentEnvironment environment;
        std::string_view activeVariantStableId;
        std::string_view activeCameraStableId;
        bool environmentEnabled = false;
        bool cameraCut = false;
        std::uint32_t sampledAnimationFrameIndex = 0u;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    [[nodiscard]] std::span<const ExperimentSceneDescriptor>
        FirstFiveExperimentSceneRegistry() noexcept;
    [[nodiscard]] std::span<const ExperimentSceneDescriptor>
        ExperimentSceneRegistry() noexcept;
    [[nodiscard]] bool IsExperimentSceneBuilt(
        ExperimentScenePreset preset) noexcept;
    [[nodiscard]] const ExperimentSceneDescriptor* FindExperimentScene(
        ExperimentScenePreset preset) noexcept;
    [[nodiscard]] const ExperimentSceneDescriptor* FindExperimentScene(
        std::string_view runtimeToken) noexcept;
    [[nodiscard]] const ExperimentSceneVariant* FindExperimentSceneVariant(
        const ExperimentScene& scene,
        std::string_view stableId) noexcept;
    [[nodiscard]] bool ApplyExperimentSceneVariant(
        ExperimentScene& scene,
        std::string_view stableId) noexcept;

    [[nodiscard]] ExperimentScene BuildBaselineGalleryExperimentScene();
    [[nodiscard]] ExperimentScene BuildIntersectionBvhExperimentScene();
    [[nodiscard]] ExperimentScene BuildWhittedOpticsExperimentScene();
    [[nodiscard]] ExperimentScene BuildCornellExperimentScene();
    [[nodiscard]] ExperimentScene BuildGgxMisMaterialExperimentScene();
    [[nodiscard]] ExperimentScene BuildEnvironmentSamplingDomeExperimentScene();
    [[nodiscard]] ExperimentScene BuildBackendParityBenchmarkExperimentScene();
    [[nodiscard]] ExperimentScene BuildTemporalStabilityCorridorExperimentScene();
    [[nodiscard]] ExperimentScene BuildTemporalStabilityCorridorExperimentScene(
        const ExperimentSceneBuildOptions& options);
    [[nodiscard]] ExperimentScene BuildManyLightsRestirArenaExperimentScene(
        const ExperimentSceneBuildOptions& options = {});
    [[nodiscard]] ExperimentScene BuildExperimentScene(
        ExperimentScenePreset preset,
        const ExperimentSceneBuildOptions& options = {});
}
