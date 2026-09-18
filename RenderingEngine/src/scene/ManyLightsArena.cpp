#include "scene/ManyLightsArena.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace RenderingEngine::Scene
{
    namespace
    {
        using namespace Contracts::AbiV0;

        constexpr std::uint32_t kStableLightBase = 0x40000000u;
        constexpr std::uint32_t kReplacementLightBase = 0x60000000u;
        constexpr std::uint32_t kRigidOccluderCount = 3u;

        struct Vec3 final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct Bounds final
        {
            Vec3 minimum{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            Vec3 maximum{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
        };

        [[nodiscard]] constexpr AbiMat4Rows IdentityMatrix() noexcept
        {
            return {
                {1.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}
            };
        }

        [[nodiscard]] AbiMat4Rows ScaleTranslationMatrix(
            const Vec3 scale,
            const Vec3 translation) noexcept
        {
            return {
                {scale.x, 0.0f, 0.0f, translation.x},
                {0.0f, scale.y, 0.0f, translation.y},
                {0.0f, 0.0f, scale.z, translation.z},
                {0.0f, 0.0f, 0.0f, 1.0f}
            };
        }

        [[nodiscard]] AbiMat4Rows InverseScaleTranslationMatrix(
            const Vec3 scale,
            const Vec3 translation)
        {
            if (!(scale.x > 0.0f) || !(scale.y > 0.0f) || !(scale.z > 0.0f))
            {
                throw std::invalid_argument("Many Lights transforms require positive scale.");
            }
            return {
                {1.0f / scale.x, 0.0f, 0.0f, -translation.x / scale.x},
                {0.0f, 1.0f / scale.y, 0.0f, -translation.y / scale.y},
                {0.0f, 0.0f, 1.0f / scale.z, -translation.z / scale.z},
                {0.0f, 0.0f, 0.0f, 1.0f}
            };
        }

        [[nodiscard]] Vec3 Subtract(const Vec3 a, const Vec3 b) noexcept
        {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        [[nodiscard]] Vec3 Cross(const Vec3 a, const Vec3 b) noexcept
        {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        [[nodiscard]] float Dot(const Vec3 a, const Vec3 b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        [[nodiscard]] Vec3 Normalize(const Vec3 value) noexcept
        {
            const float lengthSquared = Dot(value, value);
            if (!(lengthSquared > 0.0f))
            {
                return {0.0f, 1.0f, 0.0f};
            }
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            return {
                value.x * inverseLength,
                value.y * inverseLength,
                value.z * inverseLength
            };
        }

        void Expand(Bounds& bounds, const Vec3 value) noexcept
        {
            bounds.minimum.x = (std::min)(bounds.minimum.x, value.x);
            bounds.minimum.y = (std::min)(bounds.minimum.y, value.y);
            bounds.minimum.z = (std::min)(bounds.minimum.z, value.z);
            bounds.maximum.x = (std::max)(bounds.maximum.x, value.x);
            bounds.maximum.y = (std::max)(bounds.maximum.y, value.y);
            bounds.maximum.z = (std::max)(bounds.maximum.z, value.z);
        }

        void AppendTriangle(
            CanonicalScene& scene,
            Bounds& geometryBounds,
            Vec3 p0,
            Vec3 p1,
            Vec3 p2,
            const Vec3 desiredNormal)
        {
            if (!(Dot(Cross(Subtract(p1, p0), Subtract(p2, p0)), desiredNormal) > 0.0f))
            {
                std::swap(p1, p2);
            }
            const Vec3 normal = Normalize(Cross(Subtract(p1, p0), Subtract(p2, p0)));
            Vec3 tangent = Normalize(Subtract(p1, p0));
            if (!(Dot(tangent, tangent) > 0.0f))
            {
                tangent = {1.0f, 0.0f, 0.0f};
            }
            const std::array<Vec3, 3> positions{p0, p1, p2};
            const std::array<AbiFloat4, 3> texcoords{
                AbiFloat4{0.0f, 0.0f, 0.0f, 0.0f},
                AbiFloat4{1.0f, 0.0f, 0.0f, 0.0f},
                AbiFloat4{0.5f, 1.0f, 0.0f, 0.0f}
            };
            for (std::size_t index = 0u; index < positions.size(); ++index)
            {
                if (scene.vertices.size() >= std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::length_error("Many Lights vertex count exceeds uint32.");
                }
                const Vec3 position = positions[index];
                const std::uint32_t vertexIndex =
                    static_cast<std::uint32_t>(scene.vertices.size());
                scene.vertices.push_back({
                    {position.x, position.y, position.z, 1.0f},
                    {normal.x, normal.y, normal.z, 0.0f},
                    {tangent.x, tangent.y, tangent.z, 1.0f},
                    texcoords[index]
                });
                scene.indices.push_back(vertexIndex);
                Expand(geometryBounds, position);
            }
        }

        void AppendQuad(
            CanonicalScene& scene,
            Bounds& geometryBounds,
            const Vec3 p0,
            const Vec3 p1,
            const Vec3 p2,
            const Vec3 p3,
            const Vec3 normal)
        {
            AppendTriangle(scene, geometryBounds, p0, p1, p2, normal);
            AppendTriangle(scene, geometryBounds, p0, p2, p3, normal);
        }

        [[nodiscard]] std::uint32_t FinishGeometry(
            CanonicalScene& scene,
            const Bounds& bounds,
            const std::uint32_t firstIndex,
            const std::uint32_t materialId)
        {
            const std::uint32_t geometryId =
                static_cast<std::uint32_t>(scene.geometries.size());
            const std::uint32_t indexCount =
                static_cast<std::uint32_t>(scene.indices.size()) - firstIndex;
            const std::uint32_t firstPrimitive = firstIndex / 3u;
            scene.geometries.push_back({
                {firstIndex, indexCount, 0u, firstPrimitive},
                {geometryId, 0u, materialId, GeometryFlagOpaque},
                {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f},
                {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f}
            });
            return firstPrimitive;
        }

        void AddRoomGeometry(CanonicalScene& scene)
        {
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds bounds;
            AppendQuad(scene, bounds,
                {-12.0f, 0.0f, 10.0f}, {12.0f, 0.0f, 10.0f},
                {12.0f, 0.0f, -14.0f}, {-12.0f, 0.0f, -14.0f},
                {0.0f, 1.0f, 0.0f});
            AppendQuad(scene, bounds,
                {-12.0f, 0.0f, -14.0f}, {12.0f, 0.0f, -14.0f},
                {12.0f, 8.0f, -14.0f}, {-12.0f, 8.0f, -14.0f},
                {0.0f, 0.0f, 1.0f});
            AppendQuad(scene, bounds,
                {-12.0f, 0.0f, 10.0f}, {-12.0f, 0.0f, -14.0f},
                {-12.0f, 8.0f, -14.0f}, {-12.0f, 8.0f, 10.0f},
                {1.0f, 0.0f, 0.0f});
            AppendQuad(scene, bounds,
                {12.0f, 0.0f, -14.0f}, {12.0f, 0.0f, 10.0f},
                {12.0f, 8.0f, 10.0f}, {12.0f, 8.0f, -14.0f},
                {-1.0f, 0.0f, 0.0f});
            AppendQuad(scene, bounds,
                {-12.0f, 8.0f, -14.0f}, {12.0f, 8.0f, -14.0f},
                {12.0f, 8.0f, 10.0f}, {-12.0f, 8.0f, 10.0f},
                {0.0f, -1.0f, 0.0f});
            static_cast<void>(FinishGeometry(scene, bounds, firstIndex, 0u));
        }

        void AddOccluderGeometry(CanonicalScene& scene)
        {
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds bounds;
            constexpr float m = -0.5f;
            constexpr float p = 0.5f;
            AppendQuad(scene, bounds, {m,m,m}, {m,m,p}, {m,p,p}, {m,p,m}, {-1,0,0});
            AppendQuad(scene, bounds, {p,m,p}, {p,m,m}, {p,p,m}, {p,p,p}, {1,0,0});
            AppendQuad(scene, bounds, {m,m,p}, {m,m,m}, {p,m,m}, {p,m,p}, {0,-1,0});
            AppendQuad(scene, bounds, {m,p,m}, {m,p,p}, {p,p,p}, {p,p,m}, {0,1,0});
            AppendQuad(scene, bounds, {p,m,m}, {m,m,m}, {m,p,m}, {p,p,m}, {0,0,-1});
            AppendQuad(scene, bounds, {m,m,p}, {p,m,p}, {p,p,p}, {m,p,p}, {0,0,1});
            static_cast<void>(FinishGeometry(scene, bounds, firstIndex, 0u));
        }

        [[nodiscard]] std::uint32_t AddEmitterTriangleGeometry(CanonicalScene& scene)
        {
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            Bounds bounds;
            AppendTriangle(scene, bounds,
                {-0.5f, 0.0f, -0.28867513f},
                {0.5f, 0.0f, -0.28867513f},
                {0.0f, 0.0f, 0.57735026f},
                {0.0f, -1.0f, 0.0f});
            return FinishGeometry(scene, bounds, firstIndex, 1u);
        }

        [[nodiscard]] std::uint32_t Hash(std::uint32_t value) noexcept
        {
            value ^= value >> 16u;
            value *= 0x7feb352du;
            value ^= value >> 15u;
            value *= 0x846ca68bu;
            value ^= value >> 16u;
            return value;
        }

        [[nodiscard]] float UnitFloat(const std::uint32_t value) noexcept
        {
            return static_cast<float>(value & 0x00ffffffu) / 16777216.0f;
        }

        [[nodiscard]] Vec3 LightPosition(
            const std::uint32_t slot,
            const std::uint32_t lightCount,
            const std::uint32_t frameIndex,
            const bool animated) noexcept
        {
            const std::uint32_t columns = static_cast<std::uint32_t>(
                std::ceil(std::sqrt(static_cast<double>(lightCount))));
            const std::uint32_t rows = (lightCount + columns - 1u) / columns;
            const std::uint32_t column = slot % columns;
            const std::uint32_t row = slot / columns;
            const float x = columns > 1u
                ? -10.5f + 21.0f * static_cast<float>(column)
                    / static_cast<float>(columns - 1u)
                : 0.0f;
            const float z = rows > 1u
                ? -12.5f + 20.0f * static_cast<float>(row)
                    / static_cast<float>(rows - 1u)
                : -2.5f;
            const std::uint32_t hashed = Hash(slot + 1u);
            const float baseY = 4.2f + 2.6f * UnitFloat(hashed);
            if (!animated)
            {
                return {x, baseY, z};
            }
            const float time = static_cast<float>(frameIndex) * 0.03125f;
            const float phase = 6.28318530718f * UnitFloat(Hash(hashed));
            return {
                x + 0.18f * std::sin(time + phase),
                baseY + 0.12f * std::sin(time * 0.71f + phase * 1.7f),
                z + 0.18f * std::cos(time * 0.83f + phase)
            };
        }

        [[nodiscard]] Vec3 LightScale(const std::uint32_t slot) noexcept
        {
            constexpr std::array<float, 5> sizes{0.10f, 0.16f, 0.24f, 0.36f, 0.52f};
            const float size = sizes[Hash(slot) % sizes.size()];
            return {size, 1.0f, size};
        }

        [[nodiscard]] Vec3 LightRadiance(
            const std::uint32_t stableLightId,
            const std::uint32_t frameIndex,
            const bool animated) noexcept
        {
            constexpr std::array<float, 6> strengths{3.0f, 6.0f, 12.0f, 24.0f, 48.0f, 96.0f};
            constexpr std::array<Vec3, 6> colors{
                Vec3{1.0f, 0.22f, 0.12f}, Vec3{1.0f, 0.55f, 0.15f},
                Vec3{0.35f, 0.85f, 1.0f}, Vec3{0.25f, 0.45f, 1.0f},
                Vec3{0.85f, 0.28f, 1.0f}, Vec3{0.45f, 1.0f, 0.35f}
            };
            const std::uint32_t hashed = Hash(stableLightId);
            const float strength = strengths[hashed % strengths.size()];
            const Vec3 color = colors[(hashed >> 8u) % colors.size()];
            const float modulation = animated
                ? 0.72f + 0.28f * std::sin(
                    static_cast<float>(frameIndex) * 0.021f
                    + 6.28318530718f * UnitFloat(Hash(hashed)))
                : 1.0f;
            return {
                color.x * strength * modulation,
                color.y * strength * (1.0f - 0.12f * modulation),
                color.z * strength * (0.88f + 0.12f * modulation)
            };
        }

        [[nodiscard]] Vec3 OccluderPosition(
            const std::uint32_t index,
            const std::uint32_t frameIndex,
            const bool animated) noexcept
        {
            const float time = animated ? static_cast<float>(frameIndex) * 0.025f : 0.0f;
            const float phase = static_cast<float>(index) * 2.09439510239f;
            return {
                -4.0f + 4.0f * static_cast<float>(index)
                    + 0.75f * std::sin(time + phase),
                1.4f + 0.35f * static_cast<float>(index),
                -3.0f + 2.5f * std::cos(time * 0.77f + phase)
            };
        }

        [[nodiscard]] Vec3 OccluderScale(const std::uint32_t index) noexcept
        {
            return {
                1.2f + 0.4f * static_cast<float>(index),
                2.8f + 0.7f * static_cast<float>(index),
                1.0f + 0.25f * static_cast<float>(index)
            };
        }

        [[nodiscard]] std::uint32_t PersistentLightId(
            const std::uint32_t slot,
            const std::uint32_t lightCount,
            const std::uint32_t topologyEpoch) noexcept
        {
            if (topologyEpoch != 0u && slot == lightCount / 2u)
            {
                return kReplacementLightBase + topologyEpoch;
            }
            return kStableLightBase + slot;
        }

        void AddMaterials(CanonicalScene& scene)
        {
            scene.materials = {
                {
                    {0.58f, 0.61f, 0.68f, 1.0f}, {},
                    {0.05f, 0.58f, 1.0f, 0.5f}, {0.0f, 1.5f, 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f, 0.0f},
                    {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                    {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                    {MaterialModelMetallicRoughness, MaterialFlagNone, 0u, 0u}
                },
                {
                    {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
                    {0.0f, 0.9f, 1.0f, 0.5f}, {0.0f, 1.5f, 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f, 0.0f},
                    {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                    {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                    {MaterialModelMetallicRoughness,
                        MaterialFlagEmissive | MaterialFlagDoubleSided, 1u, 0u}
                }
            };
        }
    }

    std::uint32_t ResolveManyLightsArenaCount(const ManyLightsArenaTier tier) noexcept
    {
        switch (tier)
        {
        case ManyLightsArenaTier::Lights100:
            return 100u;
        case ManyLightsArenaTier::Lights1000:
            return 1000u;
        case ManyLightsArenaTier::Lights10000:
            return 10000u;
        }
        return 0u;
    }

    ManyLightsArenaFrame BuildManyLightsArena(const ManyLightsArenaOptions& options)
    {
        const std::uint32_t lightCount = ResolveManyLightsArenaCount(options.tier);
        if (lightCount == 0u || options.sceneGeneration == 0u
            || options.topologyEpoch >= 0x1fffffffu)
        {
            throw std::invalid_argument("Many Lights options violate the stable identity contract.");
        }

        ManyLightsArenaFrame frame;
        CanonicalScene& scene = frame.canonical;
        scene.stableId = "many-lights-restir-arena";
        scene.generation = options.sceneGeneration;
        AddMaterials(scene);
        AddRoomGeometry(scene);
        AddOccluderGeometry(scene);
        frame.lightPrimitiveId = AddEmitterTriangleGeometry(scene);

        const AbiMat4Rows identity = IdentityMatrix();
        scene.instances.reserve(
            static_cast<std::size_t>(1u + kRigidOccluderCount) + lightCount);
        scene.instances.push_back({
            identity, identity, identity,
            {0u, 1u, 0u, InstanceFlagVisible | InstanceFlagCastsShadow},
            {0u, 0u, 0u, 0u}
        });
        for (std::uint32_t index = 0u; index < kRigidOccluderCount; ++index)
        {
            const Vec3 scale = OccluderScale(index);
            const Vec3 current = OccluderPosition(
                index, options.frameIndex, options.animateRigidOccluders);
            const Vec3 previous = OccluderPosition(
                index, options.frameIndex == 0u ? 0u : options.frameIndex - 1u,
                options.animateRigidOccluders);
            const std::uint32_t instanceId = static_cast<std::uint32_t>(scene.instances.size());
            scene.instances.push_back({
                ScaleTranslationMatrix(scale, current),
                InverseScaleTranslationMatrix(scale, current),
                ScaleTranslationMatrix(scale, previous),
                {1u, 1u, instanceId,
                    InstanceFlagVisible | InstanceFlagCastsShadow
                        | InstanceFlagAnimatedRigid},
                {0u, 0u, 0u, 0u}
            });
        }

        frame.firstLightInstanceIndex = static_cast<std::uint32_t>(scene.instances.size());
        frame.lightSetGeneration = static_cast<std::uint64_t>(options.topologyEpoch) + 1u;
        frame.replacedLightTableIndex = options.topologyEpoch == 0u
            ? kManyLightsArenaInvalidIndex : lightCount / 2u;
        frame.lightIdentities.reserve(lightCount);
        scene.lights.reserve(lightCount);
        for (std::uint32_t slot = 0u; slot < lightCount; ++slot)
        {
            const std::uint32_t stableLightId = PersistentLightId(
                slot, lightCount, options.topologyEpoch);
            const Vec3 scale = LightScale(slot);
            const Vec3 current = LightPosition(
                slot, lightCount, options.frameIndex, options.animateLights);
            const Vec3 previous = LightPosition(
                slot, lightCount, options.frameIndex == 0u ? 0u : options.frameIndex - 1u,
                options.animateLights);
            const Vec3 radiance = LightRadiance(
                stableLightId, options.frameIndex, options.animateLights);
            const std::uint32_t instanceId = static_cast<std::uint32_t>(scene.instances.size());
            scene.instances.push_back({
                ScaleTranslationMatrix(scale, current),
                InverseScaleTranslationMatrix(scale, current),
                ScaleTranslationMatrix(scale, previous),
                {2u, 1u, instanceId,
                    InstanceFlagVisible | InstanceFlagCastsShadow
                        | InstanceFlagAnimatedRigid},
                {0u, 0u, 0u, 0u}
            });

            constexpr float unitTriangleArea = 0.43301270189f;
            scene.lights.push_back({
                {current.x, current.y, current.z, 0.0f},
                {0.0f, -1.0f, 0.0f, 0.0f},
                {radiance.x, radiance.y, radiance.z, 1.0f},
                {scale.x, scale.z, unitTriangleArea * scale.x * scale.z, 0.0f},
                {LightTypeEmissiveTriangle, slot, instanceId, frame.lightPrimitiveId},
                {kInvalidId, LightFlagEnabled | LightFlagTwoSided | LightFlagAnimated,
                    0u, 0u}
            });
            frame.lightIdentities.push_back({
                stableLightId, frame.lightPrimitiveId, 1u, slot
            });
        }

        const float cameraTime = options.animateCamera
            ? static_cast<float>(options.frameIndex) * 0.0125f : 0.0f;
        scene.cameras = {{
            "camera:many-lights-restir-arena",
            {2.2f * std::sin(cameraTime), 4.6f,
                13.5f + 0.8f * std::cos(cameraTime), 1.0f},
            {0.0f, 2.8f, -2.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            51.0f
        }};
        scene.constants.counts0 = {
            static_cast<std::uint32_t>(scene.vertices.size()),
            static_cast<std::uint32_t>(scene.indices.size()),
            static_cast<std::uint32_t>(scene.geometries.size()),
            static_cast<std::uint32_t>(scene.instances.size())
        };
        scene.constants.counts1 = {
            static_cast<std::uint32_t>(scene.materials.size()),
            static_cast<std::uint32_t>(scene.lights.size()), 0u, 0u
        };
        scene.constants.sceneBoundsMin = {-13.0f, 0.0f, -15.0f, 0.0f};
        scene.constants.sceneBoundsMax = {13.0f, 9.0f, 11.0f, 0.0f};
        scene.constants.versionFlags = {
            kAbiVersion, scene.generation, SceneFlagHasAnimatedRigidInstances, 0u
        };
        scene.constants.environment = {kInvalidId, 0u, 0u, 0u};

        const CanonicalSceneValidation validation = ValidateCanonicalScene(scene);
        if (!validation)
        {
            throw std::logic_error(
                "Constructed Many Lights scene is invalid: " + validation.reason);
        }
        return frame;
    }
}
