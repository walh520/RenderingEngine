#include "demos/ShowcaseReport.hpp"

#include <array>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace RenderingEngine::Demos;

    class TestContext final
    {
    public:
        explicit TestContext(std::ostream& output)
            : output_(output)
        {
        }

        void Expect(bool condition, std::string_view message)
        {
            if (!condition)
            {
                output_ << "FAILED: " << message << '\n';
                passed_ = false;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return passed_;
        }

    private:
        std::ostream& output_;
        bool passed_ = true;
    };

    [[nodiscard]] ImageComparisonResult MakeExactComparison(
        std::size_t componentCount)
    {
        ImageComparisonResult comparison;
        comparison.comparedComponentCount = componentCount;
        comparison.rmse = 0.0;
        comparison.psnr = std::numeric_limits<double>::infinity();
        comparison.psnrIsPositiveInfinity = true;
        comparison.maximumAbsoluteError = 0.0;
        return comparison;
    }

    [[nodiscard]] ShowcaseEvidenceIdentity MakeLiveEvidence(
        std::string artifactIdentity)
    {
        ShowcaseEvidenceIdentity identity;
        identity.artifactIdentity = std::move(artifactIdentity);
        identity.provenance = {
            EvidenceSource::ProviderReported,
            "showcase-report-provider",
            "provider retained identity"
        };
        identity.configGeneration = 17u;
        identity.sceneStableId = "cornell";
        identity.sceneGeneration = 5u;
        identity.resourceGeneration = 11u;
        identity.frameIndex = 23u;
        identity.sampleIndex = 91u;
        return identity;
    }

    [[nodiscard]] ReferenceComparisonRecord MakeReferenceRecord(
        std::string candidateId,
        ReportEvidenceClass classification = ReportEvidenceClass::ImportedArtifact,
        EvidenceSource source = EvidenceSource::ProviderReported)
    {
        ReferenceComparisonRecord record;
        record.candidate = {
            classification,
            std::move(candidateId),
            { source, "candidate\"provider\\path\nline", "candidate detail" },
            17u,
            4u,
            1u,
            1u
        };
        record.reference = {
            classification,
            "reference-golden",
            { source, "reference-provider", "reference detail" },
            3u,
            1u,
            1u,
            1u
        };
        record.candidate.sceneStableId = "cornell";
        record.candidate.cameraPresetToken = "cornell-hero";
        record.candidate.baseSeed = 1337u;
        record.candidate.sampleIndex = 17u;
        record.candidate.sceneGeneration = 5u;
        record.candidate.resourceGeneration = 11u;
        record.candidate.accumulatedSamplesPerPixel = 64u;
        record.reference.sceneStableId = "cornell";
        record.reference.cameraPresetToken = "cornell-hero";
        record.reference.baseSeed = 1337u;
        record.reference.sampleIndex = 4096u;
        record.reference.sceneGeneration = 5u;
        record.reference.resourceGeneration = 11u;
        record.reference.accumulatedSamplesPerPixel = 4096u;
        record.reference.configGeneration = record.candidate.configGeneration;
        record.comparison = MakeExactComparison(4u);
        return record;
    }

    void TestStableRfc4180Csv(TestContext& test)
    {
        const ShowcaseEvidenceIdentity algorithmEvidence =
            MakeLiveEvidence("artifacts/algorithm-runtime.json");
        const std::array<AlgorithmCompletionEntry, 3> algorithms = {
            AlgorithmCompletionEntry{
                { "z-algorithm", "Zulu", "lighting", "L9", "provider:z" },
                AlgorithmCompletionState::Implemented,
                {}
            },
            AlgorithmCompletionEntry{
                { "a-algorithm", "Alpha, \"quoted\"\nlabel", "sampling", "L6",
                    "provider:a" },
                AlgorithmCompletionState::Unavailable,
                "Provider, not available."
            },
            AlgorithmCompletionEntry{
                { "m-algorithm", "Measured", "reference", "L3", "provider:m" },
                AlgorithmCompletionState::RuntimeValidated,
                {},
                algorithmEvidence
            }
        };
        const ShowcaseTextResult algorithmCsv =
            BuildAlgorithmCompletionMatrixCsv(algorithms);
        test.Expect(static_cast<bool>(algorithmCsv),
            "valid algorithm completion entries must serialize");
        test.Expect(
            algorithmCsv.text.find("a-algorithm")
                < algorithmCsv.text.find("z-algorithm"),
            "algorithm CSV rows must use stable-token order");
        test.Expect(
            algorithmCsv.text.find("\"Alpha, \"\"quoted\"\"\r\nlabel\"")
                != std::string::npos,
            "algorithm CSV must quote commas, quotes, and canonical CRLF line breaks");
        test.Expect(
            algorithmCsv.text.starts_with(
                "stable_token,label,category,owner,provider_token,state,reason,"
                "evidence_artifact,evidence_source,evidence_provider,evidence_detail,"
                "config_generation,scene_stable_id,scene_generation,resource_generation,"
                "frame_index,sample_index\r\n"),
            "algorithm CSV must have a stable RFC 4180 header");
        test.Expect(
            algorithmCsv.text.find(
                "artifacts/algorithm-runtime.json,provider-reported,"
                "showcase-report-provider,provider retained identity,17,cornell,5,11,23,91")
                != std::string::npos,
            "algorithm CSV must retain artifact, provider, config, scene/resource, frame, and sample identity");

        const std::array<AssetLicenseEntry, 2> assets = {
            AssetLicenseEntry{
                "z-asset", "https://example.test/z", "Zed", "CC0",
                "licenses/z.md", "sha256:z"
            },
            AssetLicenseEntry{
                "a-asset", "https://example.test/a?q=1,2", "Author \"A\"", "CC-BY",
                "licenses/a.md", "sha256:a"
            }
        };
        const ShowcaseTextResult assetCsv = BuildAssetLicenseCsv(assets);
        test.Expect(static_cast<bool>(assetCsv), "valid asset licenses must serialize");
        test.Expect(
            assetCsv.text.find("a-asset") < assetCsv.text.find("z-asset"),
            "asset license CSV rows must use stable-token order");
        test.Expect(
            assetCsv.text.find("\"https://example.test/a?q=1,2\"")
                != std::string::npos
                && assetCsv.text.find("\"Author \"\"A\"\"\"")
                    != std::string::npos,
            "asset license CSV must apply RFC 4180 escaping");

        const ShowcaseEvidenceIdentity captureEvidence =
            MakeLiveEvidence("captures/final-approved.json");
        const std::array<FinalVideoShot, 3> shots = {
            FinalVideoShot{
                { "z-shot", "Zulu shot", "capture:z", "Close" },
                FinalVideoShotState::Ready,
                {}
            },
            FinalVideoShot{
                { "a-shot", "Alpha, shot", "capture:a", "Open \"strong\"" },
                FinalVideoShotState::Unavailable,
                "Capture provider is unavailable."
            },
            FinalVideoShot{
                { "m-shot", "Measured shot", "capture:m", "Retain evidence" },
                FinalVideoShotState::Approved,
                {},
                captureEvidence
            }
        };
        const ShowcaseTextResult shotCsv = BuildFinalVideoShotListCsv(shots);
        test.Expect(static_cast<bool>(shotCsv), "valid final-video shots must serialize");
        test.Expect(
            shotCsv.text.find("a-shot") < shotCsv.text.find("z-shot"),
            "final-video CSV rows must use stable-token order");
        test.Expect(
            shotCsv.text.find("\"Alpha, shot\"") != std::string::npos
                && shotCsv.text.find("\"Open \"\"strong\"\"\"")
                    != std::string::npos,
            "final-video CSV must apply RFC 4180 escaping");
        test.Expect(
            shotCsv.text.starts_with(
                "stable_token,label,capture_shot_token,narrative_purpose,state,reason,"
                "evidence_artifact,evidence_source,evidence_provider,evidence_detail,"
                "config_generation,scene_stable_id,scene_generation,resource_generation,"
                "frame_index,sample_index\r\n")
                && shotCsv.text.find(
                    "captures/final-approved.json,provider-reported,"
                    "showcase-report-provider,provider retained identity,17,cornell,5,11,23,91")
                    != std::string::npos,
            "final-video CSV must retain the structured capture evidence identity");

        const std::array<AssetLicenseEntry, 2> duplicates = {
            assets.front(), assets.front()
        };
        test.Expect(
            BuildAssetLicenseCsv(duplicates).error
                == ShowcaseReportError::DuplicateStableId,
            "CSV serializers must reject duplicate stable IDs");
    }

    void TestStructuredLiveIdentityValidation(TestContext& test)
    {
        ShowcaseEvidenceIdentity missingSceneGeneration =
            MakeLiveEvidence("artifacts/missing-scene-generation.json");
        missingSceneGeneration.sceneGeneration.reset();
        const AlgorithmCompletionEntry invalidAlgorithm = {
            { "runtime-algorithm", "Runtime algorithm", "integrator", "L3",
                "provider:runtime" },
            AlgorithmCompletionState::RuntimeValidated,
            {},
            missingSceneGeneration
        };
        test.Expect(
            BuildAlgorithmCompletionMatrixCsv(
                std::span{ &invalidAlgorithm, 1u }).error
                == ShowcaseReportError::InvalidProvenance,
            "runtime algorithm CSV must reject a missing scene generation");
        ShowcaseMarkdownReportInput invalidMarkdown;
        invalidMarkdown.title = "Invalid runtime identity";
        invalidMarkdown.algorithmCompletion =
            std::span{ &invalidAlgorithm, 1u };
        test.Expect(
            BuildShowcaseMarkdownReport(invalidMarkdown).error
                == ShowcaseReportError::InvalidProvenance,
            "Markdown report must reject the same incomplete runtime identity");

        ShowcaseEvidenceIdentity missingResourceGeneration =
            MakeLiveEvidence("artifacts/missing-resource-generation.json");
        missingResourceGeneration.resourceGeneration.reset();
        const FinalVideoShot invalidShot = {
            { "captured-shot", "Captured shot", "capture:captured", "Evidence" },
            FinalVideoShotState::Captured,
            {},
            missingResourceGeneration
        };
        test.Expect(
            BuildFinalVideoShotListCsv(std::span{ &invalidShot, 1u }).error
                == ShowcaseReportError::InvalidProvenance,
            "captured final-video CSV must reject a missing resource generation");

        ShowcaseEvidenceIdentity synthetic =
            MakeLiveEvidence("artifacts/synthetic.json");
        synthetic.provenance.source = EvidenceSource::SyntheticTest;
        const AlgorithmCompletionEntry syntheticAlgorithm = {
            { "synthetic-runtime", "Synthetic runtime", "integrator", "L3",
                "provider:synthetic" },
            AlgorithmCompletionState::VisualAccepted,
            {},
            synthetic
        };
        const FinalVideoShot syntheticShot = {
            { "synthetic-shot", "Synthetic shot", "capture:synthetic", "Evidence" },
            FinalVideoShotState::Approved,
            {},
            synthetic
        };
        test.Expect(
            BuildAlgorithmCompletionMatrixCsv(
                std::span{ &syntheticAlgorithm, 1u }).error
                    == ShowcaseReportError::SyntheticLiveConflict
                && BuildFinalVideoShotListCsv(
                    std::span{ &syntheticShot, 1u }).error
                    == ShowcaseReportError::SyntheticLiveConflict,
            "SyntheticTest identities must never serialize as runtime, visual, captured, or approved evidence");

        const AlgorithmCompletionEntry implementedWithoutEvidence = {
            { "implemented-only", "Implemented only", "integrator", "L3",
                "provider:implemented" },
            AlgorithmCompletionState::Implemented,
            {}
        };
        test.Expect(static_cast<bool>(BuildAlgorithmCompletionMatrixCsv(
                std::span{ &implementedWithoutEvidence, 1u })),
            "Implemented claims must not require runtime evidence");
    }

    void TestReferenceJsonAndProvenance(TestContext& test)
    {
        const std::array<ReferenceComparisonRecord, 2> comparisons = {
            MakeReferenceRecord("z-candidate"),
            MakeReferenceRecord("a-candidate")
        };
        const ShowcaseTextResult json = BuildReferenceComparisonJson(comparisons);
        test.Expect(static_cast<bool>(json),
            "successful reference comparisons must serialize");
        test.Expect(
            json.text.find("a-candidate") < json.text.find("z-candidate"),
            "reference comparisons must use stable identity order");
        test.Expect(
            json.text.find("candidate\\\"provider\\\\path\\nline")
                != std::string::npos,
            "reference JSON must escape quotes, slashes, and newlines");
        test.Expect(
            json.text.find("\"psnr\": null") != std::string::npos
                && json.text.find("\"psnr_positive_infinity\": true")
                    != std::string::npos,
            "positive-infinite PSNR must use valid JSON plus an explicit flag");
        test.Expect(
            json.text.find("\"frame_index\": 17") != std::string::npos
                && json.text.find("\"config_generation\": 4")
                    != std::string::npos
                && json.text.find("\"scene_stable_id\": \"cornell\"")
                    != std::string::npos
                && json.text.find("\"camera_preset_token\": \"cornell-hero\"")
                    != std::string::npos
                && json.text.find("\"base_seed\": 1337") != std::string::npos
                && json.text.find("\"accumulated_spp\": 4096")
                    != std::string::npos
                && json.text.find("\"extent\": {\"width\": 1, \"height\": 1}")
                    != std::string::npos,
            "reference JSON must retain scene, camera, seed, sample, generation, SPP, and extent identity");

        ReferenceComparisonRecord mismatchedScene =
            MakeReferenceRecord("mismatched-scene");
        mismatchedScene.reference.sceneStableId = "sponza";
        test.Expect(
            BuildReferenceComparisonJson(std::span{ &mismatchedScene, 1u }).error
                == ShowcaseReportError::InvalidInput,
            "reference comparison must reject a scene identity mismatch");

        ReferenceComparisonRecord mismatchedGeneration =
            MakeReferenceRecord("mismatched-generation");
        ++mismatchedGeneration.reference.resourceGeneration;
        test.Expect(
            BuildReferenceComparisonJson(
                std::span{ &mismatchedGeneration, 1u }).error
                == ShowcaseReportError::InvalidInput,
            "reference comparison must reject a resource generation mismatch");

        ReferenceComparisonRecord failed = MakeReferenceRecord("failed-candidate");
        failed.comparison.error = EvidenceError::InvalidImage;
        failed.comparison.message = "Comparison failed.";
        test.Expect(
            BuildReferenceComparisonJson(std::span{ &failed, 1u }).error
                == ShowcaseReportError::InvalidMetrics,
            "non-success comparison results must be rejected");

        ReferenceComparisonRecord wrongCount = MakeReferenceRecord("wrong-count");
        wrongCount.comparison.comparedComponentCount = 3u;
        test.Expect(
            BuildReferenceComparisonJson(std::span{ &wrongCount, 1u }).error
                == ShowcaseReportError::InvalidMetrics,
            "comparison metrics must match the declared image extent");

        ReferenceComparisonRecord syntheticLive = MakeReferenceRecord(
            "synthetic-live",
            ReportEvidenceClass::LiveRuntime,
            EvidenceSource::SyntheticTest);
        test.Expect(
            BuildReferenceComparisonJson(std::span{ &syntheticLive, 1u }).error
                == ShowcaseReportError::SyntheticLiveConflict,
            "SyntheticTest provenance must never serialize as live runtime evidence");

        ReferenceComparisonRecord synthetic = MakeReferenceRecord(
            "synthetic-candidate",
            ReportEvidenceClass::SyntheticTest,
            EvidenceSource::SyntheticTest);
        const ShowcaseTextResult syntheticJson =
            BuildReferenceComparisonJson(std::span{ &synthetic, 1u });
        test.Expect(static_cast<bool>(syntheticJson)
                && syntheticJson.text.find("\"classification\": \"synthetic-test\"")
                    != std::string::npos
                && syntheticJson.text.find("\"source\": \"synthetic-test\"")
                    != std::string::npos,
            "synthetic comparisons must retain explicit classification and source labels");
    }

    void TestMarkdownEvidenceBoundaries(TestContext& test)
    {
        const std::array<ShowcaseEvidenceBoundaryEntry, 3> evidence = {
            ShowcaseEvidenceBoundaryEntry{
                ShowcaseEvidenceBoundary::Performance,
                ShowcaseEvidenceState::NotRun,
                ReportEvidenceClass::ImportedArtifact,
                {},
                {},
                "Performance data validation was deferred."
            },
            ShowcaseEvidenceBoundaryEntry{
                ShowcaseEvidenceBoundary::Numeric,
                ShowcaseEvidenceState::Passed,
                ReportEvidenceClass::SyntheticTest,
                { EvidenceSource::SyntheticTest, "unit-tests", "CPU fixture" },
                "Synthetic serializer invariants passed.",
                {}
            },
            ShowcaseEvidenceBoundaryEntry{
                ShowcaseEvidenceBoundary::Static,
                ShowcaseEvidenceState::Passed,
                ReportEvidenceClass::ImportedArtifact,
                { EvidenceSource::ProviderReported, "source-audit", "local files" },
                "L10 ownership audit passed.",
                {}
            }
        };
        const std::array<std::string, 3> limitations = {
            "Visual acceptance not run.",
            "Live providers are not integrated.",
            "Visual acceptance not run."
        };
        const std::array<ResolvedShowcaseSceneCard, 1> scenes = {
            ResolvedShowcaseSceneCard{
                { "scene-a", "Scene A", "L2", "Provider-owned scene." },
                { Availability::Unavailable, "L2 scene provider is absent." }
            }
        };
        const AssetLicenseGate licenseGate = {
            { Availability::Unavailable, "Asset license gate is closed." },
            { { "asset-a", "License document is missing." } }
        };
        const ShowcaseEvidenceIdentity algorithmIdentity =
            MakeLiveEvidence("artifacts/markdown-runtime.json");
        const ShowcaseEvidenceIdentity captureIdentity =
            MakeLiveEvidence("captures/markdown-approved.json");
        const std::array<AlgorithmCompletionEntry, 1> algorithms = {
            AlgorithmCompletionEntry{
                { "runtime-reference", "Runtime reference", "reference", "L3",
                    "provider:runtime-reference" },
                AlgorithmCompletionState::RuntimeValidated,
                {},
                algorithmIdentity
            }
        };
        const std::array<FinalVideoShot, 1> finalShots = {
            FinalVideoShot{
                { "approved-shot", "Approved shot", "capture:approved",
                    "Retain capture identity." },
                FinalVideoShotState::Approved,
                {},
                captureIdentity
            }
        };

        ShowcaseMarkdownReportInput input;
        input.title = "L10 code-first report";
        input.evidence = evidence;
        input.knownLimitations = limitations;
        input.scenes = scenes;
        input.algorithmCompletion = algorithms;
        input.finalVideoShots = finalShots;
        input.licenseGate = &licenseGate;
        const ShowcaseTextResult report = BuildShowcaseMarkdownReport(input);
        test.Expect(static_cast<bool>(report), "valid Markdown report input must serialize");

        const std::size_t staticPosition = report.text.find("## Static evidence");
        const std::size_t buildPosition = report.text.find("## Build evidence");
        const std::size_t runtimePosition = report.text.find("## Runtime evidence");
        const std::size_t numericPosition = report.text.find("## Numeric evidence");
        const std::size_t visualPosition = report.text.find("## Visual evidence");
        const std::size_t performancePosition =
            report.text.find("## Performance evidence");
        test.Expect(
            staticPosition < buildPosition
                && buildPosition < runtimePosition
                && runtimePosition < numericPosition
                && numericPosition < visualPosition
                && visualPosition < performancePosition,
            "Markdown must always separate all six evidence boundaries in stable order");
        test.Expect(
            report.text.find("Classification: synthetic-test") != std::string::npos
                && report.text.find("Source: synthetic-test") != std::string::npos,
            "Markdown must visibly label synthetic evidence");
        test.Expect(
            report.text.find("L2 scene provider is absent.") != std::string::npos
                && report.text.find("Asset license gate is closed.")
                    != std::string::npos
                && report.text.find("License document is missing.")
                    != std::string::npos,
            "Markdown must retain unavailable provider and license reasons");
        test.Expect(
            report.text.find("No evidence record was supplied.") != std::string::npos,
            "missing evidence boundaries must fail closed as unavailable");
        test.Expect(
            report.text.find("## Retained evidence identities")
                    != std::string::npos
                && report.text.find("artifacts/markdown-runtime.json")
                    != std::string::npos
                && report.text.find("captures/markdown-approved.json")
                    != std::string::npos
                && report.text.find("config-generation=17")
                    != std::string::npos
                && report.text.find("scene=cornell") != std::string::npos
                && report.text.find("scene-generation=5") != std::string::npos
                && report.text.find("resource-generation=11")
                    != std::string::npos
                && report.text.find("frame-index=23") != std::string::npos
                && report.text.find("sample-index=91") != std::string::npos,
            "Markdown must preserve every structured runtime and capture identity field");
        test.Expect(
            report.text.find("Live providers are not integrated.")
                < report.text.find("Visual acceptance not run."),
            "known limitations must be sorted and deduplicated");

        ShowcaseEvidenceBoundaryEntry invalidLive = {
            ShowcaseEvidenceBoundary::Runtime,
            ShowcaseEvidenceState::Passed,
            ReportEvidenceClass::LiveRuntime,
            { EvidenceSource::SyntheticTest, "mock-runtime", "fixture" },
            "Runtime passed.",
            {}
        };
        ShowcaseMarkdownReportInput invalidInput;
        invalidInput.title = "Invalid evidence";
        invalidInput.evidence = std::span{ &invalidLive, 1u };
        test.Expect(
            BuildShowcaseMarkdownReport(invalidInput).error
                == ShowcaseReportError::SyntheticLiveConflict,
            "Markdown must reject SyntheticTest evidence labeled as live runtime");
    }
}

bool RunShowcaseReportTests(std::ostream& output)
{
    TestContext test(output);
    TestStableRfc4180Csv(test);
    TestStructuredLiveIdentityValidation(test);
    TestReferenceJsonAndProvenance(test);
    TestMarkdownEvidenceBoundaries(test);

    if (test.Passed())
    {
        output << "L10 ShowcaseReport serialization tests passed.\n";
    }
    return test.Passed();
}
