#include "app/ArtifactLayout.hpp"
#include "app/CapabilityTable.hpp"
#include "app/RuntimeConfig.hpp"
#include "app/RuntimeStatusText.hpp"
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
#include "FrameSignalOracle.hpp"

bool RunDebugProfilerModelTests(std::ostream& output);
bool RunWave2TelemetryAdapterTests(std::ostream& output);
int RunWave4TelemetryAdapterTests(std::ostream& output);
bool RunManyLightsWave4Tests(std::ostream& output);
bool RunCapturePreviewTests(std::ostream& output);
bool RunGlfwActionAdapterTests(std::ostream& output);
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
            && left.sceneVariant == right.sceneVariant
            && left.backend == right.backend
            && left.transportModel == right.transportModel
            && left.executionArchitecture == right.executionArchitecture
            && left.directLightingEstimator == right.directLightingEstimator
            && left.lightSelection == right.lightSelection
            && left.environmentSampler == right.environmentSampler
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
            && left.run.runIdentifier == right.run.runIdentifier
            && left.restir.manyLightsTier == right.restir.manyLightsTier
            && left.restir.reuseStage == right.restir.reuseStage
            && left.restir.biasMode == right.restir.biasMode
            && left.restir.initialCandidatesPerPixel
                == right.restir.initialCandidatesPerPixel
            && left.restir.spatialNeighbors == right.restir.spatialNeighbors
            && left.restir.maximumReservoirM == right.restir.maximumReservoirM
            && left.restir.maximumHistoryAge == right.restir.maximumHistoryAge
            && left.restir.comparisonCandidateBudgetPerPixel
                == right.restir.comparisonCandidateBudgetPerPixel
            && left.restir.comparisonVisibilityBudgetPerPixel
                == right.restir.comparisonVisibilityBudgetPerPixel
            && left.restir.animateLights == right.restir.animateLights
            && left.restir.animateRigidOccluders
                == right.restir.animateRigidOccluders;
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
            { "runtime-config", "runtime-config-v2-app-1" },
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
        requestedConfig.executionArchitecture = RenderingEngine::ExecutionArchitecture::CpuReference;
        requestedConfig.directLightingEstimator = RenderingEngine::DirectLightingEstimator::NextEventEstimation;
        requestedConfig.lightSelection = RenderingEngine::LightSelectionStrategy::Uniform;
        requestedConfig.reconstruction = RenderingEngine::ReconstructionMode::CurrentFrame;
        requestedConfig.render.width = 64u;
        requestedConfig.render.height = 64u;
        requestedConfig.render.vsync = RenderingEngine::RuntimeToggle::Enabled;
        requestedConfig.run.validation = RenderingEngine::RuntimeToggle::Disabled;
        metadata.requestedRuntimeConfig = requestedConfig;
        metadata.effectiveRuntimeConfig.render.width = 64u;
        metadata.effectiveRuntimeConfig.render.height = 64u;
        metadata.effectiveRuntimeConfig.reconstruction =
            RenderingEngine::ReconstructionMode::ProgressiveMean;
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
        using namespace RenderingEngine;
        using namespace RenderingEngine::Ui;

        const std::span<const ActionBinding> catalog = GetActionCatalog();
        tests.Expect(catalog.size() >= 50u, "section-10 action catalog must cover the full keyboard/help surface");

        const ActionBinding* backendForward = FindActionBinding(InputKey::B, InputModifier::None);
        const ActionBinding* backendBackward = FindActionBinding(InputKey::B, InputModifier::Shift);
        const ActionBinding* transportForward = FindActionBinding(InputKey::I, InputModifier::None);
        const ActionBinding* transportBackward = FindActionBinding(InputKey::I, InputModifier::Shift);
        const ActionBinding* executionForward = FindActionBinding(InputKey::I, InputModifier::Control);
        const ActionBinding* executionBackward = FindActionBinding(
            InputKey::I, InputModifier::Control | InputModifier::Shift);
        const ActionBinding* directForward = FindActionBinding(InputKey::L, InputModifier::None);
        const ActionBinding* directBackward = FindActionBinding(InputKey::L, InputModifier::Shift);
        const ActionBinding* proposalForward = FindActionBinding(InputKey::L, InputModifier::Control);
        const ActionBinding* proposalBackward = FindActionBinding(
            InputKey::L, InputModifier::Control | InputModifier::Shift);
        const ActionBinding* environmentForward = FindActionBinding(InputKey::L, InputModifier::Alt);
        const ActionBinding* environmentBackward = FindActionBinding(
            InputKey::L, InputModifier::Alt | InputModifier::Shift);
        const ActionBinding* shadowForward = FindActionBinding(
            InputKey::L, InputModifier::Control | InputModifier::Alt);
        const ActionBinding* shadowBackward = FindActionBinding(
            InputKey::L, InputModifier::Control | InputModifier::Alt | InputModifier::Shift);
        const ActionBinding* capture = FindActionBinding(InputKey::F4, InputModifier::None);
        const ActionBinding* exit = FindActionBinding(InputKey::F4, InputModifier::Alt);
        const ActionBinding* fov = FindActionBinding(InputKey::MouseWheel, InputModifier::Alt);
        const ActionBinding* review = FindActionBinding(InputKey::F10, InputModifier::None);
        const ActionBinding* restoreRecommendation = FindActionBinding(
            InputKey::F11, InputModifier::None);
        tests.Expect(backendForward != nullptr
            && backendForward->action == SemanticAction::CycleBackendForward,
            "B must map to forward backend cycle");
        tests.Expect(backendBackward != nullptr
            && backendBackward->action == SemanticAction::CycleBackendBackward,
            "Shift+B must map to backward backend cycle");
        tests.Expect(transportForward != nullptr
                && transportForward->action == SemanticAction::CycleTransportModelForward
                && transportBackward != nullptr
                && transportBackward->action == SemanticAction::CycleTransportModelBackward,
            "I and Shift+I must map only to transport-model cycles");
        tests.Expect(executionForward != nullptr
                && executionForward->action == SemanticAction::CycleExecutionArchitectureForward
                && executionBackward != nullptr
                && executionBackward->action == SemanticAction::CycleExecutionArchitectureBackward,
            "Ctrl+I and Ctrl+Shift+I must map only to execution-architecture cycles");
        tests.Expect(directForward != nullptr
                && directForward->action
                    == SemanticAction::CycleDirectLightingForward
                && directBackward != nullptr
                && directBackward->action
                    == SemanticAction::CycleDirectLightingBackward,
            "L and Shift+L must map only to the direct-lighting axis");
        tests.Expect(proposalForward != nullptr
                && proposalForward->action
                    == SemanticAction::CycleLightSelectionForward
                && proposalBackward != nullptr
                && proposalBackward->action
                    == SemanticAction::CycleLightSelectionBackward,
            "Ctrl+L and Ctrl+Shift+L must map only to the discrete-light-selection axis");
        tests.Expect(environmentForward != nullptr
                && environmentForward->action == SemanticAction::CycleEnvironmentSamplerForward
                && environmentBackward != nullptr
                && environmentBackward->action == SemanticAction::CycleEnvironmentSamplerBackward,
            "Alt+L and Alt+Shift+L must map only to the environment-sampler axis");
        tests.Expect(shadowForward != nullptr
                && shadowForward->action == SemanticAction::CycleShadowForward
                && shadowBackward != nullptr
                && shadowBackward->action == SemanticAction::CycleShadowBackward,
            "Ctrl+Alt+L and Ctrl+Alt+Shift+L must map only to the shadow-method axis");
        tests.Expect(capture != nullptr && capture->action == SemanticAction::RequestCapture,
            "F4 must map to capture without colliding with Alt+F4");
        tests.Expect(exit != nullptr && exit->action == SemanticAction::RequestExit,
            "Alt+F4 must map to application exit");
        tests.Expect(fov != nullptr && fov->action == SemanticAction::AdjustVerticalFov,
            "Alt+Wheel must map to RuntimeConfig FOV adjustment");
        tests.Expect(review != nullptr
                && review->action == SemanticAction::PrintCurrentReview,
            "F10 must print the current scene and algorithm review");
        tests.Expect(restoreRecommendation != nullptr
                && restoreRecommendation->action
                    == SemanticAction::RestoreCurrentSceneRecommendedProfile
                && restoreRecommendation->activation == ActionActivation::PressOnly,
            "F11 must be a press-only current-scene teaching recommendation restore");

        RuntimeConfig reviewConfig;
        reviewConfig.scene = ScenePreset::ManyLightsRestirArena;
        reviewConfig.backend = TraversalBackend::VulkanRayQuery;
        reviewConfig.executionArchitecture = ExecutionArchitecture::Wavefront;
        reviewConfig.directLightingEstimator =
            DirectLightingEstimator::RestirDirectIllumination;
        reviewConfig.lightSelection =
            LightSelectionStrategy::PowerWeighted;
        reviewConfig.reconstruction = ReconstructionMode::Svgf;
        const std::string reviewText = FormatRuntimeReview(reviewConfig);
        tests.Expect(reviewText.find(ScenePresetName(reviewConfig.scene))
                    != std::string::npos
                && reviewText.find("ReSTIR") != std::string::npos
                && reviewText.find("Vulkan Ray Query") != std::string::npos
                && reviewText.find("SVGF") != std::string::npos
                && reviewText.find("scene-recommended.many-lights.v2")
                    != std::string::npos
                && reviewText.find("偏离") != std::string::npos
                && reviewText.find("F11") != std::string::npos,
            "the F10 study card must describe current/recommended tuples and their match state");
    }

    void TestRuntimeHarness(TestContext& tests)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Ui;

        constexpr ResetMask atqp = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory
            | ResetResource::ProfilerStatistics;
        tests.Expect(ResetMaskFor(ResetCause::ProgressiveCameraMotion)
                == ResetResource::Accumulation
                && ResetMaskFor(ResetCause::CameraDiscontinuity) == atqp,
            "progressive camera motion must reset film only while a camera cut must reset A|T|Q|P");

        RuntimeConfig config;
        const RuntimeConfig initial = config;
        tests.Expect(initial.executionArchitecture == ExecutionArchitecture::Staged,
            "Wave 1 must consume the ADR 0005 PBR RuntimeConfig default without a private UI default");
        ActionQueue queue;
        const std::uint64_t firstSequence = queue.Push(SemanticAction::CycleExecutionArchitectureForward);
        const std::uint64_t secondSequence = queue.Push(SemanticAction::CycleExecutionArchitectureForward);
        queue.Push(SemanticAction::CycleExecutionArchitectureForward);
        tests.Expect(SameRuntimeConfig(config, initial), "queueing must not mutate RuntimeConfig before frame start");
        tests.Expect(firstSequence < secondSequence && queue.Size() == 3u, "ActionQueue must assign FIFO sequence numbers");

        const ActionBatchResult integratorBatch = ApplyQueuedActions(queue, config);
        tests.Expect(queue.Empty(), "ApplyQueuedActions must drain the frame queue");
        tests.Expect(integratorBatch.actions.size() == 3u
            && integratorBatch.actions[0].queued.sequence == firstSequence
            && integratorBatch.actions[1].queued.sequence == secondSequence,
            "queued actions must apply in FIFO order");
        const std::array forwardIntegrators{
            ExecutionArchitecture::Megakernel,
            ExecutionArchitecture::Wavefront,
            ExecutionArchitecture::Staged
        };
        bool forwardCycleValid = integratorBatch.actions.size()
            == forwardIntegrators.size();
        for (std::size_t index = 0; index < forwardIntegrators.size(); ++index)
        {
            if (!forwardCycleValid)
            {
                break;
            }
            RuntimeConfig expected = initial;
            expected.executionArchitecture = forwardIntegrators[index];
            forwardCycleValid = forwardCycleValid
                && integratorBatch.actions[index].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(
                    integratorBatch.actions[index].effectiveRuntimeConfig,
                    expected)
                && CapabilityTable::Evaluate(
                    integratorBatch.actions[index].effectiveRuntimeConfig).IsSupported();
        }
        tests.Expect(forwardCycleValid,
            "Ctrl+I must visit every interactive execution architecture while changing only that field");
        tests.Expect(SameRuntimeConfig(config, initial),
            "three forward execution steps must return to staged execution");
        tests.Expect(integratorBatch.actions[0].requestedResets == atqp
                && integratorBatch.actions[1].requestedResets == atqp
                && integratorBatch.actions[2].requestedResets == atqp
                && integratorBatch.requestedResets == atqp,
            "execution-only switches must reset sampling histories without rebuilding acceleration structures");

        queue.Push(SemanticAction::CycleExecutionArchitectureBackward);
        queue.Push(SemanticAction::CycleExecutionArchitectureBackward);
        queue.Push(SemanticAction::CycleExecutionArchitectureBackward);
        const ActionBatchResult reverseIntegratorBatch = ApplyQueuedActions(queue, config);
        const std::array reverseIntegrators{
            ExecutionArchitecture::Wavefront,
            ExecutionArchitecture::Megakernel,
            ExecutionArchitecture::Staged
        };
        bool reverseCycleValid = reverseIntegratorBatch.actions.size()
            == reverseIntegrators.size();
        for (std::size_t index = 0;
            reverseCycleValid && index < reverseIntegrators.size(); ++index)
        {
            RuntimeConfig expected = initial;
            expected.executionArchitecture = reverseIntegrators[index];
            reverseCycleValid = reverseIntegratorBatch.actions[index].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(
                    reverseIntegratorBatch.actions[index].effectiveRuntimeConfig,
                    expected)
                && CapabilityTable::Evaluate(
                    reverseIntegratorBatch.actions[index].effectiveRuntimeConfig).IsSupported();
        }
        tests.Expect(reverseCycleValid && SameRuntimeConfig(config, initial),
            "Ctrl+Shift+I must traverse only the execution axis in reverse and restore staged execution");
        tests.Expect(reverseIntegratorBatch.actions[0].requestedResets == atqp
                && reverseIntegratorBatch.actions[1].requestedResets == atqp
                && reverseIntegratorBatch.actions[2].requestedResets == atqp
                && reverseIntegratorBatch.requestedResets == atqp,
            "reverse execution-only switches must request only the sampling-history reset mask");

        queue.Push(SemanticAction::CycleExecutionArchitectureForward);
        queue.Push(SemanticAction::PrintCurrentReview);
        const ActionBatchResult cycleThenReviewBatch =
            ApplyQueuedActions(queue, config);
        tests.Expect(cycleThenReviewBatch.actions.size() == 2u
                && cycleThenReviewBatch.actions[1].status
                    == ActionApplyStatus::RoutedToOwner
                && cycleThenReviewBatch.actions[1].effectiveRuntimeConfig.executionArchitecture
                    == ExecutionArchitecture::Megakernel
                && FormatRuntimeReview(
                    cycleThenReviewBatch.actions[1].effectiveRuntimeConfig)
                    .find("GPU Megakernel") != std::string::npos,
            "a Ctrl+I,F10 FIFO sequence must print the newly committed execution architecture");
        config = initial;

        RuntimeConfig transportAxisConfig = initial;
        const RuntimeConfig beforeTransportAxis = transportAxisConfig;
        queue.Push(SemanticAction::CycleTransportModelForward);
        const ActionBatchResult transportAxisBatch =
            ApplyQueuedActions(queue, transportAxisConfig);
        RuntimeConfig transportAxisExpected = beforeTransportAxis;
        transportAxisExpected.transportModel = TransportModel::Whitted;
        tests.Expect(transportAxisBatch.actions.size() == 1u
                && transportAxisBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(transportAxisConfig, transportAxisExpected)
                && transportAxisConfig.executionArchitecture
                    == ExecutionArchitecture::Staged
                && transportAxisBatch.actions[0].requestedResets == atqp
                && CapabilityTable::Evaluate(transportAxisConfig).IsSupported(),
            "I must change only PBR to Whitted from the legal PBR/Staged tuple");

        RuntimeConfig environmentAxisConfig = initial;
        const RuntimeConfig beforeEnvironmentAxis = environmentAxisConfig;
        queue.Push(SemanticAction::CycleEnvironmentSamplerForward);
        const ActionBatchResult environmentAxisBatch =
            ApplyQueuedActions(queue, environmentAxisConfig);
        RuntimeConfig environmentAxisExpected = beforeEnvironmentAxis;
        environmentAxisExpected.environmentSampler =
            EnvironmentDirectionSampler::ImportanceMap;
        tests.Expect(environmentAxisBatch.actions.size() == 1u
                && environmentAxisBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(environmentAxisConfig, environmentAxisExpected)
                && environmentAxisBatch.actions[0].requestedResets == atqp
                && CapabilityTable::Evaluate(environmentAxisConfig).IsSupported(),
            "Alt+L must change only the environment-direction sampler from the legal PBR/Staged tuple");

        const RuntimeConfig beforeBackend = config;
        queue.Push(SemanticAction::CycleBackendForward);
        const ActionBatchResult backendBatch = ApplyQueuedActions(queue, config);
        RuntimeConfig backendExpected = beforeBackend;
        backendExpected.backend = TraversalBackend::GpuFlattenedSahBvh;
        tests.Expect(backendBatch.actions.size() == 1u
            && backendBatch.actions[0].status
                == ActionApplyStatus::ConfigCommitted
            && SameRuntimeConfig(config, backendExpected),
            "B must change only the backend field from legacy analytic to flattened SAH");

        config.backend = TraversalBackend::GpuFlattenedSahBvh;
        config.executionArchitecture = ExecutionArchitecture::Megakernel;
        config.directLightingEstimator =
            DirectLightingEstimator::MultipleImportanceSampling;
        config.lightSelection =
            LightSelectionStrategy::PowerWeighted;
        tests.Expect(CapabilityTable::Evaluate(config).IsSupported(),
            "the explicit Wave 2 test tuple must be supported before hotkey cycling");
        const RuntimeConfig beforeWave2Backend = config;
        queue.Push(SemanticAction::CycleBackendForward);
        const ActionBatchResult wave2BackendBatch =
            ApplyQueuedActions(queue, config);
        tests.Expect(wave2BackendBatch.actions.size() == 1u
                && wave2BackendBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && config.backend == TraversalBackend::VulkanRayQuery,
            "B must cycle between the supported Wave 2 traversal backends");
        RuntimeConfig backendOnlyExpected = beforeWave2Backend;
        backendOnlyExpected.backend = TraversalBackend::VulkanRayQuery;
        tests.Expect(SameRuntimeConfig(config, backendOnlyExpected),
            "B must change only the backend dimension of the complete tuple");

        const RuntimeConfig beforeScene = config;
        queue.Push(SemanticAction::SelectScene5);
        const ActionBatchResult sceneBatch = ApplyQueuedActions(queue, config);
        RuntimeConfig sceneExpected = beforeScene;
        sceneExpected.scene = ScenePreset::EnvironmentSamplingDome;
        tests.Expect(sceneBatch.actions.size() == 1u
            && sceneBatch.actions[0].status == ActionApplyStatus::ConfigCommitted
            && SameRuntimeConfig(config, sceneExpected),
            "scene selection must change only the scene field");

        const RuntimeConfig beforeSponza = config;
        RuntimeConfig cycleVariantConfig = config;
        ActionQueue cycleVariantQueue;
        const SceneVariantCatalog testVariants = [](const RuntimeConfig& candidate) {
            return candidate.scene == ScenePreset::EnvironmentSamplingDome
                ? std::vector<std::string>{ "uniform-sphere", "polar-sun", "seam-sun" }
                : std::vector<std::string>{ "canonical-cornell", "nee", "mis" };
        };
        cycleVariantQueue.Push(SemanticAction::CycleSceneVariantForward);
        cycleVariantQueue.Push(SemanticAction::PrintCurrentReview);
        cycleVariantQueue.Push(SemanticAction::CycleSceneVariantBackward);
        const auto variantCycleBatch = ApplyQueuedActions(cycleVariantQueue, cycleVariantConfig, testVariants);
        tests.Expect(variantCycleBatch.actions[0].status == ActionApplyStatus::ConfigCommitted
                && variantCycleBatch.actions[0].effectiveRuntimeConfig.sceneVariant == "polar-sun"
                && variantCycleBatch.actions[1].effectiveRuntimeConfig.sceneVariant == "polar-sun"
                && cycleVariantConfig.sceneVariant == "uniform-sphere"
                && variantCycleBatch.actions[0].requestedResets == ResetMaskFor(ResetCause::SceneChanged),
            "variant cycling must preserve FIFO snapshots and reset scene payload dependencies");
        RuntimeConfig expectedVariantConfig = config;
        expectedVariantConfig.sceneVariant = "uniform-sphere";
        tests.Expect(SameRuntimeConfig(cycleVariantConfig, expectedVariantConfig),
            "variant cycling must preserve all non-variant configuration fields");
        cycleVariantQueue.Push(SemanticAction::SelectScene3);
        cycleVariantQueue.Push(SemanticAction::CycleSceneVariantForward);
        (void)ApplyQueuedActions(cycleVariantQueue, cycleVariantConfig, testVariants);
        tests.Expect(cycleVariantConfig.scene == ScenePreset::CornellBox
                && cycleVariantConfig.sceneVariant == "nee",
            "a digit followed by F12 must resolve the new scene's catalog in FIFO order");
        const RuntimeConfig beforeMissingProvider = cycleVariantConfig;
        cycleVariantQueue.Push(SemanticAction::CycleSceneVariantForward);
        const auto missingVariants = ApplyQueuedActions(cycleVariantQueue, cycleVariantConfig);
        tests.Expect(missingVariants.actions[0].status == ActionApplyStatus::Rejected
                && SameRuntimeConfig(beforeMissingProvider, cycleVariantConfig),
            "missing scene variant provider must reject without fallback or mutation");
        RuntimeConfig variantConfig = config;
        variantConfig.sceneVariant = "polar-sun";
        ActionQueue variantQueue;
        variantQueue.Push(SemanticAction::SelectScene5);
        (void)ApplyQueuedActions(variantQueue, variantConfig);
        tests.Expect(variantConfig.sceneVariant == "polar-sun",
            "selecting the same scene must preserve the selected experiment");
        variantQueue.Push(SemanticAction::SelectScene3);
        const ActionBatchResult variantSceneBatch = ApplyQueuedActions(variantQueue, variantConfig);
        tests.Expect(variantSceneBatch.actions[0].status == ActionApplyStatus::ConfigCommitted
                && variantConfig.scene == ScenePreset::CornellBox
                && variantConfig.sceneVariant.empty(),
            "changing scene must clear its predecessor's local experiment ID");
        queue.Push(SemanticAction::SelectScene6);
        const ActionBatchResult sponzaBatch = ApplyQueuedActions(queue, config);
        tests.Expect(sponzaBatch.actions.size() == 1u
            && sponzaBatch.actions[0].status == ActionApplyStatus::Rejected
            && sponzaBatch.actions[0].capabilityStatus == CapabilityStatus::Unsupported
            && sponzaBatch.actions[0].reason
                == "Scene 6 unavailable: Sponza asset/provider gate",
            "Scene 6 must report its exact Sponza asset/provider gate while remaining fail-closed");
        tests.Expect(SameRuntimeConfig(config, beforeSponza),
            "a rejected Sponza action must leave the complete tuple unchanged");

        queue.Push(SemanticAction::CycleExecutionArchitectureForward, ActionPhase::Repeated);
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

        const RuntimeConfig beforeReview = config;
        queue.Push(SemanticAction::PrintCurrentReview);
        const ActionBatchResult reviewBatch = ApplyQueuedActions(queue, config);
        tests.Expect(reviewBatch.actions.size() == 1u
                && reviewBatch.actions[0].status == ActionApplyStatus::RoutedToOwner
                && reviewBatch.actions[0].routedCommand
                    == RoutedCommand::PrintCurrentReview
                && SameRuntimeConfig(config, beforeReview),
            "F10 must route a read-only review request without mutating RuntimeConfig");

        RuntimeConfig targetSppConfig;
        targetSppConfig.render.targetSamplesPerPixel = 1u;
        const RuntimeConfig beforeDebug = targetSppConfig;
        queue.Push(SemanticAction::CycleDebugViewForward);
        const ActionBatchResult debugBatch = ApplyQueuedActions(queue, targetSppConfig);
        tests.Expect(debugBatch.actions[0].status == ActionApplyStatus::ConfigCommitted,
            "a stored film SPP target must not lock interactive debug cycling");
        RuntimeConfig expectedDebug = beforeDebug;
        expectedDebug.debugView = DebugView::BaseColor;
        tests.Expect(SameRuntimeConfig(targetSppConfig, expectedDebug),
            "debug cycling must preserve the dormant film SPP target and other axes");

        RuntimeConfig mixedConfig;
        mixedConfig.scene = ScenePreset::ManyLightsRestirArena;
        mixedConfig.backend = TraversalBackend::VulkanRayQuery;
        mixedConfig.executionArchitecture = ExecutionArchitecture::Wavefront;
        mixedConfig.directLightingEstimator =
            DirectLightingEstimator::RestirDirectIllumination;
        mixedConfig.lightSelection =
            LightSelectionStrategy::PowerWeighted;
        mixedConfig.reconstruction = ReconstructionMode::Svgf;
        mixedConfig.debugView = DebugView::Emissive;
        mixedConfig.shadowMethod = ShadowMethod::Physical;
        mixedConfig.restir.manyLightsTier = ManyLightsTier::Lights100;
        tests.Expect(CapabilityTable::Evaluate(mixedConfig).IsSupported(),
            "the mixed UI fixture must start as a supported tuple");

        const RuntimeConfig beforeDirect = mixedConfig;
        queue.Push(SemanticAction::CycleDirectLightingForward);
        const ActionBatchResult directBatch =
            ApplyQueuedActions(queue, mixedConfig);
        RuntimeConfig directExpected = beforeDirect;
        directExpected.directLightingEstimator =
            DirectLightingEstimator::BsdfOnly;
        tests.Expect(directBatch.actions.size() == 1u
                && directBatch.actions[0].status
                == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(mixedConfig, directExpected)
                && directBatch.requestedResets == atqp,
            "L must cycle only the direct-lighting estimator field");

        const RuntimeConfig beforeProposal = mixedConfig;
        queue.Push(SemanticAction::CycleLightSelectionForward);
        const ActionBatchResult proposalBatch =
            ApplyQueuedActions(queue, mixedConfig);
        RuntimeConfig proposalExpected = beforeProposal;
        proposalExpected.lightSelection =
            LightSelectionStrategy::Uniform;
        tests.Expect(proposalBatch.actions.size() == 1u
                && proposalBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(mixedConfig, proposalExpected)
                && proposalBatch.requestedResets == atqp,
            "Ctrl+L must cycle only the discrete-light-selection field");

        const RuntimeConfig beforeShadow = mixedConfig;
        queue.Push(SemanticAction::CycleShadowForward);
        const ActionBatchResult shadowBatch =
            ApplyQueuedActions(queue, mixedConfig);
        RuntimeConfig shadowExpected = beforeShadow;
        shadowExpected.shadowMethod = ShadowMethod::Pcf;
        tests.Expect(shadowBatch.actions.size() == 1u
                && shadowBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(mixedConfig, shadowExpected)
                && shadowBatch.requestedResets == atqp,
            "Ctrl+Alt+L must cycle only the shadow-method field");

        queue.Push(SemanticAction::CycleReconstructionForward);
        const RuntimeConfig beforeReconstruction = mixedConfig;
        const ActionBatchResult reconstructionBatch =
            ApplyQueuedActions(queue, mixedConfig);
        RuntimeConfig reconstructionExpected = beforeReconstruction;
        reconstructionExpected.reconstruction = ReconstructionMode::CurrentFrame;
        tests.Expect(reconstructionBatch.actions.size() == 1u
                && reconstructionBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(mixedConfig, reconstructionExpected),
            "N must cycle only the reconstruction field and wrap SVGF to Current Frame");

        const std::array reconstructionCycle{
            ReconstructionMode::ProgressiveMean,
            ReconstructionMode::TemporalAccumulation,
            ReconstructionMode::SpatialFixedAtrous,
            ReconstructionMode::Svgf
        };
        bool reconstructionCycleValid = true;
        for (const ReconstructionMode expectedMode : reconstructionCycle)
        {
            const RuntimeConfig beforeCycle = mixedConfig;
            queue.Push(SemanticAction::CycleReconstructionForward);
            const ActionBatchResult cycleBatch =
                ApplyQueuedActions(queue, mixedConfig);
            RuntimeConfig expected = beforeCycle;
            expected.reconstruction = expectedMode;
            reconstructionCycleValid = reconstructionCycleValid
                && cycleBatch.actions.size() == 1u
                && cycleBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(mixedConfig, expected)
                && cycleBatch.requestedResets
                    == (ResetResource::TemporalHistory
                        | ResetResource::ProfilerStatistics);
        }
        tests.Expect(reconstructionCycleValid,
            "N must traverse Progressive Mean, Temporal, Temporal+A-Trous, and SVGF in order");

        const RuntimeConfig beforeIntegrator = mixedConfig;
        RuntimeConfig integratorForwardConfig = beforeIntegrator;
        queue.Push(SemanticAction::CycleExecutionArchitectureForward);
        const ActionBatchResult integratorForwardBatch =
            ApplyQueuedActions(queue, integratorForwardConfig);
        RuntimeConfig integratorForwardExpected = beforeIntegrator;
        integratorForwardExpected.executionArchitecture = ExecutionArchitecture::Staged;
        tests.Expect(integratorForwardBatch.actions.size() == 1u
                && integratorForwardBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(
                    integratorForwardConfig, integratorForwardExpected)
                && integratorForwardBatch.requestedResets == atqp
                && CapabilityTable::Evaluate(integratorForwardConfig).IsSupported(),
            "Ctrl+I must change only Wavefront to Staged inside an arbitrary split tuple");

        RuntimeConfig integratorBackwardConfig = beforeIntegrator;
        queue.Push(SemanticAction::CycleExecutionArchitectureBackward);
        const ActionBatchResult integratorBackwardBatch =
            ApplyQueuedActions(queue, integratorBackwardConfig);
        RuntimeConfig integratorBackwardExpected = beforeIntegrator;
        integratorBackwardExpected.executionArchitecture =
            ExecutionArchitecture::Megakernel;
        tests.Expect(integratorBackwardBatch.actions.size() == 1u
                && integratorBackwardBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(
                    integratorBackwardConfig, integratorBackwardExpected)
                && integratorBackwardBatch.requestedResets == atqp
                && CapabilityTable::Evaluate(integratorBackwardConfig).IsSupported(),
            "Ctrl+Shift+I must change only Wavefront to Megakernel inside an arbitrary split tuple");
    }

    void TestSceneRecommendedProfiles(TestContext& tests)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Ui;

        const std::span<const SceneRecommendedProfile> profiles =
            GetSceneRecommendedProfiles();
        tests.Expect(profiles.size() == 10u,
            "the teaching recommendation registry must cover all ten scenes");

        const auto expectProfile = [&tests](
            const ScenePreset scene,
            const std::string_view stableId,
            const TraversalBackend backend,
            const TransportModel transport,
            const ExecutionArchitecture execution,
            const DirectLightingEstimator direct,
            const LightSelectionStrategy lightSelection,
            const EnvironmentDirectionSampler environmentSampler,
            const ReconstructionMode reconstruction,
            const std::uint32_t bounce)
        {
            const SceneRecommendedProfile* const profile =
                FindSceneRecommendedProfile(scene);
            tests.Expect(profile != nullptr
                    && profile->stableId == stableId
                    && profile->scene == scene
                    && profile->backend == backend
                    && profile->transportModel == transport
                    && profile->executionArchitecture == execution
                    && profile->directLightingEstimator == direct
                    && profile->lightSelection == lightSelection
                    && profile->environmentSampler == environmentSampler
                    && profile->reconstruction == reconstruction
                    && profile->debugView == DebugView::Final
                    && profile->shadowMethod == ShadowMethod::Physical
                    && profile->maximumBounce == bounce,
                "a registered scene teaching recommendation differs from its fixed v1 contract");
        };

        expectProfile(ScenePreset::BaselineGallery,
            "scene-recommended.baseline.v2", TraversalBackend::CanonicalLinearGpu,
            TransportModel::Pbr, ExecutionArchitecture::Staged,
            DirectLightingEstimator::NextEventEstimation, LightSelectionStrategy::Uniform,
            EnvironmentDirectionSampler::UniformSphere, ReconstructionMode::ProgressiveMean, 8u);
        expectProfile(ScenePreset::IntersectionBvhLab,
            "scene-recommended.intersection-bvh.v2", TraversalBackend::GpuFlattenedSahBvh,
            TransportModel::Pbr, ExecutionArchitecture::Staged,
            DirectLightingEstimator::NextEventEstimation, LightSelectionStrategy::Uniform,
            EnvironmentDirectionSampler::UniformSphere, ReconstructionMode::ProgressiveMean, 1u);
        expectProfile(ScenePreset::WhittedOpticsRoom,
            "scene-recommended.whitted-optics.v2", TraversalBackend::VulkanRayQuery,
            TransportModel::Whitted, ExecutionArchitecture::Staged,
            DirectLightingEstimator::NextEventEstimation, LightSelectionStrategy::Uniform,
            EnvironmentDirectionSampler::UniformSphere, ReconstructionMode::ProgressiveMean, 12u);
        expectProfile(ScenePreset::CornellBox,
            "scene-recommended.cornell.v2", TraversalBackend::GpuFlattenedSahBvh,
            TransportModel::Pbr, ExecutionArchitecture::Staged,
            DirectLightingEstimator::MultipleImportanceSampling, LightSelectionStrategy::Uniform,
            EnvironmentDirectionSampler::UniformSphere, ReconstructionMode::ProgressiveMean, 8u);
        expectProfile(ScenePreset::GgxMisMaterialLab,
            "scene-recommended.ggx-mis.v2", TraversalBackend::VulkanRayQuery,
            TransportModel::Pbr, ExecutionArchitecture::Megakernel,
            DirectLightingEstimator::MultipleImportanceSampling,
            LightSelectionStrategy::PowerWeighted, EnvironmentDirectionSampler::UniformSphere,
            ReconstructionMode::ProgressiveMean, 8u);
        expectProfile(ScenePreset::EnvironmentSamplingDome,
            "scene-recommended.environment-dome.v2", TraversalBackend::VulkanRayQuery,
            TransportModel::Pbr, ExecutionArchitecture::Staged,
            DirectLightingEstimator::MultipleImportanceSampling,
            LightSelectionStrategy::PowerWeighted, EnvironmentDirectionSampler::ImportanceMap,
            ReconstructionMode::ProgressiveMean, 8u);
        expectProfile(ScenePreset::SponzaTraversalHall,
            "scene-recommended.sponza.v2", TraversalBackend::VulkanRayQuery,
            TransportModel::Pbr, ExecutionArchitecture::Wavefront,
            DirectLightingEstimator::MultipleImportanceSampling,
            LightSelectionStrategy::PowerWeighted, EnvironmentDirectionSampler::UniformSphere,
            ReconstructionMode::ProgressiveMean, 8u);
        expectProfile(ScenePreset::BackendParityBenchmark,
            "scene-recommended.backend-parity.v2", TraversalBackend::CanonicalLinearGpu,
            TransportModel::Pbr, ExecutionArchitecture::Staged,
            DirectLightingEstimator::NextEventEstimation, LightSelectionStrategy::Uniform,
            EnvironmentDirectionSampler::UniformSphere, ReconstructionMode::ProgressiveMean, 4u);
        expectProfile(ScenePreset::TemporalStabilityCorridor,
            "scene-recommended.temporal-stability.v2", TraversalBackend::VulkanRayQuery,
            TransportModel::Pbr, ExecutionArchitecture::Wavefront,
            DirectLightingEstimator::MultipleImportanceSampling,
            LightSelectionStrategy::PowerWeighted, EnvironmentDirectionSampler::UniformSphere,
            ReconstructionMode::Svgf, 6u);
        expectProfile(ScenePreset::ManyLightsRestirArena,
            "scene-recommended.many-lights.v2", TraversalBackend::VulkanRayQuery,
            TransportModel::Pbr, ExecutionArchitecture::Wavefront,
            DirectLightingEstimator::RestirDirectIllumination,
            LightSelectionStrategy::PowerWeighted, EnvironmentDirectionSampler::UniformSphere,
            ReconstructionMode::Svgf, 4u);

        bool uniqueScenes = true;
        bool uniqueStableIds = true;
        for (std::size_t left = 0u; left < profiles.size(); ++left)
        {
            for (std::size_t right = left + 1u; right < profiles.size(); ++right)
            {
                uniqueScenes = uniqueScenes
                    && profiles[left].scene != profiles[right].scene;
                uniqueStableIds = uniqueStableIds
                    && profiles[left].stableId != profiles[right].stableId;
            }
        }
        tests.Expect(uniqueScenes && uniqueStableIds,
            "recommendation scene keys and stable IDs must both be unique");

        const SceneRecommendedProfile* const manyLights =
            FindSceneRecommendedProfile(ScenePreset::ManyLightsRestirArena);
        tests.Expect(manyLights != nullptr && manyLights->restir.has_value()
                && manyLights->restir->manyLightsTier == ManyLightsTier::Lights100
                && manyLights->restir->reuseStage == RestirReuseStage::TemporalSpatial
                && manyLights->restir->biasMode == RestirBiasMode::ExplicitlyBiased
                && manyLights->restir->initialCandidatesPerPixel == 1u
                && manyLights->restir->spatialNeighbors == 5u
                && manyLights->restir->maximumReservoirM == 32u
                && manyLights->restir->maximumHistoryAge == 20u
                && manyLights->restir->comparisonCandidateBudgetPerPixel == 8u
                && manyLights->restir->comparisonVisibilityBudgetPerPixel == 1u
                && !manyLights->restir->animateLights
                && !manyLights->restir->animateRigidOccluders,
            "scene 9 must carry the complete fixed ReSTIR teaching settings");

        RuntimeConfig source;
        source.scene = ScenePreset::ManyLightsRestirArena;
        source.render.width = 960u;
        source.render.height = 540u;
        source.render.renderScale = 1.0f;
        source.render.samplesPerFrame = 1u;
        source.render.targetSamplesPerPixel = 17u;
        source.render.baseSeed = 0x12345678u;
        source.render.exposure = 2.25f;
        source.render.verticalFovDegrees = 61.0f;
        source.render.vsync = RuntimeToggle::Disabled;
        source.run.frameLimit = 73u;
        source.run.resizeTest = true;
        source.run.validation = RuntimeToggle::Enabled;
        source.run.captureDirectory = "D:/recommended-profile-capture";
        source.run.benchmarkPreset = "preserved-benchmark";
        source.run.referenceImage = "D:/preserved-reference.exr";
        source.run.artifactRoot = "D:/preserved-artifacts";
        source.run.runIdentifier = "preserved-run-id";
        const RuntimeConfig made = MakeSceneRecommendedConfig(source, *manyLights);
        tests.Expect(made.scene == source.scene
                && made.render.width == source.render.width
                && made.render.height == source.render.height
                && made.render.renderScale == source.render.renderScale
                && made.render.samplesPerFrame == source.render.samplesPerFrame
                && made.render.targetSamplesPerPixel
                    == source.render.targetSamplesPerPixel
                && made.render.baseSeed == source.render.baseSeed
                && made.render.exposure == source.render.exposure
                && made.render.verticalFovDegrees
                    == source.render.verticalFovDegrees
                && made.render.vsync == source.render.vsync
                && made.run.frameLimit == source.run.frameLimit
                && made.run.resizeTest == source.run.resizeTest
                && made.run.validation == source.run.validation
                && made.run.captureDirectory == source.run.captureDirectory
                && made.run.benchmarkPreset == source.run.benchmarkPreset
                && made.run.referenceImage == source.run.referenceImage
                && made.run.artifactRoot == source.run.artifactRoot
                && made.run.runIdentifier == source.run.runIdentifier,
            "recommendation construction must preserve scene, render/run controls, seed, FOV, and output paths");

        constexpr ResetMask atqp = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory
            | ResetResource::ProfilerStatistics;
        constexpr ResetMask atqpas = atqp | ResetResource::AccelerationStructures;

        for (const SceneRecommendedProfile& profile : profiles)
        {
            RuntimeConfig config;
            config.scene = profile.scene;
            config.debugView = DebugView::Emissive;
            config.render.width = 960u;
            config.render.height = 540u;
            config.render.targetSamplesPerPixel = 19u;
            config.render.baseSeed = 987654321u;
            config.render.exposure = 1.75f;
            config.render.verticalFovDegrees = 63.0f;
            config.render.vsync = RuntimeToggle::Disabled;
            config.run.frameLimit = 41u;
            config.run.validation = RuntimeToggle::Enabled;
            config.run.captureDirectory = "D:/profile-live-capture";
            config.run.artifactRoot = "D:/profile-live-artifacts";
            config.run.runIdentifier = "profile-live-run";
            const RuntimeConfig before = config;

            ActionQueue queue;
            queue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
            const ActionBatchResult batch = ApplyQueuedActions(queue, config);
            if (profile.scene == ScenePreset::SponzaTraversalHall)
            {
                tests.Expect(batch.actions.size() == 1u
                        && batch.actions[0].status == ActionApplyStatus::Rejected
                        && batch.actions[0].reason
                            == "Scene 6 unavailable: Sponza asset/provider gate"
                        && SameRuntimeConfig(config, before),
                    "the registered future Sponza recommendation must remain fail-closed and atomic");
                continue;
            }

            const RuntimeConfig expected = MakeSceneRecommendedConfig(before, profile);
            tests.Expect(batch.actions.size() == 1u
                    && batch.actions[0].status == ActionApplyStatus::ConfigCommitted
                    && SameRuntimeConfig(config, expected)
                    && SameRuntimeConfig(
                        batch.actions[0].effectiveRuntimeConfig, expected)
                    && MatchesSceneRecommendedProfile(config, profile)
                    && CapabilityTable::Evaluate(config).IsSupported(),
                "F11 must atomically restore each built scene's supported recommendation while preserving owned-out fields");
        }

        const SceneRecommendedProfile* const baseline =
            FindSceneRecommendedProfile(ScenePreset::BaselineGallery);
        RuntimeConfig nonRestirPayload = RuntimeConfig{};
        nonRestirPayload.scene = ScenePreset::BaselineGallery;
        nonRestirPayload.debugView = DebugView::Emissive;
        nonRestirPayload.restir.initialCandidatesPerPixel = 3u;
        nonRestirPayload.restir.spatialNeighbors = 4u;
        nonRestirPayload.restir.animateLights = false;
        nonRestirPayload.restir.animateRigidOccluders = false;
        const RuntimeConfig nonRestirBefore = nonRestirPayload;
        ActionQueue nonRestirQueue;
        nonRestirQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult nonRestirBatch =
            ApplyQueuedActions(nonRestirQueue, nonRestirPayload);
        const RuntimeConfig nonRestirExpected =
            MakeSceneRecommendedConfig(nonRestirBefore, *baseline);
        tests.Expect(nonRestirBatch.actions.size() == 1u
                && nonRestirBatch.actions[0].status
                    == ActionApplyStatus::ConfigCommitted
                && SameRuntimeConfig(nonRestirPayload, nonRestirExpected)
                && nonRestirBatch.requestedResets == ResetResource::None
                && nonRestirPayload.restir.initialCandidatesPerPixel == 3u
                && nonRestirPayload.restir.spatialNeighbors == 4u
                && !nonRestirPayload.restir.animateLights
                && !nonRestirPayload.restir.animateRigidOccluders,
            "F11 must preserve non-default ReSTIR settings when the current scene recommendation does not own them");
        RuntimeConfig debugOnly = MakeSceneRecommendedConfig(RuntimeConfig{}, *baseline);
        debugOnly.debugView = DebugView::Emissive;
        ActionQueue resetQueue;
        resetQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult debugOnlyBatch = ApplyQueuedActions(resetQueue, debugOnly);
        tests.Expect(debugOnlyBatch.actions[0].status == ActionApplyStatus::ConfigCommitted
                && debugOnlyBatch.requestedResets == ResetResource::None,
            "a recommendation that changes only Debug View must not reset histories");

        RuntimeConfig reconstructionOnly = MakeSceneRecommendedConfig(RuntimeConfig{}, *baseline);
        reconstructionOnly.reconstruction = ReconstructionMode::Svgf;
        resetQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult reconstructionBatch =
            ApplyQueuedActions(resetQueue, reconstructionOnly);
        tests.Expect(reconstructionBatch.requestedResets
                == (ResetResource::TemporalHistory | ResetResource::ProfilerStatistics),
            "a reconstruction-only F11 correction must request exactly T|P");

        RuntimeConfig backendOnly = MakeSceneRecommendedConfig(RuntimeConfig{}, *baseline);
        backendOnly.backend = TraversalBackend::VulkanRayQuery;
        resetQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult backendBatch = ApplyQueuedActions(resetQueue, backendOnly);
        tests.Expect(backendBatch.requestedResets == atqpas,
            "a backend correction must request A|T|Q|P|AS");

        RuntimeConfig manyLightsPayload = MakeSceneRecommendedConfig(RuntimeConfig{}, *manyLights);
        manyLightsPayload.scene = ScenePreset::ManyLightsRestirArena;
        manyLightsPayload.restir.manyLightsTier = ManyLightsTier::Lights1000;
        manyLightsPayload.restir.animateLights = true;
        manyLightsPayload.restir.animateRigidOccluders = true;
        resetQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult payloadBatch =
            ApplyQueuedActions(resetQueue, manyLightsPayload);
        tests.Expect(payloadBatch.requestedResets == atqpas,
            "scene 9 tier/animation payload restoration must rebuild scene and AS with A|T|Q|P|AS");

        RuntimeConfig manyLightsSampling = MakeSceneRecommendedConfig(RuntimeConfig{}, *manyLights);
        manyLightsSampling.scene = ScenePreset::ManyLightsRestirArena;
        manyLightsSampling.restir.initialCandidatesPerPixel = 2u;
        resetQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult restirSamplingBatch =
            ApplyQueuedActions(resetQueue, manyLightsSampling);
        tests.Expect(restirSamplingBatch.requestedResets == atqp,
            "scene 9 non-payload ReSTIR parameters must request A|T|Q|P without AS");

        RuntimeConfig alreadyRecommended = MakeSceneRecommendedConfig(RuntimeConfig{}, *baseline);
        const RuntimeConfig beforeNoOp = alreadyRecommended;
        resetQueue.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult noOpBatch =
            ApplyQueuedActions(resetQueue, alreadyRecommended);
        tests.Expect(noOpBatch.actions[0].status
                    == ActionApplyStatus::AcceptedNoConfigChange
                && noOpBatch.actions[0].reason
                    == "current scene already uses its recommended teaching profile"
                && noOpBatch.requestedResets == ResetResource::None
                && SameRuntimeConfig(alreadyRecommended, beforeNoOp),
            "repeated F11 on a matching profile must be idempotent and keep history");

        RuntimeConfig digitThenRestore = MakeSceneRecommendedConfig(RuntimeConfig{}, *manyLights);
        digitThenRestore.scene = ScenePreset::ManyLightsRestirArena;
        ActionQueue fifo;
        fifo.Push(SemanticAction::SelectScene0);
        fifo.Push(SemanticAction::RestoreCurrentSceneRecommendedProfile);
        const ActionBatchResult fifoBatch = ApplyQueuedActions(fifo, digitThenRestore);
        tests.Expect(fifoBatch.actions.size() == 2u
                && fifoBatch.actions[0].effectiveRuntimeConfig.scene
                    == ScenePreset::BaselineGallery
                && fifoBatch.actions[0].effectiveRuntimeConfig.executionArchitecture
                    == ExecutionArchitecture::Wavefront
                && fifoBatch.actions[1].status == ActionApplyStatus::ConfigCommitted
                && MatchesSceneRecommendedProfile(digitThenRestore, *baseline),
            "0 must remain scene-only and a following F11 must restore the new scene recommendation in FIFO order");
    }

    void TestBuiltInteractiveCrossProduct(TestContext& tests)
    {
        using namespace RenderingEngine;

        constexpr std::array scenes{
            ScenePreset::BaselineGallery,
            ScenePreset::IntersectionBvhLab,
            ScenePreset::WhittedOpticsRoom,
            ScenePreset::CornellBox,
            ScenePreset::GgxMisMaterialLab,
            ScenePreset::EnvironmentSamplingDome,
            ScenePreset::BackendParityBenchmark,
            ScenePreset::TemporalStabilityCorridor,
            ScenePreset::ManyLightsRestirArena
        };
        constexpr std::array transports{
            TransportModel::Pbr,
            TransportModel::Whitted
        };
        constexpr std::array executions{
            ExecutionArchitecture::Staged,
            ExecutionArchitecture::Megakernel,
            ExecutionArchitecture::Wavefront
        };
        constexpr std::array backends{
            TraversalBackend::CanonicalLinearGpu,
            TraversalBackend::GpuFlattenedSahBvh,
            TraversalBackend::VulkanRayQuery
        };
        constexpr std::array directEstimators{
            DirectLightingEstimator::BsdfOnly,
            DirectLightingEstimator::NextEventEstimation,
            DirectLightingEstimator::MultipleImportanceSampling,
            DirectLightingEstimator::RestirDirectIllumination
        };
        constexpr std::array lightSelections{
            LightSelectionStrategy::Uniform,
            LightSelectionStrategy::PowerWeighted
        };
        constexpr std::array environmentSamplers{
            EnvironmentDirectionSampler::UniformSphere,
            EnvironmentDirectionSampler::ImportanceMap
        };
        constexpr std::array reconstructions{
            ReconstructionMode::CurrentFrame,
            ReconstructionMode::ProgressiveMean,
            ReconstructionMode::TemporalAccumulation,
            ReconstructionMode::SpatialFixedAtrous,
            ReconstructionMode::Svgf
        };
        constexpr std::array debugViews{
            DebugView::Final,
            DebugView::BaseColor,
            DebugView::Normal,
            DebugView::Roughness,
            DebugView::Metallic,
            DebugView::Emissive
        };
        constexpr std::array shadowMethods{
            ShadowMethod::Pcf,
            ShadowMethod::Pcss,
            ShadowMethod::Physical
        };

        constexpr std::size_t expectedCombinationCount = scenes.size()
            * transports.size()
            * executions.size()
            * backends.size()
            * directEstimators.size()
            * lightSelections.size()
            * environmentSamplers.size()
            * reconstructions.size()
            * debugViews.size()
            * shadowMethods.size();
        static_assert(expectedCombinationCount == 233'280u);
        constexpr std::size_t expectedSupportedCount = 145'800u;

        bool catalogContainsOnlyBuiltValues = true;
        for (const ScenePreset scene : scenes)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(scene);
        }
        for (const TransportModel transport : transports)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(transport);
        }
        for (const ExecutionArchitecture execution : executions)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(execution);
        }
        for (const TraversalBackend backend : backends)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(backend);
        }
        for (const DirectLightingEstimator estimator : directEstimators)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(estimator);
        }
        for (const LightSelectionStrategy lightSelection : lightSelections)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(lightSelection);
        }
        for (const EnvironmentDirectionSampler environmentSampler : environmentSamplers)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(environmentSampler);
        }
        for (const ReconstructionMode reconstruction : reconstructions)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(reconstruction);
        }
        for (const DebugView debugView : debugViews)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(debugView);
        }
        for (const ShadowMethod shadowMethod : shadowMethods)
        {
            catalogContainsOnlyBuiltValues = catalogContainsOnlyBuiltValues
                && CapabilityTable::IsBuilt(shadowMethod);
        }
        tests.Expect(catalogContainsOnlyBuiltValues,
            "the interactive cross-product fixture must contain only built axis values");

        RuntimeConfig base;
        base.restir.manyLightsTier = ManyLightsTier::Lights100;
        base.run.headless = false;
        base.render.targetSamplesPerPixel = 0u;
        tests.Expect(CapabilityTable::Evaluate(base).IsSupported(),
            "the interactive cross-product base RuntimeConfig must be valid and supported");

        std::size_t evaluatedCount = 0u;
        std::size_t supportedCount = 0u;
        for (const ScenePreset scene : scenes)
        {
            for (const TransportModel transport : transports)
            {
                for (const ExecutionArchitecture execution : executions)
                {
                    for (const TraversalBackend backend : backends)
                    {
                        for (const DirectLightingEstimator estimator : directEstimators)
                        {
                            for (const LightSelectionStrategy lightSelection : lightSelections)
                            {
                                for (const EnvironmentDirectionSampler environmentSampler : environmentSamplers)
                                {
                                    for (const ReconstructionMode reconstruction : reconstructions)
                                    {
                                        for (const DebugView debugView : debugViews)
                                        {
                                            for (const ShadowMethod shadowMethod : shadowMethods)
                                            {
                                                RuntimeConfig candidate = base;
                                                candidate.scene = scene;
                                                candidate.transportModel = transport;
                                                candidate.executionArchitecture = execution;
                                                candidate.backend = backend;
                                                candidate.directLightingEstimator = estimator;
                                                candidate.lightSelection = lightSelection;
                                                candidate.environmentSampler = environmentSampler;
                                                candidate.reconstruction = reconstruction;
                                                candidate.debugView = debugView;
                                                candidate.shadowMethod = shadowMethod;
                                                ++evaluatedCount;
                                                const CapabilityDecision decision =
                                                    CapabilityTable::Evaluate(candidate);
                                                const bool expectedSupported =
                                                    transport == TransportModel::Pbr
                                                    || (execution
                                                            == ExecutionArchitecture::Staged
                                                        && estimator
                                                            != DirectLightingEstimator::RestirDirectIllumination);
                                                const CapabilityStatus expectedStatus =
                                                    expectedSupported
                                                    ? CapabilityStatus::Supported
                                                    : CapabilityStatus::Unsupported;
                                                tests.Expect(decision.status == expectedStatus,
                                                    "every interactive tuple must match the explicit PBR/Whitted capability boundary");
                                                if (decision.IsSupported())
                                                {
                                                    ++supportedCount;
                                                }
                                                else
                                                {
                                                    tests.Expect(!decision.reason.empty(),
                                                        "every rejected split-axis tuple must explain why it is unsupported");
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        tests.Expect(evaluatedCount == expectedCombinationCount,
            "the split-axis interactive cross-product must evaluate exactly 233280 tuples");
        tests.Expect(supportedCount == expectedSupportedCount
                && expectedCombinationCount - supportedCount == 87'480u,
            "capability evaluation must accept exactly 145800 tuples and reject exactly 87480 unsupported combinations");
    }

    void TestViewModel(TestContext& tests)
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Ui;

        const RuntimeConfig config;
        const ShowcaseViewModel model = BuildShowcaseViewModel(config);
        tests.Expect(model.dimensions.size() == 10u,
            "view model must expose ten orthogonal RuntimeConfig dimensions");
        tests.Expect(std::count(model.tupleText.begin(), model.tupleText.end(), '|') == 9,
            "formatted tuple must retain all ten dimensions");
        tests.Expect(!model.tuple.transportModel.empty()
                && !model.tuple.executionArchitecture.empty()
                && !model.tuple.directEstimator.empty()
                && !model.tuple.lightSelection.empty()
                && !model.tuple.environmentSampler.empty()
                && !model.tuple.shadowMethod.empty(),
            "tuple must show transport, execution, direct, light, environment, and shadow axes separately");
        tests.Expect(model.help.size() == GetActionCatalog().size(),
            "help model must derive from the same action catalog");
        tests.Expect(model.sceneRecommendation.registered
                && model.sceneRecommendation.matches
                && model.sceneRecommendation.stableId
                    == "scene-recommended.baseline.v2"
                && model.sceneRecommendation.text.find("F11")
                    != std::string::npos,
            "ImGui state must expose the current scene recommendation and match state");

        const auto sceneDimension = std::find_if(
            model.dimensions.begin(),
            model.dimensions.end(),
            [](const CapabilityDimensionViewModel& dimension) { return dimension.id == "scene"; });
        tests.Expect(sceneDimension != model.dimensions.end() && sceneDimension->options.size() == 10u,
            "scene panel shell must expose all ten roadmap scenes");
        if (sceneDimension != model.dimensions.end() && sceneDimension->options.size() > 5u)
        {
            const CapabilityOptionViewModel& baseline = sceneDimension->options[0];
            const CapabilityOptionViewModel& cornell = sceneDimension->options[3];
            const CapabilityOptionViewModel& environment = sceneDimension->options[5];
            const CapabilityOptionViewModel& sponza = sceneDimension->options[6];
            tests.Expect(baseline.selected && baseline.built && baseline.enabled,
                "Baseline Gallery must be selected and enabled under Wave 0 capability");
            tests.Expect(cornell.built && cornell.enabled && cornell.owner == "L2"
                    && cornell.reason.empty(),
                "built Cornell data must be selectable with the current independent axes");
            tests.Expect(environment.built && environment.enabled
                    && environment.owner == "L2" && environment.reason.empty(),
                "the built environment scene must be selectable with the current independent axes");
            tests.Expect(!sponza.built && !sponza.enabled
                    && sponza.owner == "L2" && !sponza.reason.empty(),
                "asset-gated Sponza must remain disabled with owner and capability reason");
        }

        const auto transportDimension = std::find_if(
            model.dimensions.begin(),
            model.dimensions.end(),
            [](const CapabilityDimensionViewModel& dimension)
            {
                return dimension.id == "transport";
            });
        const CapabilityOptionViewModel* selectedTransport = nullptr;
        if (transportDimension != model.dimensions.end())
        {
            const auto selected = std::find_if(
                transportDimension->options.begin(),
                transportDimension->options.end(),
                [](const CapabilityOptionViewModel& option)
                {
                    return option.selected;
                });
            if (selected != transportDimension->options.end())
            {
                selectedTransport = &*selected;
            }
        }
        tests.Expect(transportDimension != model.dimensions.end()
                && selectedTransport != nullptr
                && selectedTransport->token == "pbr"
                && selectedTransport->label == "PBR Path Transport",
            "the view model must expose PBR as a transport model rather than an execution architecture");

        const auto executionDimension = std::find_if(
            model.dimensions.begin(), model.dimensions.end(),
            [](const CapabilityDimensionViewModel& dimension)
            {
                return dimension.id == "execution";
            });
        tests.Expect(executionDimension != model.dimensions.end()
                && executionDimension->options.size() == 4u,
            "execution architecture must be presented as an independent dimension");

        const auto reconstructionDimension = std::find_if(
            model.dimensions.begin(), model.dimensions.end(),
            [](const CapabilityDimensionViewModel& dimension)
            {
                return dimension.id == "reconstruction";
            });
        bool reconstructionOptionsOrdered =
            reconstructionDimension != model.dimensions.end()
            && reconstructionDimension->options.size() == 5u;
        if (reconstructionOptionsOrdered)
        {
            const std::array<std::string_view, 5u> expectedTokens{
                "current-frame", "progressive-mean", "temporal",
                "atrous-spatial", "svgf"};
            for (std::size_t index = 0u; index < expectedTokens.size(); ++index)
            {
                reconstructionOptionsOrdered = reconstructionOptionsOrdered
                    && reconstructionDimension->options[index].token
                        == expectedTokens[index]
                    && reconstructionDimension->options[index].built
                    && reconstructionDimension->options[index].enabled;
            }
        }
        tests.Expect(reconstructionOptionsOrdered,
            "reconstruction dimension must expose Current Frame, Progressive Mean, Temporal, Temporal+A-Trous, and SVGF in order");

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
        const auto recommendationHelp = std::find_if(
            model.help.begin(),
            model.help.end(),
            [](const HelpEntryViewModel& entry)
            {
                return entry.binding != nullptr
                    && entry.binding->action
                        == SemanticAction::RestoreCurrentSceneRecommendedProfile;
            });
        tests.Expect(recommendationHelp != model.help.end()
                && recommendationHelp->enabled
                && recommendationHelp->owner == "L10",
            "the ImGui/help surface must expose enabled F11 recommendation restore");
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
        metadata.submittedCamera = std::array<float, 13>{
            1, 2, 3, 0, 0, -1, 1, 0, 0, 0, 1, 0, 52 };
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
        tests.Expect(metadataJson.find(
            "\"submitted_camera_position_forward_right_up_fov\": [1, 2, 3, 0, 0, -1, 1, 0, 0, 0, 1, 0, 52]") != std::string::npos,
            "capture must serialize observed camera values separately from its preset label");
        tests.Expect(metadataJson.find("\"state\": \"complete\"") != std::string::npos
            && metadataJson.find("\"requested\"") != std::string::npos
            && metadataJson.find("\"effective\"") != std::string::npos
            && metadataJson.find("\"scene\": \"cornell\"") != std::string::npos
            && metadataJson.find("\"backend\": \"cpu-sah\"") != std::string::npos
            && metadataJson.find("\"transport_model\": \"pbr\"") != std::string::npos
            && metadataJson.find("\"execution_architecture\": \"cpu-reference\"") != std::string::npos
            && metadataJson.find("\"direct_lighting_estimator\": \"nee\"") != std::string::npos
            && metadataJson.find("\"light_selection\": \"uniform\"") != std::string::npos
            && metadataJson.find("\"environment_sampler\": \"uniform-sphere\"") != std::string::npos
            && metadataJson.find("\"reconstruction\": \"current-frame\"") != std::string::npos
            && metadataJson.find("\"reconstruction\": \"progressive-mean\"") != std::string::npos
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
            "metadata must publish completion last and use runtime-config-v2 tokens for replay");

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
            "an invalid runtime-config-v2 tuple must not publish complete metadata");

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
        abStart.captureFrameIndex = 23u;
        abStart.captureSampleIndex = 64u;
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
            artifact.sceneStableId = request.sceneStableId;
            artifact.variantStableId = request.variantStableId;
            artifact.anchor = request.anchor;
            artifact.frameIndex = request.frameIndex;
            artifact.sampleIndex = request.sampleIndex;
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

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        try
        {
            const std::string_view command = argv[1];
            if (command == "--verify-progressive-film")
                return RenderingEngine::Tests::VerifyProgressiveFilm(argc, argv);
            if (command == "--compare-linear-exr")
                return RenderingEngine::Tests::CompareLinearExr(argc, argv);
            throw std::runtime_error("unknown Showcase test command");
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    TestContext tests;
    TestActionMap(tests);
    TestRuntimeHarness(tests);
    TestSceneRecommendedProfiles(tests);
    TestBuiltInteractiveCrossProduct(tests);
    TestViewModel(tests);
    float maxAbsError = 0.0f;
    TestCaptureBundle(tests, maxAbsError);
    const bool capturePreviewChecksPassed = RunCapturePreviewTests(std::cerr);
    const bool controllerChecksPassed = RunShowcaseControllerTests(std::cerr);
    const bool glfwActionAdapterChecksPassed = RunGlfwActionAdapterTests(std::cerr);
    const bool profilerChecksPassed = RunDebugProfilerModelTests(std::cerr);
    const bool wave2TelemetryChecksPassed = RunWave2TelemetryAdapterTests(std::cerr);
    const bool wave4TelemetryChecksPassed =
        RunWave4TelemetryAdapterTests(std::cerr) == 0;
    const bool manyLightsWave4ChecksPassed = RunManyLightsWave4Tests(std::cerr);
    const bool imguiChecksPassed = RunImGuiShowcasePanelsTests(std::cerr);
    const bool evidenceChecksPassed = RunShowcaseEvidenceTests(std::cerr);
    const bool programChecksPassed = RunShowcaseProgramTests(std::cerr);
    const bool reportChecksPassed = RunShowcaseReportTests(std::cerr);
    const bool workflowChecksPassed = RunShowcaseWorkflowTests(std::cerr);
    const bool allWaveChecksPassed = capturePreviewChecksPassed
        && controllerChecksPassed
        && glfwActionAdapterChecksPassed
        && profilerChecksPassed
        && wave2TelemetryChecksPassed
        && wave4TelemetryChecksPassed
        && manyLightsWave4ChecksPassed
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
