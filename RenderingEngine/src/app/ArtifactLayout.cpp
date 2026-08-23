#include "app/ArtifactLayout.hpp"

namespace RenderingEngine
{
    std::string NormalizeRunIdentifier(std::string_view identifier)
    {
        std::string normalized;
        normalized.reserve(identifier.size());
        for (const unsigned char character : identifier)
        {
            const bool isAsciiLetter = (character >= 'A' && character <= 'Z')
                || (character >= 'a' && character <= 'z');
            const bool isAsciiDigit = character >= '0' && character <= '9';
            if (isAsciiLetter || isAsciiDigit || character == '-' || character == '_' || character == '.')
            {
                normalized.push_back(static_cast<char>(character));
            }
            else
            {
                normalized.push_back('_');
            }
        }

        while (!normalized.empty() && normalized.front() == '.')
        {
            normalized.erase(normalized.begin());
        }
        if (normalized.empty())
        {
            return "manual";
        }
        return normalized;
    }

    ArtifactLayout ResolveArtifactLayout(
        const std::filesystem::path& artifactRoot,
        std::string_view runIdentifier)
    {
        const std::filesystem::path root = (artifactRoot.empty()
            ? std::filesystem::path(".artifacts")
            : artifactRoot).lexically_normal();
        const std::filesystem::path runDirectory =
            (root / NormalizeRunIdentifier(runIdentifier)).lexically_normal();

        ArtifactLayout layout;
        layout.artifactRoot = root;
        layout.runDirectory = runDirectory;
        layout.capturesDirectory = runDirectory / "captures";
        layout.benchmarksDirectory = runDirectory / "benchmarks";
        layout.referencesDirectory = runDirectory / "references";
        layout.logsDirectory = runDirectory / "logs";
        layout.metadataFile = runDirectory / "metadata.json";
        return layout;
    }
}
