#include "demos/ShowcaseWorkflow.hpp"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>

namespace RenderingEngine::Demos
{
    namespace
    {
        inline constexpr std::string_view kExpectedExrPath = "captures/image.exr";
        inline constexpr std::string_view kExpectedPngPath = "captures/preview.png";
        inline constexpr std::string_view kExpectedMetadataPath = "metadata.json";

        [[nodiscard]] bool HasControlCharacter(std::string_view value) noexcept
        {
            return std::any_of(
                value.begin(),
                value.end(),
                [](char character)
                {
                    const unsigned char byte = static_cast<unsigned char>(character);
                    return byte < 0x20u || byte == 0x7fu;
                });
        }

        [[nodiscard]] bool IsValidUtf8(std::string_view value) noexcept
        {
            std::size_t offset = 0u;
            while (offset < value.size())
            {
                const auto lead = static_cast<unsigned char>(value[offset]);
                if (lead <= 0x7fu)
                {
                    ++offset;
                    continue;
                }

                std::size_t sequenceLength = 0u;
                std::uint32_t codePoint = 0u;
                std::uint32_t minimumCodePoint = 0u;
                if (lead >= 0xc2u && lead <= 0xdfu)
                {
                    sequenceLength = 2u;
                    codePoint = lead & 0x1fu;
                    minimumCodePoint = 0x80u;
                }
                else if (lead >= 0xe0u && lead <= 0xefu)
                {
                    sequenceLength = 3u;
                    codePoint = lead & 0x0fu;
                    minimumCodePoint = 0x800u;
                }
                else if (lead >= 0xf0u && lead <= 0xf4u)
                {
                    sequenceLength = 4u;
                    codePoint = lead & 0x07u;
                    minimumCodePoint = 0x10000u;
                }
                else
                {
                    return false;
                }
                if (sequenceLength > value.size() - offset)
                {
                    return false;
                }
                for (std::size_t index = 1u; index < sequenceLength; ++index)
                {
                    const auto continuation =
                        static_cast<unsigned char>(value[offset + index]);
                    if ((continuation & 0xc0u) != 0x80u)
                    {
                        return false;
                    }
                    codePoint = (codePoint << 6u) | (continuation & 0x3fu);
                }
                if (codePoint < minimumCodePoint
                    || codePoint > 0x10ffffu
                    || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
                {
                    return false;
                }
                offset += sequenceLength;
            }
            return true;
        }

        [[nodiscard]] bool IsStableToken(std::string_view value) noexcept
        {
            return !value.empty() && !HasControlCharacter(value);
        }

        [[nodiscard]] char AsciiUpper(char character) noexcept
        {
            return character >= 'a' && character <= 'z'
                ? static_cast<char>(character - ('a' - 'A'))
                : character;
        }

        [[nodiscard]] bool AsciiCaseInsensitiveEqual(
            std::string_view left,
            std::string_view right) noexcept
        {
            return left.size() == right.size()
                && std::equal(
                    left.begin(),
                    left.end(),
                    right.begin(),
                    [](char leftCharacter, char rightCharacter)
                    {
                        return AsciiUpper(leftCharacter) == AsciiUpper(rightCharacter);
                    });
        }

        [[nodiscard]] bool IsReservedWindowsDeviceName(
            std::string_view value) noexcept
        {
            const std::size_t extension = value.find('.');
            std::string base(value.substr(0u, extension));
            std::transform(base.begin(), base.end(), base.begin(), AsciiUpper);
            if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
                || base == "CONIN$" || base == "CONOUT$")
            {
                return true;
            }
            return base.size() == 4u
                && (base.starts_with("COM") || base.starts_with("LPT"))
                && base[3] >= '1' && base[3] <= '9';
        }

