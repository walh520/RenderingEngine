#include "scene/ExperimentScenes.hpp"
#include "rt/cpu/Bvh.hpp"
#include "rt/gpu/CanonicalTraversalScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

int RunExperimentScenesTests()
{
    using namespace RenderingEngine::Contracts::AbiV0;
    using namespace RenderingEngine::Scene;

    int failures = 0;
    const auto expect = [&failures](const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "L2 experiment scene test failed: " << message << '\n';
            ++failures;
        }
    };

    const auto lightFlags = [](const ExperimentScene& scene)
    {
        std::vector<std::uint32_t> result;
        result.reserve(scene.canonical.lights.size());
        for (const GpuLightV0& light : scene.canonical.lights)
        {
            result.push_back(light.extra.y);
        }
        return result;
    };
    const auto environmentsEqual = [](const ExperimentEnvironment& lhs,
        const ExperimentEnvironment& rhs)
    {
        if (lhs.width != rhs.width || lhs.height != rhs.height
            || lhs.linearRgba.size() != rhs.linearRgba.size())
        {
            return false;
        }
        for (std::size_t index = 0u; index < lhs.linearRgba.size(); ++index)
        {
            const AbiFloat4 a = lhs.linearRgba[index];
            const AbiFloat4 b = rhs.linearRgba[index];
            if (a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w)
            {
                return false;
            }
        }
        return true;
    };
    const auto validateEnvironmentPdf = [&expect](const ExperimentEnvironment& value)
    {
        constexpr double pi = 3.14159265358979323846;
        double luminanceIntegral = 0.0;
        std::vector<double> masses(value.linearRgba.size(), 0.0);
        for (std::uint32_t y = 0u; y < value.height; ++y)
        {
            const double theta0 = pi * static_cast<double>(y)
                / static_cast<double>(value.height);
            const double theta1 = pi * static_cast<double>(y + 1u)
                / static_cast<double>(value.height);
            const double texelSolidAngle = (2.0 * pi / value.width)
                * (std::cos(theta0) - std::cos(theta1));
            expect(std::isfinite(texelSolidAngle) && texelSolidAngle > 0.0,
                "scene 5 lat-long texels must have finite positive solid angle at poles");
            for (std::uint32_t x = 0u; x < value.width; ++x)
            {
                const AbiFloat4 texel = value.linearRgba[
                    static_cast<std::size_t>(y) * value.width + x];
                const double luminance = 0.2126 * texel.x
                    + 0.7152 * texel.y + 0.0722 * texel.z;
                const std::size_t index =
                    static_cast<std::size_t>(y) * value.width + x;
                masses[index] = std::max(0.0, luminance) * texelSolidAngle;
                luminanceIntegral += masses[index];
            }
        }
        expect(std::isfinite(luminanceIntegral) && luminanceIntegral > 0.0,
            "scene 5 HDR environment must have a finite non-zero spherical integral");
        double pmfSum = 0.0;
        double reconstructedIntegral = 0.0;
        for (const double mass : masses)
        {
            const double pmf = mass / luminanceIntegral;
            expect(std::isfinite(pmf) && pmf >= 0.0,
                "scene 5 importance PMF must remain finite and non-negative");
            pmfSum += pmf;
            if (pmf > 0.0)
            {
                // Sample/Eval/PDF identity for the piecewise-constant texel:
                // p * (L*dOmega/p) reconstructs the same spherical integral.
                reconstructedIntegral += pmf * (mass / pmf);
            }
        }
        expect(std::abs(pmfSum - 1.0) <= 1.0e-10
                && std::abs(reconstructedIntegral - luminanceIntegral)
                    <= luminanceIntegral * 1.0e-12,
            "scene 5 importance Sample/Eval/PDF must normalize and reconstruct the HDR integral");
    };

    const auto validateTraversalCorpus = [&expect](const ExperimentScene& experiment)
    {
        using namespace RenderingEngine::Rt;
        const Gpu::CanonicalTraversalSceneBuild built =
            Gpu::BuildCanonicalTraversalScene(
                MakeCanonicalSceneView(experiment.canonical));
        expect(static_cast<bool>(built),
            "scene 1 fixed-ray corpus must expand into canonical traversal data");
        if (!built) return;
        const Cpu::Bvh<float> sah(
            built.scene.cpuTriangles, Cpu::BvhBuildMethod::BinnedSah);
        for (const ExperimentRayCase& rayCase : experiment.traversalCorpus)
        {
            const auto& input = rayCase.ray;
            const Cpu::Ray<float> ray{
                {input.originTMin.x, input.originTMin.y, input.originTMin.z},
                {input.directionTMax.x, input.directionTMax.y,
                    input.directionTMax.z},
                input.originTMin.w,
                input.directionTMax.w};
            const bool validDirection = std::isfinite(ray.direction.x)
                && std::isfinite(ray.direction.y)
                && std::isfinite(ray.direction.z)
                && (ray.direction.x != 0.0f || ray.direction.y != 0.0f
                    || ray.direction.z != 0.0f);
            if (!validDirection)
            {
                expect(rayCase.expectedHitKind == HitKindInvalid,
                    "scene 1 invalid fixed ray must be explicitly classified");
                continue;
            }
            const Cpu::Hit<float> brute = Cpu::TraceClosestBruteForce(
                std::span<const Cpu::Triangle<float>>{built.scene.cpuTriangles}, ray);
            const Cpu::Hit<float> accelerated = sah.TraceClosest(ray);
            expect(brute.IsHit() == accelerated.IsHit(),
                "scene 1 brute-force and SAH must agree on hit/miss");
            if (!brute.IsHit())
            {
                expect(rayCase.expectedHitKind == HitKindMiss,
                    "scene 1 fixed miss must remain a miss in the CPU oracle");
                continue;
            }
            const std::uint32_t brutePrimitive =
                built.scene.triangles.at(brute.primitiveId).identity.y;
            const std::uint32_t sahPrimitive =
                built.scene.triangles.at(accelerated.primitiveId).identity.y;
            const auto acceptable = [&rayCase](const std::uint32_t primitive)
            {
                return std::find(rayCase.acceptablePrimitiveIds.begin(),
                    rayCase.acceptablePrimitiveIds.begin()
                        + rayCase.acceptablePrimitiveCount, primitive)
                    != rayCase.acceptablePrimitiveIds.begin()
                        + rayCase.acceptablePrimitiveCount;
            };
            const float tolerance = rayCase.relativeDistanceTolerance
                * std::max(1.0f, std::abs(rayCase.expectedDistance));
            expect(rayCase.expectedHitKind == HitKindTriangle
                    && acceptable(brutePrimitive)
                    && acceptable(sahPrimitive)
                    && std::abs(brute.t - rayCase.expectedDistance) <= tolerance
                    && std::abs(accelerated.t - brute.t) <= tolerance
                    && std::abs(accelerated.barycentric.x - brute.barycentric.x)
                        <= tolerance
                    && std::abs(accelerated.barycentric.y - brute.barycentric.y)
                        <= tolerance,
                "scene 1 fixed ray must produce an equivalent HitRecord across brute-force and SAH");
        }
    };

    const std::span<const ExperimentSceneDescriptor> registry =
        ExperimentSceneRegistry();
    expect(registry.size() == 10u,
        "the Wave 5 catalog must publish all ten stable scene descriptors");
    expect(FirstFiveExperimentSceneRegistry().size() == 5u,
        "the legacy first-five registry view must remain stable");

    for (const ExperimentSceneDescriptor& descriptor : registry)
    {
        const bool shouldBuild = descriptor.preset
            != ExperimentScenePreset::SponzaTraversalHall;
        expect(IsExperimentSceneBuilt(descriptor.preset) == shouldBuild,
            "scene availability must match the external Sponza asset gate");
        if (!shouldBuild)
        {
            continue;
        }
        try
        {
            const ExperimentScene first = BuildExperimentScene(descriptor.preset);
            const ExperimentScene second = BuildExperimentScene(descriptor.preset);
            expect(static_cast<bool>(ValidateCanonicalScene(first.canonical)),
                "every built experiment scene must satisfy the canonical validator");
            expect(CanonicalSceneFingerprint(first.canonical)
                    == CanonicalSceneFingerprint(second.canonical),
                "every built experiment scene must be deterministic");
            expect(first.canonical.stableId == descriptor.canonicalStableId
                    && first.activeCameraStableId == descriptor.fixedCameraStableId
                    && !first.activeVariantStableId.empty(),
                "every built experiment scene must publish its fixed identity and default view");
        }
        catch (const std::exception& error)
        {
            std::cerr << "L2 experiment scene build failed for "
                << descriptor.runtimeToken << ": " << error.what() << '\n';
            ++failures;
        }
    }

    const ExperimentScene baseline = BuildBaselineGalleryExperimentScene();
    expect(baseline.canonical.materials.size() == 11u
            && baseline.canonical.geometries.size() == 11u
            && baseline.canonical.lights.size() == 3u,
        "baseline must retain six authored objects, two visible emitters, checker floor, back wall, and environment");
    expect(baseline.environment.width == 128u
            && baseline.environment.height == 64u
            && baseline.environmentEnabled
            && (baseline.canonical.constants.versionFlags.z & SceneFlagHasEnvironment) != 0u,
        "baseline must publish the deterministic Wave 0 latitude environment");
    const AbiFloat4 baselineZenith = baseline.environment.linearRgba.front();
    const AbiFloat4 baselineGround = baseline.environment.linearRgba[
        static_cast<std::size_t>(baseline.environment.height - 1u)
            * baseline.environment.width];
    expect(baselineZenith.z > 0.77f && baselineZenith.x > 0.29f
            && baselineGround.x < 0.019f && baselineGround.z < 0.013f,
        "baseline environment must preserve the blue zenith and dark warm ground of Wave 0");
    expect(baseline.environment.linearRgba[17u].x == baselineZenith.x
            && baseline.environment.linearRgba[17u].y == baselineZenith.y
            && baseline.environment.linearRgba[17u].z == baselineZenith.z,
        "baseline environment must remain latitude-only and deterministic across longitude");

    const GpuLightV0& warmLight = baseline.canonical.lights[0u];
    const GpuLightV0& coolLight = baseline.canonical.lights[1u];
    const GpuLightV0& environmentLight = baseline.canonical.lights[2u];
    expect(warmLight.identity.x == LightTypeSphereArea
            && warmLight.positionRange.x == -3.10f
            && warmLight.positionRange.y == 4.20f
            && warmLight.positionRange.z == -1.90f
            && warmLight.shapeParams.x == 0.34f
            && warmLight.radianceScale.x == 180.0f
            && warmLight.radianceScale.y == 105.0f
            && warmLight.radianceScale.z == 45.0f,
        "baseline warm sphere light must retain the exact Wave 0 transform and radiance");
    expect(coolLight.identity.x == LightTypeSphereArea
            && coolLight.positionRange.x == 3.15f
            && coolLight.positionRange.y == 2.45f
            && coolLight.positionRange.z == -3.20f
            && coolLight.shapeParams.x == 0.28f
            && coolLight.radianceScale.x == 45.0f
            && coolLight.radianceScale.y == 80.0f
            && coolLight.radianceScale.z == 180.0f,
        "baseline cool sphere light must retain the exact Wave 0 transform and radiance");
    expect(environmentLight.identity.x == LightTypeEnvironment
            && (environmentLight.extra.y & LightFlagEnabled) != 0u,
        "baseline Wave 0 environment must be enabled as a canonical Wave 2 light");

    const GpuMaterialV0& floorLight = baseline.canonical.materials[4u];
    const GpuMaterialV0& floorDark = baseline.canonical.materials[8u];
    expect(floorLight.baseColorFactor.x == 0.55f
            && floorDark.baseColorFactor.x == 0.132f
            && floorDark.baseColorFactor.y == 0.1368f
            && floorDark.baseColorFactor.z == 0.1488f,
        "baseline checker materials must preserve Wave 0's authored color and 0.24 dark multiplier");
    const bool hasLightChecker = std::any_of(
        baseline.canonical.geometries.begin(), baseline.canonical.geometries.end(),
        [](const GpuGeometryV0& geometry) { return geometry.identity.z == 4u; });
    const bool hasDarkChecker = std::any_of(
        baseline.canonical.geometries.begin(), baseline.canonical.geometries.end(),
        [](const GpuGeometryV0& geometry) { return geometry.identity.z == 8u; });
    const bool hasOriginalBackWall = std::any_of(
        baseline.canonical.geometries.begin(), baseline.canonical.geometries.end(),
        [](const GpuGeometryV0& geometry)
        {
            return geometry.identity.z == 5u
                && geometry.localBoundsMin.z == -8.5f
                && geometry.localBoundsMax.z == -8.5f;
        });
    expect(hasLightChecker && hasDarkChecker && hasOriginalBackWall,
        "baseline canonical geometry must retain both checker parities and the Wave 0 back-wall depth");

    const GpuMaterialV0& warmEmitter = baseline.canonical.materials[9u];
    const GpuMaterialV0& coolEmitter = baseline.canonical.materials[10u];
    const std::size_t visibleEmitterGeometryCount = static_cast<std::size_t>(std::count_if(
        baseline.canonical.geometries.begin(), baseline.canonical.geometries.end(),
        [](const GpuGeometryV0& geometry)
        {
            return geometry.identity.z == 9u || geometry.identity.z == 10u;
        }));
    expect(visibleEmitterGeometryCount == 2u
            && (warmEmitter.metadata.y & MaterialFlagEmissive) != 0u
            && (coolEmitter.metadata.y & MaterialFlagEmissive) != 0u
            && warmEmitter.emissiveFactorStrength.x == 180.0f
            && coolEmitter.emissiveFactorStrength.z == 180.0f,
        "baseline must restore both visible Wave 0 emissive sphere proxies");
    expect(baseline.canonical.cameras[0u].eye.x == 0.0f
            && baseline.canonical.cameras[0u].eye.y == 0.25f
            && baseline.canonical.cameras[0u].eye.z == 2.5f
            && baseline.canonical.cameras[0u].verticalFovDegrees == 52.0f,
        "baseline fixed camera and lens must remain identical to Wave 0");

    ExperimentScene ggx = BuildGgxMisMaterialExperimentScene();
    const std::vector<std::uint32_t> ggxDefaultLightFlags = lightFlags(ggx);
    const std::size_t ggxGeometryCount = ggx.canonical.geometries.size();
    const std::size_t ggxMaterialCount = ggx.canonical.materials.size();
    for (const std::string_view variantId : std::array{
        std::string_view{"material-grid"},
        std::string_view{"small-light"},
        std::string_view{"large-light"},
        std::string_view{"grazing-light"},
        std::string_view{"white-furnace"},
        std::string_view{"rough-transmission"}})
    {
        expect(FindExperimentSceneVariant(ggx, variantId) != nullptr,
            "scene 4 must retain and expose every named material variant");
    }
    const auto roughMaterial = std::find_if(
        ggx.canonical.materials.begin(), ggx.canonical.materials.end(),
        [](const GpuMaterialV0& material)
        {
            return material.surfaceParams.y > 0.0f
                && material.transmissionParams.x > 0.0f
                && material.transmissionParams.y > 1.0f;
        });
    expect(roughMaterial != ggx.canonical.materials.end(),
        "scene 4 must contain a genuinely rough transmissive material");
    if (roughMaterial != ggx.canonical.materials.end())
    {
        const std::uint32_t materialId = roughMaterial->metadata.z;
        const auto roughGeometry = std::find_if(
            ggx.canonical.geometries.begin(), ggx.canonical.geometries.end(),
            [materialId](const GpuGeometryV0& geometry)
            {
                return geometry.identity.z == materialId;
            });
        expect(roughGeometry != ggx.canonical.geometries.end(),
            "scene 4 rough transmission must be attached to visible geometry");
    }
    const std::uint64_t ggxCanonicalFingerprint =
        CanonicalSceneFingerprint(ggx.canonical);
    expect(ApplyExperimentSceneVariant(ggx, "rough-transmission"),
        "scene 4 rough transmission variant must be applicable");
    expect(ggx.activeVariantStableId == "rough-transmission"
            && ggx.activeCameraStableId == "camera:showcase-4-rough-transmission"
            && !ggx.environmentEnabled
            && ggx.canonical.geometries.size() == ggxGeometryCount
            && ggx.canonical.materials.size() == ggxMaterialCount,
        "rough transmission must change only the view/light selection over existing scene data");
    expect(std::count_if(ggx.canonical.lights.begin(), ggx.canonical.lights.end(),
        [](const GpuLightV0& light)
        {
            return (light.extra.y & LightFlagEnabled) != 0u;
        }) == 1,
        "rough transmission must retain a finite backlight for visible refraction");
    const std::vector<std::uint32_t> roughLightFlags = lightFlags(ggx);
    expect(!roughLightFlags.empty()
            && (roughLightFlags[0u] & LightFlagEnabled) != 0u,
        "rough transmission must enable the existing small sphere backlight");

    expect(ApplyExperimentSceneVariant(ggx, "white-furnace"),
        "scene 4 white furnace variant must be applicable");
    const bool furnaceLocalLightsOff = std::all_of(
        ggx.canonical.lights.begin(), ggx.canonical.lights.end(),
        [](const GpuLightV0& light)
        {
            const bool enabled = (light.extra.y & LightFlagEnabled) != 0u;
            return light.identity.x == LightTypeEnvironment ? enabled : !enabled;
        });
    expect(furnaceLocalLightsOff
            && ggx.environmentEnabled
            && ggx.activeCameraStableId == "camera:showcase-4-white-furnace",
        "white furnace must enable only its uniform environment light and fixed camera");
    expect(ApplyExperimentSceneVariant(ggx, "material-grid"),
        "scene 4 default material variant must be restorable");
    expect(lightFlags(ggx) == ggxDefaultLightFlags
            && ggx.activeVariantStableId == "material-grid"
            && ggx.activeCameraStableId == "camera:showcase-4-ggx-mis"
            && ggx.environmentEnabled,
        "scene 4 variant light flags, camera, and environment state must round-trip");
    const std::vector<std::uint32_t> beforeInvalidGgxFlags = lightFlags(ggx);
    const std::string_view beforeInvalidVariant = ggx.activeVariantStableId;
    const std::string_view beforeInvalidCamera = ggx.activeCameraStableId;
    const bool beforeInvalidEnvironment = ggx.environmentEnabled;
    expect(!ApplyExperimentSceneVariant(ggx, "scene-4-does-not-exist")
            && lightFlags(ggx) == beforeInvalidGgxFlags
            && ggx.activeVariantStableId == beforeInvalidVariant
            && ggx.activeCameraStableId == beforeInvalidCamera
            && ggx.environmentEnabled == beforeInvalidEnvironment,
        "an invalid scene 4 variant must not mutate any active selection");
    expect(CanonicalSceneFingerprint(ggx.canonical) == ggxCanonicalFingerprint,
        "scene 4 variant round-trip must restore the canonical light selection exactly");

    ExperimentScene environment =
        BuildEnvironmentSamplingDomeExperimentScene();
    const ExperimentEnvironment originalEnvironment = environment.environment;
    const auto brightest = std::max_element(
        environment.environment.linearRgba.begin(),
        environment.environment.linearRgba.end(),
        [](const AbiFloat4 lhs, const AbiFloat4 rhs)
        {
            return lhs.x + lhs.y + lhs.z < rhs.x + rhs.y + rhs.z;
        });
    expect(environment.environment.width == 128u
            && environment.environment.height == 64u
            && environment.environmentEnabled
            && brightest != environment.environment.linearRgba.end()
            && brightest->x > 100.0f,
        "environment dome must contain an enabled floating-point HDR sun distribution");
    for (const std::string_view variantId : std::array{
        std::string_view{"uniform-sphere"},
        std::string_view{"environment-importance"},
        std::string_view{"sun-alignment-view"},
        std::string_view{"polar-sun"},
        std::string_view{"seam-sun"}})
    {
        expect(FindExperimentSceneVariant(environment, variantId) != nullptr,
            "scene 5 must retain and expose every environment sampling variant");
    }
    const std::size_t environmentGeometryCount = environment.canonical.geometries.size();
    const auto brightestTexel = [](const ExperimentEnvironment& value)
    {
        std::size_t index = 0u;
        for (std::size_t candidate = 1u; candidate < value.linearRgba.size(); ++candidate)
        {
            const AbiFloat4& lhs = value.linearRgba[candidate];
            const AbiFloat4& rhs = value.linearRgba[index];
            if (lhs.x + lhs.y + lhs.z > rhs.x + rhs.y + rhs.z)
            {
                index = candidate;
            }
        }
        return index;
    };
    expect(environment.environment.profile == ExperimentEnvironmentProfile::Original,
        "scene 5 default must use the original generated environment profile");
    validateEnvironmentPdf(environment.environment);
    expect(ApplyExperimentSceneVariant(environment, "polar-sun"),
        "scene 5 polar-sun variant must be applicable");
    const std::size_t polarIndex = brightestTexel(environment.environment);
    expect(environment.environment.profile == ExperimentEnvironmentProfile::PolarSun
            && polarIndex < environment.environment.width
            && environment.environmentEnabled
            && environment.activeCameraStableId == "camera:showcase-5-polar-sun"
            && environment.canonical.geometries.size() == environmentGeometryCount,
        "polar-sun must move the existing procedural sun to the first latitude row");
    validateEnvironmentPdf(environment.environment);
    expect(ApplyExperimentSceneVariant(environment, "seam-sun"),
        "scene 5 seam-sun variant must be applicable");
    const std::size_t seamIndex = brightestTexel(environment.environment);
    const std::size_t seamX = seamIndex % environment.environment.width;
    expect(environment.environment.profile == ExperimentEnvironmentProfile::SeamSun
            && (seamX <= 1u || seamX + 2u >= environment.environment.width)
            && environment.environmentEnabled
            && environment.activeCameraStableId == "camera:showcase-5-seam-sun",
        "seam-sun must place the existing procedural sun across the longitude seam");
    validateEnvironmentPdf(environment.environment);
    const auto wrappedLongitudeTexel = [&environment](double longitude)
    {
        constexpr double twoPi = 6.28318530717958647692;
        double u = longitude / twoPi + 0.5;
        u -= std::floor(u);
        return static_cast<std::uint32_t>(
            std::floor(u * environment.environment.width))
            % environment.environment.width;
    };
    expect(wrappedLongitudeTexel(-3.14159265358979323846 - 1.0e-9)
            + 1u == environment.environment.width
            && wrappedLongitudeTexel(3.14159265358979323846 + 1.0e-9) == 0u,
        "scene 5 longitude evaluation must wrap continuously across the lat-long seam");
    expect(ApplyExperimentSceneVariant(environment, "uniform-sphere"),
        "scene 5 original environment variant must be restorable");
    expect(environment.environment.profile == ExperimentEnvironmentProfile::Original
            && environmentsEqual(environment.environment, originalEnvironment)
            && environment.activeCameraStableId == "camera:showcase-5-environment-dome"
            && environment.environmentEnabled,
        "scene 5 edge variants must round-trip to the exact original environment and camera");

    const ExperimentScene parity = BuildBackendParityBenchmarkExperimentScene();
    expect(parity.canonical.geometries.size() >= 40u,
        "backend parity must contain distinct small, medium, and dense workloads");

    const ExperimentScene intersection = BuildIntersectionBvhExperimentScene();
    expect(intersection.traversalCorpus.size() == 9u,
        "scene 1 must publish the complete named fixed-ray corpus");
    validateTraversalCorpus(intersection);

    const ExperimentScene temporal =
        BuildTemporalStabilityCorridorExperimentScene();
    expect(temporal.canonical.instances.size() == 2u
            && (temporal.canonical.constants.versionFlags.z
                & SceneFlagHasAnimatedRigidInstances) != 0u
            && (temporal.canonical.instances[1].metadata.w
                & InstanceFlagAnimatedRigid) != 0u,
        "temporal corridor must publish an independently moving rigid instance");
    for (const std::string_view variantId : std::array{
        std::string_view{"motion-corridor"},
        std::string_view{"camera-only"},
        std::string_view{"object-only"},
        std::string_view{"disocclusion-focus"},
        std::string_view{"camera-cut"}})
    {
        expect(FindExperimentSceneVariant(temporal, variantId) != nullptr,
            "scene 8 must expose every deterministic timeline variant");
    }
    expect(temporal.canonical.instances[0u].metadata.x == 0u
            && temporal.canonical.instances[0u].metadata.y
                == temporal.canonical.instances[1u].metadata.x
            && temporal.canonical.instances[1u].metadata.z == 1u
            && temporal.sampledAnimationFrameIndex == 1u
            && !temporal.cameraCut,
        "scene 8 must keep static geometry in instance 0 and the moving panel in instance 1");

    ExperimentSceneBuildOptions objectOptions;
    objectOptions.variantStableId = "object-only";
    objectOptions.animationFrameIndex = 15u;
    const ExperimentScene objectOnly = BuildTemporalStabilityCorridorExperimentScene(
        objectOptions);
    const ExperimentScene objectOnlyRepeat = BuildTemporalStabilityCorridorExperimentScene(
        objectOptions);
    expect(CanonicalSceneFingerprint(objectOnly.canonical)
            == CanonicalSceneFingerprint(objectOnlyRepeat.canonical)
            && objectOnly.activeVariantStableId == "object-only"
            && objectOnly.activeCameraStableId == "camera:showcase-8-temporal-stability"
            && objectOnly.canonical.instances[1u].objectToWorld.row0.w
                != objectOnly.canonical.instances[1u].previousObjectToWorld.row0.w,
        "object-only must deterministically move only the existing panel instance");

    ExperimentSceneBuildOptions cameraOptions;
    cameraOptions.variantStableId = "camera-only";
    cameraOptions.animationFrameIndex = 15u;
    const ExperimentScene cameraOnly = BuildExperimentScene(
        ExperimentScenePreset::TemporalStabilityCorridor, cameraOptions);
    const auto cameraOnlyPreset = std::find_if(
        cameraOnly.canonical.cameras.begin(), cameraOnly.canonical.cameras.end(),
        [&cameraOnly](const CameraPreset& camera)
        {
            return camera.stableId == cameraOnly.activeCameraStableId;
        });
    expect(cameraOnly.activeVariantStableId == "camera-only"
            && cameraOnly.activeCameraStableId == "camera:showcase-8-camera-only"
            && cameraOnly.canonical.instances[1u].objectToWorld.row0.w
                == cameraOnly.canonical.instances[1u].previousObjectToWorld.row0.w
            && cameraOnlyPreset != cameraOnly.canonical.cameras.end()
            && cameraOnlyPreset->eye.x > 0.1f,
        "camera-only must freeze the panel and sample a deterministic moving camera pose");

    ExperimentSceneBuildOptions disocclusionOptions;
    disocclusionOptions.variantStableId = "disocclusion-focus";
    disocclusionOptions.animationFrameIndex = 15u;
    const ExperimentScene disocclusion = BuildTemporalStabilityCorridorExperimentScene(
        disocclusionOptions);
    expect(disocclusion.activeCameraStableId == "camera:showcase-8-disocclusion"
            && disocclusion.canonical.instances[1u].objectToWorld.row0.w
                != disocclusion.canonical.instances[1u].previousObjectToWorld.row0.w
            && !disocclusion.cameraCut,
        "disocclusion-focus must retain the moving panel with its focused camera");

    ExperimentSceneBuildOptions cutOptions;
    cutOptions.variantStableId = "camera-cut";
    cutOptions.animationFrameIndex = 59u;
    const ExperimentScene beforeCut = BuildTemporalStabilityCorridorExperimentScene(
        cutOptions);
    cutOptions.animationFrameIndex = 60u;
    const ExperimentScene cut = BuildTemporalStabilityCorridorExperimentScene(cutOptions);
    cutOptions.animationFrameIndex = 61u;
    const ExperimentScene afterCut = BuildTemporalStabilityCorridorExperimentScene(
        cutOptions);
    ExperimentScene repeatedBeforeCut = beforeCut;
    ExperimentScene repeatedCut = cut;
    ExperimentScene repeatedAfterCut = afterCut;
    const bool repeatedBeforeCutApplied = ApplyExperimentSceneVariant(
        repeatedBeforeCut, "camera-cut");
    const bool repeatedCutApplied = ApplyExperimentSceneVariant(
        repeatedCut, "camera-cut");
    const bool repeatedAfterCutApplied = ApplyExperimentSceneVariant(
        repeatedAfterCut, "camera-cut");
    expect(beforeCut.activeCameraStableId == "camera:showcase-8-temporal-stability"
            && !beforeCut.cameraCut
            && cut.activeCameraStableId == "camera:showcase-8-disocclusion"
            && cut.cameraCut
            && afterCut.activeCameraStableId == "camera:showcase-8-disocclusion"
            && !afterCut.cameraCut
            && cut.canonical.instances[1u].objectToWorld.row0.w
                == cut.canonical.instances[1u].previousObjectToWorld.row0.w
            && repeatedBeforeCutApplied
            && repeatedBeforeCut.activeCameraStableId
                == "camera:showcase-8-temporal-stability"
            && !repeatedBeforeCut.cameraCut
            && repeatedCutApplied
            && repeatedCut.activeCameraStableId
                == "camera:showcase-8-disocclusion"
            && repeatedCut.cameraCut
            && repeatedAfterCutApplied
            && repeatedAfterCut.activeCameraStableId
                == "camera:showcase-8-disocclusion"
            && !repeatedAfterCut.cameraCut,
        "camera-cut must be a discrete N=60 event and idempotent across repeated Apply calls");

    bool unknownVariantRejected = false;
    try
    {
        ExperimentSceneBuildOptions invalidOptions;
        invalidOptions.variantStableId = "unknown-temporal-variant";
        static_cast<void>(BuildExperimentScene(
            ExperimentScenePreset::TemporalStabilityCorridor, invalidOptions));
    }
    catch (const std::invalid_argument&)
    {
        unknownVariantRejected = true;
    }
    expect(unknownVariantRejected,
        "BuildExperimentScene must reject an unknown requested variant instead of falling back");

    const ExperimentScene manyLights =
        BuildManyLightsRestirArenaExperimentScene();
    expect(manyLights.canonical.lights.size() == 100u
            && manyLights.canonical.instances.size() > 100u,
        "the production-selectable Many Lights view must contain the real 100-light arena");
    for (const std::uint32_t lightCount : std::array{100u, 1000u, 10000u})
    {
        ExperimentSceneBuildOptions options;
        options.manyLightsCount = lightCount;
        options.animationFrameIndex = 7u;
        options.animateLights = false;
        options.animateCamera = false;
        options.animateRigidOccluders = false;
        const ExperimentScene tier = BuildExperimentScene(
            ExperimentScenePreset::ManyLightsRestirArena, options);
        expect(tier.canonical.lights.size() == lightCount,
            "Many Lights scene data must match the requested 100/1k/10k tier");
        const bool transformsFrozen = std::all_of(
            tier.canonical.instances.begin(), tier.canonical.instances.end(),
            [](const GpuInstanceV0& instance)
            {
                return instance.objectToWorld.row0.w
                        == instance.previousObjectToWorld.row0.w
                    && instance.objectToWorld.row1.w
                        == instance.previousObjectToWorld.row1.w
                    && instance.objectToWorld.row2.w
                        == instance.previousObjectToWorld.row2.w;
            });
        expect(transformsFrozen,
            "a frozen Many Lights request must preserve current/previous transforms");
    }

    bool sponzaFailedClosed = false;
    try
    {
        static_cast<void>(BuildExperimentScene(
            ExperimentScenePreset::SponzaTraversalHall));
    }
    catch (const std::runtime_error&)
    {
        sponzaFailedClosed = true;
    }
    expect(sponzaFailedClosed,
        "Sponza must not be replaced by synthetic geometry when its licensed asset is absent");

    return failures == 0 ? 0 : 1;
}
