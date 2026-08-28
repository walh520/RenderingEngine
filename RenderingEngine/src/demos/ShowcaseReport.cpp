#include "demos/ShowcaseReport.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace RenderingEngine::Demos
{
    namespace
    {
        constexpr std::size_t kEvidenceBoundaryCount = 6u;

        struct UnavailableItem
        {
            std::string category;
            std::string stableId;
            std::string label;
            std::string reason;
        };

        struct RetainedEvidenceIdentityRow
        {
            std::string_view category;
            std::string_view stableId;
            const ShowcaseEvidenceIdentity* identity = nullptr;
        };

        [[nodiscard]] ShowcaseTextResult MakeError(
            ShowcaseReportError error,
            std::string message)
        {
            ShowcaseTextResult result;
            result.error = error;
            result.message = std::move(message);
            return result;
        }

        [[nodiscard]] ShowcaseReportStatus MakeStatusError(
            ShowcaseReportError error,
            std::string message)
        {
            ShowcaseReportStatus result;
            result.error = error;
            result.message = std::move(message);
            return result;
        }

        [[nodiscard]] bool IsStableToken(std::string_view value) noexcept
        {
            if (value.empty())
            {
                return false;
            }
            for (const unsigned char character : value)
            {
                const bool alphaNumeric =
                    (character >= static_cast<unsigned char>('a')
                        && character <= static_cast<unsigned char>('z'))
                    || (character >= static_cast<unsigned char>('A')
                        && character <= static_cast<unsigned char>('Z'))
                    || (character >= static_cast<unsigned char>('0')
                        && character <= static_cast<unsigned char>('9'));
                if (!alphaNumeric
                    && character != static_cast<unsigned char>('-')
                    && character != static_cast<unsigned char>('_')
                    && character != static_cast<unsigned char>('.')
                    && character != static_cast<unsigned char>(':')
                    && character != static_cast<unsigned char>('/'))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsValidUtf8(std::string_view value) noexcept
        {
            std::size_t index = 0u;
            while (index < value.size())
            {
                const auto byte = static_cast<unsigned char>(value[index]);
                if (byte <= 0x7fu)
                {
                    ++index;
                    continue;
                }

                std::size_t continuationCount = 0u;
                std::uint32_t codePoint = 0u;
                if (byte >= 0xc2u && byte <= 0xdfu)
                {
                    continuationCount = 1u;
                    codePoint = byte & 0x1fu;
                }
                else if (byte >= 0xe0u && byte <= 0xefu)
                {
                    continuationCount = 2u;
                    codePoint = byte & 0x0fu;
                }
                else if (byte >= 0xf0u && byte <= 0xf4u)
                {
                    continuationCount = 3u;
                    codePoint = byte & 0x07u;
                }
                else
                {
                    return false;
                }

                if (continuationCount > value.size() - index - 1u)
                {
                    return false;
                }
                for (std::size_t offset = 1u; offset <= continuationCount; ++offset)
                {
                    const auto continuation =
                        static_cast<unsigned char>(value[index + offset]);
                    if ((continuation & 0xc0u) != 0x80u)
                    {
                        return false;
                    }
                    codePoint = (codePoint << 6u) | (continuation & 0x3fu);
                }

                const bool overlong =
                    (continuationCount == 1u && codePoint < 0x80u)
                    || (continuationCount == 2u && codePoint < 0x800u)
                    || (continuationCount == 3u && codePoint < 0x10000u);
                const bool surrogate = codePoint >= 0xd800u && codePoint <= 0xdfffu;
                if (overlong || surrogate || codePoint > 0x10ffffu)
                {
                    return false;
                }
                index += continuationCount + 1u;
            }
            return true;
        }

        [[nodiscard]] ShowcaseReportStatus ValidateText(
            std::string_view value,
            std::string_view fieldName,
            bool allowEmpty = false)
        {
            if (!allowEmpty && value.empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(fieldName) + " must not be empty.");
            }
            if (!IsValidUtf8(value))
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(fieldName) + " must be valid UTF-8.");
            }
            return {};
        }

        [[nodiscard]] std::string_view AlgorithmStateToken(
            AlgorithmCompletionState state) noexcept
        {
            switch (state)
            {
            case AlgorithmCompletionState::Unavailable: return "unavailable";
            case AlgorithmCompletionState::Declared: return "declared";
            case AlgorithmCompletionState::Implemented: return "implemented";
            case AlgorithmCompletionState::RuntimeValidated: return "runtime-validated";
            case AlgorithmCompletionState::VisualAccepted: return "visual-accepted";
            }
            return {};
        }

        [[nodiscard]] std::string_view FinalVideoStateToken(
            FinalVideoShotState state) noexcept
        {
            switch (state)
            {
            case FinalVideoShotState::Unavailable: return "unavailable";
            case FinalVideoShotState::Ready: return "ready";
            case FinalVideoShotState::Capturing: return "capturing";
            case FinalVideoShotState::Captured: return "captured";
            case FinalVideoShotState::Approved: return "approved";
            case FinalVideoShotState::Failed: return "failed";
            }
            return {};
        }

        [[nodiscard]] std::string_view EvidenceStateToken(
            ShowcaseEvidenceState state) noexcept
        {
            switch (state)
            {
            case ShowcaseEvidenceState::Unavailable: return "unavailable";
            case ShowcaseEvidenceState::NotRun: return "not-run";
            case ShowcaseEvidenceState::Passed: return "passed";
            case ShowcaseEvidenceState::Failed: return "failed";
            }
            return {};
        }

        [[nodiscard]] std::string_view EvidenceBoundaryTitle(
            ShowcaseEvidenceBoundary boundary) noexcept
        {
            switch (boundary)
            {
            case ShowcaseEvidenceBoundary::Static: return "Static evidence";
            case ShowcaseEvidenceBoundary::Build: return "Build evidence";
            case ShowcaseEvidenceBoundary::Runtime: return "Runtime evidence";
            case ShowcaseEvidenceBoundary::Numeric: return "Numeric evidence";
            case ShowcaseEvidenceBoundary::Visual: return "Visual evidence";
            case ShowcaseEvidenceBoundary::Performance: return "Performance evidence";
            }
            return {};
        }

        [[nodiscard]] bool IsKnownAvailability(Availability availability) noexcept
        {
            switch (availability)
            {
            case Availability::Available:
            case Availability::Unavailable:
                return true;
            }
            return false;
        }

        void AppendCsvField(std::string& output, std::string_view value)
        {
            const bool needsQuotes =
                value.find_first_of(",\"\r\n") != std::string_view::npos;
            if (!needsQuotes)
            {
                output.append(value);
                return;
            }

            output.push_back('"');
            for (std::size_t index = 0u; index < value.size(); ++index)
            {
                const char character = value[index];
                if (character == '"')
                {
                    output.append("\"\"");
                }
                else if (character == '\r')
                {
                    if (index + 1u < value.size() && value[index + 1u] == '\n')
                    {
                        ++index;
                    }
                    output.append("\r\n");
                }
                else if (character == '\n')
                {
                    output.append("\r\n");
                }
                else
                {
                    output.push_back(character);
                }
            }
            output.push_back('"');
        }

        template <typename Integer>
        void AppendInteger(std::string& output, Integer value)
        {
            char buffer[32]{};
            const auto conversion = std::to_chars(
                std::begin(buffer), std::end(buffer), value);
            output.append(buffer, conversion.ptr);
        }

        void AppendEvidenceIdentityCsv(
            std::string& output,
            const std::optional<ShowcaseEvidenceIdentity>& identity)
        {
            const auto appendText = [&output](std::string_view value)
            {
                output.push_back(',');
                AppendCsvField(output, value);
            };
            const auto appendOptionalInteger = [&output](
                const std::optional<std::uint64_t>& value)
            {
                output.push_back(',');
                if (value.has_value())
                {
                    AppendInteger(output, *value);
                }
            };

            if (!identity.has_value())
            {
                for (std::size_t index = 0; index < 10u; ++index)
                {
                    output.push_back(',');
                }
                return;
            }

            appendText(identity->artifactIdentity);
            appendText(EvidenceSourceToken(identity->provenance.source));
            appendText(identity->provenance.provider);
            appendText(identity->provenance.detail);
            appendOptionalInteger(identity->configGeneration);
            appendText(identity->sceneStableId);
            appendOptionalInteger(identity->sceneGeneration);
            appendOptionalInteger(identity->resourceGeneration);
            appendOptionalInteger(identity->frameIndex);
            appendOptionalInteger(identity->sampleIndex);
        }

        void AppendDouble(std::string& output, double value)
        {
            if (value == 0.0)
            {
                output.push_back('0');
                return;
            }
            char buffer[64]{};
            const auto conversion = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value,
                std::chars_format::general,
                std::numeric_limits<double>::max_digits10);
            output.append(buffer, conversion.ptr);
        }

        void AppendJsonString(std::string& output, std::string_view value)
        {
            constexpr char hexadecimal[] = "0123456789abcdef";
            output.push_back('"');
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': output.append("\\\""); break;
                case '\\': output.append("\\\\"); break;
                case '\b': output.append("\\b"); break;
                case '\f': output.append("\\f"); break;
                case '\n': output.append("\\n"); break;
                case '\r': output.append("\\r"); break;
                case '\t': output.append("\\t"); break;
                default:
                    if (character < 0x20u)
                    {
                        output.append("\\u00");
                        output.push_back(hexadecimal[(character >> 4u) & 0x0fu]);
                        output.push_back(hexadecimal[character & 0x0fu]);
                    }
                    else
                    {
                        output.push_back(static_cast<char>(character));
                    }
                    break;
                }
            }
            output.push_back('"');
        }

        [[nodiscard]] std::string MarkdownInline(std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            bool previousWasSpace = false;
            for (const char character : value)
            {
                const bool lineBreak = character == '\r' || character == '\n';
                if (lineBreak)
                {
                    if (!previousWasSpace)
                    {
                        result.push_back(' ');
                        previousWasSpace = true;
                    }
                    continue;
                }
                if (character == '\\' || character == '`')
                {
                    result.push_back('\\');
                }
                result.push_back(character);
                previousWasSpace = character == ' ';
            }
            return result;
        }

        void AppendEvidenceIdentityMarkdown(
            std::string& output,
            const RetainedEvidenceIdentityRow& row)
        {
            const ShowcaseEvidenceIdentity& identity = *row.identity;
            const auto appendOptionalInteger = [&output](
                const std::optional<std::uint64_t>& value)
            {
                if (value.has_value())
                {
                    AppendInteger(output, *value);
                }
                else
                {
                    output.append("missing");
                }
            };

            output.append("- [");
            output.append(MarkdownInline(row.category));
            output.append("] ");
            output.append(MarkdownInline(row.stableId));
            output.append(": artifact=");
            output.append(MarkdownInline(identity.artifactIdentity));
            output.append(", source=");
            output.append(EvidenceSourceToken(identity.provenance.source));
            output.append(", provider=");
            output.append(MarkdownInline(identity.provenance.provider));
            if (!identity.provenance.detail.empty())
            {
                output.append(", detail=");
                output.append(MarkdownInline(identity.provenance.detail));
            }
            output.append(", config-generation=");
            appendOptionalInteger(identity.configGeneration);
            output.append(", scene=");
            output.append(MarkdownInline(identity.sceneStableId));
            output.append(", scene-generation=");
            appendOptionalInteger(identity.sceneGeneration);
            output.append(", resource-generation=");
            appendOptionalInteger(identity.resourceGeneration);
            output.append(", frame-index=");
            appendOptionalInteger(identity.frameIndex);
            output.append(", sample-index=");
            appendOptionalInteger(identity.sampleIndex);
            output.push_back('\n');
        }

        [[nodiscard]] ShowcaseReportStatus ValidateProvenance(
            ReportEvidenceClass classification,
            const EvidenceProvenance& provenance,
            std::string_view context)
        {
            if (ReportEvidenceClassToken(classification).empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidEnum,
                    std::string(context) + " has an unknown evidence classification.");
            }
            if (EvidenceSourceToken(provenance.source).empty()
                || provenance.provider.empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidProvenance,
                    std::string(context)
                        + " needs a known source and non-empty provider.");
            }
            if (!IsValidUtf8(provenance.provider)
                || !IsValidUtf8(provenance.detail))
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidProvenance,
                    std::string(context) + " provenance must be valid UTF-8.");
            }

            const bool syntheticSource =
                provenance.source == EvidenceSource::SyntheticTest;
            if (classification == ReportEvidenceClass::SyntheticTest)
            {
                if (!syntheticSource)
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidProvenance,
                        std::string(context)
                            + " classified SyntheticTest must use synthetic-test source.");
                }
            }
            else if (syntheticSource)
            {
                return MakeStatusError(
                    classification == ReportEvidenceClass::LiveRuntime
                        ? ShowcaseReportError::SyntheticLiveConflict
                        : ShowcaseReportError::InvalidProvenance,
                    std::string(context)
                        + " synthetic-test source cannot use a non-synthetic classification.");
            }
            return {};
        }

        [[nodiscard]] ShowcaseReportStatus ValidateAlgorithmEntries(
            std::span<const AlgorithmCompletionEntry> entries)
        {
            std::set<std::string_view> stableIds;
            for (const AlgorithmCompletionEntry& entry : entries)
            {
                if (!IsStableToken(entry.algorithm.stableToken)
                    || !IsStableToken(entry.algorithm.providerToken))
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidInput,
                        "Algorithm and provider stable tokens must be non-empty ASCII tokens.");
                }
                if (!stableIds.insert(entry.algorithm.stableToken).second)
                {
                    return MakeStatusError(
                        ShowcaseReportError::DuplicateStableId,
                        "Algorithm stable tokens must be unique.");
                }
                if (AlgorithmStateToken(entry.state).empty())
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidEnum,
                        "Algorithm completion state is unknown.");
                }
                for (const auto [value, name] : {
                        std::pair{ entry.algorithm.label, "Algorithm label" },
                        std::pair{ entry.algorithm.category, "Algorithm category" },
                        std::pair{ entry.algorithm.owner, "Algorithm owner" } })
                {
                    const ShowcaseReportStatus status = ValidateText(value, name);
                    if (!status)
                    {
                        return status;
                    }
                }
                const ShowcaseReportStatus reasonStatus =
                    ValidateText(entry.reason, "Algorithm reason", true);
                if (!reasonStatus)
                {
                    return reasonStatus;
                }
                if (entry.state == AlgorithmCompletionState::Unavailable
                    && entry.reason.empty())
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidInput,
                        "Unavailable algorithms must include a reason.");
                }
                if (entry.state == AlgorithmCompletionState::RuntimeValidated
                    || entry.state == AlgorithmCompletionState::VisualAccepted)
                {
                    if (!entry.evidence.has_value())
                    {
                        return MakeStatusError(
                            ShowcaseReportError::InvalidProvenance,
                            "Runtime/visual algorithm completion requires evidence identity.");
                    }
                    const AvailabilityStatus evidenceStatus =
                        ValidateLiveEvidenceIdentity(*entry.evidence);
                    if (!evidenceStatus.IsAvailable())
                    {
                        return MakeStatusError(
                            entry.evidence->provenance.source
                                    == EvidenceSource::SyntheticTest
                                ? ShowcaseReportError::SyntheticLiveConflict
                                : ShowcaseReportError::InvalidProvenance,
                            evidenceStatus.reason);
                    }
                }
            }
            return {};
        }

        [[nodiscard]] ShowcaseReportStatus ValidateAssetEntries(
            std::span<const AssetLicenseEntry> entries)
        {
            std::set<std::string> stableIds;
            for (const AssetLicenseEntry& entry : entries)
            {
                if (!IsStableToken(entry.assetToken))
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidInput,
                        "Asset stable tokens must be non-empty ASCII tokens.");
                }
                if (!stableIds.insert(entry.assetToken).second)
                {
                    return MakeStatusError(
                        ShowcaseReportError::DuplicateStableId,
                        "Asset stable tokens must be unique.");
                }
                for (const auto& [value, name] : {
                        std::pair<const std::string&, std::string_view>{
                            entry.sourceUri, "Asset source URI" },
                        { entry.attribution, "Asset attribution" },
                        { entry.licenseName, "Asset license name" },
                        { entry.licenseDocument, "Asset license document" },
                        { entry.contentHash, "Asset content hash" } })
                {
                    const ShowcaseReportStatus status = ValidateText(value, name);
                    if (!status)
                    {
                        return status;
                    }
                }
            }
            return {};
        }

        [[nodiscard]] ShowcaseReportStatus ValidateFinalVideoShots(
            std::span<const FinalVideoShot> shots)
        {
            std::set<std::string_view> stableIds;
            for (const FinalVideoShot& shot : shots)
            {
                if (!IsStableToken(shot.descriptor.stableToken)
                    || !IsStableToken(shot.descriptor.captureShotToken))
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidInput,
                        "Final-video and capture-shot tokens must be non-empty ASCII tokens.");
                }
                if (!stableIds.insert(shot.descriptor.stableToken).second)
                {
                    return MakeStatusError(
                        ShowcaseReportError::DuplicateStableId,
                        "Final-video shot tokens must be unique.");
                }
                if (FinalVideoStateToken(shot.state).empty())
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidEnum,
                        "Final-video shot state is unknown.");
                }
                for (const auto [value, name] : {
                        std::pair{ shot.descriptor.label, "Final-video label" },
                        std::pair{ shot.descriptor.narrativePurpose,
                            "Final-video narrative purpose" } })
                {
                    const ShowcaseReportStatus status = ValidateText(value, name);
                    if (!status)
                    {
                        return status;
                    }
                }
                const ShowcaseReportStatus reasonStatus =
                    ValidateText(shot.reason, "Final-video reason", true);
                if (!reasonStatus)
                {
                    return reasonStatus;
                }
                if ((shot.state == FinalVideoShotState::Unavailable
                        || shot.state == FinalVideoShotState::Failed)
                    && shot.reason.empty())
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidInput,
                        "Unavailable or failed final-video shots need a reason.");
                }
                if (shot.state == FinalVideoShotState::Captured
                    || shot.state == FinalVideoShotState::Approved)
                {
                    if (!shot.captureEvidence.has_value())
                    {
                        return MakeStatusError(
                            ShowcaseReportError::InvalidProvenance,
                            "Captured/approved final-video shots require capture evidence identity.");
                    }
                    const AvailabilityStatus evidenceStatus =
                        ValidateLiveEvidenceIdentity(*shot.captureEvidence);
                    if (!evidenceStatus.IsAvailable())
                    {
                        return MakeStatusError(
                            shot.captureEvidence->provenance.source
                                    == EvidenceSource::SyntheticTest
                                ? ShowcaseReportError::SyntheticLiveConflict
                                : ShowcaseReportError::InvalidProvenance,
                            evidenceStatus.reason);
                    }
                }
            }
            return {};
        }

        [[nodiscard]] ShowcaseReportStatus ValidateReferenceIdentity(
            const ReferenceImageIdentity& identity,
            std::string_view context,
            std::size_t& componentCount)
        {
            if (!IsStableToken(identity.stableId))
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(context) + " stable ID is invalid.");
            }
            const ShowcaseReportStatus provenanceStatus =
                ValidateProvenance(
                    identity.classification, identity.provenance, context);
            if (!provenanceStatus)
            {
                return provenanceStatus;
            }
            if (!IsStableToken(identity.sceneStableId))
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(context) + " scene stable ID is invalid.");
            }
            if (!IsStableToken(identity.cameraPresetToken))
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(context) + " camera preset token is invalid.");
            }
            if (identity.accumulatedSamplesPerPixel == 0u)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(context) + " accumulated SPP must be non-zero.");
            }
            if (identity.width == 0u || identity.height == 0u)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    std::string(context) + " extent must be non-zero.");
            }

            const std::size_t width = identity.width;
            const std::size_t height = identity.height;
            if (width > std::numeric_limits<std::size_t>::max() / height)
            {
                return MakeStatusError(
                    ShowcaseReportError::SizeOverflow,
                    std::string(context) + " extent overflows addressable size.");
            }
            const std::size_t pixels = width * height;
            if (pixels > std::numeric_limits<std::size_t>::max() / 4u)
            {
                return MakeStatusError(
                    ShowcaseReportError::SizeOverflow,
                    std::string(context) + " RGBA component count overflows addressable size.");
            }
            componentCount = pixels * 4u;
            return {};
        }

        [[nodiscard]] ShowcaseReportStatus ValidateReferenceComparison(
            const ReferenceComparisonRecord& record)
        {
            std::size_t candidateComponentCount = 0u;
            std::size_t referenceComponentCount = 0u;
            ShowcaseReportStatus status = ValidateReferenceIdentity(
                record.candidate,
                "Candidate",
                candidateComponentCount);
            if (!status)
            {
                return status;
            }
            status = ValidateReferenceIdentity(
                record.reference,
                "Reference",
                referenceComponentCount);
            if (!status)
            {
                return status;
            }
            if (record.candidate.width != record.reference.width
                || record.candidate.height != record.reference.height
                || candidateComponentCount != referenceComponentCount)
            {
                return MakeStatusError(
                    ShowcaseReportError::ExtentMismatch,
                    "Candidate and reference extents must match.");
            }
            if (record.candidate.sceneStableId != record.reference.sceneStableId
                || record.candidate.cameraPresetToken
                    != record.reference.cameraPresetToken
                || record.candidate.baseSeed != record.reference.baseSeed
                || record.candidate.configGeneration
                    != record.reference.configGeneration
                || record.candidate.sceneGeneration
                    != record.reference.sceneGeneration
                || record.candidate.resourceGeneration
                    != record.reference.resourceGeneration)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    "Candidate and reference must share scene, camera, seed, and generation identity.");
            }
            if (!record.comparison)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidMetrics,
                    "Only successful image comparison results may be serialized.");
            }
            if (record.comparison.comparedComponentCount != candidateComponentCount)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidMetrics,
                    "Compared component count does not match the declared extent.");
            }
            if (!std::isfinite(record.comparison.rmse)
                || record.comparison.rmse < 0.0
                || !std::isfinite(record.comparison.maximumAbsoluteError)
                || record.comparison.maximumAbsoluteError < 0.0
                || record.comparison.rmse > record.comparison.maximumAbsoluteError)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidMetrics,
                    "RMSE and maximum absolute error are inconsistent or non-finite.");
            }

            if (record.comparison.psnrIsPositiveInfinity)
            {
                if (!std::isinf(record.comparison.psnr)
                    || record.comparison.psnr < 0.0
                    || record.comparison.rmse != 0.0
                    || record.comparison.maximumAbsoluteError != 0.0)
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidMetrics,
                        "Positive-infinite PSNR must describe an exact comparison.");
                }
            }
            else if (!std::isfinite(record.comparison.psnr)
                || record.comparison.rmse == 0.0)
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidMetrics,
                    "Finite PSNR must be finite and accompany non-zero RMSE.");
            }
            return {};
        }

        [[nodiscard]] auto ReferenceSortKey(const ReferenceComparisonRecord& record)
        {
            return std::tie(
                record.candidate.stableId,
                record.reference.stableId,
                record.candidate.sceneStableId,
                record.candidate.cameraPresetToken,
                record.candidate.baseSeed,
                record.candidate.configGeneration,
                record.candidate.sceneGeneration,
                record.candidate.resourceGeneration,
                record.candidate.frameIndex,
                record.candidate.sampleIndex,
                record.candidate.accumulatedSamplesPerPixel,
                record.reference.configGeneration,
                record.reference.sceneGeneration,
                record.reference.resourceGeneration,
                record.reference.frameIndex,
                record.reference.sampleIndex,
                record.reference.accumulatedSamplesPerPixel,
                record.candidate.classification,
                record.reference.classification,
                record.candidate.provenance.source,
                record.candidate.provenance.provider,
                record.candidate.provenance.detail,
                record.reference.provenance.source,
                record.reference.provenance.provider,
                record.reference.provenance.detail);
        }

        void AppendReferenceIdentityJson(
            std::string& output,
            std::string_view memberName,
            const ReferenceImageIdentity& identity)
        {
            output.append("      \"");
            output.append(memberName);
            output.append("\": {\n        \"classification\": ");
            AppendJsonString(
                output,
                ReportEvidenceClassToken(identity.classification));
            output.append(",\n        \"stable_id\": ");
            AppendJsonString(output, identity.stableId);
            output.append(",\n        \"source\": ");
            AppendJsonString(output, EvidenceSourceToken(identity.provenance.source));
            output.append(",\n        \"provider\": ");
            AppendJsonString(output, identity.provenance.provider);
            output.append(",\n        \"detail\": ");
            AppendJsonString(output, identity.provenance.detail);
            output.append(",\n        \"frame_index\": ");
            AppendInteger(output, identity.frameIndex);
            output.append(",\n        \"config_generation\": ");
            AppendInteger(output, identity.configGeneration);
            output.append(",\n        \"scene_stable_id\": ");
            AppendJsonString(output, identity.sceneStableId);
            output.append(",\n        \"camera_preset_token\": ");
            AppendJsonString(output, identity.cameraPresetToken);
            output.append(",\n        \"base_seed\": ");
            AppendInteger(output, identity.baseSeed);
            output.append(",\n        \"sample_index\": ");
            AppendInteger(output, identity.sampleIndex);
            output.append(",\n        \"scene_generation\": ");
            AppendInteger(output, identity.sceneGeneration);
            output.append(",\n        \"resource_generation\": ");
            AppendInteger(output, identity.resourceGeneration);
            output.append(",\n        \"accumulated_spp\": ");
            AppendInteger(output, identity.accumulatedSamplesPerPixel);
            output.append(",\n        \"extent\": {\"width\": ");
            AppendInteger(output, identity.width);
            output.append(", \"height\": ");
            AppendInteger(output, identity.height);
            output.append("}\n      }");
        }

        [[nodiscard]] ShowcaseReportStatus ValidateBoundaryEntry(
            const ShowcaseEvidenceBoundaryEntry& entry)
        {
            if (EvidenceBoundaryTitle(entry.boundary).empty()
                || EvidenceStateToken(entry.state).empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidEnum,
                    "Evidence boundary or state is unknown.");
            }
            ShowcaseReportStatus status =
                ValidateText(entry.summary, "Evidence summary", true);
            if (!status)
            {
                return status;
            }
            status = ValidateText(entry.reason, "Evidence reason", true);
            if (!status)
            {
                return status;
            }

            const bool observed = entry.state == ShowcaseEvidenceState::Passed
                || entry.state == ShowcaseEvidenceState::Failed;
            if (entry.state == ShowcaseEvidenceState::Passed && entry.summary.empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    "Passed evidence must include a summary.");
            }
            if ((entry.state == ShowcaseEvidenceState::Unavailable
                    || entry.state == ShowcaseEvidenceState::NotRun
                    || entry.state == ShowcaseEvidenceState::Failed)
                && entry.reason.empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    "Unavailable, not-run, and failed evidence must include a reason.");
            }
            if (observed)
            {
                status = ValidateProvenance(
                    entry.classification,
                    entry.provenance,
                    EvidenceBoundaryTitle(entry.boundary));
                if (!status)
                {
                    return status;
                }
            }
            return {};
        }

        [[nodiscard]] ShowcaseReportStatus ValidateUnavailableItem(
            const UnavailableItem& item)
        {
            if (item.category.empty() || item.stableId.empty()
                || item.label.empty() || item.reason.empty())
            {
                return MakeStatusError(
                    ShowcaseReportError::InvalidInput,
                    "Unavailable report items need category, ID, label, and reason.");
            }
            for (const std::string* value : {
                    &item.category, &item.stableId, &item.label, &item.reason })
            {
                if (!IsValidUtf8(*value))
                {
                    return MakeStatusError(
                        ShowcaseReportError::InvalidInput,
                        "Unavailable report items must be valid UTF-8.");
                }
            }
            return {};
        }

        void AddMissingProviderItem(
            std::vector<UnavailableItem>& items,
            std::string category,
            std::string stableId,
            std::string label)
        {
            items.push_back({
                std::move(category),
                std::move(stableId),
                std::move(label),
                "No provider data was supplied."
            });
        }
    }

    std::string_view ReportEvidenceClassToken(
        ReportEvidenceClass classification) noexcept
    {
        switch (classification)
        {
        case ReportEvidenceClass::LiveRuntime: return "live-runtime";
        case ReportEvidenceClass::ImportedArtifact: return "imported-artifact";
        case ReportEvidenceClass::SyntheticTest: return "synthetic-test";
        }
        return {};
    }

    ShowcaseTextResult BuildAlgorithmCompletionMatrixCsv(
        std::span<const AlgorithmCompletionEntry> entries)
    {
        const ShowcaseReportStatus status = ValidateAlgorithmEntries(entries);
        if (!status)
        {
            return MakeError(status.error, status.message);
        }

        std::vector<const AlgorithmCompletionEntry*> rows;
        rows.reserve(entries.size());
        for (const AlgorithmCompletionEntry& entry : entries)
        {
            rows.push_back(&entry);
        }
        std::sort(rows.begin(), rows.end(),
            [](const AlgorithmCompletionEntry* left,
                const AlgorithmCompletionEntry* right)
            {
                return std::tie(
                        left->algorithm.stableToken,
                        left->algorithm.providerToken,
                        left->algorithm.label,
                        left->state,
                        left->reason)
                    < std::tie(
                        right->algorithm.stableToken,
                        right->algorithm.providerToken,
                        right->algorithm.label,
                        right->state,
                        right->reason);
            });

        ShowcaseTextResult result;
        result.text = "stable_token,label,category,owner,provider_token,state,reason,"
            "evidence_artifact,evidence_source,evidence_provider,evidence_detail,"
            "config_generation,scene_stable_id,scene_generation,resource_generation,"
            "frame_index,sample_index\r\n";
        for (const AlgorithmCompletionEntry* entry : rows)
        {
            AppendCsvField(result.text, entry->algorithm.stableToken);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->algorithm.label);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->algorithm.category);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->algorithm.owner);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->algorithm.providerToken);
            result.text.push_back(',');
            AppendCsvField(result.text, AlgorithmStateToken(entry->state));
            result.text.push_back(',');
            AppendCsvField(result.text, entry->reason);
            AppendEvidenceIdentityCsv(result.text, entry->evidence);
            result.text.append("\r\n");
        }
        return result;
    }

    ShowcaseTextResult BuildAssetLicenseCsv(
        std::span<const AssetLicenseEntry> entries)
    {
        const ShowcaseReportStatus status = ValidateAssetEntries(entries);
        if (!status)
        {
            return MakeError(status.error, status.message);
        }

        std::vector<const AssetLicenseEntry*> rows;
        rows.reserve(entries.size());
        for (const AssetLicenseEntry& entry : entries)
        {
            rows.push_back(&entry);
        }
        std::sort(rows.begin(), rows.end(),
            [](const AssetLicenseEntry* left, const AssetLicenseEntry* right)
            {
                return std::tie(
                        left->assetToken,
                        left->sourceUri,
                        left->licenseDocument,
                        left->contentHash)
                    < std::tie(
                        right->assetToken,
                        right->sourceUri,
                        right->licenseDocument,
                        right->contentHash);
            });

        ShowcaseTextResult result;
        result.text = "asset_token,source_uri,attribution,license_name,license_document,content_hash\r\n";
        for (const AssetLicenseEntry* entry : rows)
        {
            AppendCsvField(result.text, entry->assetToken);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->sourceUri);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->attribution);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->licenseName);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->licenseDocument);
            result.text.push_back(',');
            AppendCsvField(result.text, entry->contentHash);
            result.text.append("\r\n");
        }
        return result;
    }

    ShowcaseTextResult BuildFinalVideoShotListCsv(
        std::span<const FinalVideoShot> shots)
    {
        const ShowcaseReportStatus status = ValidateFinalVideoShots(shots);
        if (!status)
        {
            return MakeError(status.error, status.message);
        }

        std::vector<const FinalVideoShot*> rows;
        rows.reserve(shots.size());
        for (const FinalVideoShot& shot : shots)
        {
            rows.push_back(&shot);
        }
        std::sort(rows.begin(), rows.end(),
            [](const FinalVideoShot* left, const FinalVideoShot* right)
            {
                return std::tie(
                        left->descriptor.stableToken,
                        left->descriptor.captureShotToken,
                        left->state,
                        left->reason)
                    < std::tie(
                        right->descriptor.stableToken,
                        right->descriptor.captureShotToken,
                        right->state,
                        right->reason);
            });

        ShowcaseTextResult result;
        result.text = "stable_token,label,capture_shot_token,narrative_purpose,state,reason,"
            "evidence_artifact,evidence_source,evidence_provider,evidence_detail,"
            "config_generation,scene_stable_id,scene_generation,resource_generation,"
            "frame_index,sample_index\r\n";
        for (const FinalVideoShot* shot : rows)
        {
            AppendCsvField(result.text, shot->descriptor.stableToken);
            result.text.push_back(',');
            AppendCsvField(result.text, shot->descriptor.label);
            result.text.push_back(',');
            AppendCsvField(result.text, shot->descriptor.captureShotToken);
            result.text.push_back(',');
            AppendCsvField(result.text, shot->descriptor.narrativePurpose);
            result.text.push_back(',');
            AppendCsvField(result.text, FinalVideoStateToken(shot->state));
            result.text.push_back(',');
            AppendCsvField(result.text, shot->reason);
            AppendEvidenceIdentityCsv(result.text, shot->captureEvidence);
            result.text.append("\r\n");
        }
        return result;
    }

    ShowcaseTextResult BuildReferenceComparisonJson(
        std::span<const ReferenceComparisonRecord> comparisons)
    {
        std::vector<const ReferenceComparisonRecord*> rows;
        rows.reserve(comparisons.size());
        for (const ReferenceComparisonRecord& comparison : comparisons)
        {
            const ShowcaseReportStatus status =
                ValidateReferenceComparison(comparison);
            if (!status)
            {
                return MakeError(status.error, status.message);
            }
            rows.push_back(&comparison);
        }
        std::sort(rows.begin(), rows.end(),
            [](const ReferenceComparisonRecord* left,
                const ReferenceComparisonRecord* right)
            {
                return ReferenceSortKey(*left) < ReferenceSortKey(*right);
            });
        for (std::size_t index = 1u; index < rows.size(); ++index)
        {
            if (ReferenceSortKey(*rows[index - 1u]) == ReferenceSortKey(*rows[index]))
            {
                return MakeError(
                    ShowcaseReportError::DuplicateStableId,
                    "Reference comparison identities must be unique.");
            }
        }

        ShowcaseTextResult result;
        result.text = "{\n  \"schema\": \"l10-reference-comparison-v1\",\n  \"comparisons\": [";
        if (!rows.empty())
        {
            result.text.push_back('\n');
        }
        for (std::size_t index = 0u; index < rows.size(); ++index)
        {
            const ReferenceComparisonRecord& record = *rows[index];
            result.text.append("    {\n");
            AppendReferenceIdentityJson(result.text, "candidate", record.candidate);
            result.text.append(",\n");
            AppendReferenceIdentityJson(result.text, "reference", record.reference);
            result.text.append(",\n      \"metrics\": {\n        \"compared_component_count\": ");
            AppendInteger(result.text, record.comparison.comparedComponentCount);
            result.text.append(",\n        \"rmse\": ");
            AppendDouble(result.text, record.comparison.rmse);
            result.text.append(",\n        \"psnr\": ");
            if (record.comparison.psnrIsPositiveInfinity)
            {
                result.text.append("null");
            }
            else
            {
                AppendDouble(result.text, record.comparison.psnr);
            }
            result.text.append(",\n        \"psnr_positive_infinity\": ");
            result.text.append(
                record.comparison.psnrIsPositiveInfinity ? "true" : "false");
            result.text.append(",\n        \"maximum_absolute_error\": ");
            AppendDouble(result.text, record.comparison.maximumAbsoluteError);
            result.text.append("\n      }\n    }");
            if (index + 1u != rows.size())
            {
                result.text.push_back(',');
            }
            result.text.push_back('\n');
        }
        result.text.append("  ]\n}\n");
        return result;
    }

    ShowcaseTextResult BuildShowcaseMarkdownReport(
        const ShowcaseMarkdownReportInput& input)
    {
        const ShowcaseReportStatus titleStatus =
            ValidateText(input.title, "Showcase report title");
        if (!titleStatus)
        {
            return MakeError(titleStatus.error, titleStatus.message);
        }

        std::array<const ShowcaseEvidenceBoundaryEntry*, kEvidenceBoundaryCount>
            boundaries{};
        for (const ShowcaseEvidenceBoundaryEntry& entry : input.evidence)
        {
            const ShowcaseReportStatus status = ValidateBoundaryEntry(entry);
            if (!status)
            {
                return MakeError(status.error, status.message);
            }
            const std::size_t index = static_cast<std::size_t>(entry.boundary);
            if (index >= boundaries.size())
            {
                return MakeError(
                    ShowcaseReportError::InvalidEnum,
                    "Evidence boundary is outside the stable report range.");
            }
            if (boundaries[index] != nullptr)
            {
                return MakeError(
                    ShowcaseReportError::DuplicateStableId,
                    "Each evidence boundary may appear at most once.");
            }
            boundaries[index] = &entry;
        }

        std::vector<std::string> limitations;
        limitations.reserve(input.knownLimitations.size());
        for (const std::string& limitation : input.knownLimitations)
        {
            const ShowcaseReportStatus status =
                ValidateText(limitation, "Known limitation");
            if (!status)
            {
                return MakeError(status.error, status.message);
            }
            limitations.push_back(limitation);
        }
        std::sort(limitations.begin(), limitations.end());
        limitations.erase(
            std::unique(limitations.begin(), limitations.end()),
            limitations.end());

        std::vector<UnavailableItem> unavailable;
        if (input.scenes.empty())
        {
            AddMissingProviderItem(
                unavailable, "scene", "scene-registry", "Scene registry");
        }
        else
        {
            std::set<std::string_view> tokens;
            for (const ResolvedShowcaseSceneCard& scene : input.scenes)
            {
                if (!IsStableToken(scene.card.stableToken)
                    || !IsStableToken(scene.card.owner)
                    || !tokens.insert(scene.card.stableToken).second
                    || !IsKnownAvailability(scene.availability.state))
                {
                    return MakeError(
                        ShowcaseReportError::InvalidInput,
                        "Scene report inputs need unique stable tokens and known availability.");
                }
                const ShowcaseReportStatus labelStatus =
                    ValidateText(scene.card.label, "Scene label");
                if (!labelStatus)
                {
                    return MakeError(labelStatus.error, labelStatus.message);
                }
                if (!scene.availability.IsAvailable())
                {
                    if (scene.availability.reason.empty())
                    {
                        return MakeError(
                            ShowcaseReportError::InvalidInput,
                            "Unavailable scenes must include a reason.");
                    }
                    unavailable.push_back({
                        "scene",
                        std::string(scene.card.stableToken),
                        std::string(scene.card.label),
                        scene.availability.reason
                    });
                }
            }
        }

        if (input.algorithmCompletion.empty())
        {
            AddMissingProviderItem(
                unavailable, "algorithm", "completion-matrix", "Algorithm completion matrix");
        }
        else
        {
            const ShowcaseReportStatus status =
                ValidateAlgorithmEntries(input.algorithmCompletion);
            if (!status)
            {
                return MakeError(status.error, status.message);
            }
            for (const AlgorithmCompletionEntry& entry : input.algorithmCompletion)
            {
                if (!entry.IsRunnable())
                {
                    unavailable.push_back({
                        "algorithm",
                        std::string(entry.algorithm.stableToken),
                        std::string(entry.algorithm.label),
                        entry.reason.empty()
                            ? "Algorithm is declared but not runnable."
                            : entry.reason
                    });
                }
            }
        }

        if (input.manyLightsComparisons.empty())
        {
            AddMissingProviderItem(
                unavailable, "many-lights", "comparison-presets", "Many Lights comparisons");
        }
        else
        {
            std::set<std::string_view> tokens;
            for (const ManyLightsComparisonRequest& request
                : input.manyLightsComparisons)
            {
                if (!IsStableToken(request.preset.stableToken)
                    || request.preset.lightCount == 0u
                    || !tokens.insert(request.preset.stableToken).second
                    || !IsKnownAvailability(request.availability.state))
                {
                    return MakeError(
                        ShowcaseReportError::InvalidInput,
                        "Many Lights report inputs need unique presets, counts, and known availability.");
                }
                const ShowcaseReportStatus labelStatus =
                    ValidateText(request.preset.label, "Many Lights label");
                if (!labelStatus)
                {
                    return MakeError(labelStatus.error, labelStatus.message);
                }
                if (!request.availability.IsAvailable())
                {
                    if (request.availability.reason.empty())
                    {
                        return MakeError(
                            ShowcaseReportError::InvalidInput,
                            "Unavailable Many Lights comparisons must include a reason.");
                    }
                    unavailable.push_back({
                        "many-lights",
                        std::string(request.preset.stableToken),
                        std::string(request.preset.label),
                        request.availability.reason
                    });
                }
            }
        }

        if (input.captureShots.empty())
        {
            AddMissingProviderItem(
                unavailable, "capture", "capture-plan", "Capture shot plan");
        }
        else
        {
            std::set<std::string_view> tokens;
            for (const CaptureShotPlanEntry& capture : input.captureShots)
            {
                if (!IsStableToken(capture.shot.stableToken)
                    || !IsStableToken(capture.shot.sceneToken)
                    || !IsStableToken(capture.shot.cameraPresetToken)
                    || !tokens.insert(capture.shot.stableToken).second
                    || !IsKnownAvailability(capture.availability.state))
                {
                    return MakeError(
                        ShowcaseReportError::InvalidInput,
                        "Capture report inputs need unique stable tokens and known availability.");
                }
                const ShowcaseReportStatus labelStatus =
                    ValidateText(capture.shot.label, "Capture label");
                if (!labelStatus)
                {
                    return MakeError(labelStatus.error, labelStatus.message);
                }
                if (!capture.availability.IsAvailable())
                {
                    if (capture.availability.reason.empty())
                    {
                        return MakeError(
                            ShowcaseReportError::InvalidInput,
                            "Unavailable capture shots must include a reason.");
                    }
                    unavailable.push_back({
                        "capture",
                        std::string(capture.shot.stableToken),
                        std::string(capture.shot.label),
                        capture.availability.reason
                    });
                }
            }
        }

        if (input.finalVideoShots.empty())
        {
            AddMissingProviderItem(
                unavailable, "final-video", "shot-list", "Final video shot list");
        }
        else
        {
            const ShowcaseReportStatus status =
                ValidateFinalVideoShots(input.finalVideoShots);
            if (!status)
            {
                return MakeError(status.error, status.message);
            }
            for (const FinalVideoShot& shot : input.finalVideoShots)
            {
                if (shot.state == FinalVideoShotState::Unavailable
                    || shot.state == FinalVideoShotState::Failed)
                {
                    unavailable.push_back({
                        "final-video",
                        std::string(shot.descriptor.stableToken),
                        std::string(shot.descriptor.label),
                        shot.reason
                    });
                }
            }
        }

        if (input.licenseGate == nullptr)
        {
            AddMissingProviderItem(
                unavailable, "license", "asset-license-gate", "Asset license gate");
        }
        else
        {
            if (!IsKnownAvailability(input.licenseGate->availability.state))
            {
                return MakeError(
                    ShowcaseReportError::InvalidEnum,
                    "Asset license availability is unknown.");
            }
            if (input.licenseGate->CanPublish())
            {
                if (!input.licenseGate->issues.empty())
                {
                    return MakeError(
                        ShowcaseReportError::InvalidInput,
                        "An available asset license gate cannot contain issues.");
                }
            }
            else
            {
                if (input.licenseGate->availability.reason.empty())
                {
                    return MakeError(
                        ShowcaseReportError::InvalidInput,
                        "Unavailable asset license gate must include a reason.");
                }
                unavailable.push_back({
                    "license",
                    "asset-license-gate",
                    "Asset license gate",
                    input.licenseGate->availability.reason
                });
                for (const AssetLicenseIssue& issue : input.licenseGate->issues)
                {
                    if (!IsStableToken(issue.assetToken) || issue.reason.empty())
                    {
                        return MakeError(
                            ShowcaseReportError::InvalidInput,
                            "Asset license issues need stable tokens and reasons.");
                    }
                    unavailable.push_back({
                        "license",
                        issue.assetToken,
                        "Asset license issue",
                        issue.reason
                    });
                }
            }
        }

        for (const UnavailableItem& item : unavailable)
        {
            const ShowcaseReportStatus status = ValidateUnavailableItem(item);
            if (!status)
            {
                return MakeError(status.error, status.message);
            }
        }
        std::sort(unavailable.begin(), unavailable.end(),
            [](const UnavailableItem& left, const UnavailableItem& right)
            {
                return std::tie(
                        left.category, left.stableId, left.label, left.reason)
                    < std::tie(
                        right.category, right.stableId, right.label, right.reason);
            });

        std::vector<RetainedEvidenceIdentityRow> retainedIdentities;
        retainedIdentities.reserve(
            input.algorithmCompletion.size() + input.finalVideoShots.size());
        for (const AlgorithmCompletionEntry& entry : input.algorithmCompletion)
        {
            if (entry.evidence.has_value())
            {
                retainedIdentities.push_back({
                    "algorithm", entry.algorithm.stableToken, &*entry.evidence
                });
            }
        }
        for (const FinalVideoShot& shot : input.finalVideoShots)
        {
            if (shot.captureEvidence.has_value())
            {
                retainedIdentities.push_back({
                    "final-video", shot.descriptor.stableToken,
                    &*shot.captureEvidence
                });
            }
        }
        std::sort(retainedIdentities.begin(), retainedIdentities.end(),
            [](const RetainedEvidenceIdentityRow& left,
                const RetainedEvidenceIdentityRow& right)
            {
                return std::tie(left.category, left.stableId)
                    < std::tie(right.category, right.stableId);
            });

        ShowcaseTextResult result;
        result.text = "# " + MarkdownInline(input.title) + "\n\n";
        result.text.append(
            "> Evidence is serialized from explicit caller/provider records. "
            "This report does not infer GPU, Vulkan, numerical, visual, or "
            "performance success from code presence.\n\n");

        for (std::size_t index = 0u; index < boundaries.size(); ++index)
        {
            const auto boundary = static_cast<ShowcaseEvidenceBoundary>(index);
            result.text.append("## ");
            result.text.append(EvidenceBoundaryTitle(boundary));
            result.text.append("\n\n");
            const ShowcaseEvidenceBoundaryEntry* const entry = boundaries[index];
            if (entry == nullptr)
            {
                result.text.append(
                    "- Status: unavailable\n"
                    "- Reason: No evidence record was supplied.\n\n");
                continue;
            }

            result.text.append("- Status: ");
            result.text.append(EvidenceStateToken(entry->state));
            result.text.push_back('\n');
            const bool observed = entry->state == ShowcaseEvidenceState::Passed
                || entry->state == ShowcaseEvidenceState::Failed;
            if (observed)
            {
                result.text.append("- Classification: ");
                result.text.append(ReportEvidenceClassToken(entry->classification));
                result.text.append("\n- Source: ");
                result.text.append(EvidenceSourceToken(entry->provenance.source));
                result.text.append("\n- Provider: ");
                result.text.append(MarkdownInline(entry->provenance.provider));
                result.text.push_back('\n');
                if (!entry->provenance.detail.empty())
                {
                    result.text.append("- Provenance detail: ");
                    result.text.append(MarkdownInline(entry->provenance.detail));
                    result.text.push_back('\n');
                }
            }
            if (!entry->summary.empty())
            {
                result.text.append("- Summary: ");
                result.text.append(MarkdownInline(entry->summary));
                result.text.push_back('\n');
            }
            if (!entry->reason.empty())
            {
                result.text.append("- Reason: ");
                result.text.append(MarkdownInline(entry->reason));
                result.text.push_back('\n');
            }
            result.text.push_back('\n');
        }

        result.text.append("## Retained evidence identities\n\n");
        if (retainedIdentities.empty())
        {
            result.text.append("- None supplied.\n\n");
        }
        else
        {
            for (const RetainedEvidenceIdentityRow& row : retainedIdentities)
            {
                AppendEvidenceIdentityMarkdown(result.text, row);
            }
            result.text.push_back('\n');
        }

        result.text.append("## Unavailable items\n\n");
        if (unavailable.empty())
        {
            result.text.append("- None reported.\n\n");
        }
        else
        {
            for (const UnavailableItem& item : unavailable)
            {
                result.text.append("- [");
                result.text.append(MarkdownInline(item.category));
                result.text.append("] ");
                result.text.append(MarkdownInline(item.stableId));
                result.text.append(" - ");
                result.text.append(MarkdownInline(item.label));
                result.text.append(": ");
                result.text.append(MarkdownInline(item.reason));
                result.text.push_back('\n');
            }
            result.text.push_back('\n');
        }

        result.text.append("## Known limitations\n\n");
        if (limitations.empty())
        {
            result.text.append("- None declared.\n");
        }
        else
        {
            for (const std::string& limitation : limitations)
            {
                result.text.append("- ");
                result.text.append(MarkdownInline(limitation));
                result.text.push_back('\n');
            }
        }
        return result;
    }
}
