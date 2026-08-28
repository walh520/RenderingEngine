#include "app/ArtifactLayout.hpp"
#include "app/RuntimeConfig.hpp"
#include "demos/CaptureBundleWriter.hpp"
#include "demos/ShowcaseReport.hpp"
#include "demos/ShowcaseWorkflow.hpp"
#include "ui/ActionMap.hpp"
#include "ui/RuntimeConfigHarness.hpp"
#include "ui/ShowcaseViewModel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <tinyexr.h>

bool RunDebugProfilerModelTests(std::ostream& output);
bool RunImGuiShowcasePanelsTests(std::ostream& output);
bool RunShowcaseControllerTests(std::ostream& output);
bool RunShowcaseEvidenceTests(std::ostream& output);
bool RunShowcaseProgramTests(std::ostream& output);
bool RunShowcaseReportTests(std::ostream& output);
bool RunShowcaseWorkflowTests(std::ostream& output);

namespace
{
    class TestContext final
    {
    public:
        void Expect(bool condition, std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "L10 Wave 1 test failed: " << message << '\n';
                ++failureCount_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failureCount_ == 0;
        }

    private:
        int failureCount_ = 0;
    };

    class TemporaryDirectory final
    {
    public:
        explicit TemporaryDirectory(std::string_view label)
        {
            const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            root_ = std::filesystem::temp_directory_path()
                / (std::string("RenderingEngine-L10-") + std::string(label) + "-" + std::to_string(ticks));
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        ~TemporaryDirectory()
        {
            std::error_code error;
            (void)std::filesystem::remove_all(root_, error);
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept
        {
            return root_;
        }

    private:
        std::filesystem::path root_;
    };

    [[nodiscard]] bool SameRuntimeConfig(
        const RenderingEngine::RuntimeConfig& left,
        const RenderingEngine::RuntimeConfig& right)
    {
        return left.version == right.version
            && left.scene == right.scene
            && left.backend == right.backend
            && left.integrator == right.integrator
            && left.directLightingEstimator == right.directLightingEstimator
            && left.lightProposalDistribution == right.lightProposalDistribution
            && left.reconstruction == right.reconstruction
            && left.debugView == right.debugView
            && left.shadowMethod == right.shadowMethod
            && left.render.width == right.render.width
            && left.render.height == right.render.height
            && left.render.renderScale == right.render.renderScale
            && left.render.samplesPerFrame == right.render.samplesPerFrame
            && left.render.targetSamplesPerPixel == right.render.targetSamplesPerPixel
            && left.render.maximumBounce == right.render.maximumBounce
            && left.render.baseSeed == right.render.baseSeed
            && left.render.exposure == right.render.exposure
            && left.render.verticalFovDegrees == right.render.verticalFovDegrees
            && left.render.vsync == right.render.vsync
            && left.run.frameLimit == right.run.frameLimit
            && left.run.resizeTest == right.run.resizeTest
            && left.run.headless == right.run.headless
            && left.run.validation == right.run.validation
            && left.run.captureDirectory == right.run.captureDirectory
            && left.run.benchmarkPreset == right.run.benchmarkPreset
            && left.run.referenceImage == right.run.referenceImage
            && left.run.artifactRoot == right.run.artifactRoot
            && left.run.runIdentifier == right.run.runIdentifier;
    }

    [[nodiscard]] std::vector<unsigned char> ReadBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    [[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    [[nodiscard]] RenderingEngine::Demos::CaptureMetadata MakeCaptureMetadata()
    {
        using namespace RenderingEngine::Demos;

        CaptureMetadata metadata;
        metadata.schemaVersion = "artifact-layout-v0";
        metadata.contractVersions = {
            { "abi", "abi-v0-numeric-1" },
            { "runtime-config", "runtime-config-v0-app-0" },
            { "artifact-layout", "artifact-layout-v0" }
        };
        metadata.evidenceIdentity.providerId = "l10.bundle-test";
        metadata.evidenceIdentity.origin = CaptureEvidenceOrigin::SyntheticTest;
        metadata.evidenceIdentity.availability = CaptureEvidenceAvailability::Fresh;
        metadata.evidenceIdentity.frameIndex = 17u;
        metadata.evidenceIdentity.sampleIndex = 64u;
        metadata.evidenceIdentity.configGeneration = 3u;
        metadata.evidenceIdentity.sceneGeneration = 5u;
        metadata.evidenceIdentity.resourceGeneration = 7u;
        metadata.gitCommit = "10f791a-test-fixture";
        metadata.dirtyWorktree = true;
        metadata.executableConfiguration = "Test";
        metadata.buildIdentity = "l10-wave1-cpu-test";
        metadata.gpuName = "caller-supplied-test-device";
        metadata.driverVersion = "caller-supplied-test-driver";
        metadata.vulkanApiVersion = "not-used-by-cpu-test";
        metadata.vulkanSdkVersion = "1.4.328.1-test-fixture";
        metadata.sceneId = "baseline";
        metadata.sceneGeneration = "5";
        metadata.sceneHash = "sha256:test-scene";
        metadata.assetHashes = { { "procedural-baseline", "sha256:test-asset" } };
        metadata.cameraPreset = "baseline \"fixed\" \\ preset\nline2";
        RenderingEngine::RuntimeConfig requestedConfig;
        requestedConfig.scene = RenderingEngine::ScenePreset::CornellBox;
        requestedConfig.backend = RenderingEngine::TraversalBackend::CpuSahBvh;
        requestedConfig.integrator = RenderingEngine::Integrator::CpuReferencePathTracer;
        requestedConfig.directLightingEstimator = RenderingEngine::DirectLightingEstimator::NextEventEstimation;
        requestedConfig.lightProposalDistribution = RenderingEngine::LightProposalDistribution::UniformLights;
        requestedConfig.reconstruction = RenderingEngine::ReconstructionMode::TemporalFixedAtrous;
        requestedConfig.render.width = 64u;
        requestedConfig.render.height = 64u;
        requestedConfig.render.vsync = RenderingEngine::RuntimeToggle::Enabled;
        requestedConfig.run.validation = RenderingEngine::RuntimeToggle::Disabled;
        metadata.requestedRuntimeConfig = requestedConfig;
        metadata.effectiveRuntimeConfig.render.width = 64u;
        metadata.effectiveRuntimeConfig.render.height = 64u;
        metadata.shaderHashes = { { "capture-test", "sha256:test-shader" } };
        metadata.renderStartedAtUtc = "2026-08-24T00:00:00Z";
        metadata.renderCompletedAtUtc = "2026-08-24T00:00:01Z";
        metadata.linearColorSpace = "linear-rec709";
        metadata.previewDisplayTransform = "test-rgba8-fixture";
        return metadata;
    }

    void AlignEffectiveLayout(
        RenderingEngine::Demos::CaptureMetadata& metadata,
        const RenderingEngine::ArtifactLayout& layout)
    {
        metadata.effectiveRuntimeConfig.run.artifactRoot = layout.artifactRoot;
        metadata.effectiveRuntimeConfig.run.runIdentifier =
            layout.runDirectory.filename().string();
        if (metadata.effectiveRuntimeConfig.run.captureDirectory.has_value())
        {
            metadata.effectiveRuntimeConfig.run.captureDirectory = layout.artifactRoot;
        }
    }

    void TestActionMap(TestContext& tests)
    {
        using namespace RenderingEngine::Ui;

        const std::span<const ActionBinding> catalog = GetActionCatalog();
        tests.Expect(catalog.size() >= 50u, "section-10 action catalog must cover the full keyboard/help surface");

        const ActionBinding* backendForward = FindActionBinding(InputKey::B, InputModifier::None);
        const ActionBinding* backendBackward = FindActionBinding(InputKey::B, InputModifier::Shift);
        const ActionBinding* capture = FindActionBinding(InputKey::F4, InputModifier::None);
        const ActionBinding* exit = FindActionBinding(InputKey::F4, InputModifier::Alt);
        const ActionBinding* fov = FindActionBinding(InputKey::MouseWheel, InputModifier::Alt);
        tests.Expect(backendForward != nullptr
            && backendForward->action == SemanticAction::CycleBackendForward,
            "B must map to forward backend cycle");
        tests.Expect(backendBackward != nullptr
            && backendBackward->action == SemanticAction::CycleBackendBackward,
            "Shift+B must map to backward backend cycle");
        tests.Expect(capture != nullptr && capture->action == SemanticAction::RequestCapture,
            "F4 must map to capture without colliding with Alt+F4");
        tests.Expect(exit != nullptr && exit->action == SemanticAction::RequestExit,
            "Alt+F4 must map to application exit");
        tests.Expect(fov != nullptr && fov->action == SemanticAction::AdjustVerticalFov,
            "Alt+Wheel must map to RuntimeConfig FOV adjustment");
    }

    void TestRuntimeHarness(TestContext& tests)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Ui;

        constexpr ResetMask atqp = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory
            | ResetResource::ProfilerStatistics;

        RuntimeConfig config;
        const RuntimeConfig initial = config;
        ActionQueue queue;
        const std::uint64_t firstSequence = queue.Push(SemanticAction::CycleIntegratorForward);
        const std::uint64_t secondSequence = queue.Push(SemanticAction::CycleIntegratorBackward);
        tests.Expect(SameRuntimeConfig(config, initial), "queueing must not mutate RuntimeConfig before frame start");
        tests.Expect(firstSequence < secondSequence && queue.Size() == 2u, "ActionQueue must assign FIFO sequence numbers");

        const ActionBatchResult integratorBatch = ApplyQueuedActions(queue, config);
        tests.Expect(queue.Empty(), "ApplyQueuedActions must drain the frame queue");
        tests.Expect(integratorBatch.actions.size() == 2u
            && integratorBatch.actions[0].queued.sequence == firstSequence
            && integratorBatch.actions[1].queued.sequence == secondSequence,
            "queued actions must apply in FIFO order");
        tests.Expect(integratorBatch.actions[0].status == ActionApplyStatus::ConfigCommitted
            && integratorBatch.actions[1].status == ActionApplyStatus::ConfigCommitted,
            "built Whitted/PBR integrator cycles must commit");
        tests.Expect(config.integrator == Integrator::Whitted,
            "forward then backward integrator cycles must restore the initial tuple");
        tests.Expect(integratorBatch.requestedResets == atqp,
            "integrator changes must request A+T+Q+P and no AS reset");

        const RuntimeConfig beforeBackend = config;
        queue.Push(SemanticAction::CycleBackendForward);
        const ActionBatchResult backendBatch = ApplyQueuedActions(queue, config);
        tests.Expect(backendBatch.actions.size() == 1u
            && backendBatch.actions[0].status == ActionApplyStatus::AcceptedNoConfigChange,
            "a cycle with only one built backend must remain a supported no-op");
        tests.Expect(SameRuntimeConfig(config, beforeBackend), "built-only backend cycle must not select future modes");

        const RuntimeConfig beforeScene = config;
        queue.Push(SemanticAction::SelectScene3);
        const ActionBatchResult sceneBatch = ApplyQueuedActions(queue, config);
        tests.Expect(sceneBatch.actions.size() == 1u
            && sceneBatch.actions[0].status == ActionApplyStatus::Rejected
            && sceneBatch.actions[0].capabilityStatus == CapabilityStatus::Unsupported
            && !sceneBatch.actions[0].reason.empty(),
            "an unbuilt scene selection must reject with the shared capability reason");
        tests.Expect(SameRuntimeConfig(config, beforeScene), "rejected scene action must leave the complete tuple unchanged");

        queue.Push(SemanticAction::CycleIntegratorForward, ActionPhase::Repeated);
        const ActionBatchResult repeatBatch = ApplyQueuedActions(queue, config);
        tests.Expect(repeatBatch.actions.size() == 1u
            && repeatBatch.actions[0].status == ActionApplyStatus::IgnoredInputPhase,
            "discrete cycles must ignore repeat events");

        queue.Push(SemanticAction::IncreaseMaximumBounce);
        const ActionBatchResult bounceBatch = ApplyQueuedActions(queue, config);
        tests.Expect(config.render.maximumBounce == 9u
            && bounceBatch.actions[0].status == ActionApplyStatus::ConfigCommitted
            && bounceBatch.requestedResets == atqp,
            "maximum-bounce action must commit through RuntimeConfig and request A+T+Q+P");

        const float exposureBefore = config.render.exposure;
        queue.Push(SemanticAction::IncreaseExposure);
        const ActionBatchResult exposureBatch = ApplyQueuedActions(queue, config);
        tests.Expect(config.render.exposure > exposureBefore
            && exposureBatch.requestedResets == ResetResource::None,
            "display-only exposure must commit without history reset");

        const RuntimeConfig beforeScale = config;
        queue.Push(SemanticAction::IncreaseRenderScale);
        const ActionBatchResult scaleBatch = ApplyQueuedActions(queue, config);
        tests.Expect(scaleBatch.actions[0].status == ActionApplyStatus::Rejected
            && scaleBatch.actions[0].capabilityStatus == CapabilityStatus::Unsupported,
            "recognized but unbuilt render scaling must reject explicitly");
        tests.Expect(SameRuntimeConfig(config, beforeScale), "rejected render scale must preserve the live config");

        queue.Push(SemanticAction::ResetHistories);
        const ActionBatchResult resetBatch = ApplyQueuedActions(queue, config);
        const ResetMask expectedManualReset = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory;
        tests.Expect(resetBatch.actions[0].status == ActionApplyStatus::RoutedToOwner
            && resetBatch.actions[0].routedCommand == RoutedCommand::ResetHistories
            && resetBatch.requestedResets == expectedManualReset,
            "R must route an A+T+Q reset request without claiming resource ownership");

        RuntimeConfig targetSppConfig;
        targetSppConfig.render.targetSamplesPerPixel = 1u;
        const RuntimeConfig beforeDebug = targetSppConfig;
        queue.Push(SemanticAction::CycleDebugViewForward);
        const ActionBatchResult debugBatch = ApplyQueuedActions(queue, targetSppConfig);
        tests.Expect(debugBatch.actions[0].status == ActionApplyStatus::AcceptedNoConfigChange,
            "debug cycle must skip complete-tuple-invalid built candidates");
        tests.Expect(SameRuntimeConfig(targetSppConfig, beforeDebug),
            "complete-tuple filtering must preserve target-SPP final view");

        const std::span<const LightSamplingPresetDescriptor> presets = GetLightSamplingPresetCatalog();
        tests.Expect(presets.size() == 6u
            && presets[2].estimator == DirectLightingEstimator::NextEventEstimation
            && presets[2].proposal == LightProposalDistribution::UniformLights
            && presets[3].proposal == LightProposalDistribution::PowerWeightedLights,
            "L presets must keep direct estimator and proposal as explicit independent fields");
    }

    void TestViewModel(TestContext& tests)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Ui;

        const RuntimeConfig config;
        const ShowcaseViewModel model = BuildShowcaseViewModel(config);
        tests.Expect(model.dimensions.size() == 7u,
            "view model must expose seven orthogonal RuntimeConfig dimensions");
        tests.Expect(std::count(model.tupleText.begin(), model.tupleText.end(), '|') == 6,
            "formatted tuple must retain all seven dimensions");
        tests.Expect(!model.tuple.directEstimator.empty() && !model.tuple.lightProposal.empty(),
            "tuple must show direct estimator and light proposal separately");
        tests.Expect(model.help.size() == GetActionCatalog().size(),
            "help model must derive from the same action catalog");

        const auto sceneDimension = std::find_if(
            model.dimensions.begin(),
            model.dimensions.end(),
            [](const CapabilityDimensionViewModel& dimension) { return dimension.id == "scene"; });
        tests.Expect(sceneDimension != model.dimensions.end() && sceneDimension->options.size() == 10u,
            "scene panel shell must expose all ten roadmap scenes");
        if (sceneDimension != model.dimensions.end() && sceneDimension->options.size() > 3u)
        {
            const CapabilityOptionViewModel& baseline = sceneDimension->options[0];
            const CapabilityOptionViewModel& cornell = sceneDimension->options[3];
            tests.Expect(baseline.selected && baseline.built && baseline.enabled,
                "Baseline Gallery must be selected and enabled under Wave 0 capability");
            tests.Expect(!cornell.built && !cornell.enabled && cornell.owner == "L2" && !cornell.reason.empty(),
                "future scene must remain disabled with owner and capability reason");
        }

        const auto captureHelp = std::find_if(
            model.help.begin(),
            model.help.end(),
            [](const HelpEntryViewModel& entry)
            {
                return entry.binding != nullptr
                    && entry.binding->action == SemanticAction::RequestCapture;
            });
        tests.Expect(captureHelp != model.help.end()
            && !captureHelp->enabled
            && !captureHelp->owner.empty()
            && !captureHelp->reason.empty(),
            "unconnected production capture action must be disabled and explained");
    }

    void TestCaptureBundle(TestContext& tests, float& measuredMaxAbsError)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Demos;

        TemporaryDirectory temporary("capture");
        const ArtifactLayout layout = ResolveArtifactLayout(temporary.Path(), "fixture");
        constexpr std::uint32_t fixtureWidth = 64u;
        constexpr std::uint32_t fixtureHeight = 64u;
        const std::array<float, 16> linearPattern = {
            0.0f, 0.25f, 0.5f, 1.0f,
            1.0f, 2.0f, 4.0f, 0.5f,
            -0.25f, 0.75f, 1.25f, 1.0f,
            0.1f, 0.2f, 0.3f, 0.4f
        };
        const std::array<std::uint8_t, 16> previewPattern = {
            0u, 64u, 128u, 255u,
            255u, 200u, 150u, 128u,
            10u, 20u, 30u, 40u,
            1u, 2u, 3u, 4u
        };
        const std::size_t componentCount =
            static_cast<std::size_t>(fixtureWidth) * fixtureHeight * 4u;
        std::vector<float> linear(componentCount);
        std::vector<std::uint8_t> preview(componentCount);
        for (std::size_t index = 0u; index < componentCount; ++index)
        {
            linear[index] = linearPattern[index % linearPattern.size()];
            preview[index] = previewPattern[index % previewPattern.size()];
        }
        const CaptureImageView image = {
            fixtureWidth,
            fixtureHeight,
            std::span<const float>(linear),
            std::span<const std::uint8_t>(preview)
        };
        CaptureMetadata metadata = MakeCaptureMetadata();
        AlignEffectiveLayout(metadata, layout);

        const CaptureBundleResult result = WriteCaptureBundle(layout, image, metadata);
        tests.Expect(static_cast<bool>(result), "valid caller-supplied pixels and metadata must produce a capture bundle");
        tests.Expect(std::filesystem::is_regular_file(layout.capturesDirectory / "image.exr")
            && std::filesystem::is_regular_file(layout.capturesDirectory / "preview.png")
            && std::filesystem::is_regular_file(layout.metadataFile),
            "capture bundle must contain the three normative Wave 1 files");

        float* loadedExr = nullptr;
        int exrWidth = 0;
        int exrHeight = 0;
        const char* exrError = nullptr;
        const std::string exrPath = (layout.capturesDirectory / "image.exr").string();
        const int exrStatus = LoadEXR(&loadedExr, &exrWidth, &exrHeight, exrPath.c_str(), &exrError);
        tests.Expect(exrStatus == TINYEXR_SUCCESS && loadedExr != nullptr
            && exrWidth == static_cast<int>(fixtureWidth)
            && exrHeight == static_cast<int>(fixtureHeight),
            "TinyEXR must round-trip the RuntimeConfig-sized RGBA32F fixture");
        const bool exrFixtureIsUsable = exrStatus == TINYEXR_SUCCESS
            && loadedExr != nullptr
            && exrWidth == static_cast<int>(fixtureWidth)
            && exrHeight == static_cast<int>(fixtureHeight);
        measuredMaxAbsError = std::numeric_limits<float>::infinity();
        if (exrFixtureIsUsable)
        {
            measuredMaxAbsError = 0.0f;
            for (std::size_t index = 0; index < linear.size(); ++index)
            {
                measuredMaxAbsError = std::max(
                    measuredMaxAbsError,
                    std::abs(loadedExr[index] - linear[index]));
            }
        }
        if (loadedExr != nullptr)
        {
            std::free(loadedExr);
        }
        if (exrError != nullptr)
        {
            FreeEXRErrorMessage(exrError);
        }
        tests.Expect(measuredMaxAbsError <= 1.0e-6f,
            "EXR float round-trip max absolute error must be <= 1e-6");

        int pngWidth = 0;
        int pngHeight = 0;
        int pngChannels = 0;
        const std::string pngPath = (layout.capturesDirectory / "preview.png").string();
        stbi_uc* loadedPng = stbi_load(pngPath.c_str(), &pngWidth, &pngHeight, &pngChannels, 4);
        tests.Expect(loadedPng != nullptr
                && pngWidth == static_cast<int>(fixtureWidth)
                && pngHeight == static_cast<int>(fixtureHeight),
            "stb_image must read the generated RuntimeConfig-sized preview PNG");
        const bool pngFixtureIsUsable = loadedPng != nullptr
            && pngWidth == static_cast<int>(fixtureWidth)
            && pngHeight == static_cast<int>(fixtureHeight);
        if (pngFixtureIsUsable)
        {
            const bool bytesMatch = std::equal(preview.begin(), preview.end(), loadedPng);
            tests.Expect(bytesMatch, "preview PNG must preserve every caller-provided RGBA8 byte");
        }
        if (loadedPng != nullptr)
        {
            stbi_image_free(loadedPng);
        }

        const std::string metadataJson = ReadTextFile(layout.metadataFile);
        tests.Expect(metadataJson.find("\"state\": \"complete\"") != std::string::npos
            && metadataJson.find("\"requested\"") != std::string::npos
            && metadataJson.find("\"effective\"") != std::string::npos
            && metadataJson.find("\"scene\": \"cornell\"") != std::string::npos
            && metadataJson.find("\"backend\": \"cpu-sah\"") != std::string::npos
            && metadataJson.find("\"integrator\": \"cpu-reference\"") != std::string::npos
            && metadataJson.find("\"direct_lighting_estimator\": \"nee\"") != std::string::npos
            && metadataJson.find("\"light_proposal_distribution\": \"uniform\"") != std::string::npos
            && metadataJson.find("\"reconstruction\": \"temporal-atrous\"") != std::string::npos
            && metadataJson.find("\"vsync\": \"on\"") != std::string::npos
            && metadataJson.find("\"validation\": \"off\"") != std::string::npos
            && metadataJson.find("\"origin\": \"synthetic-test\"") != std::string::npos
            && metadataJson.find("\"availability\": \"fresh\"") != std::string::npos
            && metadataJson.find("\"config_generation\": 3") != std::string::npos
            && metadataJson.find("\"scene_generation\": 5") != std::string::npos
            && metadataJson.find("\"resource_generation\": 7") != std::string::npos
            && metadataJson.find("\"requested\": {\"status\": \"unsupported\"")
                != std::string::npos
            && metadataJson.find("\"effective\": {\"status\": \"supported\"")
                != std::string::npos
            && metadataJson.find("cpu-sah-bvh") == std::string::npos
            && metadataJson.find("cpu-reference-path-tracer") == std::string::npos
            && metadataJson.find("next-event-estimation") == std::string::npos
            && metadataJson.find("uniform-lights") == std::string::npos
            && metadataJson.find("temporal-fixed-atrous") == std::string::npos
            && metadataJson.find("enabled") == std::string::npos
            && metadataJson.find("disabled") == std::string::npos
            && metadataJson.find("baseline \\\"fixed\\\" \\\\ preset\\nline2") != std::string::npos
            && metadataJson.find("captures/image.exr") != std::string::npos
            && metadataJson.find("captures/preview.png") != std::string::npos,
            "metadata must publish completion last and use canonical cli-v0 tokens for replay");

        const std::vector<unsigned char> pngBeforeCollision = ReadBinaryFile(layout.capturesDirectory / "preview.png");
        const CaptureBundleResult collision = WriteCaptureBundle(layout, image, metadata);
        const std::vector<unsigned char> pngAfterCollision = ReadBinaryFile(layout.capturesDirectory / "preview.png");
        tests.Expect(collision.error == CaptureBundleError::RunDirectoryCollision,
            "an existing run ID must fail with an explicit collision error");
        tests.Expect(pngBeforeCollision == pngAfterCollision,
            "collision handling must not overwrite an existing artifact");

        const ArtifactLayout badMetadataLayout = ResolveArtifactLayout(temporary.Path(), "bad-metadata");
        CaptureMetadata badMetadata = metadata;
        AlignEffectiveLayout(badMetadata, badMetadataLayout);
        badMetadata.gpuName.clear();
        const CaptureBundleResult badMetadataResult = WriteCaptureBundle(badMetadataLayout, image, badMetadata);
        tests.Expect(badMetadataResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(badMetadataLayout.runDirectory),
            "missing caller truth must fail before creating a plausible artifact directory");

        const ArtifactLayout missingAssetHashLayout =
            ResolveArtifactLayout(temporary.Path(), "missing-asset-hash");
        CaptureMetadata missingAssetHashMetadata = metadata;
        AlignEffectiveLayout(missingAssetHashMetadata, missingAssetHashLayout);
        missingAssetHashMetadata.assetHashes.clear();
        const CaptureBundleResult missingAssetHashResult =
            WriteCaptureBundle(missingAssetHashLayout, image, missingAssetHashMetadata);
        tests.Expect(missingAssetHashResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(missingAssetHashLayout.runDirectory),
            "complete capture metadata must include at least one caller-supplied asset hash");

        const ArtifactLayout badImageLayout = ResolveArtifactLayout(temporary.Path(), "bad-image");
        const CaptureImageView badImage = {
            fixtureWidth,
            fixtureHeight,
            std::span<const float>(linear).first(linear.size() - 1u),
            std::span<const std::uint8_t>(preview)
        };
        const CaptureBundleResult badImageResult = WriteCaptureBundle(badImageLayout, badImage, metadata);
        tests.Expect(badImageResult.error == CaptureBundleError::InvalidImage
            && !std::filesystem::exists(badImageLayout.runDirectory),
            "invalid pixel spans must fail before any filesystem write");

        const ArtifactLayout badRuntimeLayout = ResolveArtifactLayout(temporary.Path(), "bad-runtime");
        CaptureMetadata badRuntimeMetadata = metadata;
        AlignEffectiveLayout(badRuntimeMetadata, badRuntimeLayout);
        badRuntimeMetadata.effectiveRuntimeConfig.render.width = 0u;
        const CaptureBundleResult badRuntimeResult =
            WriteCaptureBundle(badRuntimeLayout, image, badRuntimeMetadata);
        tests.Expect(badRuntimeResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(badRuntimeLayout.runDirectory),
            "an invalid runtime-config-v0 tuple must not publish complete metadata");

        const ArtifactLayout staleEvidenceLayout =
            ResolveArtifactLayout(temporary.Path(), "stale-evidence");
        CaptureMetadata staleEvidenceMetadata = metadata;
        AlignEffectiveLayout(staleEvidenceMetadata, staleEvidenceLayout);
        staleEvidenceMetadata.evidenceIdentity.availability =
            CaptureEvidenceAvailability::Stale;
        staleEvidenceMetadata.evidenceIdentity.reason = "scene generation advanced";
        const CaptureBundleResult staleEvidenceResult =
            WriteCaptureBundle(staleEvidenceLayout, image, staleEvidenceMetadata);
        tests.Expect(staleEvidenceResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(staleEvidenceLayout.runDirectory),
            "stale provider evidence must not be published as a complete capture");

        const ArtifactLayout generationMismatchLayout =
            ResolveArtifactLayout(temporary.Path(), "generation-mismatch");
        CaptureMetadata generationMismatchMetadata = metadata;
        AlignEffectiveLayout(generationMismatchMetadata, generationMismatchLayout);
        generationMismatchMetadata.sceneGeneration = "6";
        const CaptureBundleResult generationMismatchResult = WriteCaptureBundle(
            generationMismatchLayout,
            image,
            generationMismatchMetadata);
        tests.Expect(generationMismatchResult.error == CaptureBundleError::InvalidMetadata
                && !std::filesystem::exists(generationMismatchLayout.runDirectory),
            "metadata and Fresh evidence must not publish different scene generations");

        const ArtifactLayout extentMismatchLayout =
            ResolveArtifactLayout(temporary.Path(), "extent-mismatch");
        CaptureMetadata extentMismatchMetadata = metadata;
        AlignEffectiveLayout(extentMismatchMetadata, extentMismatchLayout);
        extentMismatchMetadata.effectiveRuntimeConfig.render.width = 128u;
        const CaptureBundleResult extentMismatchResult = WriteCaptureBundle(
            extentMismatchLayout,
            image,
            extentMismatchMetadata);
        tests.Expect(extentMismatchResult.error == CaptureBundleError::InvalidMetadata
                && !std::filesystem::exists(extentMismatchLayout.runDirectory),
            "capture image extent must match the effective RuntimeConfig resolution");

        const ArtifactLayout sceneMismatchLayout =
            ResolveArtifactLayout(temporary.Path(), "scene-mismatch");
        CaptureMetadata sceneMismatchMetadata = metadata;
        AlignEffectiveLayout(sceneMismatchMetadata, sceneMismatchLayout);
        sceneMismatchMetadata.sceneId = "cornell";
        const CaptureBundleResult sceneMismatchResult = WriteCaptureBundle(
            sceneMismatchLayout,
            image,
            sceneMismatchMetadata);
        tests.Expect(sceneMismatchResult.error == CaptureBundleError::InvalidMetadata
                && !std::filesystem::exists(sceneMismatchLayout.runDirectory),
            "capture scene identity must match the effective RuntimeConfig scene");

        const ArtifactLayout mismatchedLayout =
            ResolveArtifactLayout(temporary.Path(), "layout-mismatch");
        CaptureMetadata mismatchedMetadata = metadata;
        const CaptureBundleResult mismatchedResult =
            WriteCaptureBundle(mismatchedLayout, image, mismatchedMetadata);
        tests.Expect(mismatchedResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(mismatchedLayout.runDirectory),
            "effective RuntimeConfig output identity must match the ArtifactLayout");

        const ArtifactLayout duplicateIdentityLayout =
            ResolveArtifactLayout(temporary.Path(), "duplicate-identity");
        CaptureMetadata duplicateIdentityMetadata = metadata;
        AlignEffectiveLayout(duplicateIdentityMetadata, duplicateIdentityLayout);
        duplicateIdentityMetadata.shaderHashes.push_back(
            duplicateIdentityMetadata.shaderHashes.front());
        const CaptureBundleResult duplicateIdentityResult =
            WriteCaptureBundle(duplicateIdentityLayout, image, duplicateIdentityMetadata);
        tests.Expect(duplicateIdentityResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(duplicateIdentityLayout.runDirectory),
            "duplicate named provenance identities must be rejected before I/O");

        ArtifactLayout badRunIdLayout = ResolveArtifactLayout(temporary.Path(), "placeholder");
        badRunIdLayout.runDirectory = badRunIdLayout.artifactRoot / ".hidden";
        badRunIdLayout.capturesDirectory = badRunIdLayout.runDirectory / "captures";
        badRunIdLayout.benchmarksDirectory = badRunIdLayout.runDirectory / "benchmarks";
        badRunIdLayout.referencesDirectory = badRunIdLayout.runDirectory / "references";
        badRunIdLayout.logsDirectory = badRunIdLayout.runDirectory / "logs";
        badRunIdLayout.metadataFile = badRunIdLayout.runDirectory / "metadata.json";
        const CaptureBundleResult badRunIdResult = WriteCaptureBundle(badRunIdLayout, image, metadata);
        tests.Expect(badRunIdResult.error == CaptureBundleError::InvalidLayout
            && !std::filesystem::exists(badRunIdLayout.runDirectory),
            "a non-normalized artifact-layout-v0 run ID must fail before I/O");

        ArtifactLayout unnormalizedLayout = ResolveArtifactLayout(temporary.Path(), "unnormalized");
        unnormalizedLayout.artifactRoot /= ".";
        const CaptureBundleResult unnormalizedResult =
            WriteCaptureBundle(unnormalizedLayout, image, metadata);
        tests.Expect(unnormalizedResult.error == CaptureBundleError::InvalidLayout
            && !std::filesystem::exists(unnormalizedLayout.runDirectory),
            "the writer must reject an unnormalized layout instead of validating only a normalized copy");

        const ArtifactLayout badUtf8Layout = ResolveArtifactLayout(temporary.Path(), "bad-utf8");
        CaptureMetadata badUtf8Metadata = metadata;
        AlignEffectiveLayout(badUtf8Metadata, badUtf8Layout);
        badUtf8Metadata.sceneId.assign("\xc3\x28", 2u);
        const CaptureBundleResult badUtf8Result =
            WriteCaptureBundle(badUtf8Layout, image, badUtf8Metadata);
        tests.Expect(badUtf8Result.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(badUtf8Layout.runDirectory),
            "invalid UTF-8 must be rejected instead of producing invalid JSON");

        const ArtifactLayout showcaseLayout = ResolveArtifactLayout(temporary.Path(), "all-waves");
        BenchmarkPlan benchmarkPlan;
        benchmarkPlan.measurementFrameCount = 1u;
        benchmarkPlan.repeatCount = 1u;
        benchmarkPlan.sceneGeneration = 21u;
        benchmarkPlan.resourceGeneration = 22u;
        benchmarkPlan.conditions = { { "scene", "synthetic-bundle" } };
        benchmarkPlan.requiredMetrics = {
            { EvidenceSampleKind::Timing, "trace", "ms",
                EvidenceMeasurementMethod::CpuWallClock },
            { EvidenceSampleKind::Counter, "trace", "rays",
                EvidenceMeasurementMethod::ProviderCounter }
        };
        const EvidenceProvenance syntheticProvenance = {
            EvidenceSource::SyntheticTest,
            "l10.bundle-test",
            "deterministic CPU integration fixture"
        };
        const std::array<EvidenceSample, 2> benchmarkSamples = {{
            { EvidenceSampleKind::Timing, "trace", "ms", 1.25,
                syntheticProvenance, 0u, 0u, 0u, benchmarkPlan.conditions,
                21u, 22u, 64u },
            { EvidenceSampleKind::Counter, "trace", "rays", 1024.0,
                syntheticProvenance, 0u, 0u, 0u, benchmarkPlan.conditions,
                21u, 22u, 64u }
        }};
        const CsvResult timingsCsv = BuildTimingsCsv(benchmarkSamples, benchmarkPlan);
        const CsvResult countersCsv = BuildCountersCsv(benchmarkSamples, benchmarkPlan);

        const ShowcaseProgramModel program = BuildShowcaseProgramModel({}, {}, {});
        const ShowcaseTextResult completionMatrixCsv =
            BuildAlgorithmCompletionMatrixCsv(program.algorithmCompletion);
        const ShowcaseTextResult assetLicensesCsv = BuildAssetLicenseCsv({});
        FinalVideoShotList videoShots = BuildFinalVideoShotList(
            program.captureShots,
            program.licenseGate);
        const ShowcaseTextResult videoShotListCsv =
            BuildFinalVideoShotListCsv(videoShots.Shots());

        const LinearRgbaImageView linearImage = {
            fixtureWidth,
            fixtureHeight,
            std::span<const float>(linear)
        };
        ReferenceComparisonRecord comparisonRecord;
        comparisonRecord.candidate = {
            ReportEvidenceClass::SyntheticTest,
            "synthetic.candidate",
            syntheticProvenance,
            0u,
            0u,
            fixtureWidth,
            fixtureHeight
        };
        comparisonRecord.reference = {
            ReportEvidenceClass::SyntheticTest,
            "synthetic.reference",
            syntheticProvenance,
            0u,
            0u,
            fixtureWidth,
            fixtureHeight
        };
        comparisonRecord.candidate.sceneStableId = "synthetic-bundle";
        comparisonRecord.candidate.cameraPresetToken = "synthetic-fixed-camera";
        comparisonRecord.candidate.baseSeed = 1337u;
        comparisonRecord.candidate.sampleIndex = 64u;
        comparisonRecord.candidate.sceneGeneration = 21u;
        comparisonRecord.candidate.resourceGeneration = 22u;
        comparisonRecord.candidate.accumulatedSamplesPerPixel = 64u;
        comparisonRecord.reference.sceneStableId = "synthetic-bundle";
        comparisonRecord.reference.cameraPresetToken = "synthetic-fixed-camera";
        comparisonRecord.reference.baseSeed = 1337u;
        comparisonRecord.reference.sampleIndex = 4096u;
        comparisonRecord.reference.sceneGeneration = 21u;
        comparisonRecord.reference.resourceGeneration = 22u;
        comparisonRecord.reference.accumulatedSamplesPerPixel = 4096u;
        comparisonRecord.comparison = CompareLinearRgba(linearImage, linearImage, 4.0);
        const ShowcaseTextResult referenceJson =
            BuildReferenceComparisonJson(std::span{ &comparisonRecord, 1u });

        ShowcaseWorkflow abWorkflow;
        ShowcaseWorkflowStart abStart;
        abStart.sceneStableId = "synthetic-bundle";
        abStart.sceneGeneration = 21u;
        abStart.resourceGeneration = 22u;
        abStart.anchor = { "synthetic-fixed-camera", 1337u, 0u };
        abStart.variantAStableId = "uniform";
        abStart.variantBStableId = "power-weighted";
        abStart.configGeneration = 0u;
        abStart.captureProvider = {
            "l10.bundle-ab-provider",
            Availability::Available,
            {}
        };
        const ShowcaseWorkflowStatus abStartStatus = abWorkflow.Start(abStart);
        const auto makeAbArtifact = [](const ShowcaseCaptureWorkRequest& request,
                                      std::string runId)
        {
            ShowcaseProviderArtifactRecord artifact;
            artifact.workflowIdentity = request.workflowIdentity;
            artifact.requestIdentity = request.requestIdentity;
            artifact.variant = request.variant;
            artifact.configGeneration = request.configGeneration;
            artifact.sceneGeneration = request.sceneGeneration;
            artifact.resourceGeneration = request.resourceGeneration;
            artifact.runId = std::move(runId);
            artifact.exrPath = "captures/image.exr";
            artifact.pngPath = "captures/preview.png";
            artifact.metadataPath = "metadata.json";
            artifact.provenance = {
                EvidenceSource::ProviderReported,
                "l10.bundle-ab-provider",
                "deterministic workflow fixture"
            };
            return artifact;
        };
        bool abCompleted = static_cast<bool>(abStartStatus)
            && abWorkflow.Requests().size() == 2u;
        if (abCompleted)
        {
            abCompleted = static_cast<bool>(abWorkflow.Submit(
                makeAbArtifact(abWorkflow.Requests()[0], "synthetic-ab-a")))
                && static_cast<bool>(abWorkflow.Submit(
                    makeAbArtifact(abWorkflow.Requests()[1], "synthetic-ab-b")));
        }
        const ShowcaseAbManifest* const abManifest =
            abWorkflow.CompletedManifest();
        const std::string abManifestJson = abManifest == nullptr
            ? std::string{}
            : BuildShowcaseAbManifestJson(*abManifest);

        const std::array<ShowcaseEvidenceBoundaryEntry, 1> reportEvidence = {{
            {
                ShowcaseEvidenceBoundary::Numeric,
                ShowcaseEvidenceState::Passed,
                ReportEvidenceClass::SyntheticTest,
                syntheticProvenance,
                "Serializer and bundle integration fixture passed.",
                {}
            }
        }};
        const std::array<std::string, 2> knownLimitations = {
            "Live renderer providers are not integrated.",
            "Visual and performance data validation is deferred."
        };
        ShowcaseMarkdownReportInput reportInput;
        reportInput.title = "L10 synthetic bundle integration report";
        reportInput.evidence = reportEvidence;
        reportInput.knownLimitations = knownLimitations;
        reportInput.scenes = program.scenes;
        reportInput.algorithmCompletion = program.algorithmCompletion;
        reportInput.manyLightsComparisons = program.manyLightsComparisons;
        reportInput.captureShots = program.captureShots;
        reportInput.finalVideoShots = videoShots.Shots();
        reportInput.licenseGate = &program.licenseGate;
        const ShowcaseTextResult reportMarkdown = BuildShowcaseMarkdownReport(reportInput);

        tests.Expect(static_cast<bool>(timingsCsv)
            && static_cast<bool>(countersCsv)
            && static_cast<bool>(completionMatrixCsv)
            && static_cast<bool>(assetLicensesCsv)
            && static_cast<bool>(videoShotListCsv)
            && static_cast<bool>(referenceJson)
            && static_cast<bool>(reportMarkdown)
            && abCompleted
            && !abManifestJson.empty(),
            "all-wave serializers must produce explicitly synthetic integration artifacts");
        const auto bytes = [](const std::string& value)
        {
            return std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(value.data()),
                value.size());
        };
        const std::array<CaptureAdditionalArtifact, 8> additionalArtifacts = {{
            { "benchmarks/timings.csv", bytes(timingsCsv.text) },
            { "benchmarks/counters.csv", bytes(countersCsv.text) },
            { "references/comparison.json", bytes(referenceJson.text) },
            { "reports/showcase.md", bytes(reportMarkdown.text) },
            { "reports/ab-manifest.json", bytes(abManifestJson) },
            { "reports/completion-matrix.csv", bytes(completionMatrixCsv.text) },
            { "reports/asset-licenses.csv", bytes(assetLicensesCsv.text) },
            { "reports/video-shot-list.csv", bytes(videoShotListCsv.text) }
        }};
        CaptureMetadata showcaseMetadata = metadata;
        AlignEffectiveLayout(showcaseMetadata, showcaseLayout);
        showcaseMetadata.requestedArtifacts = {
            "captures/image.exr",
            "captures/preview.png",
            "benchmarks/timings.csv",
            "benchmarks/counters.csv",
            "references/comparison.json",
            "reports/showcase.md",
            "reports/ab-manifest.json",
            "reports/completion-matrix.csv",
            "reports/asset-licenses.csv",
            "reports/video-shot-list.csv",
            "metadata.json"
        };
        const CaptureBundleResult showcaseResult = WriteShowcaseBundle(
            showcaseLayout,
            image,
            showcaseMetadata,
            additionalArtifacts);
        tests.Expect(static_cast<bool>(showcaseResult)
            && showcaseResult.additionalFiles.size() == additionalArtifacts.size()
            && ReadTextFile(showcaseLayout.benchmarksDirectory / "timings.csv") == timingsCsv.text
            && ReadTextFile(showcaseLayout.benchmarksDirectory / "counters.csv") == countersCsv.text
            && ReadTextFile(showcaseLayout.referencesDirectory / "comparison.json") == referenceJson.text
            && ReadTextFile(showcaseLayout.runDirectory / "reports" / "showcase.md") == reportMarkdown.text
            && ReadTextFile(showcaseLayout.runDirectory / "reports" / "ab-manifest.json")
                == abManifestJson,
            "all-wave showcase transaction must write supplied evidence before completion metadata");
        const std::string showcaseJson = ReadTextFile(showcaseLayout.metadataFile);
        tests.Expect(showcaseJson.find("benchmarks/timings.csv") != std::string::npos
            && showcaseJson.find("benchmarks/counters.csv") != std::string::npos
            && showcaseJson.find("references/comparison.json") != std::string::npos
            && showcaseJson.find("reports/showcase.md") != std::string::npos
            && showcaseJson.find("reports/ab-manifest.json") != std::string::npos
            && showcaseJson.find("reports/completion-matrix.csv") != std::string::npos
            && showcaseJson.find("reports/asset-licenses.csv") != std::string::npos
            && showcaseJson.find("reports/video-shot-list.csv") != std::string::npos,
            "completion metadata must list every requested and produced evidence artifact");

