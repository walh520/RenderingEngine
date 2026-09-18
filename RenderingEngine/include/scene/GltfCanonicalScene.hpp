#pragma once

#include "scene/CanonicalScene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace RenderingEngine::Scene
{
    enum class GltfCanonicalSceneError : std::uint8_t
    {
        None = 0,
        FileNotFound,
        ParseFailed,
        BufferLoadFailed,
        UnsupportedFeature,
        InvalidStructure,
        InvalidCount,
        InvalidNumericValue,
        InvalidIndex,
        DegenerateTriangle,
        InvalidTransform,
        InvalidIdentity
    };

    struct GltfCanonicalSceneLoadOptions
    {
        std::string stableId;
        std::uint32_t generation = 1u;
    };

    struct GltfCanonicalSceneLoadResult
    {
        CanonicalScene scene{};
        GltfCanonicalSceneError error = GltfCanonicalSceneError::None;
        std::string reason;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == GltfCanonicalSceneError::None;
        }
    };

    [[nodiscard]] GltfCanonicalSceneLoadResult LoadGltfCanonicalScene(
        const std::filesystem::path& path,
        GltfCanonicalSceneLoadOptions options = {});
}
