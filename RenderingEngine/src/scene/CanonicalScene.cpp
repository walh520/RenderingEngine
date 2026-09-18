#include "scene/CanonicalScene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace RenderingEngine::Scene
{
    namespace
    {
        constexpr std::array kCanonicalRegistry{
            SceneRegistryEntry{
                kWave1CanonicalTriangleStableId,
                "Canonical Triangle",
                kWave1CanonicalTriangleCameraStableId
            },
            SceneRegistryEntry{
                kWave2CanonicalCornellStableId,
                "Canonical Cornell Box",
                kWave2CanonicalCornellCameraStableId
            }
        };

        [[nodiscard]] constexpr AbiMat4Rows IdentityMatrix() noexcept
        {
            return {
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f }
            };
        }

        [[nodiscard]] bool IsFinite(const AbiFloat4& value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z)
                && std::isfinite(value.w);
        }

        [[nodiscard]] bool IsFinite(const AbiMat4Rows& value) noexcept
        {
            return IsFinite(value.row0)
                && IsFinite(value.row1)
                && IsFinite(value.row2)
                && IsFinite(value.row3);
        }

        [[nodiscard]] bool IsStableToken(const std::string_view value) noexcept
        {
            return !value.empty()
                && std::all_of(
                    value.begin(),
                    value.end(),
                    [](const char character)
                    {
                        const auto byte = static_cast<unsigned char>(character);
                        return byte >= 0x20u && byte != 0x7fu;
                    });
        }

        [[nodiscard]] CanonicalSceneValidation Failure(
            const CanonicalSceneError error,
            std::string reason)
        {
            return { error, std::move(reason) };
        }

        void HashByte(std::uint64_t& hash, const std::uint8_t value) noexcept
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }

        template <typename Value>
        void HashValue(std::uint64_t& hash, const Value& value) noexcept
        {
            const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(Value)>>(value);
            for (const std::uint8_t byte : bytes)
            {
                HashByte(hash, byte);
            }
        }

        void HashText(std::uint64_t& hash, const std::string_view text) noexcept
        {
            for (const char character : text)
            {
                HashByte(hash, static_cast<std::uint8_t>(character));
            }
            HashByte(hash, 0u);
        }

        struct CornellPoint
        {
            float x{};
            float y{};
            float z{};
        };

        [[nodiscard]] CornellPoint Subtract(
            const CornellPoint lhs,
            const CornellPoint rhs) noexcept
        {
            return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        [[nodiscard]] CornellPoint Cross(
            const CornellPoint lhs,
            const CornellPoint rhs) noexcept
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        [[nodiscard]] float Dot(
            const CornellPoint lhs,
            const CornellPoint rhs) noexcept
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        [[nodiscard]] CornellPoint ChooseTangent(const CornellPoint normal) noexcept
        {
            return std::abs(normal.x) < 0.9f
                ? CornellPoint{1.0f, 0.0f, 0.0f}
                : CornellPoint{0.0f, 0.0f, 1.0f};
        }

        void AppendVertex(
            CanonicalScene& scene,
            const CornellPoint position,
            const CornellPoint normal,
            const CornellPoint tangent,
            const float u,
            const float v)
        {
            if (scene.vertices.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                throw std::length_error("Canonical Cornell vertex identity exceeds uint32.");
            }
            const std::uint32_t vertexIndex = static_cast<std::uint32_t>(scene.vertices.size());
            scene.vertices.push_back({
                {position.x, position.y, position.z, 1.0f},
                {normal.x, normal.y, normal.z, 0.0f},
                {tangent.x, tangent.y, tangent.z, 1.0f},
                {u, v, 0.0f, 0.0f}
            });
            scene.indices.push_back(vertexIndex);
        }

        std::uint32_t AddCornellQuad(
            CanonicalScene& scene,
            const CornellPoint p0,
            CornellPoint p1,
            const CornellPoint p2,
            CornellPoint p3,
            const CornellPoint desiredNormal,
            const std::uint32_t materialId)
        {
            if (!(Dot(Cross(Subtract(p1, p0), Subtract(p2, p0)), desiredNormal) > 0.0f))
            {
                std::swap(p1, p3);
            }
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
            const std::uint32_t firstPrimitive = firstIndex / 3u;
            const CornellPoint tangent = ChooseTangent(desiredNormal);
            AppendVertex(scene, p0, desiredNormal, tangent, 0.0f, 0.0f);
            AppendVertex(scene, p1, desiredNormal, tangent, 1.0f, 0.0f);
            AppendVertex(scene, p2, desiredNormal, tangent, 1.0f, 1.0f);
            AppendVertex(scene, p0, desiredNormal, tangent, 0.0f, 0.0f);
            AppendVertex(scene, p2, desiredNormal, tangent, 1.0f, 1.0f);
            AppendVertex(scene, p3, desiredNormal, tangent, 0.0f, 1.0f);

            const auto minimum = CornellPoint{
                std::min({p0.x, p1.x, p2.x, p3.x}),
                std::min({p0.y, p1.y, p2.y, p3.y}),
                std::min({p0.z, p1.z, p2.z, p3.z})};
            const auto maximum = CornellPoint{
                std::max({p0.x, p1.x, p2.x, p3.x}),
                std::max({p0.y, p1.y, p2.y, p3.y}),
                std::max({p0.z, p1.z, p2.z, p3.z})};
            const std::uint32_t geometryId = static_cast<std::uint32_t>(scene.geometries.size());
            scene.geometries.push_back({
                {firstIndex, 6u, 0u, firstPrimitive},
                {geometryId, 0u, materialId, GeometryFlagOpaque},
                {minimum.x, minimum.y, minimum.z, 0.0f},
                {maximum.x, maximum.y, maximum.z, 0.0f}
            });
            return firstPrimitive;
        }

        void AddCornellBox(
            CanonicalScene& scene,
            const CornellPoint minimum,
            const CornellPoint maximum,
            const std::uint32_t materialId)
        {
            const CornellPoint p000{minimum.x, minimum.y, minimum.z};
            const CornellPoint p001{minimum.x, minimum.y, maximum.z};
            const CornellPoint p010{minimum.x, maximum.y, minimum.z};
            const CornellPoint p011{minimum.x, maximum.y, maximum.z};
            const CornellPoint p100{maximum.x, minimum.y, minimum.z};
            const CornellPoint p101{maximum.x, minimum.y, maximum.z};
            const CornellPoint p110{maximum.x, maximum.y, minimum.z};
            const CornellPoint p111{maximum.x, maximum.y, maximum.z};
            AddCornellQuad(scene, p000, p001, p011, p010, {-1.0f, 0.0f, 0.0f}, materialId);
            AddCornellQuad(scene, p100, p110, p111, p101, {1.0f, 0.0f, 0.0f}, materialId);
            AddCornellQuad(scene, p000, p100, p101, p001, {0.0f, -1.0f, 0.0f}, materialId);
            AddCornellQuad(scene, p010, p011, p111, p110, {0.0f, 1.0f, 0.0f}, materialId);
            AddCornellQuad(scene, p000, p010, p110, p100, {0.0f, 0.0f, -1.0f}, materialId);
            AddCornellQuad(scene, p001, p101, p111, p011, {0.0f, 0.0f, 1.0f}, materialId);
        }

        void FinalizeCanonicalScene(CanonicalScene& scene, const std::uint32_t sceneFlags) noexcept
        {
            scene.constants.counts0 = {
                static_cast<std::uint32_t>(scene.vertices.size()),
                static_cast<std::uint32_t>(scene.indices.size()),
                static_cast<std::uint32_t>(scene.geometries.size()),
                static_cast<std::uint32_t>(scene.instances.size())};
            scene.constants.counts1 = {
                static_cast<std::uint32_t>(scene.materials.size()),
                static_cast<std::uint32_t>(scene.lights.size()), 0u, 0u};
            scene.constants.versionFlags = {kAbiVersion, scene.generation, sceneFlags, 0u};
            scene.constants.environment = {kInvalidId, 0u, 0u, 0u};
        }
    }

    CanonicalScene BuildCanonicalTriangleScene()
    {
        CanonicalScene scene;
        scene.stableId = kWave1CanonicalTriangleStableId;
        scene.generation = kWave1CanonicalTriangleGeneration;
        scene.vertices = {
            {
                kWave1CanonicalTrianglePositions[0],
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f, 1.0f },
                { 0.0f, 0.0f, 0.0f, 0.0f }
            },
            {
                kWave1CanonicalTrianglePositions[1],
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f, 1.0f },
                { 1.0f, 0.0f, 0.0f, 0.0f }
            },
            {
                kWave1CanonicalTrianglePositions[2],
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f, 1.0f },
                { 0.5f, 1.0f, 0.0f, 0.0f }
            }
        };
        scene.indices.assign(
            kWave1CanonicalTriangleIndices.begin(),
            kWave1CanonicalTriangleIndices.end());
        scene.geometries = {{
            { 0u, 3u, 0u, 0u },
            { 0u, 0u, 0u, GeometryFlagOpaque },
            { -0.75f, -0.50f, 0.0f, 0.0f },
            { 0.75f, 0.75f, 0.0f, 0.0f }
        }};

        const AbiMat4Rows identity = IdentityMatrix();
        scene.instances = {{
            identity,
            identity,
            identity,
            { 0u, 1u, 0u, InstanceFlagVisible | InstanceFlagCastsShadow },
            { 0u, 0u, 0u, 0u }
        }};
        scene.materials = {{
            { 0.82f, 0.24f, 0.08f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.45f, 1.0f, 0.5f },
            { 0.0f, 1.5f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f, 0.0f },
            { kInvalidId, kInvalidId, kInvalidId, kInvalidId },
            { kInvalidId, kInvalidId, kInvalidId, kInvalidId },
            { MaterialModelMetallicRoughness, MaterialFlagNone, 0u, 0u }
        }};
        scene.lights = {{
            { 0.0f, 2.0f, 2.0f, 0.0f },
            { 0.0f, -0.70710678f, -0.70710678f, -1.0f },
            { 4.0f, 4.0f, 4.0f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { LightTypeDirectional, 0u, kInvalidId, kInvalidId },
            { kInvalidId, LightFlagEnabled | LightFlagDelta, 0u, 0u }
        }};
        scene.cameras = {{
            std::string{kWave1CanonicalTriangleCameraStableId},
            { 0.0f, 0.0f, 2.5f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 1.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            52.0f
        }};
        scene.constants.counts0 = { 3u, 3u, 1u, 1u };
        scene.constants.counts1 = { 1u, 1u, 0u, 0u };
        scene.constants.sceneBoundsMin = { -0.75f, -0.50f, 0.0f, 0.0f };
        scene.constants.sceneBoundsMax = { 0.75f, 0.75f, 0.0f, 0.0f };
        scene.constants.versionFlags = { kAbiVersion, scene.generation, SceneFlagNone, 0u };
        scene.constants.environment = { kInvalidId, 0u, 0u, 0u };
        return scene;
    }

    CanonicalScene BuildCanonicalCornellScene()
    {
        CanonicalScene scene;
        scene.stableId = kWave2CanonicalCornellStableId;
        scene.generation = kWave2CanonicalCornellGeneration;
        scene.materials = {
            {{0.73f, 0.73f, 0.73f, 1.0f}, {}, {0.0f, 0.8f, 1.0f, 0.5f},
             {0.0f, 1.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {MaterialModelMetallicRoughness, MaterialFlagNone, 0u, 0u}},
            {{0.63f, 0.065f, 0.05f, 1.0f}, {}, {0.0f, 0.8f, 1.0f, 0.5f},
             {0.0f, 1.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {MaterialModelMetallicRoughness, MaterialFlagNone, 1u, 0u}},
            {{0.14f, 0.45f, 0.091f, 1.0f}, {}, {0.0f, 0.8f, 1.0f, 0.5f},
             {0.0f, 1.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {MaterialModelMetallicRoughness, MaterialFlagNone, 2u, 0u}},
            {{0.0f, 0.0f, 0.0f, 1.0f}, {17.0f, 15.0f, 12.0f, 1.0f},
             {0.0f, 1.0f, 1.0f, 0.5f}, {0.0f, 1.5f, 0.0f, 0.0f},
             {1.0f, 1.0f, 1.0f, 0.0f},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
             {MaterialModelMetallicRoughness, MaterialFlagEmissive, 3u, 0u}}
        };

        constexpr float minimumX = -1.0f;
        constexpr float maximumX = 1.0f;
        constexpr float minimumY = -1.0f;
        constexpr float maximumY = 1.0f;
        constexpr float frontZ = -1.0f;
        constexpr float backZ = -3.0f;
        AddCornellQuad(scene, {minimumX, minimumY, frontZ}, {maximumX, minimumY, frontZ},
            {maximumX, minimumY, backZ}, {minimumX, minimumY, backZ},
            {0.0f, 1.0f, 0.0f}, 0u);
        AddCornellQuad(scene, {minimumX, maximumY, frontZ}, {minimumX, maximumY, backZ},
            {maximumX, maximumY, backZ}, {maximumX, maximumY, frontZ},
            {0.0f, -1.0f, 0.0f}, 0u);
        AddCornellQuad(scene, {minimumX, minimumY, backZ}, {maximumX, minimumY, backZ},
            {maximumX, maximumY, backZ}, {minimumX, maximumY, backZ},
            {0.0f, 0.0f, 1.0f}, 0u);
        AddCornellQuad(scene, {minimumX, minimumY, frontZ}, {minimumX, minimumY, backZ},
            {minimumX, maximumY, backZ}, {minimumX, maximumY, frontZ},
            {1.0f, 0.0f, 0.0f}, 1u);
        AddCornellQuad(scene, {maximumX, minimumY, frontZ}, {maximumX, maximumY, frontZ},
            {maximumX, maximumY, backZ}, {maximumX, minimumY, backZ},
            {-1.0f, 0.0f, 0.0f}, 2u);

        const std::uint32_t lightFirstPrimitive = AddCornellQuad(scene,
            {-0.30f, 0.985f, -2.30f}, {0.30f, 0.985f, -2.30f},
            {0.30f, 0.985f, -1.70f}, {-0.30f, 0.985f, -1.70f},
            {0.0f, -1.0f, 0.0f}, 3u);
        AddCornellBox(scene, {-0.72f, -0.999f, -2.55f}, {-0.10f, -0.30f, -1.72f}, 0u);
        AddCornellBox(scene, {0.16f, -0.999f, -2.75f}, {0.74f, 0.22f, -1.93f}, 0u);

        const AbiMat4Rows identity = IdentityMatrix();
        scene.instances = {{
            identity, identity, identity,
            {0u, static_cast<std::uint32_t>(scene.geometries.size()), 0u,
             InstanceFlagVisible | InstanceFlagCastsShadow},
            {0u, 0u, 0u, 0u}}};
        scene.lights = {
            {{0.0f, 0.985f, -2.10f, 0.0f}, {0.0f, -1.0f, 0.0f, -1.0f},
             {17.0f, 15.0f, 12.0f, 1.0f}, {0.0f, 0.0f, 0.18f, 0.0f},
             {LightTypeEmissiveTriangle, 0u, 0u, lightFirstPrimitive},
             {kInvalidId, LightFlagEnabled, 0u, 0u}},
            {{0.0f, 0.985f, -1.90f, 0.0f}, {0.0f, -1.0f, 0.0f, -1.0f},
             {17.0f, 15.0f, 12.0f, 1.0f}, {0.0f, 0.0f, 0.18f, 0.0f},
             {LightTypeEmissiveTriangle, 1u, 0u, lightFirstPrimitive + 1u},
             {kInvalidId, LightFlagEnabled, 0u, 0u}}
        };
        scene.cameras = {{
            std::string{kWave2CanonicalCornellCameraStableId},
            {0.0f, 0.0f, 0.15f, 1.0f},
            {0.0f, 0.0f, -2.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            39.0f
        }};
        scene.constants.sceneBoundsMin = {minimumX, minimumY, backZ, 0.0f};
        scene.constants.sceneBoundsMax = {maximumX, maximumY, frontZ, 0.0f};
        FinalizeCanonicalScene(scene, SceneFlagNone);
        return scene;
    }

    CanonicalSceneValidation ValidateCanonicalScene(const CanonicalScene& scene)
    {
        if (!IsStableToken(scene.stableId) || scene.generation == 0u)
        {
            return Failure(CanonicalSceneError::MissingIdentity,
                "A canonical scene requires a stable ID and non-zero generation.");
        }
        if (scene.vertices.empty()
            || scene.indices.empty()
            || scene.indices.size() % 3u != 0u
            || scene.geometries.empty()
            || scene.instances.empty()
            || scene.materials.empty()
            || scene.cameras.empty())
        {
            return Failure(CanonicalSceneError::InvalidCount,
                "Canonical mesh, geometry, instance, material, and camera arrays must be non-empty.");
        }
        if (scene.constants.counts0.x != scene.vertices.size()
            || scene.constants.counts0.y != scene.indices.size()
            || scene.constants.counts0.z != scene.geometries.size()
            || scene.constants.counts0.w != scene.instances.size()
            || scene.constants.counts1.x != scene.materials.size()
            || scene.constants.counts1.y != scene.lights.size()
            || scene.constants.versionFlags.x != kAbiVersion
            || scene.constants.versionFlags.y != scene.generation)
        {
            return Failure(CanonicalSceneError::InvalidCount,
                "Scene constants do not match canonical array counts or version identity.");
        }
        if (!IsFinite(scene.constants.sceneBoundsMin)
            || !IsFinite(scene.constants.sceneBoundsMax)
            || scene.constants.sceneBoundsMin.x > scene.constants.sceneBoundsMax.x
            || scene.constants.sceneBoundsMin.y > scene.constants.sceneBoundsMax.y
            || scene.constants.sceneBoundsMin.z > scene.constants.sceneBoundsMax.z)
        {
            return Failure(CanonicalSceneError::InvalidBounds,
                "Canonical scene bounds must be finite and ordered.");
        }

        for (const GpuVertexV0& vertex : scene.vertices)
        {
            if (!IsFinite(vertex.position)
                || !IsFinite(vertex.normal)
                || !IsFinite(vertex.tangent)
                || !IsFinite(vertex.texcoord0))
            {
                return Failure(CanonicalSceneError::InvalidNumericValue,
                    "Canonical vertices must contain finite values.");
            }
        }
        for (const std::uint32_t index : scene.indices)
        {
            if (index >= scene.vertices.size())
            {
                return Failure(CanonicalSceneError::InvalidIndex,
                    "A canonical triangle index is outside the vertex array.");
            }
        }
        for (std::size_t offset = 0; offset < scene.indices.size(); offset += 3u)
        {
            const AbiFloat4& a = scene.vertices[scene.indices[offset]].position;
            const AbiFloat4& b = scene.vertices[scene.indices[offset + 1u]].position;
            const AbiFloat4& c = scene.vertices[scene.indices[offset + 2u]].position;
            const double abX = static_cast<double>(b.x) - a.x;
            const double abY = static_cast<double>(b.y) - a.y;
            const double abZ = static_cast<double>(b.z) - a.z;
            const double acX = static_cast<double>(c.x) - a.x;
            const double acY = static_cast<double>(c.y) - a.y;
            const double acZ = static_cast<double>(c.z) - a.z;
            const double crossX = abY * acZ - abZ * acY;
            const double crossY = abZ * acX - abX * acZ;
            const double crossZ = abX * acY - abY * acX;
            if (crossX * crossX + crossY * crossY + crossZ * crossZ <= 1.0e-20)
            {
                return Failure(CanonicalSceneError::DegenerateTriangle,
                    "Canonical triangle geometry must be non-degenerate.");
            }
        }

        std::uint64_t expectedPrimitiveId = 0u;
        for (std::size_t index = 0; index < scene.geometries.size(); ++index)
        {
            const GpuGeometryV0& geometry = scene.geometries[index];
            if (geometry.identity.x != index
                || geometry.identity.z >= scene.materials.size()
                || geometry.indexRange.y == 0u
                || geometry.indexRange.y % 3u != 0u
                || geometry.indexRange.x > scene.indices.size()
                || geometry.indexRange.y > scene.indices.size() - geometry.indexRange.x
                || !IsFinite(geometry.localBoundsMin)
                || !IsFinite(geometry.localBoundsMax)
                || geometry.localBoundsMin.x > geometry.localBoundsMax.x
                || geometry.localBoundsMin.y > geometry.localBoundsMax.y
                || geometry.localBoundsMin.z > geometry.localBoundsMax.z
                || expectedPrimitiveId > std::numeric_limits<std::uint32_t>::max()
                || geometry.indexRange.w != expectedPrimitiveId)
            {
                return Failure(CanonicalSceneError::InvalidStableIdentity,
                    "Geometry identity, material, range, or bounds are invalid.");
            }
            expectedPrimitiveId += geometry.indexRange.y / 3u;
        }
        for (std::size_t index = 0; index < scene.instances.size(); ++index)
        {
            const GpuInstanceV0& instance = scene.instances[index];
            if (instance.metadata.z != index
                || instance.metadata.x >= scene.geometries.size()
                || instance.metadata.y == 0u
                || instance.metadata.y > scene.geometries.size() - instance.metadata.x
                || !IsFinite(instance.objectToWorld)
                || !IsFinite(instance.worldToObject)
                || !IsFinite(instance.previousObjectToWorld))
            {
                return Failure(CanonicalSceneError::InvalidStableIdentity,
                    "Instance identity, geometry range, or current/previous transforms are invalid.");
            }
        }
        for (std::size_t index = 0; index < scene.materials.size(); ++index)
        {
            const GpuMaterialV0& material = scene.materials[index];
            if (material.metadata.z != index
                || !IsFinite(material.baseColorFactor)
                || !IsFinite(material.emissiveFactorStrength)
                || !IsFinite(material.surfaceParams)
                || !IsFinite(material.transmissionParams)
                || !IsFinite(material.attenuationColorDistance))
            {
                return Failure(CanonicalSceneError::InvalidStableIdentity,
                    "Material stable identity or numeric values are invalid.");
            }
        }
        for (std::size_t index = 0; index < scene.lights.size(); ++index)
        {
            const GpuLightV0& light = scene.lights[index];
            if (light.identity.y != index
                || !IsFinite(light.positionRange)
                || !IsFinite(light.directionCosOuter)
                || !IsFinite(light.radianceScale)
                || !IsFinite(light.shapeParams))
            {
                return Failure(CanonicalSceneError::InvalidStableIdentity,
                    "Light stable identity or numeric values are invalid.");
            }
        }
        for (std::size_t index = 0; index < scene.cameras.size(); ++index)
        {
            const CameraPreset& camera = scene.cameras[index];
            if (!IsStableToken(camera.stableId)
                || !IsFinite(camera.eye)
                || !IsFinite(camera.target)
                || !IsFinite(camera.up)
                || !std::isfinite(camera.verticalFovDegrees)
                || camera.verticalFovDegrees <= 0.0f
                || camera.verticalFovDegrees >= 180.0f)
            {
                return Failure(CanonicalSceneError::InvalidCamera,
                    "Camera presets require a stable identity, finite vectors, and a valid vertical FOV.");
            }
            const auto duplicate = std::find_if(
                scene.cameras.begin(),
                scene.cameras.begin() + static_cast<std::ptrdiff_t>(index),
                [&camera](const CameraPreset& candidate)
                {
                    return candidate.stableId == camera.stableId;
                });
            if (duplicate != scene.cameras.begin() + static_cast<std::ptrdiff_t>(index))
            {
                return Failure(CanonicalSceneError::InvalidCamera,
                    "Camera stable identities must be unique within a scene.");
            }
        }
        return {};
    }

    std::uint64_t CanonicalSceneFingerprint(const CanonicalScene& scene) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        HashText(hash, scene.stableId);
        HashValue(hash, scene.generation);
        HashValue(hash, scene.constants);
        for (const GpuVertexV0& value : scene.vertices) HashValue(hash, value);
        for (const std::uint32_t value : scene.indices) HashValue(hash, value);
        for (const GpuGeometryV0& value : scene.geometries) HashValue(hash, value);
        for (const GpuInstanceV0& value : scene.instances) HashValue(hash, value);
        for (const GpuMaterialV0& value : scene.materials) HashValue(hash, value);
        for (const GpuLightV0& value : scene.lights) HashValue(hash, value);
        for (const CameraPreset& camera : scene.cameras)
        {
            HashText(hash, camera.stableId);
            HashValue(hash, camera.eye);
            HashValue(hash, camera.target);
            HashValue(hash, camera.up);
            HashValue(hash, camera.verticalFovDegrees);
        }
        return hash;
    }

    CanonicalSceneView MakeCanonicalSceneView(const CanonicalScene& scene) noexcept
    {
        return {
            scene.stableId,
            CanonicalSceneFingerprint(scene),
            scene.generation,
            &scene.constants,
            scene.vertices,
            scene.indices,
            scene.geometries,
            scene.instances,
            scene.materials,
            scene.lights,
            scene.cameras
        };
    }

    std::span<const SceneRegistryEntry> Wave1SceneRegistry() noexcept
    {
        return std::span<const SceneRegistryEntry>{kCanonicalRegistry.data(), 1u};
    }

    std::span<const SceneRegistryEntry> CanonicalSceneRegistry() noexcept
    {
        return kCanonicalRegistry;
    }

    const SceneRegistryEntry* FindWave1Scene(const std::string_view stableId) noexcept
    {
        const auto found = std::find_if(
            kCanonicalRegistry.begin(),
            kCanonicalRegistry.begin() + 1,
            [stableId](const SceneRegistryEntry& entry)
            {
                return entry.stableId == stableId;
            });
        return found == kCanonicalRegistry.begin() + 1 ? nullptr : &*found;
    }

    const SceneRegistryEntry* FindCanonicalScene(const std::string_view stableId) noexcept
    {
        const auto found = std::find_if(
            kCanonicalRegistry.begin(),
            kCanonicalRegistry.end(),
            [stableId](const SceneRegistryEntry& entry)
            {
                return entry.stableId == stableId;
            });
        return found == kCanonicalRegistry.end() ? nullptr : &*found;
    }
}