        const ArtifactLayout unsafeEvidenceLayout = ResolveArtifactLayout(temporary.Path(), "unsafe-evidence");
        const std::array<CaptureAdditionalArtifact, 1> unsafeArtifacts = {{
            { "reports/../escape.txt", bytes(reportMarkdown.text) }
        }};
        CaptureMetadata unsafeMetadata = metadata;
        AlignEffectiveLayout(unsafeMetadata, unsafeEvidenceLayout);
        unsafeMetadata.requestedArtifacts.push_back("reports/../escape.txt");
        const CaptureBundleResult unsafeEvidenceResult = WriteShowcaseBundle(
            unsafeEvidenceLayout,
            image,
            unsafeMetadata,
            unsafeArtifacts);
        tests.Expect(unsafeEvidenceResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(unsafeEvidenceLayout.runDirectory),
            "additional evidence traversal must be rejected before filesystem I/O");

        const ArtifactLayout devicePathLayout = ResolveArtifactLayout(temporary.Path(), "device-path");
        const std::array<CaptureAdditionalArtifact, 1> devicePathArtifacts = {{
            { "reports/NUL.txt", bytes(reportMarkdown.text) }
        }};
        CaptureMetadata devicePathMetadata = metadata;
        AlignEffectiveLayout(devicePathMetadata, devicePathLayout);
        devicePathMetadata.requestedArtifacts.push_back("reports/NUL.txt");
        const CaptureBundleResult devicePathResult = WriteShowcaseBundle(
            devicePathLayout,
            image,
            devicePathMetadata,
            devicePathArtifacts);
        tests.Expect(devicePathResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(devicePathLayout.runDirectory),
            "Windows DOS device artifact components must be rejected before I/O");

