#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace RenderingEngine
{
    struct ArtifactLayout
    {
        std::filesystem::path artifactRoot;
        std::filesystem::path runDirectory;
        std::filesystem::path capturesDirectory;
        std::filesystem::path benchmarksDirectory;
        std::filesystem::path referencesDirectory;
        std::filesystem::path logsDirectory;
        std::filesystem::path metadataFile;
    };

    [[nodiscard]] std::string NormalizeRunIdentifier(std::string_view identifier);

    // This function only plans deterministic paths. It never creates a directory or file.
    [[nodiscard]] ArtifactLayout ResolveArtifactLayout(
        const std::filesystem::path& artifactRoot,
        std::string_view runIdentifier);
}
