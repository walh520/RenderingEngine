#pragma once

#include "app/ArtifactLayout.hpp"
#include "app/RuntimeConfig.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Demos
{
    struct CaptureImageView
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        // Both buffers are required and contain width * height * 4 interleaved
        // components. The caller owns the display transform that produces previewRgba8;
        // this writer never tonemaps or quantizes linearRgba implicitly.
        std::span<const float> linearRgba;
        std::span<const std::uint8_t> previewRgba8;
    };

    struct CaptureContractVersion
    {
        std::string name;
        std::string version;
    };

    struct CaptureNamedHash
    {
        std::string name;
        std::string hash;
    };

    struct CaptureAdditionalArtifact
    {
        // Normalized UTF-8 path relative to the run directory.  L10 evidence
        // extensions live under benchmarks/, references/, logs/, or reports/.
        std::string relativePath;
        std::span<const std::uint8_t> bytes;
    };

    enum class CaptureEvidenceOrigin : std::uint8_t
    {
        LiveRuntime,
        ImportedArtifact,
        SyntheticTest
    };

    enum class CaptureEvidenceAvailability : std::uint8_t
    {
        Unavailable,
        Pending,
        Fresh,
        Stale,
        Invalid
    };

    struct CaptureEvidenceIdentity
    {
        std::string providerId;
        CaptureEvidenceOrigin origin = CaptureEvidenceOrigin::LiveRuntime;
        CaptureEvidenceAvailability availability = CaptureEvidenceAvailability::Unavailable;
        std::uint64_t frameIndex = 0;
        std::uint64_t sampleIndex = 0;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string reason;
    };

    // Every environment, source, scene, and transform fact is caller-supplied.
    // The writer deliberately does not query Git, Vulkan, the GPU, or the clock,
    // because doing so could make metadata describe a different render process.
    struct CaptureMetadata
    {
        std::string schemaVersion;
        std::vector<CaptureContractVersion> contractVersions;
        CaptureEvidenceIdentity evidenceIdentity;

        std::string gitCommit;
        bool dirtyWorktree = false;
        std::string executableConfiguration;
        std::string buildIdentity;

        std::string gpuName;
        std::string driverVersion;
        std::string vulkanApiVersion;
        std::string vulkanSdkVersion;

        std::string sceneId;
        std::string sceneGeneration;
        std::string sceneHash;
        std::vector<CaptureNamedHash> assetHashes;
        std::string cameraPreset;

        // Keeping both tuples prevents a capability fallback from being hidden.
        // They may be equal when no fallback or normalization change occurred.
        RuntimeConfig requestedRuntimeConfig;
        RuntimeConfig effectiveRuntimeConfig;

        std::vector<CaptureNamedHash> shaderHashes;
        std::string renderStartedAtUtc;
        std::string renderCompletedAtUtc;
        std::string linearColorSpace;
        std::string previewDisplayTransform;

        // Paths are relative to the run directory and use '/' separators.
        std::vector<std::string> requestedArtifacts = {
            "captures/image.exr",
            "captures/preview.png",
            "metadata.json"
        };
    };

    enum class CaptureBundleError
    {
        None = 0,
        InvalidImage,
        InvalidMetadata,
        InvalidLayout,
        RunDirectoryCollision,
        DirectoryCreationFailed,
        ExrEncodingFailed,
        PngEncodingFailed,
        FileWriteFailed,
        MetadataCommitFailed,
        UnexpectedFailure
    };

    struct CaptureBundleResult
    {
        CaptureBundleError error = CaptureBundleError::None;
        std::string message;
        std::filesystem::path runDirectory;
        std::filesystem::path linearImageFile;
        std::filesystem::path previewImageFile;
        std::filesystem::path metadataFile;
        std::vector<std::filesystem::path> additionalFiles;
        bool rollbackAttempted = false;
        bool rollbackSucceeded = true;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == CaptureBundleError::None;
        }
    };

    // Writes captures/image.exr and captures/preview.png first, then publishes
    // metadata.json as the final completion marker. Existing run directories are
    // never overwritten. Operational failures are returned; allocation failures
    // and other exceptions that prevent construction of a result may still escape.
    [[nodiscard]] CaptureBundleResult WriteCaptureBundle(
        const ArtifactLayout& layout,
        const CaptureImageView& image,
        const CaptureMetadata& metadata);

    // General all-wave transaction.  Every requested evidence file is written
    // before metadata.json is atomically published as the completion marker.
    // The original three-file overload remains the capture-only convenience API.
    [[nodiscard]] CaptureBundleResult WriteShowcaseBundle(
        const ArtifactLayout& layout,
        const CaptureImageView& image,
        const CaptureMetadata& metadata,
        std::span<const CaptureAdditionalArtifact> additionalArtifacts);
}