        const ArtifactLayout streamPathLayout = ResolveArtifactLayout(temporary.Path(), "stream-path");
        const std::array<CaptureAdditionalArtifact, 1> streamPathArtifacts = {{
            { "reports/result:alternate.txt", bytes(reportMarkdown.text) }
        }};
        CaptureMetadata streamPathMetadata = metadata;
        AlignEffectiveLayout(streamPathMetadata, streamPathLayout);
        streamPathMetadata.requestedArtifacts.push_back("reports/result:alternate.txt");
        const CaptureBundleResult streamPathResult = WriteShowcaseBundle(
            streamPathLayout,
            image,
            streamPathMetadata,
            streamPathArtifacts);
        tests.Expect(streamPathResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(streamPathLayout.runDirectory),
            "Windows alternate-data-stream artifact paths must be rejected before I/O");

        const ArtifactLayout superscriptDeviceLayout =
            ResolveArtifactLayout(temporary.Path(), "superscript-device-path");
        const std::array<CaptureAdditionalArtifact, 1> superscriptDeviceArtifacts = {{
            { "reports/COM\u00b9.txt", bytes(reportMarkdown.text) }
        }};
        CaptureMetadata superscriptDeviceMetadata = metadata;
        AlignEffectiveLayout(superscriptDeviceMetadata, superscriptDeviceLayout);
        superscriptDeviceMetadata.requestedArtifacts.push_back("reports/COM\u00b9.txt");
        const CaptureBundleResult superscriptDeviceResult = WriteShowcaseBundle(
            superscriptDeviceLayout,
            image,
            superscriptDeviceMetadata,
            superscriptDeviceArtifacts);
        tests.Expect(superscriptDeviceResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(superscriptDeviceLayout.runDirectory),
            "Windows superscript DOS-device aliases must be rejected before I/O");