        [[nodiscard]] bool IsArtifactLayoutRunId(std::string_view value) noexcept
        {
            if (value.empty()
                || value == "."
                || value == ".."
                || value.front() == '.'
                || value.back() == '.'
                || value.back() == ' '
                || value.find('/') != std::string_view::npos
                || value.find('\\') != std::string_view::npos
                || value.find(':') != std::string_view::npos
                || HasControlCharacter(value)
                || IsReservedWindowsDeviceName(value))
            {
                return false;
            }

            return std::all_of(
                value.begin(),
                value.end(),
                [](char character)
                {
                    const bool isLetter = (character >= 'A' && character <= 'Z')
                        || (character >= 'a' && character <= 'z');
                    const bool isDigit = character >= '0' && character <= '9';
                    return isLetter || isDigit
                        || character == '-'
                        || character == '_'
                        || character == '.';
                });
        }

        [[nodiscard]] bool ArtifactRecordsEqual(
            const ShowcaseProviderArtifactRecord& left,
            const ShowcaseProviderArtifactRecord& right) noexcept
        {
            return left.workflowIdentity == right.workflowIdentity
                && left.requestIdentity == right.requestIdentity
                && left.variant == right.variant
                && left.configGeneration == right.configGeneration
                && left.sceneGeneration == right.sceneGeneration
                && left.resourceGeneration == right.resourceGeneration
                && left.runId == right.runId
                && left.exrPath == right.exrPath
                && left.pngPath == right.pngPath
                && left.metadataPath == right.metadataPath
                && left.provenance.source == right.provenance.source
                && left.provenance.provider == right.provenance.provider
                && left.provenance.detail == right.provenance.detail;
        }

        void AppendUnsigned(std::string& output, std::uint64_t value)
        {
            char buffer[32]{};
            const auto conversion = std::to_chars(
                std::begin(buffer), std::end(buffer), value);
            output.append(buffer, conversion.ptr);
        }

