#pragma once

#include "demos/ShowcaseEvidence.hpp"
#include "demos/ShowcaseProgram.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Demos
{
    enum class ShowcaseReportError : std::uint8_t
    {
        None = 0,
        InvalidInput,
        InvalidEnum,
        DuplicateStableId,
        InvalidProvenance,
        SyntheticLiveConflict,
        InvalidMetrics,
        ExtentMismatch,
        SizeOverflow
    };

    struct ShowcaseReportStatus
    {
        ShowcaseReportError error = ShowcaseReportError::None;
        std::string message;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == ShowcaseReportError::None;
        }
    };

    struct ShowcaseTextResult : ShowcaseReportStatus
    {
        std::string text;
    };

    // CSV output is deterministic and uses RFC 4180 quoting plus CRLF rows.
    // Inputs are sorted by their stable tokens; duplicate tokens are rejected.
    [[nodiscard]] ShowcaseTextResult BuildAlgorithmCompletionMatrixCsv(
        std::span<const AlgorithmCompletionEntry> entries);
    [[nodiscard]] ShowcaseTextResult BuildAssetLicenseCsv(
        std::span<const AssetLicenseEntry> entries);
    [[nodiscard]] ShowcaseTextResult BuildFinalVideoShotListCsv(
        std::span<const FinalVideoShot> shots);

    // Classification is independent of the provider-reported source. The
    // serializer validates both so SyntheticTest cannot be presented as live.
    enum class ReportEvidenceClass : std::uint8_t
    {
        LiveRuntime = 0,
        ImportedArtifact,
        SyntheticTest
    };

    [[nodiscard]] std::string_view ReportEvidenceClassToken(
        ReportEvidenceClass classification) noexcept;

    struct ReferenceImageIdentity
    {
        ReportEvidenceClass classification = ReportEvidenceClass::ImportedArtifact;
        std::string stableId;
        EvidenceProvenance provenance;
        std::uint64_t frameIndex = 0;
        std::uint64_t configGeneration = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::string sceneStableId;
        std::string cameraPresetToken;
        std::uint64_t baseSeed = 0;
        std::uint64_t sampleIndex = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::uint64_t accumulatedSamplesPerPixel = 0;
    };

    struct ReferenceComparisonRecord
    {
        ReferenceImageIdentity candidate;
        ReferenceImageIdentity reference;
        ImageComparisonResult comparison;
    };

    // Successful comparisons only. Positive-infinite PSNR is represented as a
    // JSON null plus psnr_positive_infinity=true; JSON never receives NaN/Inf.
    [[nodiscard]] ShowcaseTextResult BuildReferenceComparisonJson(
        std::span<const ReferenceComparisonRecord> comparisons);

    enum class ShowcaseEvidenceBoundary : std::uint8_t
    {
        Static = 0,
        Build,
        Runtime,
        Numeric,
        Visual,
        Performance
    };

    enum class ShowcaseEvidenceState : std::uint8_t
    {
        Unavailable = 0,
        NotRun,
        Passed,
        Failed
    };

    struct ShowcaseEvidenceBoundaryEntry
    {
        ShowcaseEvidenceBoundary boundary = ShowcaseEvidenceBoundary::Static;
        ShowcaseEvidenceState state = ShowcaseEvidenceState::Unavailable;
        ReportEvidenceClass classification = ReportEvidenceClass::ImportedArtifact;
        EvidenceProvenance provenance;
        std::string summary;
        std::string reason;
    };

    struct ShowcaseMarkdownReportInput
    {
        std::string title;
        std::span<const ShowcaseEvidenceBoundaryEntry> evidence;
        std::span<const std::string> knownLimitations;

        // These spans may be empty while providers are not integrated. The
        // report then emits an explicit unavailable item rather than a claim.
        std::span<const ResolvedShowcaseSceneCard> scenes;
        std::span<const AlgorithmCompletionEntry> algorithmCompletion;
        std::span<const ManyLightsComparisonRequest> manyLightsComparisons;
        std::span<const CaptureShotPlanEntry> captureShots;
        std::span<const FinalVideoShot> finalVideoShots;
        const AssetLicenseGate* licenseGate = nullptr;
    };

    // Always renders six separate evidence sections, known limitations, and a
    // stable list of unavailable program items with their reasons.
    [[nodiscard]] ShowcaseTextResult BuildShowcaseMarkdownReport(
        const ShowcaseMarkdownReportInput& input);
}