        const ArtifactLayout caseAliasLayout =
            ResolveArtifactLayout(temporary.Path(), "case-alias");
        const std::array<CaptureAdditionalArtifact, 2> caseAliasArtifacts = {{
            { "reports/result.csv", bytes(reportMarkdown.text) },
            { "reports/RESULT.csv", bytes(reportMarkdown.text) }
        }};
        CaptureMetadata caseAliasMetadata = metadata;
        AlignEffectiveLayout(caseAliasMetadata, caseAliasLayout);
        caseAliasMetadata.requestedArtifacts.push_back("reports/result.csv");
        caseAliasMetadata.requestedArtifacts.push_back("reports/RESULT.csv");
        const CaptureBundleResult caseAliasResult = WriteShowcaseBundle(
            caseAliasLayout,
            image,
            caseAliasMetadata,
            caseAliasArtifacts);
        tests.Expect(caseAliasResult.error == CaptureBundleError::InvalidMetadata
            && !std::filesystem::exists(caseAliasLayout.runDirectory),
            "ASCII case-alias artifact paths must be rejected before I/O");

        const ArtifactLayout unicodeAliasLayout =
            ResolveArtifactLayout(temporary.Path(), "unicode-alias");
        const std::array<CaptureAdditionalArtifact, 1> unicodeAliasArtifacts = {{
            { "reports/\u00c4.csv", bytes(reportMarkdown.text) }
        }};
        CaptureMetadata unicodeAliasMetadata = metadata;
        AlignEffectiveLayout(unicodeAliasMetadata, unicodeAliasLayout);
        unicodeAliasMetadata.requestedArtifacts.push_back("reports/\u00c4.csv");
        const CaptureBundleResult unicodeAliasResult = WriteShowcaseBundle(
            unicodeAliasLayout,
            image,
            unicodeAliasMetadata,
            unicodeAliasArtifacts);
        tests.Expect(unicodeAliasResult.error == CaptureBundleError::InvalidMetadata
                && !std::filesystem::exists(unicodeAliasLayout.runDirectory),
            "non-ASCII artifact paths must fail closed before Windows alias resolution");
    }
}

