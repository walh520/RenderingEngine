#include "demos/ShowcaseWorkflow.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace
{
    using namespace RenderingEngine::Demos;

    class TestContext final
    {
    public:
        explicit TestContext(std::ostream& output) noexcept
            : m_output(output)
        {
        }

        void Expect(bool condition, std::string_view message)
        {
            if (!condition)
            {
                m_output << "L10 ShowcaseWorkflow test failed: " << message << '\n';
                m_passed = false;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return m_passed;
        }

    private:
        std::ostream& m_output;
        bool m_passed = true;
    };

    [[nodiscard]] ShowcaseWorkflowStart MakeStart(
        Availability availability = Availability::Available)
    {
        ShowcaseWorkflowStart start;
        start.sceneStableId = "cornell";
        start.sceneGeneration = 7u;
        start.resourceGeneration = 11u;
        start.anchor = { "camera:cornell-reference", 0x1234'5678u, 900u };
        start.variantAStableId = "path-tracing:baseline";
        start.variantBStableId = "path-tracing:mis";
        start.configGeneration = 42u;
        start.captureFrameIndex = 77u;
        start.captureSampleIndex = 4096u;
        start.captureProvider = {
            "capture:renderer-readback",
            availability,
            availability == Availability::Available ? "" : "readback lane is offline"
        };
        return start;
    }

    [[nodiscard]] ShowcaseProviderArtifactRecord MakeArtifact(
        const ShowcaseCaptureWorkRequest& request,
        std::string_view runId)
    {
        ShowcaseProviderArtifactRecord artifact;
        artifact.workflowIdentity = request.workflowIdentity;
        artifact.requestIdentity = request.requestIdentity;
        artifact.variant = request.variant;
        artifact.configGeneration = request.configGeneration;
        artifact.sceneGeneration = request.sceneGeneration;
        artifact.resourceGeneration = request.resourceGeneration;
        artifact.sceneStableId = request.sceneStableId;
        artifact.variantStableId = request.variantStableId;
        artifact.anchor = request.anchor;
        artifact.frameIndex = request.frameIndex;
        artifact.sampleIndex = request.sampleIndex;
        artifact.runId = runId;
        artifact.exrPath = "captures/image.exr";
        artifact.pngPath = "captures/preview.png";
        artifact.metadataPath = "metadata.json";
        artifact.provenance = {
            EvidenceSource::ProviderReported,
            request.providerToken,
            "provider-published immutable capture bundle"
        };
        return artifact;
    }

    void TestOneClickPlanAndCompletion(TestContext& test)
    {
        ShowcaseWorkflow workflow;
        test.Expect(static_cast<bool>(workflow.Start(MakeStart())),
            "one Start call must create a valid A/B capture plan");
        test.Expect(workflow.State() == ShowcaseWorkflowState::AwaitingA,
            "a started workflow must await variant A first");

        const std::span<const ShowcaseCaptureWorkRequest> requests = workflow.Requests();
        test.Expect(requests.size() == 2u,
            "one-click start must expose exactly two immutable work requests");
        if (requests.size() != 2u)
        {
            return;
        }

        const ShowcaseCaptureWorkRequest requestA = requests[0];
        const ShowcaseCaptureWorkRequest requestB = requests[1];
        test.Expect(requestA.variant == ComparisonVariant::A
                && requestB.variant == ComparisonVariant::B
                && requestA.requestIdentity != requestB.requestIdentity,
            "work requests must identify ordered A and B legs independently");
        test.Expect(requestA.configGeneration == 42u
                && requestA.sceneGeneration == 7u
                && requestA.resourceGeneration == 11u
                && requestB.configGeneration == requestA.configGeneration
                && requestB.sceneGeneration == requestA.sceneGeneration
                && requestB.resourceGeneration == requestA.resourceGeneration,
            "both requests must freeze config, scene, and resource generations");
        test.Expect(requestA.anchor.cameraPresetToken == requestB.anchor.cameraPresetToken
                && requestA.anchor.baseSeed == requestB.anchor.baseSeed
                && requestA.anchor.animationOriginTick == requestB.anchor.animationOriginTick,
            "camera, base seed, and animation origin must be invariant across A/B");
        test.Expect(requestA.frameIndex == 77u
                && requestA.sampleIndex == 4096u
                && requestB.frameIndex == requestA.frameIndex
                && requestB.sampleIndex == requestA.sampleIndex,
            "one A/B pair must use one frame/sample coordinate to isolate variant");

        const ShowcaseProviderArtifactRecord artifactA = MakeArtifact(requestA, "run-a");
        const ShowcaseProviderArtifactRecord artifactB = MakeArtifact(requestB, "run-b");
        test.Expect(static_cast<bool>(workflow.Submit(artifactA))
                && workflow.State() == ShowcaseWorkflowState::AwaitingB
                && workflow.PendingRequest() != nullptr
                && workflow.PendingRequest()->variant == ComparisonVariant::B,
            "accepting A must advance the headless workflow to AwaitingB");
        const ShowcaseWorkflowStatus retryA = workflow.Submit(artifactA);
        test.Expect(retryA.error == ShowcaseWorkflowError::DuplicateSubmission
                && workflow.State() == ShowcaseWorkflowState::AwaitingB
                && workflow.PendingRequest() != nullptr
                && workflow.PendingRequest()->requestIdentity == requestB.requestIdentity,
            "an exact A retry must preserve accepted A and continue awaiting B");
        test.Expect(static_cast<bool>(workflow.Submit(artifactB))
                && workflow.State() == ShowcaseWorkflowState::Complete,
            "accepting the matching B record must complete the workflow");

        const ShowcaseAbManifest* const manifest = workflow.CompletedManifest();
        test.Expect(manifest != nullptr
                && manifest->workflowIdentity == requestA.workflowIdentity
                && manifest->configGeneration == requestA.configGeneration
                && manifest->sceneGeneration == requestA.sceneGeneration
                && manifest->resourceGeneration == requestA.resourceGeneration
                && manifest->sceneStableId == requestA.sceneStableId,
            "completion must publish a stable workflow/scene/generation manifest");
        if (manifest != nullptr)
        {
            test.Expect(manifest->legs[0].variant == ComparisonVariant::A
                    && manifest->legs[0].variantStableId == requestA.variantStableId
                    && manifest->legs[0].configGeneration == requestA.configGeneration
                    && manifest->legs[0].sceneGeneration == requestA.sceneGeneration
                    && manifest->legs[0].resourceGeneration == requestA.resourceGeneration
                    && manifest->legs[0].runId == "run-a"
                    && manifest->legs[1].variant == ComparisonVariant::B
                    && manifest->legs[1].variantStableId == requestB.variantStableId
                    && manifest->legs[1].sceneGeneration == requestB.sceneGeneration
                    && manifest->legs[1].resourceGeneration == requestB.resourceGeneration
                    && manifest->legs[1].runId == "run-b",
                "manifest leg order and provider artifact paths must remain stable");

            const std::string json = BuildShowcaseAbManifestJson(*manifest);
            test.Expect(json == BuildShowcaseAbManifestJson(*manifest)
                    && json.ends_with("  ]\n}\n")
                    && json.find("\"workflow_identity\": 1") != std::string::npos
                    && json.find("\"config_generation\": 42") != std::string::npos
                    && json.find("\"scene\": {\"stable_id\": \"cornell\", \"generation\": 7}")
                        != std::string::npos
                    && json.find("\"resource_generation\": 11") != std::string::npos
                    && json.find("\"request_identity\": 1")
                        < json.find("\"request_identity\": 2")
                    && json.find("\"frame_index\": 77, \"sample_index\": 4096")
                        != std::string::npos
                    && json.find("\"paths\": {\"exr\": \"captures/image.exr\", \"png\": \"captures/preview.png\", \"metadata\": \"metadata.json\"}")
                        != std::string::npos
                    && json.find("\"provenance\": {\"source\": \"provider-reported\"")
                        != std::string::npos,
                "manifest JSON must deterministically persist every A/B evidence identity field");
        }

        const ShowcaseWorkflowStatus duplicateComplete = workflow.Submit(artifactB);
        test.Expect(duplicateComplete.error == ShowcaseWorkflowError::DuplicateSubmission
                && workflow.State() == ShowcaseWorkflowState::Complete
                && workflow.CompletedManifest() != nullptr,
            "a reliable-provider retry after completion must not revoke the completed manifest");
    }

    void TestProviderAndSubmissionRejections(TestContext& test)
    {
        ShowcaseWorkflow unavailable;
        const ShowcaseWorkflowStatus unavailableStatus =
            unavailable.Start(MakeStart(Availability::Unavailable));
        test.Expect(unavailableStatus.error == ShowcaseWorkflowError::ProviderUnavailable
                && unavailable.State() == ShowcaseWorkflowState::Failed
                && unavailable.Requests().empty(),
            "an unavailable provider must fail closed without emitting work");

        ShowcaseWorkflow outOfOrder;
        test.Expect(static_cast<bool>(outOfOrder.Start(MakeStart())),
            "out-of-order fixture must start");
        const auto orderRequests = outOfOrder.Requests();
        const ShowcaseWorkflowStatus orderStatus =
            outOfOrder.Submit(MakeArtifact(orderRequests[1], "run-b"));
        test.Expect(orderStatus.error == ShowcaseWorkflowError::SubmissionOrderMismatch
                && outOfOrder.State() == ShowcaseWorkflowState::AwaitingA,
            "B submitted before A must be rejected without losing the pending A request");

        ShowcaseWorkflow generation;
        test.Expect(static_cast<bool>(generation.Start(MakeStart())),
            "generation fixture must start");
        ShowcaseProviderArtifactRecord wrongGeneration =
            MakeArtifact(generation.Requests()[0], "run-a");
        ++wrongGeneration.configGeneration;
        const ShowcaseWorkflowStatus generationStatus = generation.Submit(wrongGeneration);
        test.Expect(generationStatus.error == ShowcaseWorkflowError::GenerationMismatch
                && generation.State() == ShowcaseWorkflowState::AwaitingA,
            "another config generation must be rejected without consuming A");

        ShowcaseProviderArtifactRecord wrongSceneGeneration =
            MakeArtifact(generation.Requests()[0], "run-a");
        ++wrongSceneGeneration.sceneGeneration;
        const ShowcaseWorkflowStatus sceneGenerationStatus =
            generation.Submit(wrongSceneGeneration);
        test.Expect(sceneGenerationStatus.error == ShowcaseWorkflowError::GenerationMismatch
                && generation.State() == ShowcaseWorkflowState::AwaitingA,
            "another scene generation must be rejected without consuming A");

        ShowcaseProviderArtifactRecord wrongResourceGeneration =
            MakeArtifact(generation.Requests()[0], "run-a");
        ++wrongResourceGeneration.resourceGeneration;
        const ShowcaseWorkflowStatus resourceGenerationStatus =
            generation.Submit(wrongResourceGeneration);
        test.Expect(resourceGenerationStatus.error == ShowcaseWorkflowError::GenerationMismatch
                && generation.State() == ShowcaseWorkflowState::AwaitingA,
            "another resource generation must be rejected without consuming A");

        ShowcaseWorkflow identity;
        test.Expect(static_cast<bool>(identity.Start(MakeStart())),
            "identity fixture must start");
        ShowcaseProviderArtifactRecord wrongIdentity =
            MakeArtifact(identity.Requests()[0], "run-a");
        ++wrongIdentity.requestIdentity;
        const ShowcaseWorkflowStatus identityStatus = identity.Submit(wrongIdentity);
        test.Expect(identityStatus.error == ShowcaseWorkflowError::RequestIdentityMismatch
                && identity.State() == ShowcaseWorkflowState::AwaitingA,
            "an unknown request identity must be rejected without consuming A");

        ShowcaseProviderArtifactRecord wrongFrame =
            MakeArtifact(identity.Requests()[0], "run-a");
        ++wrongFrame.frameIndex;
        const ShowcaseWorkflowStatus frameStatus = identity.Submit(wrongFrame);
        test.Expect(frameStatus.error == ShowcaseWorkflowError::RequestIdentityMismatch
                && identity.State() == ShowcaseWorkflowState::AwaitingA,
            "another frame coordinate must be rejected without consuming A");

        ShowcaseProviderArtifactRecord wrongAnchor =
            MakeArtifact(identity.Requests()[0], "run-a");
        ++wrongAnchor.anchor.baseSeed;
        const ShowcaseWorkflowStatus anchorStatus = identity.Submit(wrongAnchor);
        test.Expect(anchorStatus.error == ShowcaseWorkflowError::RequestIdentityMismatch
                && identity.State() == ShowcaseWorkflowState::AwaitingA,
            "another comparison anchor must be rejected without consuming A");

        ShowcaseProviderArtifactRecord wrongVariantStableId =
            MakeArtifact(identity.Requests()[0], "run-a");
        wrongVariantStableId.variantStableId = "path-tracing:foreign";
        const ShowcaseWorkflowStatus variantStableIdStatus =
            identity.Submit(wrongVariantStableId);
        test.Expect(variantStableIdStatus.error
                    == ShowcaseWorkflowError::RequestIdentityMismatch
                && identity.State() == ShowcaseWorkflowState::AwaitingA,
            "another stable variant identity must be rejected without consuming A");

        ShowcaseWorkflow duplicate;
        test.Expect(static_cast<bool>(duplicate.Start(MakeStart())),
            "duplicate fixture must start");
        const ShowcaseProviderArtifactRecord duplicateA =
            MakeArtifact(duplicate.Requests()[0], "run-a");
        test.Expect(static_cast<bool>(duplicate.Submit(duplicateA)),
            "duplicate fixture must accept A once");
        const ShowcaseWorkflowStatus duplicateStatus = duplicate.Submit(duplicateA);
        test.Expect(duplicateStatus.error == ShowcaseWorkflowError::DuplicateSubmission
                && duplicate.State() == ShowcaseWorkflowState::AwaitingB,
            "a second record for A must preserve AwaitingB");
        test.Expect(static_cast<bool>(duplicate.Submit(
                MakeArtifact(duplicate.Requests()[1], "run-b")))
                && duplicate.State() == ShowcaseWorkflowState::Complete,
            "a valid B must still complete after an A retry");

        ShowcaseWorkflow synthetic;
        test.Expect(static_cast<bool>(synthetic.Start(MakeStart())),
            "synthetic provenance fixture must start");
        ShowcaseProviderArtifactRecord syntheticArtifact =
            MakeArtifact(synthetic.Requests()[0], "run-a");
        syntheticArtifact.provenance.source = EvidenceSource::SyntheticTest;
        const ShowcaseWorkflowStatus syntheticStatus = synthetic.Submit(syntheticArtifact);
        test.Expect(syntheticStatus.error == ShowcaseWorkflowError::SyntheticEvidenceRejected
                && synthetic.State() == ShowcaseWorkflowState::AwaitingA,
            "SyntheticTest provenance must not masquerade as or consume live evidence");

        ShowcaseWorkflow provenance;
        test.Expect(static_cast<bool>(provenance.Start(MakeStart())),
            "provider provenance fixture must start");
        ShowcaseProviderArtifactRecord wrongProvider =
            MakeArtifact(provenance.Requests()[0], "run-a");
        wrongProvider.provenance.provider = "capture:another-provider";
        const ShowcaseWorkflowStatus providerStatus = provenance.Submit(wrongProvider);
        test.Expect(providerStatus.error == ShowcaseWorkflowError::InvalidProvenance
                && provenance.State() == ShowcaseWorkflowState::AwaitingA,
            "wrong provider provenance must be rejected without consuming A");

        ShowcaseWorkflow missingPath;
        test.Expect(static_cast<bool>(missingPath.Start(MakeStart())),
            "missing-file-token fixture must start");
        ShowcaseProviderArtifactRecord missing =
            MakeArtifact(missingPath.Requests()[0], "run-a");
        missing.metadataPath.clear();
        const ShowcaseWorkflowStatus missingStatus = missingPath.Submit(missing);
        test.Expect(missingStatus.error == ShowcaseWorkflowError::MissingArtifactToken
                && missingPath.State() == ShowcaseWorkflowState::AwaitingA,
            "missing EXR/PNG/metadata path tokens must be rejected without consuming A");

        ShowcaseWorkflow invalidUtf8;
        test.Expect(static_cast<bool>(invalidUtf8.Start(MakeStart())),
            "invalid UTF-8 submission fixture must start");
        ShowcaseProviderArtifactRecord invalidUtf8Artifact =
            MakeArtifact(invalidUtf8.Requests()[0], "run-a");
        invalidUtf8Artifact.runId = std::string{ static_cast<char>(0xc3), '(' };
        const ShowcaseWorkflowStatus invalidUtf8Status =
            invalidUtf8.Submit(invalidUtf8Artifact);
        test.Expect(invalidUtf8Status.error == ShowcaseWorkflowError::InvalidArtifactToken
                && invalidUtf8.State() == ShowcaseWorkflowState::AwaitingA
                && invalidUtf8.PendingRequest() != nullptr,
            "invalid UTF-8 artifact text must be rejected without losing accepted progress");
    }

    void TestRunIdAndArtifactLayoutValidation(TestContext& test)
    {
        constexpr std::array<std::string_view, 12> unsafeRunIds = {
            "nested/run",
            R"(nested\run)",
            "stream:run",
            ".",
            "..",
            ".hidden",
            "run.",
            "run ",
            "bad id",
            "CON",
            "nul.capture",
            "Lpt9.output"
        };
        for (const std::string_view runId : unsafeRunIds)
        {
            ShowcaseWorkflow workflow;
            test.Expect(static_cast<bool>(workflow.Start(MakeStart())),
                "unsafe run-ID fixture must start");
            const ShowcaseWorkflowStatus status = workflow.Submit(
                MakeArtifact(workflow.Requests()[0], runId));
            test.Expect(status.error == ShowcaseWorkflowError::InvalidArtifactToken
                    && workflow.State() == ShowcaseWorkflowState::AwaitingA,
                "unsafe, device, or non-artifact-layout run ID must be rejected recoverably");
        }

        for (std::size_t pathIndex = 0; pathIndex < 3u; ++pathIndex)
        {
            ShowcaseWorkflow workflow;
            test.Expect(static_cast<bool>(workflow.Start(MakeStart())),
                "strict artifact-path fixture must start");
            ShowcaseProviderArtifactRecord artifact =
                MakeArtifact(workflow.Requests()[0], "path-layout-run");
            if (pathIndex == 0u)
            {
                artifact.exrPath = "references/image.exr";
            }
            else if (pathIndex == 1u)
            {
                artifact.pngPath = "captures/alternate.png";
            }
            else
            {
                artifact.metadataPath = "reports/metadata.json";
            }
            const ShowcaseWorkflowStatus status = workflow.Submit(artifact);
            test.Expect(status.error == ShowcaseWorkflowError::InvalidArtifactToken
                    && workflow.State() == ShowcaseWorkflowState::AwaitingA,
                "same-extension alternatives must not replace fixed capture bundle paths");
        }

        ShowcaseWorkflow collision;
        test.Expect(static_cast<bool>(collision.Start(MakeStart())),
            "ASCII case-collision fixture must start");
        test.Expect(static_cast<bool>(collision.Submit(
                MakeArtifact(collision.Requests()[0], "CaseRun"))),
            "ASCII case-collision fixture must accept A");
        const ShowcaseWorkflowStatus collisionStatus = collision.Submit(
            MakeArtifact(collision.Requests()[1], "caserun"));
        test.Expect(collisionStatus.error == ShowcaseWorkflowError::DuplicateSubmission
                && collision.State() == ShowcaseWorkflowState::AwaitingB,
            "A/B run IDs that differ only by ASCII case must collide without losing A");
        test.Expect(static_cast<bool>(collision.Submit(
                MakeArtifact(collision.Requests()[1], "case-run-b")))
                && collision.State() == ShowcaseWorkflowState::Complete,
            "a case-distinct replacement B run ID must still complete the pair");
    }

    void TestCancelAndRestart(TestContext& test)
    {
        ShowcaseWorkflow workflow;
        test.Expect(static_cast<bool>(workflow.Start(MakeStart())),
            "cancel fixture must start");
        const std::uint64_t oldWorkflowIdentity =
            workflow.Requests().front().workflowIdentity;
        ShowcaseProviderArtifactRecord invalid =
            MakeArtifact(workflow.Requests().front(), "run-a");
        invalid.exrPath = "../escape.exr";
        test.Expect(workflow.Submit(invalid).error
                == ShowcaseWorkflowError::InvalidArtifactToken
                && workflow.State() == ShowcaseWorkflowState::AwaitingA,
            "unsafe artifact path fixture must reject without consuming A");

        workflow.Cancel();
        test.Expect(workflow.State() == ShowcaseWorkflowState::Idle
                && workflow.Requests().empty()
                && workflow.PendingRequest() == nullptr
                && workflow.CompletedManifest() == nullptr,
            "Cancel must restore a reusable empty Idle state");
        test.Expect(static_cast<bool>(workflow.Start(MakeStart()))
                && workflow.State() == ShowcaseWorkflowState::AwaitingA
                && workflow.Requests().front().workflowIdentity != oldWorkflowIdentity,
            "a cancelled workflow must restart with a fresh identity");
    }
}

bool RunShowcaseWorkflowTests(std::ostream& output)
{
    TestContext test(output);
    TestOneClickPlanAndCompletion(test);
    TestProviderAndSubmissionRejections(test);
    TestRunIdAndArtifactLayoutValidation(test);
    TestCancelAndRestart(test);
    if (test.Passed())
    {
        output << "L10 ShowcaseWorkflow orchestration tests passed.\n";
    }
    return test.Passed();
}