        void AppendJsonString(std::string& output, std::string_view value)
        {
            constexpr char digits[] = "0123456789abcdef";
            output.push_back('"');
            for (const char character : value)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
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
                    if (byte < 0x20u)
                    {
                        output.append("\\u00");
                        output.push_back(digits[(byte >> 4u) & 0x0fu]);
                        output.push_back(digits[byte & 0x0fu]);
                    }
                    else
                    {
                        output.push_back(character);
                    }
                    break;
                }
            }
            output.push_back('"');
        }

        [[nodiscard]] std::string_view VariantToken(ComparisonVariant variant) noexcept
        {
            return variant == ComparisonVariant::A ? "A" : "B";
        }
    }

    std::string BuildShowcaseAbManifestJson(const ShowcaseAbManifest& manifest)
    {
        std::string output;
        output.reserve(1536u);
        output.append("{\n  \"schema\": \"showcase-ab-manifest-v1\",\n  \"workflow_identity\": ");
        AppendUnsigned(output, manifest.workflowIdentity);
        output.append(",\n  \"config_generation\": ");
        AppendUnsigned(output, manifest.configGeneration);
        output.append(",\n  \"scene\": {\"stable_id\": ");
        AppendJsonString(output, manifest.sceneStableId);
        output.append(", \"generation\": ");
        AppendUnsigned(output, manifest.sceneGeneration);
        output.append("},\n  \"resource_generation\": ");
        AppendUnsigned(output, manifest.resourceGeneration);
        output.append(",\n  \"anchor\": {\"camera_preset_token\": ");
        AppendJsonString(output, manifest.anchor.cameraPresetToken);
        output.append(", \"base_seed\": ");
        AppendUnsigned(output, manifest.anchor.baseSeed);
        output.append(", \"animation_origin_tick\": ");
        AppendUnsigned(output, manifest.anchor.animationOriginTick);
        output.append("},\n  \"legs\": [\n");

        for (std::size_t index = 0; index < manifest.legs.size(); ++index)
        {
            const ShowcaseAbManifestLeg& leg = manifest.legs[index];
            output.append("    {\"variant\": ");
            AppendJsonString(output, VariantToken(leg.variant));
            output.append(", \"variant_stable_id\": ");
            AppendJsonString(output, leg.variantStableId);
            output.append(", \"request_identity\": ");
            AppendUnsigned(output, leg.requestIdentity);
            output.append(", \"config_generation\": ");
            AppendUnsigned(output, leg.configGeneration);
            output.append(", \"scene_generation\": ");
            AppendUnsigned(output, leg.sceneGeneration);
            output.append(", \"resource_generation\": ");
            AppendUnsigned(output, leg.resourceGeneration);
            output.append(", \"frame_index\": ");
            AppendUnsigned(output, leg.frameIndex);
            output.append(", \"sample_index\": ");
            AppendUnsigned(output, leg.sampleIndex);
            output.append(", \"run_id\": ");
            AppendJsonString(output, leg.runId);
            output.append(", \"paths\": {\"exr\": ");
            AppendJsonString(output, leg.exrPath);
            output.append(", \"png\": ");
            AppendJsonString(output, leg.pngPath);
            output.append(", \"metadata\": ");
            AppendJsonString(output, leg.metadataPath);
            output.append("}, \"provenance\": {\"source\": ");
            AppendJsonString(output, EvidenceSourceToken(leg.provenance.source));
            output.append(", \"provider\": ");
            AppendJsonString(output, leg.provenance.provider);
            output.append(", \"detail\": ");
            AppendJsonString(output, leg.provenance.detail);
            output.append("}}");
            output.append(index + 1u == manifest.legs.size() ? "\n" : ",\n");
        }
        output.append("  ]\n}\n");
        return output;
    }

    ShowcaseWorkflowStatus ShowcaseWorkflow::Start(
        const ShowcaseWorkflowStart& start)
    {
        if (m_state != ShowcaseWorkflowState::Idle)
        {
            return Fail(
                ShowcaseWorkflowError::InvalidState,
                "A workflow can start only from Idle; cancel the current run first.");
        }

        ResetRunData();
        if (!IsValidUtf8(start.sceneStableId)
            || !IsValidUtf8(start.anchor.cameraPresetToken)
            || !IsValidUtf8(start.variantAStableId)
            || !IsValidUtf8(start.variantBStableId)
            || !IsValidUtf8(start.captureProvider.providerToken)
            || !IsValidUtf8(start.captureProvider.reason))
        {
            return Fail(
                ShowcaseWorkflowError::InvalidStart,
                "All A/B start strings must be valid UTF-8.");
        }
        if (!IsStableToken(start.sceneStableId)
            || !IsStableToken(start.anchor.cameraPresetToken)
            || !IsStableToken(start.variantAStableId)
            || !IsStableToken(start.variantBStableId)
            || start.variantAStableId == start.variantBStableId)
        {
            return Fail(
                ShowcaseWorkflowError::InvalidStart,
                "Scene, camera, and two distinct variant stable IDs are required.");
        }
        if (!IsStableToken(start.captureProvider.providerToken))
        {
            return Fail(
                ShowcaseWorkflowError::InvalidStart,
                "A non-empty capture provider token is required.");
        }
        if (start.captureProvider.state != Availability::Available)
        {
            std::string message = "Capture provider is unavailable.";
            if (!start.captureProvider.reason.empty())
            {
                message.append(" ");
                message.append(start.captureProvider.reason);
            }
            return Fail(ShowcaseWorkflowError::ProviderUnavailable, std::move(message));
        }
        if (m_nextWorkflowIdentity == 0
            || m_nextRequestIdentity == 0
            || m_nextRequestIdentity == std::numeric_limits<std::uint64_t>::max())
        {
            return Fail(
                ShowcaseWorkflowError::InvalidState,
                "Workflow request identity space is exhausted.");
        }

        FixedAbComparisonSession comparison(start.anchor);
        if (!comparison.IsValid())
        {
            return Fail(
                ShowcaseWorkflowError::InvalidStart,
                "The fixed A/B comparison anchor is invalid.");
        }

        const std::uint64_t workflowIdentity = m_nextWorkflowIdentity++;
        const std::uint64_t requestAIdentity = m_nextRequestIdentity++;
        const std::uint64_t requestBIdentity = m_nextRequestIdentity++;
        m_providerToken = start.captureProvider.providerToken;

        const FixedComparisonSnapshot snapshotA = comparison.Snapshot();
        ShowcaseCaptureWorkRequest& requestA = m_requests[0];
        requestA.workflowIdentity = workflowIdentity;
        requestA.requestIdentity = requestAIdentity;
        requestA.variant = ComparisonVariant::A;
        requestA.sceneStableId = start.sceneStableId;
        requestA.sceneGeneration = start.sceneGeneration;
        requestA.resourceGeneration = start.resourceGeneration;
        requestA.variantStableId = start.variantAStableId;
        requestA.anchor = snapshotA.anchor;
        requestA.frameIndex = snapshotA.frameIndex;
        requestA.sampleIndex = snapshotA.sampleIndex;
        requestA.configGeneration = start.configGeneration;
        requestA.providerToken = m_providerToken;

        comparison.SelectVariant(ComparisonVariant::B);
        const FixedComparisonSnapshot snapshotB = comparison.Snapshot();
        ShowcaseCaptureWorkRequest& requestB = m_requests[1];
        requestB.workflowIdentity = workflowIdentity;
        requestB.requestIdentity = requestBIdentity;
        requestB.variant = ComparisonVariant::B;
        requestB.sceneStableId = start.sceneStableId;
        requestB.sceneGeneration = start.sceneGeneration;
        requestB.resourceGeneration = start.resourceGeneration;
        requestB.variantStableId = start.variantBStableId;
        requestB.anchor = snapshotB.anchor;
        requestB.frameIndex = snapshotB.frameIndex;
        requestB.sampleIndex = snapshotB.sampleIndex;
        requestB.configGeneration = start.configGeneration;
        requestB.providerToken = m_providerToken;

        m_requestCount = m_requests.size();
        m_state = ShowcaseWorkflowState::AwaitingA;
        m_lastStatus = {};
        return m_lastStatus;
    }

    ShowcaseWorkflowStatus ShowcaseWorkflow::Submit(
        const ShowcaseProviderArtifactRecord& artifact)
    {
        if (m_requestCount > m_requests.size()
            || m_artifactCount > m_artifacts.size())
        {
            return Fail(
                ShowcaseWorkflowError::InvalidState,
                "Internal workflow record counts are inconsistent.");
        }

        if (!IsValidUtf8(artifact.runId)
            || !IsValidUtf8(artifact.exrPath)
            || !IsValidUtf8(artifact.pngPath)
            || !IsValidUtf8(artifact.metadataPath))
        {
            return Reject(
                ShowcaseWorkflowError::InvalidArtifactToken,
                "Run ID and artifact paths must be valid UTF-8.");
        }
        if (!IsValidUtf8(artifact.provenance.provider)
            || !IsValidUtf8(artifact.provenance.detail))
        {
            return Reject(
                ShowcaseWorkflowError::InvalidProvenance,
                "Artifact provenance strings must be valid UTF-8.");
        }

        for (std::size_t index = 0; index < m_artifactCount; ++index)
        {
            if (artifact.workflowIdentity == m_artifacts[index].workflowIdentity
                && artifact.requestIdentity == m_artifacts[index].requestIdentity)
            {
                return Reject(
                    ShowcaseWorkflowError::DuplicateSubmission,
                    ArtifactRecordsEqual(artifact, m_artifacts[index])
                        ? "The provider retried an already accepted artifact record."
                        : "The provider supplied a conflicting record for an accepted request.");
            }
        }

        if (m_state != ShowcaseWorkflowState::AwaitingA
            && m_state != ShowcaseWorkflowState::AwaitingB)
        {
            return Reject(
                ShowcaseWorkflowError::InvalidState,
                "Artifacts are accepted only while AwaitingA or AwaitingB.");
        }

        const std::size_t expectedIndex =
            m_state == ShowcaseWorkflowState::AwaitingA ? 0u : 1u;
        if (m_requestCount != m_requests.size()
            || m_artifactCount != expectedIndex
            || m_requests[0].variant != ComparisonVariant::A
            || m_requests[1].variant != ComparisonVariant::B
            || m_requests[0].workflowIdentity != m_requests[1].workflowIdentity
            || m_requests[0].configGeneration != m_requests[1].configGeneration
            || m_requests[0].sceneGeneration != m_requests[1].sceneGeneration
            || m_requests[0].resourceGeneration != m_requests[1].resourceGeneration
            || m_requests[0].frameIndex != m_requests[1].frameIndex
            || m_requests[0].sampleIndex != m_requests[1].sampleIndex)
        {
            return Fail(
                ShowcaseWorkflowError::InvalidState,
                "Internal A/B workflow invariants are inconsistent.");
        }
        const ShowcaseCaptureWorkRequest& expected = m_requests[expectedIndex];

        if (artifact.workflowIdentity != expected.workflowIdentity)
        {
            return Reject(
                ShowcaseWorkflowError::RequestIdentityMismatch,
                "Artifact workflow identity does not match the active workflow.");
        }
        if (artifact.variant != expected.variant)
        {
            return Reject(
                ShowcaseWorkflowError::SubmissionOrderMismatch,
                "Artifact variant does not match the currently awaited A/B leg.");
        }
        if (artifact.requestIdentity != expected.requestIdentity)
        {
            return Reject(
                ShowcaseWorkflowError::RequestIdentityMismatch,
                "Artifact request identity does not match the awaited work request.");
        }
        if (artifact.configGeneration != expected.configGeneration)
        {
            return Reject(
                ShowcaseWorkflowError::GenerationMismatch,
                "Artifact config generation does not match the work request.");
        }
        if (artifact.sceneGeneration != expected.sceneGeneration
            || artifact.resourceGeneration != expected.resourceGeneration)
        {
            return Reject(
                ShowcaseWorkflowError::GenerationMismatch,
                "Artifact scene/resource generation does not match the work request.");
        }
        if (artifact.provenance.source == EvidenceSource::SyntheticTest)
        {
            return Reject(
                ShowcaseWorkflowError::SyntheticEvidenceRejected,
                "SyntheticTest artifacts cannot satisfy a live showcase workflow.");
        }
        if (artifact.provenance.source != EvidenceSource::ProviderReported
            || artifact.provenance.provider != expected.providerToken
            || artifact.provenance.detail.empty())
        {
            return Reject(
                ShowcaseWorkflowError::InvalidProvenance,
                "Live artifacts require matching ProviderReported provenance and detail.");
        }
        if (artifact.runId.empty()
            || artifact.exrPath.empty()
            || artifact.pngPath.empty()
            || artifact.metadataPath.empty())
        {
            return Reject(
                ShowcaseWorkflowError::MissingArtifactToken,
                "Run ID and EXR, PNG, and metadata path tokens are required.");
        }
        if (!IsArtifactLayoutRunId(artifact.runId))
        {
            return Reject(
                ShowcaseWorkflowError::InvalidArtifactToken,
                "Run ID must be a safe, normalized, single artifact-layout path segment.");
        }
        if (artifact.exrPath != kExpectedExrPath
            || artifact.pngPath != kExpectedPngPath
            || artifact.metadataPath != kExpectedMetadataPath)
        {
            return Reject(
                ShowcaseWorkflowError::InvalidArtifactToken,
                "Artifact paths must exactly match the capture bundle layout.");
        }
        if (expectedIndex == 1u
            && AsciiCaseInsensitiveEqual(artifact.runId, m_artifacts[0].runId))
        {
            return Reject(
                ShowcaseWorkflowError::DuplicateSubmission,
                "Variant A and B must use ASCII-case-distinct provider run IDs.");
        }

        m_artifacts[expectedIndex] = artifact;
        ++m_artifactCount;
        if (expectedIndex == 0u)
        {
            m_state = ShowcaseWorkflowState::AwaitingB;
            m_lastStatus = {};
            return m_lastStatus;
        }

        m_manifest.workflowIdentity = expected.workflowIdentity;
        m_manifest.configGeneration = expected.configGeneration;
        m_manifest.sceneStableId = expected.sceneStableId;
        m_manifest.sceneGeneration = expected.sceneGeneration;
        m_manifest.resourceGeneration = expected.resourceGeneration;
        m_manifest.anchor = expected.anchor;
        for (std::size_t index = 0; index < m_requests.size(); ++index)
        {
            const ShowcaseCaptureWorkRequest& request = m_requests[index];
            const ShowcaseProviderArtifactRecord& accepted = m_artifacts[index];
            m_manifest.legs[index] = {
                request.variant,
                request.variantStableId,
                request.requestIdentity,
                request.configGeneration,
                request.sceneGeneration,
                request.resourceGeneration,
                request.frameIndex,
                request.sampleIndex,
                accepted.runId,
                accepted.exrPath,
                accepted.pngPath,
                accepted.metadataPath,
                accepted.provenance
            };
        }
        m_hasManifest = true;
        m_state = ShowcaseWorkflowState::Complete;
        m_lastStatus = {};
        return m_lastStatus;
    }

    void ShowcaseWorkflow::Cancel() noexcept
    {
        ResetRunData();
        m_state = ShowcaseWorkflowState::Idle;
        m_lastStatus = {};
    }

    void ShowcaseWorkflow::InvalidateGenerations(std::string message)
    {
        if (m_state != ShowcaseWorkflowState::AwaitingA
            && m_state != ShowcaseWorkflowState::AwaitingB)
        {
            return;
        }
        if (message.empty())
        {
            message = "The active A/B generations were invalidated by composition.";
        }
        static_cast<void>(Fail(
            ShowcaseWorkflowError::GenerationMismatch,
            std::move(message)));
    }

    ShowcaseWorkflowState ShowcaseWorkflow::State() const noexcept
    {
        return m_state;
    }

    std::span<const ShowcaseCaptureWorkRequest> ShowcaseWorkflow::Requests() const noexcept
    {
        return std::span<const ShowcaseCaptureWorkRequest>(
            m_requests.data(),
            m_requestCount);
    }

    const ShowcaseCaptureWorkRequest* ShowcaseWorkflow::PendingRequest() const noexcept
    {
        if (m_state == ShowcaseWorkflowState::AwaitingA)
        {
            return &m_requests[0];
        }
        if (m_state == ShowcaseWorkflowState::AwaitingB)
        {
            return &m_requests[1];
        }
        return nullptr;
    }

    const ShowcaseAbManifest* ShowcaseWorkflow::CompletedManifest() const noexcept
    {
        return m_hasManifest && m_state == ShowcaseWorkflowState::Complete
            ? &m_manifest
            : nullptr;
    }

    const ShowcaseWorkflowStatus& ShowcaseWorkflow::LastStatus() const noexcept
    {
        return m_lastStatus;
    }

    ShowcaseWorkflowStatus ShowcaseWorkflow::Fail(
        ShowcaseWorkflowError error,
        std::string message)
    {
        if (m_state != ShowcaseWorkflowState::Complete || !m_hasManifest)
        {
            m_state = ShowcaseWorkflowState::Failed;
            m_hasManifest = false;
        }
        m_lastStatus = { error, std::move(message) };
        return m_lastStatus;
    }

    ShowcaseWorkflowStatus ShowcaseWorkflow::Reject(
        ShowcaseWorkflowError error,
        std::string message)
    {
        m_lastStatus = { error, std::move(message) };
        return m_lastStatus;
    }

    void ShowcaseWorkflow::ResetRunData() noexcept
    {
        m_requests = {};
        m_artifacts = {};
        m_manifest = {};
        m_providerToken.clear();
        m_requestCount = 0;
        m_artifactCount = 0;
        m_hasManifest = false;
    }
}