int main()
{
    TestContext tests;
    TestActionMap(tests);
    TestRuntimeHarness(tests);
    TestViewModel(tests);
    float maxAbsError = 0.0f;
    TestCaptureBundle(tests, maxAbsError);
    const bool controllerChecksPassed = RunShowcaseControllerTests(std::cerr);
    const bool profilerChecksPassed = RunDebugProfilerModelTests(std::cerr);
    const bool imguiChecksPassed = RunImGuiShowcasePanelsTests(std::cerr);
    const bool evidenceChecksPassed = RunShowcaseEvidenceTests(std::cerr);
    const bool programChecksPassed = RunShowcaseProgramTests(std::cerr);
    const bool reportChecksPassed = RunShowcaseReportTests(std::cerr);
    const bool workflowChecksPassed = RunShowcaseWorkflowTests(std::cerr);
    const bool allWaveChecksPassed = controllerChecksPassed
        && profilerChecksPassed
        && imguiChecksPassed
        && evidenceChecksPassed
        && programChecksPassed
        && reportChecksPassed
        && workflowChecksPassed;

    if (!tests.Passed() || !allWaveChecksPassed)
    {
        return EXIT_FAILURE;
    }
    std::cout << "L10 all-wave headless showcase checks passed.\n";
    std::cout << "EXR RGBA32F max_abs_error=" << maxAbsError << " (threshold 1e-6).\n";
    return EXIT_SUCCESS;
}
