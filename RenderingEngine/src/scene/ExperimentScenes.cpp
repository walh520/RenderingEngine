#include "scene/ExperimentScenes.hpp"
#include "scene/ManyLightsArena.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace RenderingEngine::Scene
{
    namespace
    {
        using namespace Contracts::AbiV0;
        using RayRecord = Contracts::AbiV1::GpuRayQueueRecordV1;

        constexpr float kPi = 3.14159265358979323846f;

        constexpr std::array kExperimentRegistry{
            ExperimentSceneDescriptor{
                ExperimentScenePreset::BaselineGallery,
                0u,
                "baseline",
                "showcase-0-baseline-canonical-adapter",
                "Baseline Gallery",
                "camera:showcase-0-baseline",
                ExperimentAlgorithmWhittedReflection
                    | ExperimentAlgorithmWhittedRefraction
                    | ExperimentAlgorithmShadowRay
                    | ExperimentAlgorithmGgxSmithFresnel
                    | ExperimentAlgorithmAccumulation,
                8u,
                64u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::IntersectionBvhLab,
                1u,
                "intersection-bvh",
                "showcase-1-intersection-bvh-lab",
                "Intersection & BVH Lab",
                "camera:showcase-1-intersection-bvh",
                ExperimentAlgorithmClosestAnyHit
                    | ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmCpuSah
                    | ExperimentAlgorithmFlattenedSah
                    | ExperimentAlgorithmGpuLbvh,
                1u,
                1u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::WhittedOpticsRoom,
                2u,
                "whitted-optics",
                "showcase-2-whitted-optics-room",
                "Whitted Optics Room",
                "camera:showcase-2-whitted-optics",
                ExperimentAlgorithmClosestAnyHit
                    | ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmWhittedReflection
                    | ExperimentAlgorithmWhittedRefraction
                    | ExperimentAlgorithmFresnelTir
                    | ExperimentAlgorithmShadowRay
                    | ExperimentAlgorithmBeerLambert,
                12u,
                64u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::CornellBox,
                3u,
                "cornell",
                "showcase-3-cornell-box",
                "Cornell Box",
                "camera:showcase-3-cornell",
                ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmDiffuseGi
                    | ExperimentAlgorithmNextEventEstimation
                    | ExperimentAlgorithmMultipleImportanceSampling
                    | ExperimentAlgorithmRussianRoulette
                    | ExperimentAlgorithmAccumulation,
                8u,
                4096u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::GgxMisMaterialLab,
                4u,
                "ggx-mis",
                "showcase-4-ggx-mis-material-lab",
                "GGX & MIS Material Lab",
                "camera:showcase-4-ggx-mis",
                ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmNextEventEstimation
                    | ExperimentAlgorithmMultipleImportanceSampling
                    | ExperimentAlgorithmGgxSmithFresnel
                    | ExperimentAlgorithmGgxVndf
                    | ExperimentAlgorithmWhiteFurnace
                    | ExperimentAlgorithmAccumulation,
                8u,
                1024u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::EnvironmentSamplingDome,
                5u,
                "environment-dome",
                "showcase-5-environment-sampling-dome",
                "Environment Sampling Dome",
                "camera:showcase-5-environment-dome",
                ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmNextEventEstimation
                    | ExperimentAlgorithmMultipleImportanceSampling
                    | ExperimentAlgorithmGgxSmithFresnel
                    | ExperimentAlgorithmEnvironmentImportance
                    | ExperimentAlgorithmAccumulation,
                8u,
                1024u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::SponzaTraversalHall,
                6u,
                "sponza",
                "showcase-6-sponza-traversal-hall",
                "Sponza Traversal Hall",
                "camera:showcase-6-sponza",
                ExperimentAlgorithmClosestAnyHit
                    | ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmCpuSah
                    | ExperimentAlgorithmFlattenedSah
                    | ExperimentAlgorithmGpuLbvh,
                8u,
                256u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::BackendParityBenchmark,
                7u,
                "backend-parity",
                "showcase-7-backend-parity-benchmark",
                "Backend Parity Benchmark",
                "camera:showcase-7-backend-parity",
                ExperimentAlgorithmClosestAnyHit
                    | ExperimentAlgorithmBackendParity
                    | ExperimentAlgorithmCpuSah
                    | ExperimentAlgorithmFlattenedSah
                    | ExperimentAlgorithmGpuLbvh
                    | ExperimentAlgorithmAccumulation,
                4u,
                64u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::TemporalStabilityCorridor,
                8u,
                "temporal-stability",
                "showcase-8-temporal-stability-corridor",
                "Temporal Stability Corridor",
                "camera:showcase-8-temporal-stability",
                ExperimentAlgorithmMultipleImportanceSampling
                    | ExperimentAlgorithmAccumulation
                    | ExperimentAlgorithmTemporalReconstruction
                    | ExperimentAlgorithmAtrous
                    | ExperimentAlgorithmSvgf,
                6u,
                64u
            },
            ExperimentSceneDescriptor{
                ExperimentScenePreset::ManyLightsRestirArena,
                9u,
                "many-lights",
                "showcase-9-many-lights-restir-arena",
                "Many Lights / ReSTIR Arena",
                "camera:showcase-9-many-lights",
                ExperimentAlgorithmNextEventEstimation
                    | ExperimentAlgorithmMultipleImportanceSampling
                    | ExperimentAlgorithmAccumulation
                    | ExperimentAlgorithmRestirDi
                    | ExperimentAlgorithmStableLightHistory,
                4u,
                256u
            }
        };

        struct Vec3
        {
            float x{};
            float y{};
            float z{};
        };

        [[nodiscard]] Vec3 operator+(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
        }

        [[nodiscard]] Vec3 operator-(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        [[nodiscard]] Vec3 operator*(const Vec3 value, const float scalar) noexcept
        {
            return {value.x * scalar, value.y * scalar, value.z * scalar};
        }

        [[nodiscard]] float Dot(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        [[nodiscard]] Vec3 Cross(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        [[nodiscard]] float LengthSquared(const Vec3 value) noexcept
        {
            return Dot(value, value);
        }

        [[nodiscard]] Vec3 Normalize(const Vec3 value) noexcept
        {
            const float lengthSquared = LengthSquared(value);
            if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared))
            {
                return {};
            }
            return value * (1.0f / std::sqrt(lengthSquared));
        }

        [[nodiscard]] Vec3 EnvironmentSunDirection(
            const ExperimentEnvironmentProfile profile) noexcept
        {
            switch (profile)
            {
            case ExperimentEnvironmentProfile::PolarSun:
                // +Y maps to theta ~= 0. The first latitude row therefore
                // exercises the sin(theta) pole limit.
                return {0.0f, 1.0f, 0.0f};
            case ExperimentEnvironmentProfile::SeamSun:
                // +X maps to phi = 0/2pi. Its finite angular radius lights
                // both edge texels and exercises longitude wrapping.
                return {1.0f, 0.0f, 0.0f};
            case ExperimentEnvironmentProfile::Original:
            default:
                return Normalize({-0.42f, 0.68f, -0.60f});
            }
        }

        void FillEnvironmentImage(
            ExperimentEnvironment& environment,
            const ExperimentEnvironmentProfile profile) noexcept
        {
            const std::uint64_t texelCount =
                static_cast<std::uint64_t>(environment.width)
                * static_cast<std::uint64_t>(environment.height);
            if (environment.width == 0u || environment.height == 0u
                || texelCount != environment.linearRgba.size())
            {
                return;
            }
            const Vec3 sunDirection = EnvironmentSunDirection(profile);
            for (std::uint32_t y = 0u; y < environment.height; ++y)
            {
                const float theta = kPi * (static_cast<float>(y) + 0.5f)
                    / static_cast<float>(environment.height);
                const float sineTheta = std::sin(theta);
                const float cosineTheta = std::cos(theta);
                for (std::uint32_t x = 0u; x < environment.width; ++x)
                {
                    const float phi = 2.0f * kPi * (static_cast<float>(x) + 0.5f)
                        / static_cast<float>(environment.width);
                    const Vec3 direction{
                        sineTheta * std::cos(phi), cosineTheta,
                        sineTheta * std::sin(phi)
                    };
                    const float horizon = std::pow(
                        (std::max)(0.0f, 1.0f - std::abs(direction.y)), 3.0f);
                    const float upper = (std::max)(0.0f, direction.y);
                    Vec3 radiance{
                        0.055f + 0.34f * horizon + 0.08f * upper,
                        0.075f + 0.27f * horizon + 0.15f * upper,
                        0.12f + 0.18f * horizon + 0.32f * upper
                    };
                    const float sunAlignment = Dot(direction, sunDirection);
                    if (sunAlignment > 0.9991f)
                    {
                        const float core = (sunAlignment - 0.9991f) / 0.0009f;
                        radiance = radiance + Vec3{520.0f, 390.0f, 235.0f}
                            * (core * core);
                    }
                    environment.linearRgba[
                        static_cast<std::size_t>(y) * environment.width + x] = {
                        radiance.x, radiance.y, radiance.z, 1.0f
                    };
                }
            }
            environment.profile = profile;
        }

        [[nodiscard]] Vec3 RotateY(const Vec3 value, const float radians) noexcept
        {
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            return {
                cosine * value.x + sine * value.z,
                value.y,
                -sine * value.x + cosine * value.z
            };
        }

        [[nodiscard]] Vec3 FromAbi(const AbiFloat4 value) noexcept
        {
            return {value.x, value.y, value.z};
        }

        struct Bounds
        {
            Vec3 minimum{
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)()
            };
            Vec3 maximum{
                (std::numeric_limits<float>::lowest)(),
                (std::numeric_limits<float>::lowest)(),
                (std::numeric_limits<float>::lowest)()
            };
            bool empty = true;
        };

        void Expand(Bounds& bounds, const Vec3 point) noexcept
        {
            bounds.minimum.x = (std::min)(bounds.minimum.x, point.x);
            bounds.minimum.y = (std::min)(bounds.minimum.y, point.y);
            bounds.minimum.z = (std::min)(bounds.minimum.z, point.z);
            bounds.maximum.x = (std::max)(bounds.maximum.x, point.x);
            bounds.maximum.y = (std::max)(bounds.maximum.y, point.y);
            bounds.maximum.z = (std::max)(bounds.maximum.z, point.z);
            bounds.empty = false;
        }

        [[nodiscard]] constexpr AbiMat4Rows IdentityMatrix() noexcept
        {
            return {
                {1.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}
            };
        }

        [[nodiscard]] constexpr AbiMat4Rows TranslationMatrix(
            const Vec3 translation) noexcept
        {
            return {
                {1.0f, 0.0f, 0.0f, translation.x},
                {0.0f, 1.0f, 0.0f, translation.y},
                {0.0f, 0.0f, 1.0f, translation.z},
                {0.0f, 0.0f, 0.0f, 1.0f}
            };
        }

        [[nodiscard]] Vec3 TangentForNormal(const Vec3 normal) noexcept
        {
            const Vec3 reference = std::abs(normal.y) < 0.9f
                ? Vec3{0.0f, 1.0f, 0.0f}
                : Vec3{1.0f, 0.0f, 0.0f};
            return Normalize(Cross(reference, normal));
        }

        [[nodiscard]] std::uint32_t AppendVertex(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            Bounds& geometryBounds,
            const Vec3 position,
            const Vec3 normal,
            const Vec3 tangent,
            const float u,
            const float v)
        {
            if (scene.vertices.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                throw std::length_error("Experiment scene vertex count exceeds uint32.");
            }
            const std::uint32_t index = static_cast<std::uint32_t>(scene.vertices.size());
            scene.vertices.push_back({
                {position.x, position.y, position.z, 1.0f},
                {normal.x, normal.y, normal.z, 0.0f},
                {tangent.x, tangent.y, tangent.z, 1.0f},
                {u, v, 0.0f, 0.0f}
            });
            Expand(sceneBounds, position);
            Expand(geometryBounds, position);
            return index;
        }

        void AppendIndex(CanonicalScene& scene, const std::uint32_t index)
        {
            if (scene.indices.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                throw std::length_error("Experiment scene index count exceeds uint32.");
            }
            scene.indices.push_back(index);
        }

        void AppendFlatTriangle(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            Bounds& geometryBounds,
            Vec3 p0,
            Vec3 p1,
            Vec3 p2,
            Vec3 desiredNormal)
        {
            desiredNormal = Normalize(desiredNormal);
            if (!(LengthSquared(desiredNormal) > 0.0f))
            {
                desiredNormal = Normalize(Cross(p1 - p0, p2 - p0));
            }
            if (!(Dot(Cross(p1 - p0, p2 - p0), desiredNormal) > 0.0f))
            {
                std::swap(p1, p2);
            }
            Vec3 tangent = Normalize(p1 - p0);
            if (!(LengthSquared(tangent) > 0.0f))
            {
                tangent = TangentForNormal(desiredNormal);
            }
            const std::uint32_t i0 = AppendVertex(
                scene, sceneBounds, geometryBounds, p0, desiredNormal, tangent, 0.0f, 0.0f);
            const std::uint32_t i1 = AppendVertex(
                scene, sceneBounds, geometryBounds, p1, desiredNormal, tangent, 1.0f, 0.0f);
            const std::uint32_t i2 = AppendVertex(
                scene, sceneBounds, geometryBounds, p2, desiredNormal, tangent, 0.5f, 1.0f);
            AppendIndex(scene, i0);
            AppendIndex(scene, i1);
            AppendIndex(scene, i2);
        }

        void AppendFlatQuad(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            Bounds& geometryBounds,
            const Vec3 p0,
            Vec3 p1,
            const Vec3 p2,
            Vec3 p3,
            const Vec3 desiredNormal)
        {
            if (!(Dot(Cross(p1 - p0, p2 - p0), desiredNormal) > 0.0f))
            {
                std::swap(p1, p3);
            }
            AppendFlatTriangle(scene, sceneBounds, geometryBounds, p0, p1, p2, desiredNormal);
            AppendFlatTriangle(scene, sceneBounds, geometryBounds, p0, p2, p3, desiredNormal);
        }

        struct GeometryResult
        {
            std::uint32_t geometryId = kInvalidId;
            std::uint32_t firstPrimitive = kInvalidId;
            std::uint32_t primitiveCount = 0u;
        };

        [[nodiscard]] GeometryResult FinishGeometry(
            CanonicalScene& scene,
            const Bounds& geometryBounds,
            const std::uint32_t firstIndex,
            const std::uint32_t materialId,
            const std::uint32_t flags = GeometryFlagOpaque)
        {
            if (geometryBounds.empty || firstIndex > scene.indices.size())
            {
                throw std::logic_error("Experiment geometry contains no triangles.");
            }
            const std::size_t indexCountSize = scene.indices.size() - firstIndex;
            if (indexCountSize == 0u || indexCountSize % 3u != 0u
                || indexCountSize > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::logic_error("Experiment geometry index range is invalid.");
            }
            if (scene.geometries.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                throw std::length_error("Experiment geometry count exceeds uint32.");
            }
            const std::uint32_t geometryId = static_cast<std::uint32_t>(scene.geometries.size());
            const std::uint32_t indexCount = static_cast<std::uint32_t>(indexCountSize);
            const std::uint32_t firstPrimitive = firstIndex / 3u;
            scene.geometries.push_back({
                {firstIndex, indexCount, 0u, firstPrimitive},
                {geometryId, 0u, materialId, flags},
                {geometryBounds.minimum.x, geometryBounds.minimum.y,
                    geometryBounds.minimum.z, 0.0f},
                {geometryBounds.maximum.x, geometryBounds.maximum.y,
                    geometryBounds.maximum.z, 0.0f}
            });
            return {geometryId, firstPrimitive, indexCount / 3u};
        }

        [[nodiscard]] GeometryResult AddTriangleGeometry(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            const Vec3 p0,
            const Vec3 p1,
            const Vec3 p2,
            const Vec3 normal,
            const std::uint32_t materialId,
            const std::uint32_t flags = GeometryFlagOpaque)
        {
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds geometryBounds;
            AppendFlatTriangle(scene, sceneBounds, geometryBounds, p0, p1, p2, normal);
            return FinishGeometry(scene, geometryBounds, firstIndex, materialId, flags);
        }

        [[nodiscard]] GeometryResult AddQuadGeometry(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            const Vec3 p0,
            const Vec3 p1,
            const Vec3 p2,
            const Vec3 p3,
            const Vec3 normal,
            const std::uint32_t materialId,
            const std::uint32_t flags = GeometryFlagOpaque)
        {
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds geometryBounds;
            AppendFlatQuad(scene, sceneBounds, geometryBounds, p0, p1, p2, p3, normal);
            return FinishGeometry(scene, geometryBounds, firstIndex, materialId, flags);
        }

        void AddLegacyCheckerFloorGeometry(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            const float minimumX,
            const float maximumX,
            const float minimumZ,
            const float maximumZ,
            const float y,
            const std::uint32_t lightMaterialId,
            const std::uint32_t darkMaterialId)
        {
            // Wave 0 evaluated floor(floorPosition * 0.75) in the analytic
            // shader. Keep those exact world-space cell boundaries while
            // expressing the checker through ordinary canonical geometry and
            // materials, so both Wave 2 traversal backends see the same scene.
            constexpr float checkerFrequency = 0.75f;
            const int firstX = static_cast<int>(std::floor(minimumX * checkerFrequency));
            const int lastX = static_cast<int>(std::ceil(maximumX * checkerFrequency)) - 1;
            const int firstZ = static_cast<int>(std::floor(minimumZ * checkerFrequency));
            const int lastZ = static_cast<int>(std::ceil(maximumZ * checkerFrequency)) - 1;

            const auto appendParity = [&](const int wantedParity,
                                          const std::uint32_t materialId)
            {
                const std::uint32_t firstIndex =
                    static_cast<std::uint32_t>(scene.indices.size());
                Bounds geometryBounds;
                for (int tileZ = firstZ; tileZ <= lastZ; ++tileZ)
                {
                    const float z0 = std::max(
                        minimumZ, static_cast<float>(tileZ) / checkerFrequency);
                    const float z1 = std::min(
                        maximumZ, static_cast<float>(tileZ + 1) / checkerFrequency);
                    for (int tileX = firstX; tileX <= lastX; ++tileX)
                    {
                        if (std::abs(tileX + tileZ) % 2 != wantedParity)
                        {
                            continue;
                        }
                        const float x0 = std::max(
                            minimumX, static_cast<float>(tileX) / checkerFrequency);
                        const float x1 = std::min(
                            maximumX, static_cast<float>(tileX + 1) / checkerFrequency);
                        AppendFlatQuad(scene, sceneBounds, geometryBounds,
                            {x0, y, z1}, {x1, y, z1},
                            {x1, y, z0}, {x0, y, z0},
                            {0.0f, 1.0f, 0.0f});
                    }
                }
                static_cast<void>(FinishGeometry(
                    scene, geometryBounds, firstIndex, materialId));
            };

            // checker==1 kept the authored material; checker==0 multiplied it
            // by 0.24 in Wave 0's SurfaceBaseColor implementation.
            appendParity(1, lightMaterialId);
            appendParity(0, darkMaterialId);
        }

        [[nodiscard]] GeometryResult AddBoxGeometry(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            const Vec3 center,
            const Vec3 halfExtent,
            const float yawRadians,
            const std::uint32_t materialId,
            const std::uint32_t flags = GeometryFlagOpaque)
        {
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds geometryBounds;
            const auto point = [center, yawRadians](const Vec3 local)
            {
                return center + RotateY(local, yawRadians);
            };
            const Vec3 p000 = point({-halfExtent.x, -halfExtent.y, -halfExtent.z});
            const Vec3 p001 = point({-halfExtent.x, -halfExtent.y, halfExtent.z});
            const Vec3 p010 = point({-halfExtent.x, halfExtent.y, -halfExtent.z});
            const Vec3 p011 = point({-halfExtent.x, halfExtent.y, halfExtent.z});
            const Vec3 p100 = point({halfExtent.x, -halfExtent.y, -halfExtent.z});
            const Vec3 p101 = point({halfExtent.x, -halfExtent.y, halfExtent.z});
            const Vec3 p110 = point({halfExtent.x, halfExtent.y, -halfExtent.z});
            const Vec3 p111 = point({halfExtent.x, halfExtent.y, halfExtent.z});
            const auto normal = [yawRadians](const Vec3 local)
            {
                return RotateY(local, yawRadians);
            };
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                p000, p001, p011, p010, normal({-1.0f, 0.0f, 0.0f}));
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                p100, p110, p111, p101, normal({1.0f, 0.0f, 0.0f}));
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                p000, p100, p101, p001, {0.0f, -1.0f, 0.0f});
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                p010, p011, p111, p110, {0.0f, 1.0f, 0.0f});
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                p000, p010, p110, p100, normal({0.0f, 0.0f, -1.0f}));
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                p001, p101, p111, p011, normal({0.0f, 0.0f, 1.0f}));
            return FinishGeometry(scene, geometryBounds, firstIndex, materialId, flags);
        }

        [[nodiscard]] GeometryResult AddTriangularPrismGeometry(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            const Vec3 a,
            const Vec3 b,
            const Vec3 c,
            const float halfWidth,
            const std::uint32_t materialId,
            const std::uint32_t flags = GeometryFlagOpaque)
        {
            if (!(halfWidth > 0.0f))
            {
                throw std::invalid_argument("Experiment prism width must be positive.");
            }
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds geometryBounds;
            const Vec3 width{halfWidth, 0.0f, 0.0f};
            const Vec3 leftA = a - width;
            const Vec3 leftB = b - width;
            const Vec3 leftC = c - width;
            const Vec3 rightA = a + width;
            const Vec3 rightB = b + width;
            const Vec3 rightC = c + width;
            AppendFlatTriangle(scene, sceneBounds, geometryBounds,
                leftA, leftC, leftB, {-1.0f, 0.0f, 0.0f});
            AppendFlatTriangle(scene, sceneBounds, geometryBounds,
                rightA, rightB, rightC, {1.0f, 0.0f, 0.0f});
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                leftA, rightA, rightB, leftB, {0.0f, -1.0f, 0.0f});
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                leftB, rightB, rightC, leftC,
                Normalize(Cross(rightB - leftB, rightC - leftB)));
            AppendFlatQuad(scene, sceneBounds, geometryBounds,
                leftC, rightC, rightA, leftA,
                Normalize(Cross(rightC - leftC, rightA - leftC)));
            return FinishGeometry(scene, geometryBounds, firstIndex, materialId, flags);
        }

        void AppendOrientedIndices(
            CanonicalScene& scene,
            std::uint32_t i0,
            std::uint32_t i1,
            std::uint32_t i2)
        {
            const Vec3 p0 = FromAbi(scene.vertices[i0].position);
            const Vec3 p1 = FromAbi(scene.vertices[i1].position);
            const Vec3 p2 = FromAbi(scene.vertices[i2].position);
            const Vec3 expected = FromAbi(scene.vertices[i0].normal)
                + FromAbi(scene.vertices[i1].normal)
                + FromAbi(scene.vertices[i2].normal);
            if (!(Dot(Cross(p1 - p0, p2 - p0), expected) > 0.0f))
            {
                std::swap(i1, i2);
            }
            AppendIndex(scene, i0);
            AppendIndex(scene, i1);
            AppendIndex(scene, i2);
        }

        [[nodiscard]] GeometryResult AddUvSphereGeometry(
            CanonicalScene& scene,
            Bounds& sceneBounds,
            const Vec3 center,
            const float radius,
            const std::uint32_t materialId,
            const bool inward = false,
            const std::uint32_t slices = 24u,
            const std::uint32_t stacks = 12u,
            const std::uint32_t flags = GeometryFlagOpaque)
        {
            if (!(radius > 0.0f) || slices < 3u || stacks < 3u)
            {
                throw std::invalid_argument("Experiment sphere tessellation is invalid.");
            }
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds geometryBounds;
            std::vector<std::uint32_t> vertices;
            vertices.reserve(2u + static_cast<std::size_t>(stacks - 1u) * slices);
            const float normalSign = inward ? -1.0f : 1.0f;
            vertices.push_back(AppendVertex(scene, sceneBounds, geometryBounds,
                center + Vec3{0.0f, radius, 0.0f},
                {0.0f, normalSign, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.5f, 0.0f));
            for (std::uint32_t stack = 1u; stack < stacks; ++stack)
            {
                const float theta = kPi * static_cast<float>(stack) / static_cast<float>(stacks);
                const float sineTheta = std::sin(theta);
                const float cosineTheta = std::cos(theta);
                for (std::uint32_t slice = 0u; slice < slices; ++slice)
                {
                    const float phi = 2.0f * kPi * static_cast<float>(slice)
                        / static_cast<float>(slices);
                    const Vec3 outward{
                        sineTheta * std::cos(phi),
                        cosineTheta,
                        sineTheta * std::sin(phi)
                    };
                    const Vec3 tangent{-std::sin(phi), 0.0f, std::cos(phi)};
                    vertices.push_back(AppendVertex(scene, sceneBounds, geometryBounds,
                        center + outward * radius, outward * normalSign, tangent,
                        static_cast<float>(slice) / static_cast<float>(slices),
                        static_cast<float>(stack) / static_cast<float>(stacks)));
                }
            }
            const std::uint32_t bottom = AppendVertex(scene, sceneBounds, geometryBounds,
                center + Vec3{0.0f, -radius, 0.0f},
                {0.0f, -normalSign, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.5f, 1.0f);
            const std::uint32_t top = vertices.front();
            for (std::uint32_t slice = 0u; slice < slices; ++slice)
            {
                const std::uint32_t next = (slice + 1u) % slices;
                AppendOrientedIndices(scene, top, vertices[1u + slice], vertices[1u + next]);
            }
            for (std::uint32_t stack = 0u; stack + 2u < stacks; ++stack)
            {
                const std::size_t current = 1u + static_cast<std::size_t>(stack) * slices;
                const std::size_t nextRing = current + slices;
                for (std::uint32_t slice = 0u; slice < slices; ++slice)
                {
                    const std::uint32_t next = (slice + 1u) % slices;
                    const std::uint32_t a = vertices[current + slice];
                    const std::uint32_t b = vertices[current + next];
                    const std::uint32_t c = vertices[nextRing + next];
                    const std::uint32_t d = vertices[nextRing + slice];
                    AppendOrientedIndices(scene, a, b, c);
                    AppendOrientedIndices(scene, a, c, d);
                }
            }
            const std::size_t lastRing = 1u + static_cast<std::size_t>(stacks - 2u) * slices;
            for (std::uint32_t slice = 0u; slice < slices; ++slice)
            {
                const std::uint32_t next = (slice + 1u) % slices;
                AppendOrientedIndices(
                    scene, vertices[lastRing + slice], bottom, vertices[lastRing + next]);
            }
            return FinishGeometry(scene, geometryBounds, firstIndex, materialId, flags);
        }

        [[nodiscard]] std::uint32_t AddMaterial(
            CanonicalScene& scene,
            const Vec3 baseColor,
            const float metallic,
            const float roughness,
            const std::uint32_t model = MaterialModelMetallicRoughness,
            const float transmission = 0.0f,
            const float ior = 1.5f,
            const Vec3 attenuationColor = {1.0f, 1.0f, 1.0f},
            const float attenuationDistance = 0.0f,
            const Vec3 emission = {},
            const float emissionStrength = 0.0f,
            const std::uint32_t flags = MaterialFlagNone)
        {
            if (scene.materials.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                throw std::length_error("Experiment material count exceeds uint32.");
            }
            const std::uint32_t materialId = static_cast<std::uint32_t>(scene.materials.size());
            scene.materials.push_back({
                {baseColor.x, baseColor.y, baseColor.z, 1.0f},
                {emission.x, emission.y, emission.z, emissionStrength},
                {metallic, roughness, 1.0f, 0.5f},
                {transmission, ior, 0.0f, 0.0f},
                {attenuationColor.x, attenuationColor.y, attenuationColor.z,
                    attenuationDistance},
                {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                {model, flags, materialId, 0u}
            });
            return materialId;
        }

        [[nodiscard]] std::uint32_t AddLight(
            CanonicalScene& scene,
            const AbiFloat4 positionRange,
            const AbiFloat4 directionCosOuter,
            const AbiFloat4 radianceScale,
            const AbiFloat4 shapeParams,
            const std::uint32_t type,
            const std::uint32_t instanceId,
            const std::uint32_t primitiveId,
            const std::uint32_t textureId,
            const std::uint32_t flags)
        {
            if (scene.lights.size() >= 64u)
            {
                throw std::length_error("First-five experiment light masks support at most 64 lights.");
            }
            const std::uint32_t lightId = static_cast<std::uint32_t>(scene.lights.size());
            scene.lights.push_back({
                positionRange,
                directionCosOuter,
                radianceScale,
                shapeParams,
                {type, lightId, instanceId, primitiveId},
                {textureId, flags, 0u, 0u}
            });
            return lightId;
        }

        void AddDirectionalLight(
            CanonicalScene& scene,
            const Vec3 direction,
            const Vec3 radiance)
        {
            const Vec3 normalized = Normalize(direction);
            static_cast<void>(AddLight(scene,
                {0.0f, 0.0f, 0.0f, 0.0f},
                {normalized.x, normalized.y, normalized.z, -1.0f},
                {radiance.x, radiance.y, radiance.z, 1.0f},
                {}, LightTypeDirectional, kInvalidId, kInvalidId, kInvalidId,
                LightFlagEnabled | LightFlagDelta));
        }

        void AddSphereAreaLight(
            CanonicalScene& scene,
            const Vec3 position,
            const float radius,
            const Vec3 radiance)
        {
            static_cast<void>(AddLight(scene,
                {position.x, position.y, position.z, 0.0f},
                {0.0f, -1.0f, 0.0f, -1.0f},
                {radiance.x, radiance.y, radiance.z, 1.0f},
                {radius, 0.0f, 0.0f, 0.0f},
                LightTypeSphereArea, kInvalidId, kInvalidId, kInvalidId,
                LightFlagEnabled));
        }

        void AddTriangleLights(
            CanonicalScene& scene,
            const GeometryResult geometry,
            const Vec3 radiance,
            const bool twoSided = false)
        {
            for (std::uint32_t local = 0u; local < geometry.primitiveCount; ++local)
            {
                const std::uint32_t primitiveId = geometry.firstPrimitive + local;
                const std::uint32_t firstIndex = primitiveId * 3u;
                const Vec3 p0 = FromAbi(scene.vertices[scene.indices[firstIndex]].position);
                const Vec3 p1 = FromAbi(scene.vertices[scene.indices[firstIndex + 1u]].position);
                const Vec3 p2 = FromAbi(scene.vertices[scene.indices[firstIndex + 2u]].position);
                const Vec3 cross = Cross(p1 - p0, p2 - p0);
                const float area = 0.5f * std::sqrt(LengthSquared(cross));
                const Vec3 normal = Normalize(cross);
                const Vec3 centroid = (p0 + p1 + p2) * (1.0f / 3.0f);
                static_cast<void>(AddLight(scene,
                    {centroid.x, centroid.y, centroid.z, 0.0f},
                    {normal.x, normal.y, normal.z, 0.0f},
                    {radiance.x, radiance.y, radiance.z, 1.0f},
                    {0.0f, 0.0f, area, 0.0f},
                    LightTypeEmissiveTriangle, 0u, primitiveId, kInvalidId,
                    LightFlagEnabled | (twoSided ? LightFlagTwoSided : 0u)));
            }
        }

        [[nodiscard]] RayRecord MakeRay(
            const std::uint32_t rayId,
            const Vec3 origin,
            const Vec3 direction,
            const float tMinimum,
            const float tMaximum) noexcept
        {
            return {
                {origin.x, origin.y, origin.z, tMinimum},
                {direction.x, direction.y, direction.z, tMaximum},
                {rayId, rayId, 0u, 0xffu},
                {1u, 0u, 0u, 0x45585031u}
            };
        }

        [[nodiscard]] ExperimentRayCase HitRayCase(
            const std::string_view stableId,
            const RayRecord ray,
            const float distance,
            const std::uint32_t primitive0,
            const std::uint32_t primitive1 = kInvalidId)
        {
            ExperimentRayCase result;
            result.stableId = stableId;
            result.ray = ray;
            result.expectedHitKind = HitKindTriangle;
            result.acceptablePrimitiveIds = {primitive0, primitive1};
            result.acceptablePrimitiveCount = primitive1 == kInvalidId ? 1u : 2u;
            result.expectedDistance = distance;
            result.anyHit = ExperimentAnyHitExpectation::Occluded;
            return result;
        }

        [[nodiscard]] ExperimentRayCase MissRayCase(
            const std::string_view stableId,
            const RayRecord ray)
        {
            ExperimentRayCase result;
            result.stableId = stableId;
            result.ray = ray;
            result.expectedHitKind = HitKindMiss;
            result.expectedDistance = ray.directionTMax.w;
            result.anyHit = ExperimentAnyHitExpectation::Unoccluded;
            return result;
        }

        [[nodiscard]] ExperimentRayCase InvalidRayCase(
            const std::string_view stableId,
            const RayRecord ray)
        {
            ExperimentRayCase result;
            result.stableId = stableId;
            result.ray = ray;
            result.expectedHitKind = HitKindInvalid;
            result.expectedDistance = ray.directionTMax.w;
            result.anyHit = ExperimentAnyHitExpectation::Invalid;
            return result;
        }

        [[nodiscard]] bool ActivateVariant(
            ExperimentScene& experiment,
            const ExperimentSceneVariant& variant) noexcept
        {
            const auto camera = std::find_if(
                experiment.canonical.cameras.begin(), experiment.canonical.cameras.end(),
                [&variant](const CameraPreset& candidate)
                {
                    return candidate.stableId == variant.cameraStableId;
                });
            constexpr std::string_view kTemporalCutCameraStableId =
                "camera:showcase-8-disocclusion";
            const auto cutCamera = variant.cameraCut
                ? std::find_if(
                    experiment.canonical.cameras.begin(),
                    experiment.canonical.cameras.end(),
                    [kTemporalCutCameraStableId](const CameraPreset& candidate)
                    {
                        return candidate.stableId == kTemporalCutCameraStableId;
                    })
                : experiment.canonical.cameras.end();
            if (camera == experiment.canonical.cameras.end()
                || (variant.cameraCut
                    && cutCamera == experiment.canonical.cameras.end())
                || (variant.enableEnvironment && experiment.environment.Empty()))
            {
                return false;
            }
            if (variant.environmentProfile != experiment.environment.profile)
            {
                if (experiment.environment.Empty())
                {
                    return false;
                }
                FillEnvironmentImage(experiment.environment,
                    variant.environmentProfile);
                if (experiment.environment.profile != variant.environmentProfile)
                {
                    return false;
                }
            }
            for (GpuLightV0& light : experiment.canonical.lights)
            {
                light.extra.y &= ~LightFlagEnabled;
                const bool enabled = light.identity.y < 64u
                    ? (variant.enabledLightMask & (1ull << light.identity.y)) != 0ull
                    : variant.enabledLightMask == ~0ull;
                if (enabled)
                {
                    light.extra.y |= LightFlagEnabled;
                }
            }
            experiment.activeVariantStableId = variant.stableId;
            experiment.activeCameraStableId = variant.cameraStableId;
            if (variant.cameraCut && experiment.sampledAnimationFrameIndex >= 60u)
            {
                experiment.activeCameraStableId = kTemporalCutCameraStableId;
            }
            experiment.environmentEnabled = variant.enableEnvironment;
            // The scene-8 cut is a one-frame event at the explicit 60 Hz
            // sample boundary. The post-cut camera remains selected, but only
            // N=60 asks the host to reset history.
            experiment.cameraCut = variant.cameraCut
                && experiment.sampledAnimationFrameIndex == 60u;
            return true;
        }

        void FinalizeScene(
            ExperimentScene& experiment,
            const Bounds& bounds,
            std::uint32_t sceneFlags = SceneFlagNone)
        {
            CanonicalScene& scene = experiment.canonical;
            if (bounds.empty || scene.geometries.empty() || scene.materials.empty()
                || scene.cameras.empty())
            {
                throw std::logic_error("Experiment scene is incomplete.");
            }
            const bool hasAnyEnvironmentData = experiment.environment.width != 0u
                || experiment.environment.height != 0u
                || !experiment.environment.linearRgba.empty();
            const std::uint64_t environmentTexelCount =
                static_cast<std::uint64_t>(experiment.environment.width)
                * static_cast<std::uint64_t>(experiment.environment.height);
            if (hasAnyEnvironmentData
                && (experiment.environment.width == 0u
                    || experiment.environment.height == 0u
                    || environmentTexelCount != experiment.environment.linearRgba.size()))
            {
                throw std::logic_error("Experiment environment dimensions do not match its texels.");
            }
            if (experiment.variants.empty() || !ActivateVariant(experiment, experiment.variants.front()))
            {
                throw std::logic_error("Experiment scene has no valid default variant.");
            }
            const AbiMat4Rows identity = IdentityMatrix();
            scene.instances = {{
                identity,
                identity,
                identity,
                {0u, static_cast<std::uint32_t>(scene.geometries.size()), 0u,
                    InstanceFlagVisible | InstanceFlagCastsShadow},
                {0u, 0u, 0u, 0u}
            }};
            scene.constants.counts0 = {
                static_cast<std::uint32_t>(scene.vertices.size()),
                static_cast<std::uint32_t>(scene.indices.size()),
                static_cast<std::uint32_t>(scene.geometries.size()),
                1u
            };
            scene.constants.counts1 = {
                static_cast<std::uint32_t>(scene.materials.size()),
                static_cast<std::uint32_t>(scene.lights.size()),
                experiment.environment.Empty() ? 0u : 1u,
                experiment.environment.Empty() ? 0u : 1u
            };
            scene.constants.sceneBoundsMin = {
                bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f};
            scene.constants.sceneBoundsMax = {
                bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f};
            if (!experiment.environment.Empty())
            {
                sceneFlags |= SceneFlagHasEnvironment;
                scene.constants.environment = {
                    0u, experiment.environment.width, experiment.environment.height, 0u};
            }
            else
            {
                scene.constants.environment = {kInvalidId, 0u, 0u, 0u};
            }
            scene.constants.versionFlags = {kAbiVersion, scene.generation, sceneFlags, 0u};
            const CanonicalSceneValidation validation = ValidateCanonicalScene(scene);
            if (!validation)
            {
                throw std::logic_error("Generated experiment scene is invalid: " + validation.reason);
            }
        }

        constexpr float kTemporalFrameRate = 60.0f;
        constexpr float kPanelMotionFrequencyHz = 4.0f;
        constexpr float kCameraMotionFrequencyHz = 0.5f;

        [[nodiscard]] float TemporalFrameTimeSeconds(
            const std::uint32_t frameIndex) noexcept
        {
            return static_cast<float>(frameIndex) / kTemporalFrameRate;
        }

        // The panel is authored at the origin and instanced into the corridor.
        // Sampling at frame N and N-1 keeps the motion-vector record in the
        // canonical instance ABI; no parallel host transform history is needed.
        [[nodiscard]] Vec3 TemporalPanelPosition(
            const std::uint32_t frameIndex) noexcept
        {
            const float phase = 2.0f * kPi * kPanelMotionFrequencyHz
                * TemporalFrameTimeSeconds(frameIndex);
            return {
                0.85f * std::cos(phase),
                2.0f,
                -7.2f
            };
        }

        void SampleTemporalPanelTransforms(
            ExperimentScene& experiment,
            const std::uint32_t frameIndex,
            const bool animate)
        {
            if (experiment.canonical.instances.size() < 2u)
            {
                throw std::logic_error(
                    "Temporal corridor must publish its moving panel instance.");
            }
            const Vec3 current = animate
                ? TemporalPanelPosition(frameIndex)
                : TemporalPanelPosition(0u);
            const Vec3 previous = animate && frameIndex > 0u
                ? TemporalPanelPosition(frameIndex - 1u)
                : current;
            GpuInstanceV0& panel = experiment.canonical.instances[1u];
            panel.objectToWorld = TranslationMatrix(current);
            panel.worldToObject = TranslationMatrix(current * -1.0f);
            panel.previousObjectToWorld = TranslationMatrix(previous);
        }

        void SampleTemporalCamera(
            ExperimentScene& experiment,
            const std::uint32_t frameIndex,
            const bool animate)
        {
            const auto camera = std::find_if(
                experiment.canonical.cameras.begin(),
                experiment.canonical.cameras.end(),
                [&experiment](const CameraPreset& candidate)
                {
                    return candidate.stableId == experiment.activeCameraStableId;
                });
            if (camera == experiment.canonical.cameras.end())
            {
                throw std::logic_error(
                    "Temporal corridor selected camera is not present.");
            }
            if (!animate)
            {
                return;
            }
            const float phase = 2.0f * kPi * kCameraMotionFrequencyHz
                * TemporalFrameTimeSeconds(frameIndex);
            const float offset = 0.65f * std::sin(phase);
            camera->eye.x += offset;
            camera->target.x += offset;
        }

        [[nodiscard]] ExperimentScene BeginScene(const ExperimentScenePreset preset)
        {
            const ExperimentSceneDescriptor* const descriptor = FindExperimentScene(preset);
            if (descriptor == nullptr)
            {
                throw std::invalid_argument("Unknown first-five experiment scene preset.");
            }
            ExperimentScene result;
            result.descriptor = *descriptor;
            result.canonical.stableId.assign(descriptor->canonicalStableId);
            result.canonical.generation = 1u;
            return result;
        }
    }

    std::span<const ExperimentSceneDescriptor> FirstFiveExperimentSceneRegistry() noexcept
    {
        return {kExperimentRegistry.data(), 5u};
    }

    std::span<const ExperimentSceneDescriptor> ExperimentSceneRegistry() noexcept
    {
        return kExperimentRegistry;
    }

    bool IsExperimentSceneBuilt(const ExperimentScenePreset preset) noexcept
    {
        return preset != ExperimentScenePreset::SponzaTraversalHall
            && FindExperimentScene(preset) != nullptr;
    }

    const ExperimentSceneDescriptor* FindExperimentScene(
        const ExperimentScenePreset preset) noexcept
    {
        const auto found = std::find_if(
            kExperimentRegistry.begin(), kExperimentRegistry.end(),
            [preset](const ExperimentSceneDescriptor& descriptor)
            {
                return descriptor.preset == preset;
            });
        return found == kExperimentRegistry.end() ? nullptr : &*found;
    }

    const ExperimentSceneDescriptor* FindExperimentScene(
        const std::string_view runtimeToken) noexcept
    {
        const auto found = std::find_if(
            kExperimentRegistry.begin(), kExperimentRegistry.end(),
            [runtimeToken](const ExperimentSceneDescriptor& descriptor)
            {
                return descriptor.runtimeToken == runtimeToken;
            });
        return found == kExperimentRegistry.end() ? nullptr : &*found;
    }

    const ExperimentSceneVariant* FindExperimentSceneVariant(
        const ExperimentScene& scene,
        const std::string_view stableId) noexcept
    {
        const auto found = std::find_if(
            scene.variants.begin(), scene.variants.end(),
            [stableId](const ExperimentSceneVariant& variant)
            {
                return variant.stableId == stableId;
            });
        return found == scene.variants.end() ? nullptr : &*found;
    }

    bool ApplyExperimentSceneVariant(
        ExperimentScene& scene,
        const std::string_view stableId) noexcept
    {
        const ExperimentSceneVariant* const variant =
            FindExperimentSceneVariant(scene, stableId);
        return variant != nullptr && ActivateVariant(scene, *variant);
    }

    ExperimentScene BuildBaselineGalleryExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::BaselineGallery);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;
        const std::uint32_t red = AddMaterial(
            scene, {0.80f, 0.055f, 0.035f}, 0.0f, 0.28f);
        const std::uint32_t glass = AddMaterial(scene, {0.96f, 0.985f, 1.0f}, 0.0f, 0.02f,
            MaterialModelSmoothDielectric, 1.0f, 1.52f, {0.72f, 0.90f, 1.0f}, 4.0f,
            {}, 0.0f, MaterialFlagDoubleSided);
        const std::uint32_t gold = AddMaterial(
            scene, {1.0f, 0.710f, 0.290f}, 1.0f, 0.16f);
        const std::uint32_t silver = AddMaterial(
            scene, {0.91f, 0.920f, 0.920f}, 1.0f, 0.48f);
        const std::uint32_t floor = AddMaterial(
            scene, {0.55f, 0.570f, 0.620f}, 0.0f, 0.65f);
        const std::uint32_t background = AddMaterial(
            scene, {0.13f, 0.170f, 0.240f}, 0.0f, 0.85f);
        const std::uint32_t green = AddMaterial(
            scene, {0.08f, 0.520f, 0.180f}, 0.0f, 0.78f);
        const std::uint32_t ivory = AddMaterial(
            scene, {0.82f, 0.810f, 0.760f}, 0.0f, 0.12f);
        const std::uint32_t floorDark = AddMaterial(
            scene, {0.132f, 0.1368f, 0.1488f}, 0.0f, 0.65f);
        const std::uint32_t warmEmitter = AddMaterial(
            scene, {1.0f, 0.720f, 0.420f}, 0.0f, 0.30f,
            MaterialModelMetallicRoughness, 0.0f, 1.5f,
            {1.0f, 1.0f, 1.0f}, 0.0f,
            {180.0f, 105.0f, 45.0f}, 1.0f, MaterialFlagEmissive);
        const std::uint32_t coolEmitter = AddMaterial(
            scene, {0.35f, 0.550f, 1.0f}, 0.0f, 0.30f,
            MaterialModelMetallicRoughness, 0.0f, 1.5f,
            {1.0f, 1.0f, 1.0f}, 0.0f,
            {45.0f, 80.0f, 180.0f}, 1.0f, MaterialFlagEmissive);

        // Preserve the Wave 0 composition in canonical form. The analytic
        // infinite planes are represented by presentation-sized finite quads;
        // the camera, checker phase, back-wall depth, materials and light
        // geometry retain their original authored values.
        AddLegacyCheckerFloorGeometry(
            scene, bounds, -12.0f, 12.0f, -8.5f, 5.0f, -1.0f,
            floor, floorDark);
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-12.0f, -1.0f, -8.5f}, {12.0f, -1.0f, -8.5f},
            {12.0f, 10.0f, -8.5f}, {-12.0f, 10.0f, -8.5f},
            {0.0f, 0.0f, 1.0f}, background));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {-1.55f, -0.14f, -4.20f}, 0.86f, red));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {0.10f, -0.28f, -3.65f}, 0.72f, glass, false, 24u, 12u,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {1.62f, -0.12f, -4.35f}, 0.88f, gold));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {-0.72f, 0.42f, -6.15f}, 1.12f, silver));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {1.55f, -0.30f, -6.30f}, 0.70f, green));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {-2.62f, -0.46f, -6.05f}, 0.54f, ivory));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {-3.10f, 4.20f, -1.90f}, 0.34f, warmEmitter));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {3.15f, 2.45f, -3.20f}, 0.28f, coolEmitter));

        // Restore the exact Wave 0 finite-light positions, sizes and radiance.
        // Sampling remains Wave 2's power-weighted one-light MIS path; this
        // scene deliberately exposes its low-SPP variance instead of changing
        // the estimator to recover the old all-lights-per-bounce shortcut.
        AddSphereAreaLight(scene,
            {-3.10f, 4.20f, -1.90f}, 0.34f,
            {180.0f, 105.0f, 45.0f});
        AddSphereAreaLight(scene,
            {3.15f, 2.45f, -3.20f}, 0.28f,
            {45.0f, 80.0f, 180.0f});

        // Wave 0's analytic shader evaluated this latitude-only environment
        // on every miss. Publish the same function as a deterministic linear
        // lat-long texture so the canonical Wave 2 environment sampler owns it.
        result.environment.width = 128u;
        result.environment.height = 64u;
        result.environment.linearRgba.resize(
            static_cast<std::size_t>(result.environment.width)
                * result.environment.height);
        constexpr Vec3 horizon{0.055f, 0.070f, 0.105f};
        constexpr Vec3 zenith{0.30f, 0.47f, 0.78f};
        constexpr Vec3 ground{0.018f, 0.014f, 0.012f};
        for (std::uint32_t row = 0u; row < result.environment.height; ++row)
        {
            const float theta = kPi * (static_cast<float>(row) + 0.5f)
                / static_cast<float>(result.environment.height);
            const float elevation = std::clamp(
                std::cos(theta) * 0.5f + 0.5f, 0.0f, 1.0f);
            const float normalizedSky = std::clamp(elevation / 0.52f, 0.0f, 1.0f);
            const float skyWeight = normalizedSky * normalizedSky
                * (3.0f - 2.0f * normalizedSky);
            const Vec3 sky = horizon + (zenith - horizon) * (elevation * elevation);
            const Vec3 radiance = ground + (sky - ground) * skyWeight;
            for (std::uint32_t column = 0u; column < result.environment.width; ++column)
            {
                result.environment.linearRgba[
                    static_cast<std::size_t>(row) * result.environment.width + column] =
                    {radiance.x, radiance.y, radiance.z, 1.0f};
            }
        }
        static_cast<void>(AddLight(scene,
            {}, {0.0f, 1.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {},
            LightTypeEnvironment, kInvalidId, kInvalidId, 0u, LightFlagEnabled));
        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 0.25f, 2.5f, 1.0f},
            {0.0f, 0.197664f, 1.501370f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            52.0f
        }};
        result.variants.push_back({"filtered-shadow",
            result.descriptor.fixedCameraStableId, ~0ull, true});
        result.variants.push_back({"physical-shadow",
            result.descriptor.fixedCameraStableId, ~0ull, true});
        FinalizeScene(result, bounds);
        return result;
    }

    ExperimentScene BuildIntersectionBvhExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::IntersectionBvhLab);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;
        const std::uint32_t neutral = AddMaterial(scene, {0.68f, 0.72f, 0.78f}, 0.0f, 0.72f);
        const std::uint32_t shared = AddMaterial(scene, {0.12f, 0.55f, 0.95f}, 0.0f, 0.55f);
        const std::uint32_t thin = AddMaterial(scene, {0.95f, 0.32f, 0.06f}, 0.0f, 0.48f);
        const std::uint32_t grid = AddMaterial(scene, {0.12f, 0.70f, 0.32f}, 0.0f, 0.78f);
        const std::uint32_t cluster = AddMaterial(scene, {0.76f, 0.12f, 0.72f}, 0.0f, 0.62f);

        const Vec3 single0{-4.0f, -0.80f, -0.20f};
        const Vec3 single1{-2.8f, -0.80f, 0.20f};
        const Vec3 single2{-3.4f, 0.60f, 0.0f};
        Vec3 singleNormal = Normalize(Cross(single1 - single0, single2 - single0));
        if (singleNormal.z < 0.0f) singleNormal = singleNormal * -1.0f;
        const GeometryResult single = AddTriangleGeometry(
            scene, bounds, single0, single1, single2, singleNormal, neutral);
        const GeometryResult sharedEdge = AddQuadGeometry(scene, bounds,
            {-1.5f, -0.8f, 0.0f}, {0.5f, -0.8f, 0.0f},
            {0.5f, 1.0f, 0.0f}, {-1.5f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, shared);
        const Vec3 thin0{1.0f, -0.05f, 0.0f};
        const Vec3 thin1{4.0f, -0.02f, 0.0f};
        const Vec3 thin2{1.10f, 0.02f, 0.0f};
        const GeometryResult thinTriangle = AddTriangleGeometry(
            scene, bounds, thin0, thin1, thin2, {0.0f, 0.0f, 1.0f}, thin);
        const GeometryResult tinyTriangle = AddTriangleGeometry(scene, bounds,
            {4.20f, 0.35f, 0.0f}, {4.201f, 0.35f, 0.0f},
            {4.20f, 0.351f, 0.0f}, {0.0f, 0.0f, 1.0f}, thin);

        const std::uint32_t gridFirstIndex = static_cast<std::uint32_t>(scene.indices.size());
        Bounds gridBounds;
        constexpr std::uint32_t columns = 16u;
        constexpr std::uint32_t rows = 8u;
        constexpr float gridMinimumX = -4.0f;
        constexpr float gridMinimumY = -3.5f;
        constexpr float cellX = 5.0f / static_cast<float>(columns);
        constexpr float cellY = 2.0f / static_cast<float>(rows);
        for (std::uint32_t row = 0u; row < rows; ++row)
        {
            for (std::uint32_t column = 0u; column < columns; ++column)
            {
                const float x0 = gridMinimumX + static_cast<float>(column) * cellX;
                const float x1 = x0 + cellX;
                const float y0 = gridMinimumY + static_cast<float>(row) * cellY;
                const float y1 = y0 + cellY;
                AppendFlatQuad(scene, bounds, gridBounds,
                    {x0, y0, -1.0f}, {x1, y0, -1.0f},
                    {x1, y1, -1.0f}, {x0, y1, -1.0f},
                    {0.0f, 0.0f, 1.0f});
            }
        }
        const GeometryResult denseGrid = FinishGeometry(
            scene, gridBounds, gridFirstIndex, grid);

        const std::uint32_t clusterFirstIndex = static_cast<std::uint32_t>(scene.indices.size());
        Bounds clusterBounds;
        const Vec3 clusterCenter{3.0f, 2.0f, -1.5f};
        for (std::uint32_t index = 0u; index < 12u; ++index)
        {
            const float angle = 2.0f * kPi * static_cast<float>(index) / 12.0f;
            const Vec3 radial{std::cos(angle), std::sin(angle), 0.0f};
            const Vec3 tangent{-radial.y, radial.x, 0.0f};
            AppendFlatTriangle(scene, bounds, clusterBounds,
                clusterCenter + radial * 0.38f,
                clusterCenter + tangent * 0.22f - radial * 0.19f,
                clusterCenter - tangent * 0.22f - radial * 0.19f,
                {0.0f, 0.0f, 1.0f});
        }
        static_cast<void>(FinishGeometry(scene, clusterBounds, clusterFirstIndex, cluster));
        const GeometryResult largeTriangle = AddTriangleGeometry(scene, bounds,
            {-5.0f, 3.2f, -3.0f}, {5.0f, 3.2f, -3.0f},
            {0.0f, 4.3f, -3.0f}, {0.0f, 0.0f, 1.0f}, neutral);
        AddDirectionalLight(scene, {-0.4f, -0.7f, -0.6f}, {3.0f, 3.0f, 3.0f});

        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 0.0f, 8.0f, 1.0f},
            {0.0f, 0.0f, -0.7f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            48.0f
        }};
        result.traversalCorpus.push_back(HitRayCase("single-triangle",
            MakeRay(0u, {-3.4f, -0.33333334f, 2.0f}, {0.0f, 0.0f, -1.0f}, 0.001f, 100.0f),
            2.0f, single.firstPrimitive));
        result.traversalCorpus.push_back(HitRayCase("shared-edge-equivalent",
            MakeRay(1u, {-0.5f, 0.1f, 2.0f}, {0.0f, 0.0f, -1.0f}, 0.001f, 100.0f),
            2.0f, sharedEdge.firstPrimitive, sharedEdge.firstPrimitive + 1u));
        result.traversalCorpus.push_back(HitRayCase("thin-triangle",
            MakeRay(2u, {2.0333333f, -0.0166667f, 2.0f},
                {0.0f, 0.0f, -1.0f}, 0.001f, 100.0f),
            2.0f, thinTriangle.firstPrimitive));
        result.traversalCorpus.push_back(HitRayCase("dense-grid",
            MakeRay(3u, {gridMinimumX + cellX * 0.70f,
                gridMinimumY + cellY * 0.20f, 2.0f},
                {0.0f, 0.0f, -1.0f}, 0.001f, 100.0f),
            3.0f, denseGrid.firstPrimitive));
        result.traversalCorpus.push_back(MissRayCase("parallel-negative-zero",
            MakeRay(4u, {-3.4f, -0.33333334f, 2.0f},
                {1.0f, -0.0f, 0.0f}, 0.001f, 100.0f)));
        result.traversalCorpus.push_back(MissRayCase("origin-inside-aabb-away-from-triangle",
            MakeRay(5u, {-3.4f, -0.33333334f, 0.15f},
                {0.0f, 0.0f, 1.0f}, 0.001f, 100.0f)));
        result.traversalCorpus.push_back(InvalidRayCase("zero-direction-invalid",
            MakeRay(6u, {0.0f, 0.0f, 0.0f}, {}, 1.0f, 1.0f)));
        result.traversalCorpus.push_back(HitRayCase("tiny-scale-triangle",
            MakeRay(7u, {4.200333f, 0.350333f, 2.0f},
                {0.0f, 0.0f, -1.0f}, 0.001f, 100.0f),
            2.0f, tinyTriangle.firstPrimitive));
        result.traversalCorpus.push_back(HitRayCase("large-scale-triangle",
            MakeRay(8u, {0.0f, 3.566667f, 2.0f},
                {0.0f, 0.0f, -1.0f}, 0.001f, 100.0f),
            5.0f, largeTriangle.firstPrimitive));
        result.variants.push_back({"parity", result.descriptor.fixedCameraStableId, ~0ull, false});
        FinalizeScene(result, bounds);
        return result;
    }

    ExperimentScene BuildWhittedOpticsExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::WhittedOpticsRoom);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;
        const std::uint32_t floor = AddMaterial(scene, {0.48f, 0.50f, 0.54f}, 0.0f, 0.72f);
        const std::uint32_t mirror = AddMaterial(scene, {0.96f, 0.97f, 0.99f}, 1.0f, 0.02f);
        const std::uint32_t glass = AddMaterial(scene, {1.0f, 1.0f, 1.0f}, 0.0f, 0.0f,
            MaterialModelSmoothDielectric, 1.0f, 1.52f, {0.72f, 0.90f, 0.98f}, 1.8f,
            {}, 0.0f, MaterialFlagDoubleSided);
        const std::uint32_t emitter = AddMaterial(scene, {}, 0.0f, 1.0f,
            MaterialModelMetallicRoughness, 0.0f, 1.5f, {1.0f, 1.0f, 1.0f}, 0.0f,
            {20.0f, 18.0f, 14.0f}, 1.0f, MaterialFlagEmissive);
        const std::uint32_t occluder = AddMaterial(scene, {0.035f, 0.04f, 0.05f}, 0.0f, 0.92f);

        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-10.0f, -1.0f, 3.0f}, {10.0f, -1.0f, 3.0f},
            {10.0f, -1.0f, -14.0f}, {-10.0f, -1.0f, -14.0f},
            {0.0f, 1.0f, 0.0f}, floor));
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-10.0f, -1.0f, -10.5f}, {10.0f, -1.0f, -10.5f},
            {10.0f, 8.0f, -10.5f}, {-10.0f, 8.0f, -10.5f},
            {0.0f, 0.0f, 1.0f}, floor));
        static_cast<void>(AddBoxGeometry(scene, bounds,
            {-2.65f, 0.25f, -5.7f}, {1.25f, 1.45f, 0.035f}, 0.36f, mirror));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {-1.65f, -0.18f, -3.65f}, 0.82f, mirror));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {0.15f, -0.18f, -4.15f}, 0.82f, glass, false, 24u, 12u,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {1.95f, -0.18f, -4.35f}, 0.82f, glass, false, 24u, 12u,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {1.95f, -0.18f, -4.35f}, 0.58f, glass, true, 24u, 12u,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddBoxGeometry(scene, bounds,
            {3.20f, 0.15f, -5.65f}, {0.12f, 1.15f, 0.92f}, 0.58f, glass,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddTriangularPrismGeometry(scene, bounds,
            {-3.85f, -1.0f, -4.90f}, {-3.85f, -1.0f, -3.60f},
            {-3.85f, 0.90f, -4.40f}, 0.50f, glass,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddBoxGeometry(scene, bounds,
            {0.10f, -0.42f, -2.35f}, {0.34f, 0.58f, 0.34f}, -0.18f, occluder));
        // The broader softbox keeps the Raw optics preview readable at low
        // SPP without changing its radiance or estimator contract.
        const GeometryResult light = AddQuadGeometry(scene, bounds,
            {-0.75f, 2.75f, -5.00f}, {0.75f, 2.75f, -5.00f},
            {0.75f, 2.75f, -3.40f}, {-0.75f, 2.75f, -3.40f},
            {0.0f, -1.0f, 0.0f}, emitter);
        AddTriangleLights(scene, light, {20.0f, 18.0f, 14.0f});
        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 0.55f, 3.5f, 1.0f},
            {0.0f, -0.05f, -4.7f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            49.0f
        }, {
            "camera:showcase-2-tir-grazing",
            {-5.60f, 0.50f, -1.80f, 1.0f},
            {-3.85f, -0.05f, -4.25f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            38.0f
        }};
        result.variants.push_back({"reflection-refraction",
            result.descriptor.fixedCameraStableId, ~0ull, false});
        result.variants.push_back({"tir-grazing", "camera:showcase-2-tir-grazing", ~0ull, false});
        FinalizeScene(result, bounds);
        return result;
    }

    ExperimentScene BuildCornellExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::CornellBox);
        result.canonical = BuildCanonicalCornellScene();
        result.canonical.stableId.assign(result.descriptor.canonicalStableId);
        result.canonical.generation = 1u;
        result.canonical.constants.versionFlags.y = result.canonical.generation;
        if (result.canonical.cameras.empty())
        {
            throw std::logic_error("Canonical Cornell scene has no fixed camera.");
        }
        result.canonical.cameras.front().stableId.assign(result.descriptor.fixedCameraStableId);
        result.variants.push_back({"canonical-cornell", result.descriptor.fixedCameraStableId, ~0ull, false});
        result.variants.push_back({"nee", result.descriptor.fixedCameraStableId, ~0ull, false});
        result.variants.push_back({"mis", result.descriptor.fixedCameraStableId, ~0ull, false});
        if (!ApplyExperimentSceneVariant(result, result.variants.front().stableId))
        {
            throw std::logic_error("Cornell experiment default variant is invalid.");
        }
        const CanonicalSceneValidation validation = ValidateCanonicalScene(result.canonical);
        if (!validation)
        {
            throw std::logic_error("Generated Cornell experiment scene is invalid: " + validation.reason);
        }
        return result;
    }

    ExperimentScene BuildGgxMisMaterialExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::GgxMisMaterialLab);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;
        constexpr std::array roughnessValues{0.05f, 0.15f, 0.35f, 0.60f, 0.90f};
        std::array<std::uint32_t, roughnessValues.size()> dielectrics{};
        std::array<std::uint32_t, roughnessValues.size()> mixedMetals{};
        std::array<std::uint32_t, roughnessValues.size()> conductors{};
        for (std::size_t index = 0u; index < roughnessValues.size(); ++index)
        {
            dielectrics[index] = AddMaterial(
                scene, {0.80f, 0.80f, 0.80f}, 0.0f, roughnessValues[index]);
            mixedMetals[index] = AddMaterial(
                scene, {0.86f, 0.86f, 0.86f}, 0.5f, roughnessValues[index]);
            conductors[index] = AddMaterial(
                scene, {0.92f, 0.92f, 0.92f}, 1.0f, roughnessValues[index]);
        }
        // A metallic-roughness ABI v0 record with non-zero transmission is the
        // production bridge into L6's private rough-dielectric BSDF. Scenes 0
        // and 2 retain smooth glass, so both transmission paths stay reachable.
        const std::uint32_t glass = AddMaterial(scene, {1.0f, 1.0f, 1.0f}, 0.0f, 0.18f,
            MaterialModelMetallicRoughness, 1.0f, 1.5f, {0.82f, 0.94f, 1.0f}, 2.0f,
            {}, 0.0f, MaterialFlagDoubleSided);
        const std::uint32_t stage = AddMaterial(
            scene, {0.14f, 0.17f, 0.22f}, 0.0f, 0.82f);
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-7.0f, -1.10f, 2.0f}, {7.0f, -1.10f, 2.0f},
            {7.0f, -1.10f, -10.0f}, {-7.0f, -1.10f, -10.0f},
            {0.0f, 1.0f, 0.0f}, stage));
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-7.0f, -1.10f, -8.6f}, {7.0f, -1.10f, -8.6f},
            {7.0f, 5.8f, -8.6f}, {-7.0f, 5.8f, -8.6f},
            {0.0f, 0.0f, 1.0f}, stage));
        constexpr std::array xPositions{-3.2f, -1.6f, 0.0f, 1.6f, 3.2f};
        for (std::size_t index = 0u; index < xPositions.size(); ++index)
        {
            static_cast<void>(AddUvSphereGeometry(scene, bounds,
                {xPositions[index], -0.35f, -5.0f}, 0.72f, dielectrics[index]));
            static_cast<void>(AddUvSphereGeometry(scene, bounds,
                {xPositions[index], 1.20f, -5.0f}, 0.72f, mixedMetals[index]));
            static_cast<void>(AddUvSphereGeometry(scene, bounds,
                {xPositions[index], 2.75f, -5.0f}, 0.72f, conductors[index]));
        }
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {4.75f, -0.35f, -5.0f}, 0.72f, glass, false, 24u, 12u,
            GeometryFlagOpaque | GeometryFlagDoubleSided));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {12.0f, 0.0f, -5.0f}, 0.72f, dielectrics[2u]));

        // Analytic sphere-area lights keep the white-furnace variant free of
        // emissive geometry while still exercising small, large, and grazing
        // finite-light proposals in the material-grid variant. Radiance is
        // scaled by radius squared to keep emitted power comparable.  The
        // common 8x showcase scale preserves those ratios while avoiding an
        // effectively black Raw material grid at presentation exposure.
        AddSphereAreaLight(scene,
            {-2.15f, 4.35f, -5.25f}, 0.20f, {360.0f, 304.0f, 240.0f});
        AddSphereAreaLight(scene,
            {0.95f, 4.40f, 0.50f}, 1.20f, {10.0f, 8.444448f, 6.666664f});
        AddSphereAreaLight(scene,
            {-4.55f, 0.85f, -5.60f}, 0.55f,
            {47.603304f, 40.198344f, 31.735536f});
        const std::uint32_t finiteLightCount = static_cast<std::uint32_t>(scene.lights.size());

        result.environment.width = 1u;
        result.environment.height = 1u;
        result.environment.linearRgba = {{1.0f, 1.0f, 1.0f, 1.0f}};
        const std::uint32_t environmentLight = AddLight(scene,
            {}, {0.0f, 1.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {},
            LightTypeEnvironment, kInvalidId, kInvalidId, 0u, LightFlagEnabled);

        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 1.20f, 5.3f, 1.0f},
            {0.5f, 0.80f, -5.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            48.0f
        }, {
            "camera:showcase-4-white-furnace",
            {12.0f, 0.0f, -1.6f, 1.0f},
            {12.0f, 0.0f, -5.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            32.0f
        }, {
            "camera:showcase-4-rough-transmission",
            {7.0f, 1.80f, 0.65f, 1.0f},
            {4.75f, 0.15f, -5.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            38.0f
        }};
        const std::uint64_t finiteMask = finiteLightCount == 64u
            ? ~0ull
            : ((1ull << finiteLightCount) - 1ull);
        // The default material lab contains both its finite-light rig and the
        // environment. Light proposal changes can therefore be compared on
        // one immutable scene instead of silently selecting another variant.
        result.variants.push_back({"material-grid",
            result.descriptor.fixedCameraStableId,
            finiteMask | (1ull << environmentLight), true});
        result.variants.push_back({"small-light",
            result.descriptor.fixedCameraStableId, 1ull << 0u, false});
        result.variants.push_back({"large-light",
            result.descriptor.fixedCameraStableId, 1ull << 1u, false});
        result.variants.push_back({"grazing-light",
            result.descriptor.fixedCameraStableId, 1ull << 2u, false});
        result.variants.push_back({"white-furnace",
            "camera:showcase-4-white-furnace", 1ull << environmentLight, true});
        // Reuse the existing rough glass sphere and finite-light rig. The
        // dedicated camera makes the non-zero transmission lobe an actual
        // presentation target instead of a sphere hidden at the grid edge.
        result.variants.push_back({"rough-transmission",
            "camera:showcase-4-rough-transmission", 1ull << 0u, false});
        FinalizeScene(result, bounds, SceneFlagHasEnvironment);
        return result;
    }

    ExperimentScene BuildEnvironmentSamplingDomeExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::EnvironmentSamplingDome);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;

        const std::uint32_t floorMaterial = AddMaterial(
            scene, {0.20f, 0.22f, 0.25f}, 0.0f, 0.82f);
        const std::uint32_t diffuseMaterial = AddMaterial(
            scene, {0.78f, 0.18f, 0.08f}, 0.0f, 0.72f);
        const std::uint32_t glossyMaterial = AddMaterial(
            scene, {0.88f, 0.90f, 0.94f}, 0.0f, 0.14f);
        const std::uint32_t mirrorMaterial = AddMaterial(
            scene, {0.94f, 0.97f, 1.0f}, 1.0f, 0.025f);
        const std::uint32_t pedestalMaterial = AddMaterial(
            scene, {0.32f, 0.36f, 0.42f}, 0.15f, 0.36f);

        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-7.5f, 0.0f, 2.0f}, {7.5f, 0.0f, 2.0f},
            {7.5f, 0.0f, -12.0f}, {-7.5f, 0.0f, -12.0f},
            {0.0f, 1.0f, 0.0f}, floorMaterial));

        constexpr std::array xPositions{-3.2f, 0.0f, 3.2f};
        const std::array materialIds{diffuseMaterial, glossyMaterial, mirrorMaterial};
        for (std::size_t index = 0u; index < xPositions.size(); ++index)
        {
            const float yaw = 0.12f
                * static_cast<float>(static_cast<int>(index) - 1);
            static_cast<void>(AddBoxGeometry(scene, bounds,
                {xPositions[index], 0.35f, -5.0f}, {1.05f, 0.35f, 1.05f},
                yaw, pedestalMaterial));
            static_cast<void>(AddUvSphereGeometry(scene, bounds,
                {xPositions[index], 1.72f, -5.0f}, 1.35f, materialIds[index],
                false, 32u, 16u));
        }

        // A deterministic floating-point latitude/longitude HDR environment is
        // generated in linear radiance.  It contains a sub-degree hot sun plus
        // a broad horizon, so uniform-sphere and luminance*sin(theta) proposals
        // exercise materially different variance without an external asset.
        result.environment.width = 128u;
        result.environment.height = 64u;
        result.environment.linearRgba.resize(
            static_cast<std::size_t>(result.environment.width)
            * result.environment.height);
        FillEnvironmentImage(result.environment,
            ExperimentEnvironmentProfile::Original);
        static_cast<void>(AddLight(scene,
            {}, {0.0f, 1.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {},
            LightTypeEnvironment, kInvalidId, kInvalidId, 0u, LightFlagEnabled));

        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 2.65f, 6.4f, 1.0f},
            {0.0f, 1.35f, -5.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            47.0f
        }, {
            "camera:showcase-5-sun-alignment",
            {-5.8f, 3.8f, 2.8f, 1.0f},
            {0.0f, 1.2f, -5.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            54.0f
        }, {
            "camera:showcase-5-polar-sun",
            {0.0f, 2.65f, 6.4f, 1.0f},
            {0.0f, 10.0f, 6.4f, 1.0f},
            {0.0f, 0.0f, -1.0f, 0.0f},
            38.0f
        }, {
            "camera:showcase-5-seam-sun",
            {0.0f, 2.65f, 6.4f, 1.0f},
            {10.0f, 2.65f, 6.4f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            38.0f
        }};
        result.variants.push_back({"uniform-sphere",
            result.descriptor.fixedCameraStableId, ~0ull, true});
        result.variants.push_back({"environment-importance",
            result.descriptor.fixedCameraStableId, ~0ull, true});
        result.variants.push_back({"sun-alignment-view",
            "camera:showcase-5-sun-alignment", ~0ull, true});
        // These edge views deliberately share the same generated 128x64
        // environment and geometry. They make pole (sin(theta)) and
        // longitude-seam wrapping checks reachable without introducing
        // another numbered scene or an external HDR asset.
        result.variants.push_back({"polar-sun",
            "camera:showcase-5-polar-sun", ~0ull, true, false,
            ExperimentEnvironmentProfile::PolarSun});
        result.variants.push_back({"seam-sun",
            "camera:showcase-5-seam-sun", ~0ull, true, false,
            ExperimentEnvironmentProfile::SeamSun});
        FinalizeScene(result, bounds, SceneFlagHasEnvironment);
        return result;
    }

    ExperimentScene BuildBackendParityBenchmarkExperimentScene()
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::BackendParityBenchmark);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;
        const std::uint32_t floorMaterial = AddMaterial(
            scene, {0.16f, 0.18f, 0.22f}, 0.0f, 0.78f);
        const std::uint32_t smallMaterial = AddMaterial(
            scene, {0.14f, 0.58f, 0.90f}, 0.05f, 0.38f);
        const std::uint32_t mediumMaterial = AddMaterial(
            scene, {0.93f, 0.42f, 0.10f}, 0.35f, 0.24f);
        const std::uint32_t largeMaterial = AddMaterial(
            scene, {0.42f, 0.82f, 0.28f}, 0.0f, 0.56f);

        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-13.0f, 0.0f, 3.0f}, {13.0f, 0.0f, 3.0f},
            {13.0f, 0.0f, -17.0f}, {-13.0f, 0.0f, -17.0f},
            {0.0f, 1.0f, 0.0f}, floorMaterial));
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-13.0f, 0.0f, -17.0f}, {13.0f, 0.0f, -17.0f},
            {13.0f, 9.0f, -17.0f}, {-13.0f, 9.0f, -17.0f},
            {0.0f, 0.0f, 1.0f}, floorMaterial));

        // Three deterministic complexity islands: low-poly primitives,
        // medium tessellation, and a dense repeated-geometry field.
        static_cast<void>(AddTriangularPrismGeometry(scene, bounds,
            {-7.7f, 0.25f, -2.4f}, {-7.7f, 0.25f, -4.0f},
            {-7.7f, 3.1f, -3.2f}, 0.65f, smallMaterial));
        static_cast<void>(AddBoxGeometry(scene, bounds,
            {-7.7f, 1.15f, -7.4f}, {1.25f, 1.15f, 1.25f}, 0.35f,
            smallMaterial));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {0.0f, 1.65f, -4.8f}, 1.65f, mediumMaterial, false, 32u, 18u));
        static_cast<void>(AddUvSphereGeometry(scene, bounds,
            {0.0f, 1.05f, -9.2f}, 1.05f, mediumMaterial, false, 48u, 24u));
        for (std::uint32_t row = 0u; row < 6u; ++row)
        {
            for (std::uint32_t column = 0u; column < 6u; ++column)
            {
                const float x = 5.0f + 0.92f * static_cast<float>(column);
                const float z = -2.8f - 1.35f * static_cast<float>(row);
                const float height = 0.28f + 0.11f
                    * static_cast<float>((row * 7u + column * 3u) % 6u);
                static_cast<void>(AddBoxGeometry(scene, bounds,
                    {x, height, z}, {0.32f, height, 0.44f},
                    0.09f * static_cast<float>(row + column), largeMaterial));
            }
        }
        AddSphereAreaLight(scene, {-6.0f, 6.8f, -2.0f}, 0.65f,
            {64.0f, 52.0f, 40.0f});
        AddSphereAreaLight(scene, {5.5f, 7.2f, -7.0f}, 1.15f,
            {24.0f, 34.0f, 56.0f});
        AddDirectionalLight(scene, {-0.35f, -1.0f, -0.25f},
            {1.40f, 1.68f, 2.20f});

        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 6.2f, 12.8f, 1.0f},
            {0.7f, 1.2f, -6.2f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            56.0f
        }, {
            "camera:showcase-7-dense-field",
            {10.8f, 5.0f, 7.0f, 1.0f},
            {7.2f, 0.8f, -6.2f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            48.0f
        }};
        result.variants.push_back({"all-complexities",
            result.descriptor.fixedCameraStableId, ~0ull, false});
        result.variants.push_back({"dense-field",
            "camera:showcase-7-dense-field", ~0ull, false});
        FinalizeScene(result, bounds);
        return result;
    }

    ExperimentScene BuildTemporalStabilityCorridorExperimentScene()
    {
        return BuildTemporalStabilityCorridorExperimentScene(
            ExperimentSceneBuildOptions{});
    }

    ExperimentScene BuildTemporalStabilityCorridorExperimentScene(
        const ExperimentSceneBuildOptions& buildOptions)
    {
        ExperimentScene result = BeginScene(
            ExperimentScenePreset::TemporalStabilityCorridor);
        CanonicalScene& scene = result.canonical;
        Bounds bounds;
        const std::uint32_t dark = AddMaterial(
            scene, {0.025f, 0.032f, 0.045f}, 0.0f, 0.92f);
        const std::uint32_t white = AddMaterial(
            scene, {0.80f, 0.83f, 0.90f}, 0.0f, 0.42f);
        const std::uint32_t accent = AddMaterial(
            scene, {0.92f, 0.08f, 0.035f}, 0.15f, 0.26f);
        const std::uint32_t emitter = AddMaterial(
            scene, {1.0f, 1.0f, 1.0f}, 0.0f, 0.8f,
            MaterialModelMetallicRoughness, 0.0f, 1.5f,
            {1.0f, 1.0f, 1.0f}, 0.0f, {0.65f, 0.82f, 1.0f}, 18.0f,
            MaterialFlagEmissive | MaterialFlagDoubleSided);

        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-3.6f, 0.0f, 4.0f}, {3.6f, 0.0f, 4.0f},
            {3.6f, 0.0f, -22.0f}, {-3.6f, 0.0f, -22.0f},
            {0.0f, 1.0f, 0.0f}, dark));
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-3.6f, 5.8f, -22.0f}, {3.6f, 5.8f, -22.0f},
            {3.6f, 5.8f, 4.0f}, {-3.6f, 5.8f, 4.0f},
            {0.0f, -1.0f, 0.0f}, dark));
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {-3.6f, 0.0f, -22.0f}, {-3.6f, 0.0f, 4.0f},
            {-3.6f, 5.8f, 4.0f}, {-3.6f, 5.8f, -22.0f},
            {1.0f, 0.0f, 0.0f}, white));
        static_cast<void>(AddQuadGeometry(scene, bounds,
            {3.6f, 0.0f, 4.0f}, {3.6f, 0.0f, -22.0f},
            {3.6f, 5.8f, -22.0f}, {3.6f, 5.8f, 4.0f},
            {-1.0f, 0.0f, 0.0f}, dark));

        for (std::uint32_t index = 0u; index < 10u; ++index)
        {
            const float z = -1.0f - 1.9f * static_cast<float>(index);
            const float x = index % 2u == 0u ? -2.75f : 2.75f;
            static_cast<void>(AddBoxGeometry(scene, bounds,
                {x, 2.25f, z}, {0.055f, 2.25f, 0.72f},
                index % 3u == 0u ? 0.18f : -0.12f,
                index % 2u == 0u ? accent : white));
        }
        for (std::uint32_t index = 0u; index < 5u; ++index)
        {
            const float z = -2.0f - 4.2f * static_cast<float>(index);
            const GeometryResult strip = AddQuadGeometry(scene, bounds,
                {-1.15f, 5.72f, z - 0.18f}, {1.15f, 5.72f, z - 0.18f},
                {1.15f, 5.72f, z + 0.18f}, {-1.15f, 5.72f, z + 0.18f},
                {0.0f, -1.0f, 0.0f}, emitter,
                GeometryFlagOpaque | GeometryFlagDoubleSided);
            AddTriangleLights(scene, strip, {8.0f, 11.0f, 18.0f}, true);
        }

        // The last geometry is instanced independently.  Its current and
        // previous transforms expose a deterministic disocclusion/motion-vector
        // case while the corridor remains one static instance.
        const GeometryResult movingPanel = AddBoxGeometry(scene, bounds,
            {0.0f, 0.0f, 0.0f}, {0.075f, 1.65f, 1.15f}, 0.0f, accent);
        scene.cameras = {{
            std::string{result.descriptor.fixedCameraStableId},
            {0.0f, 2.45f, 7.5f, 1.0f},
            {0.0f, 2.1f, -10.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            50.0f
        }, {
            "camera:showcase-8-disocclusion",
            {-2.7f, 2.2f, 1.0f, 1.0f},
            {0.0f, 1.8f, -7.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            46.0f
        }, {
            "camera:showcase-8-camera-only",
            {0.0f, 2.45f, 7.5f, 1.0f},
            {0.0f, 2.1f, -10.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            50.0f
        }};
        result.variants.push_back({"motion-corridor",
            result.descriptor.fixedCameraStableId, ~0ull, false});
        result.variants.push_back({"camera-only",
            "camera:showcase-8-camera-only", ~0ull, false});
        result.variants.push_back({"object-only",
            result.descriptor.fixedCameraStableId, ~0ull, false});
        result.variants.push_back({"disocclusion-focus",
            "camera:showcase-8-disocclusion", ~0ull, false});
        result.variants.push_back({"camera-cut",
            result.descriptor.fixedCameraStableId, ~0ull, false, true});

        const std::string_view requestedVariant = buildOptions.variantStableId.empty()
            ? result.variants.front().stableId
            : buildOptions.variantStableId;
        if (FindExperimentSceneVariant(result, requestedVariant) == nullptr)
        {
            throw std::invalid_argument(
                "Unknown Temporal Stability Corridor variant.");
        }
        result.sampledAnimationFrameIndex = buildOptions.animationFrameIndex;
        FinalizeScene(result, bounds, SceneFlagHasAnimatedRigidInstances);
        if (!ApplyExperimentSceneVariant(result, requestedVariant))
        {
            throw std::logic_error(
                "Temporal Stability Corridor variant could not be activated.");
        }
        GpuInstanceV0& staticInstance = scene.instances.front();
        staticInstance.metadata.y = movingPanel.geometryId;
        scene.instances.push_back({
            TranslationMatrix(TemporalPanelPosition(0u)),
            TranslationMatrix(TemporalPanelPosition(0u) * -1.0f),
            TranslationMatrix(TemporalPanelPosition(0u)),
            {movingPanel.geometryId, 1u, 1u,
                InstanceFlagVisible | InstanceFlagCastsShadow
                    | InstanceFlagAnimatedRigid},
            {0u, 0u, 0u, 0u}
        });
        scene.constants.counts0.w = static_cast<std::uint32_t>(scene.instances.size());
        const bool cameraOnly = requestedVariant == "camera-only";
        const bool cameraCut = requestedVariant == "camera-cut";
        const bool movePanel = buildOptions.animateRigidOccluders
            && !cameraOnly && !cameraCut;
        SampleTemporalPanelTransforms(result,
            buildOptions.animationFrameIndex, movePanel);
        const bool moveCamera = buildOptions.animateCamera
            && cameraOnly;
        SampleTemporalCamera(result,
            buildOptions.animationFrameIndex, moveCamera);
        const CanonicalSceneValidation validation = ValidateCanonicalScene(scene);
        if (!validation)
        {
            throw std::logic_error(
                "Generated temporal-stability scene is invalid: " + validation.reason);
        }
        return result;
    }

    ExperimentScene BuildManyLightsRestirArenaExperimentScene(
        const ExperimentSceneBuildOptions& buildOptions)
    {
        ExperimentScene result = BeginScene(ExperimentScenePreset::ManyLightsRestirArena);
        ManyLightsArenaOptions options;
        switch (buildOptions.manyLightsCount)
        {
        case 100u: options.tier = ManyLightsArenaTier::Lights100; break;
        case 1000u: options.tier = ManyLightsArenaTier::Lights1000; break;
        case 10000u: options.tier = ManyLightsArenaTier::Lights10000; break;
        default:
            throw std::invalid_argument(
                "Many Lights experiment requires exactly 100, 1000, or 10000 lights.");
        }
        options.frameIndex = buildOptions.animationFrameIndex;
        options.animateLights = buildOptions.animateLights;
        options.animateCamera = buildOptions.animateCamera;
        options.animateRigidOccluders = buildOptions.animateRigidOccluders;
        ManyLightsArenaFrame arena = BuildManyLightsArena(options);
        result.canonical = std::move(arena.canonical);
        result.canonical.stableId.assign(result.descriptor.canonicalStableId);
        result.canonical.generation = 1u;
        result.canonical.constants.versionFlags.y = result.canonical.generation;
        if (result.canonical.cameras.empty())
        {
            throw std::logic_error("Many Lights arena has no fixed camera.");
        }
        result.canonical.cameras.front().stableId.assign(
            result.descriptor.fixedCameraStableId);
        const std::string_view tierStableId = buildOptions.manyLightsCount == 100u
            ? std::string_view{"lights-100"}
            : (buildOptions.manyLightsCount == 1000u
                ? std::string_view{"lights-1000"}
                : std::string_view{"lights-10000"});
        result.variants.push_back({tierStableId,
            result.descriptor.fixedCameraStableId, ~0ull, false});
        if (!ApplyExperimentSceneVariant(result, result.variants.front().stableId))
        {
            throw std::logic_error("Many Lights arena default variant is invalid.");
        }
        const CanonicalSceneValidation validation = ValidateCanonicalScene(result.canonical);
        if (!validation)
        {
            throw std::logic_error(
                "Generated Many Lights experiment scene is invalid: " + validation.reason);
        }
        return result;
    }

    ExperimentScene BuildExperimentScene(
        const ExperimentScenePreset preset,
        const ExperimentSceneBuildOptions& options)
    {
        ExperimentScene result;
        switch (preset)
        {
        case ExperimentScenePreset::BaselineGallery:
            result = BuildBaselineGalleryExperimentScene();
            break;
        case ExperimentScenePreset::IntersectionBvhLab:
            result = BuildIntersectionBvhExperimentScene();
            break;
        case ExperimentScenePreset::WhittedOpticsRoom:
            result = BuildWhittedOpticsExperimentScene();
            break;
        case ExperimentScenePreset::CornellBox:
            result = BuildCornellExperimentScene();
            break;
        case ExperimentScenePreset::GgxMisMaterialLab:
            result = BuildGgxMisMaterialExperimentScene();
            break;
        case ExperimentScenePreset::EnvironmentSamplingDome:
            result = BuildEnvironmentSamplingDomeExperimentScene();
            break;
        case ExperimentScenePreset::SponzaTraversalHall:
            throw std::runtime_error(
                "Sponza scene is fail-closed until the pinned Intel asset, CC BY attribution, hash, texture-capable glTF ingestion, and production provider are present.");
        case ExperimentScenePreset::BackendParityBenchmark:
            result = BuildBackendParityBenchmarkExperimentScene();
            break;
        case ExperimentScenePreset::TemporalStabilityCorridor:
            result = BuildTemporalStabilityCorridorExperimentScene(options);
            break;
        case ExperimentScenePreset::ManyLightsRestirArena:
            result = BuildManyLightsRestirArenaExperimentScene(options);
            break;
        default:
            throw std::invalid_argument("Unknown experiment scene preset.");
        }
        if (!options.variantStableId.empty()
            && preset != ExperimentScenePreset::TemporalStabilityCorridor
            && !ApplyExperimentSceneVariant(result, options.variantStableId))
        {
            throw std::invalid_argument(
                "Unknown experiment scene variant for the selected preset.");
        }
        return result;
    }
}
