#pragma once

#include "demos/ShowcaseEvidence.hpp"
#include "demos/ShowcaseProgram.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace RenderingEngine::Demos
{
    enum class ShowcaseWorkflowState : std::uint8_t
    {
        Idle = 0,
        AwaitingA,
        AwaitingB,
        Complete,
        Failed
    };

    enum class ShowcaseWorkflowError : std::uint8_t
    {
        None = 0,
        InvalidState,
        InvalidStart,
        ProviderUnavailable,
        RequestIdentityMismatch,
        GenerationMismatch,
        SubmissionOrderMismatch,
        DuplicateSubmission,
        InvalidProvenance,
        SyntheticEvidenceRejected,
        MissingArtifactToken,
        InvalidArtifactToken
    };

    struct ShowcaseWorkflowStatus
    {
        ShowcaseWorkflowError error = ShowcaseWorkflowError::None;
        std::string message;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == ShowcaseWorkflowError::None;
        }
    };

    struct ShowcaseWorkflowStart
    {
        std::string sceneStableId;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        FixedComparisonAnchor anchor;
        std::string variantAStableId;
        std::string variantBStableId;
        std::uint64_t configGeneration = 0;

        // The renderer/readback owner supplies this fact. L10 copies the token
        // and never infers availability from a scene or algorithm selection.
        ProviderAvailability captureProvider;
    };

    struct ShowcaseCaptureWorkRequest
    {
        std::uint64_t workflowIdentity = 0;
        std::uint64_t requestIdentity = 0;
        ComparisonVariant variant = ComparisonVariant::A;
        std::string sceneStableId;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string variantStableId;
        FixedComparisonAnchor anchor;
        std::uint64_t frameIndex = 0;
        std::uint64_t sampleIndex = 0;
        std::uint64_t configGeneration = 0;
        std::string providerToken;
    };

    struct ShowcaseProviderArtifactRecord
    {
        std::uint64_t workflowIdentity = 0;
        std::uint64_t requestIdentity = 0;
        ComparisonVariant variant = ComparisonVariant::A;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string runId;

        // Paths are provider-supplied, normalized UTF-8 tokens relative to the
        // run directory. Live capture records require the artifact-layout paths
        // captures/image.exr, captures/preview.png, and metadata.json exactly.
        // The workflow neither opens nor verifies these files.
        std::string exrPath;
        std::string pngPath;
        std::string metadataPath;
        EvidenceProvenance provenance;
    };

    struct ShowcaseAbManifestLeg
    {
        ComparisonVariant variant = ComparisonVariant::A;
        std::string variantStableId;
        std::uint64_t requestIdentity = 0;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::uint64_t frameIndex = 0;
        std::uint64_t sampleIndex = 0;
        std::string runId;
        std::string exrPath;
        std::string pngPath;
        std::string metadataPath;
        EvidenceProvenance provenance;
    };

    // Stable, report-ready data only. It is intentionally not a claim that the
    // named files exist or that their pixels have passed numerical/visual QA.
    struct ShowcaseAbManifest
    {
        std::uint64_t workflowIdentity = 0;
        std::uint64_t configGeneration = 0;
        std::string sceneStableId;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        FixedComparisonAnchor anchor;
        std::array<ShowcaseAbManifestLeg, 2> legs;
    };

    // Deterministic UTF-8 JSON suitable for persistence beside provider-owned
    // artifacts. This serializes identities and provenance; it does not inspect
    // the referenced files or promote them to numerical/visual evidence.
    [[nodiscard]] std::string BuildShowcaseAbManifestJson(
        const ShowcaseAbManifest& manifest);

    // Provider-driven, headless A/B orchestration. Start creates both immutable
    // work requests at the same frame/sample coordinate so variant is the only
    // changing input. Submit advances only after a matching provider artifact
    // record arrives; it performs no rendering, timing, file I/O, or image work.
    class ShowcaseWorkflow final
    {
    public:
        [[nodiscard]] ShowcaseWorkflowStatus Start(
            const ShowcaseWorkflowStart& start);
        [[nodiscard]] ShowcaseWorkflowStatus Submit(
            const ShowcaseProviderArtifactRecord& artifact);
        // The composition/controller calls this when the config, scene, or
        // resource tuple changes while a pair is active. Accepted partial work
        // is invalidated visibly and can be recovered with Cancel.
        void InvalidateGenerations(std::string message);
        void Cancel() noexcept;

        [[nodiscard]] ShowcaseWorkflowState State() const noexcept;
        [[nodiscard]] std::span<const ShowcaseCaptureWorkRequest> Requests() const noexcept;
        [[nodiscard]] const ShowcaseCaptureWorkRequest* PendingRequest() const noexcept;
        [[nodiscard]] const ShowcaseAbManifest* CompletedManifest() const noexcept;
        [[nodiscard]] const ShowcaseWorkflowStatus& LastStatus() const noexcept;

    private:
        [[nodiscard]] ShowcaseWorkflowStatus Fail(
            ShowcaseWorkflowError error,
            std::string message);
        [[nodiscard]] ShowcaseWorkflowStatus Reject(
            ShowcaseWorkflowError error,
            std::string message);
        void ResetRunData() noexcept;

        ShowcaseWorkflowState m_state = ShowcaseWorkflowState::Idle;
        ShowcaseWorkflowStatus m_lastStatus;
        std::array<ShowcaseCaptureWorkRequest, 2> m_requests;
        std::array<ShowcaseProviderArtifactRecord, 2> m_artifacts;
        ShowcaseAbManifest m_manifest;
        std::string m_providerToken;
        std::uint64_t m_nextWorkflowIdentity = 1;
        std::uint64_t m_nextRequestIdentity = 1;
        std::size_t m_requestCount = 0;
        std::size_t m_artifactCount = 0;
        bool m_hasManifest = false;
    };
}
