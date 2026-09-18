#pragma once

#include "contracts/SceneAbiV0.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Scene
{
    using namespace Contracts::AbiV0;

    inline constexpr std::string_view kWave1CanonicalTriangleStableId =
        "canonical-triangle";
    inline constexpr std::string_view kWave1CanonicalTriangleCameraStableId =
        "camera:canonical-triangle";
    inline constexpr std::uint32_t kWave1CanonicalTriangleGeneration = 1u;
    inline constexpr std::string_view kWave2CanonicalCornellStableId =
        "canonical-cornell";
    inline constexpr std::string_view kWave2CanonicalCornellCameraStableId =
        "camera:canonical-cornell";
    inline constexpr std::uint32_t kWave2CanonicalCornellGeneration = 1u;
    inline constexpr std::array<AbiFloat4, 3> kWave1CanonicalTrianglePositions{
        AbiFloat4{ -0.75f, -0.50f, 0.0f, 1.0f },
        AbiFloat4{ 0.75f, -0.50f, 0.0f, 1.0f },
        AbiFloat4{ 0.0f, 0.75f, 0.0f, 1.0f }
    };
    inline constexpr std::array<std::uint32_t, 3> kWave1CanonicalTriangleIndices{
        0u, 1u, 2u
    };

#if defined(_MSC_VER)
#pragma warning(push)
    // CameraPreset intentionally aggregates 16-byte ABI vectors with a
    // standard-library string. Its host-only layout is not serialized.
#pragma warning(disable: 4324)
#endif
    struct CameraPreset
    {
        std::string stableId;
        AbiFloat4 eye;
        AbiFloat4 target;
        AbiFloat4 up;
        float verticalFovDegrees = 52.0f;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    struct CanonicalScene
    {
        std::string stableId;
        std::uint32_t generation = 1;
        GpuSceneConstantsV0 constants{};
        std::vector<GpuVertexV0> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<GpuGeometryV0> geometries;
        std::vector<GpuInstanceV0> instances;
        std::vector<GpuMaterialV0> materials;
        std::vector<GpuLightV0> lights;
        std::vector<CameraPreset> cameras;
    };

    // Non-owning, immutable upload/build input for Wave 2 traversal backends.
    // ValidateCanonicalScene must succeed before creating the view, and the
    // owner must keep CanonicalScene alive for the lifetime of the view.
    struct CanonicalSceneView
    {
        std::string_view stableId;
        std::uint64_t fingerprint = 0u;
        std::uint32_t generation = 0u;
        const GpuSceneConstantsV0* constants = nullptr;
        std::span<const GpuVertexV0> vertices;
        std::span<const std::uint32_t> indices;
        std::span<const GpuGeometryV0> geometries;
        std::span<const GpuInstanceV0> instances;
        std::span<const GpuMaterialV0> materials;
        std::span<const GpuLightV0> lights;
        std::span<const CameraPreset> cameras;
    };

    enum class CanonicalSceneError : std::uint8_t
    {
        None = 0,
        MissingIdentity,
        InvalidCount,
        InvalidNumericValue,
        InvalidIndex,
        DegenerateTriangle,
        InvalidStableIdentity,
        InvalidBounds,
        InvalidCamera
    };

    struct CanonicalSceneValidation
    {
        CanonicalSceneError error = CanonicalSceneError::None;
        std::string reason;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == CanonicalSceneError::None;
        }
    };

    struct SceneRegistryEntry
    {
        std::string_view stableId;
        std::string_view displayName;
        std::string_view fixedCameraStableId;
    };

    [[nodiscard]] CanonicalScene BuildCanonicalTriangleScene();
    [[nodiscard]] CanonicalScene BuildCanonicalCornellScene();
    [[nodiscard]] CanonicalSceneValidation ValidateCanonicalScene(
        const CanonicalScene& scene);
    [[nodiscard]] std::uint64_t CanonicalSceneFingerprint(
        const CanonicalScene& scene) noexcept;
    [[nodiscard]] CanonicalSceneView MakeCanonicalSceneView(
        const CanonicalScene& scene) noexcept;
    [[nodiscard]] std::span<const SceneRegistryEntry> Wave1SceneRegistry() noexcept;
    [[nodiscard]] std::span<const SceneRegistryEntry> CanonicalSceneRegistry() noexcept;
    [[nodiscard]] const SceneRegistryEntry* FindWave1Scene(
        std::string_view stableId) noexcept;
    [[nodiscard]] const SceneRegistryEntry* FindCanonicalScene(
        std::string_view stableId) noexcept;
}
