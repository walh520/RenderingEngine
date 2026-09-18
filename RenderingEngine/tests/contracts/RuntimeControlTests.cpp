#include "app/ArtifactLayout.hpp"
#include "app/CapabilityTable.hpp"
#include "app/CommandLine.hpp"
#include "app/IntegratedModuleRegistry.hpp"
#include "app/RuntimeConfig.hpp"

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class TestContext final
    {
    public:
        void Expect(bool condition, std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "Runtime control test failed: " << message << '\n';
                ++failureCount_;
            }
        }

        template <typename Callable>
        void ExpectCommandLineError(Callable&& callable, std::string_view message)
        {
            try
            {
                callable();
                Expect(false, message);
            }
            catch (const RenderingEngine::CommandLineError&)
            {
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failureCount_ == 0;
        }

    private:
        int failureCount_ = 0;
    };

    [[nodiscard]] RenderingEngine::CommandLineParseResult Parse(
        std::initializer_list<std::string_view> arguments)
    {
        std::vector<std::string> storage;
        storage.reserve(arguments.size() + 1);
        storage.emplace_back("RenderingEngine.Contracts.Tests");
        for (const std::string_view argument : arguments)
        {
            storage.emplace_back(argument);
        }

        std::vector<char*> rawArguments;
        rawArguments.reserve(storage.size());
        for (std::string& argument : storage)
        {
            rawArguments.push_back(argument.data());
        }
        return RenderingEngine::ParseCommandLine(
            static_cast<int>(rawArguments.size()),
            rawArguments.data());
    }
}

bool RunRuntimeControlTests()
{
    using namespace RenderingEngine;

    static_assert(static_cast<std::uint32_t>(ReconstructionMode::ProgressiveMean) == 0u);
    static_assert(static_cast<std::uint32_t>(ReconstructionMode::CurrentFrame) == 4u);

    TestContext tests;

    const CommandLineParseResult zeroLimits = Parse({ "--frames", "0", "--spp", "0" });
    tests.Expect(zeroLimits.config.run.frameLimit == 0, "--frames 0 must preserve interactive execution");
    tests.Expect(zeroLimits.config.render.targetSamplesPerPixel == 0, "--spp 0 must preserve no-target accumulation");
    tests.Expect(CapabilityTable::Evaluate(zeroLimits.config).IsSupported(), "default zero frame/SPP limits must remain supported");
    tests.Expect(zeroLimits.config.transportModel == TransportModel::Pbr
            && zeroLimits.config.executionArchitecture == ExecutionArchitecture::Staged,
        "omitting transport and execution options must select staged PBR");

    const RuntimeConfig defaultConfig;
    tests.Expect(defaultConfig.transportModel == TransportModel::Pbr
            && defaultConfig.executionArchitecture == ExecutionArchitecture::Staged,
        "RuntimeConfig must default to staged PBR");
    tests.Expect(defaultConfig.render.width == 1280u
            && defaultConfig.render.height == 720u,
        "the interactive runtime must default to the requested 1280x720 showcase extent");
    tests.Expect(CommandLineHelpText().find("pbr (default)") != std::string_view::npos,
        "help text must advertise the same PBR default as RuntimeConfig");
    tests.Expect(RuntimeVersionText().find("runtime-config-v2") != std::string_view::npos
            && RuntimeVersionText().find("abi-v3") != std::string_view::npos,
        "the executable version must publish the Wave 4 control and GPU contracts");

    const CommandLineParseResult wave4 = Parse({
        "--scene", "many-lights",
        "--backend", "ray-query",
        "--transport", "pbr",
        "--execution", "megakernel",
        "--direct-lighting", "restir-di",
        "--light-selection", "power",
        "--environment-sampler", "uniform-sphere",
        "--many-lights-tier", "10k",
        "--restir-stage", "temporal-spatial",
        "--restir-bias", "reference-correction",
        "--restir-candidates", "12",
        "--restir-neighbors", "7",
        "--restir-max-m", "64",
        "--restir-history-age", "24",
        "--comparison-candidate-budget", "12",
        "--comparison-visibility-budget", "1",
        "--animate-many-lights", "off",
        "--animate-rigid-occluders", "on",
        "--debug-view", "reservoir-rejection"
    });
    tests.Expect(wave4.config.scene == ScenePreset::ManyLightsRestirArena
            && wave4.config.backend == TraversalBackend::VulkanRayQuery
            && wave4.config.transportModel == TransportModel::Pbr
            && wave4.config.executionArchitecture == ExecutionArchitecture::Megakernel
            && wave4.config.directLightingEstimator
                == DirectLightingEstimator::RestirDirectIllumination
            && wave4.config.lightSelection == LightSelectionStrategy::PowerWeighted
            && wave4.config.environmentSampler
                == EnvironmentDirectionSampler::UniformSphere
            && wave4.config.restir.manyLightsTier == ManyLightsTier::Lights10000
            && wave4.config.restir.reuseStage == RestirReuseStage::TemporalSpatial
            && wave4.config.restir.biasMode == RestirBiasMode::ReferenceCorrection,
        "Wave 4 CLI tokens must map all split-axis RuntimeConfig fields independently");
    tests.Expect(wave4.config.restir.initialCandidatesPerPixel == 12u
            && wave4.config.restir.spatialNeighbors == 7u
            && wave4.config.restir.maximumReservoirM == 64u
            && wave4.config.restir.maximumHistoryAge == 24u
            && wave4.config.restir.comparisonCandidateBudgetPerPixel == 12u
            && wave4.config.restir.comparisonVisibilityBudgetPerPixel == 1u,
        "Wave 4 candidate, neighbor, M, history, and comparison budgets must round-trip");
    tests.Expect(!wave4.config.restir.animateLights
            && wave4.config.restir.animateRigidOccluders
            && wave4.config.debugView == DebugView::ReservoirRejection,
        "Wave 4 animation toggles and diagnostic view must remain explicit");
    tests.Expect(CapabilityTable::Evaluate(wave4.config).IsSupported(),
        "production reservoir diagnostic views must be available with their ReSTIR producer");

    const CommandLineParseResult wave4Debug = Parse({
        "--scene", "many-lights",
        "--backend", "ray-query",
        "--transport", "pbr",
        "--execution", "wavefront",
        "--direct-lighting", "restir-di",
        "--light-selection", "power",
        "--environment-sampler", "uniform-sphere",
        "--reconstruction", "svgf",
        "--many-lights-tier", "100",
        "--restir-stage", "temporal-spatial",
        "--restir-bias", "explicitly-biased",
        "--restir-candidates", "1",
        "--restir-neighbors", "5",
        "--animate-many-lights", "off",
        "--animate-rigid-occluders", "off",
        "--debug-view", "final",
        "--resolution", "1280x720",
        "--validation", "on"
    });
    tests.Expect(CapabilityTable::Evaluate(wave4Debug.config).IsSupported()
            && !wave4Debug.config.restir.animateLights
            && !wave4Debug.config.restir.animateRigidOccluders,
        "the bounded 100-light Ray Query + Wavefront + ReSTIR + SVGF debug tuple must be supported");

    const CommandLineParseResult cpuReference = Parse({
        "--scene", "cornell",
        "--backend", "cpu-sah",
        "--transport", "pbr",
        "--execution", "cpu-reference",
        "--direct-lighting", "mis",
        "--light-selection", "uniform",
        "--environment-sampler", "uniform-sphere",
        "--reconstruction", "progressive-mean",
        "--headless",
        "--spp", "1"
    });
    tests.Expect(cpuReference.config.transportModel == TransportModel::Pbr
            && cpuReference.config.executionArchitecture
                == ExecutionArchitecture::CpuReference
            && cpuReference.config.lightSelection == LightSelectionStrategy::Uniform
            && cpuReference.config.environmentSampler
                == EnvironmentDirectionSampler::UniformSphere
            && CapabilityTable::Evaluate(cpuReference.config).IsSupported(),
        "the CPU-reference execution axis must parse independently and accept its canonical headless Cornell tuple");

    RuntimeConfig wave4Rejected = wave4Debug.config;
    wave4Rejected.reconstruction = ReconstructionMode::ProgressiveMean;
    tests.Expect(CapabilityTable::Evaluate(wave4Rejected).IsSupported(),
        "ReSTIR direct lighting must compose into the shared progressive-mean output path");
    wave4Rejected = wave4Debug.config;
    wave4Rejected.restir.manyLightsTier = ManyLightsTier::Lights1000;
    tests.Expect(CapabilityTable::Evaluate(wave4Rejected).IsSupported(),
        "the mixed runtime must accept the built 1k/10k Many Lights tiers");
    wave4Rejected = wave4Debug.config;
    wave4Rejected.debugView = DebugView::ReservoirM;
    tests.Expect(CapabilityTable::Evaluate(wave4Rejected).IsSupported(),
        "reservoir views must consume the production ReSTIR debug output");
    wave4Rejected.directLightingEstimator = DirectLightingEstimator::MultipleImportanceSampling;
    tests.Expect(CapabilityTable::Evaluate(wave4Rejected).status == CapabilityStatus::Unsupported,
        "reservoir views must reject an absent ReSTIR producer without fallback");

    RuntimeConfig showcaseScene = Parse({
        "--scene", "environment-dome",
        "--backend", "ray-query",
        "--transport", "pbr",
        "--execution", "megakernel",
        "--direct-lighting", "mis",
        "--light-selection", "uniform",
        "--environment-sampler", "importance-map",
        "--reconstruction", "progressive-mean"
    }).config;
    tests.Expect(showcaseScene.scene == ScenePreset::EnvironmentSamplingDome
            && showcaseScene.backend == TraversalBackend::VulkanRayQuery
            && showcaseScene.transportModel == TransportModel::Pbr
            && showcaseScene.executionArchitecture == ExecutionArchitecture::Megakernel
            && showcaseScene.directLightingEstimator
                == DirectLightingEstimator::MultipleImportanceSampling
            && showcaseScene.lightSelection == LightSelectionStrategy::Uniform
            && showcaseScene.environmentSampler == EnvironmentDirectionSampler::ImportanceMap
            && showcaseScene.reconstruction == ReconstructionMode::ProgressiveMean,
        "CLI v2 must preserve each independently selected runtime axis");
    const CommandLineParseResult currentFrame = Parse({
        "--reconstruction", "current-frame"
    });
    const CommandLineParseResult progressiveMean = Parse({
        "--reconstruction", "progressive-mean"
    });
    tests.Expect(currentFrame.config.reconstruction == ReconstructionMode::CurrentFrame
            && progressiveMean.config.reconstruction == ReconstructionMode::ProgressiveMean
            && currentFrame.config.reconstruction != progressiveMean.config.reconstruction,
        "current-frame and progressive-mean CLI tokens must remain legal and distinct");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--reconstruction", "raw" }); },
        "the removed raw reconstruction token must be rejected without an alias");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--restir-bias", "unbiased-reference" }); },
        "unproven unbiased-reference token must be removed without an alias");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--restir-bias", "unbiased" }); },
        "unproven unbiased shorthand must be removed without an alias");
    const CommandLineParseResult progressiveMeanSpp = Parse({
        "--reconstruction", "progressive-mean", "--spp", "1"
    });
    tests.Expect(CapabilityTable::Evaluate(progressiveMeanSpp.config).IsSupported(),
        "GPU Progressive Mean with a non-zero --spp target must remain a legal terminating mode");
    for (const std::string_view reconstructionToken : {
            std::string_view{"current-frame"}, std::string_view{"temporal"},
            std::string_view{"atrous-spatial"}, std::string_view{"svgf"}})
    {
        tests.ExpectCommandLineError([&] {
            (void)Parse({ "--reconstruction", reconstructionToken, "--spp", "1" });
        }, "startup must reject an unattainable film SPP stop condition");
        const CommandLineParseResult boundedFrames = Parse({
            "--reconstruction", reconstructionToken, "--frames", "1"
        });
        tests.Expect(CapabilityTable::Evaluate(boundedFrames.config).IsSupported(),
            "Current Frame and temporal GPU modes must use bounded --frames termination");
        RuntimeConfig switched = boundedFrames.config;
        switched.render.targetSamplesPerPixel = 19u;
        tests.Expect(CapabilityTable::Evaluate(switched).IsSupported(),
            "interactive reconstruction changes must preserve a dormant film target");
    }
    for (const ScenePreset scene : {
            ScenePreset::EnvironmentSamplingDome,
            ScenePreset::BackendParityBenchmark,
            ScenePreset::TemporalStabilityCorridor,
            ScenePreset::ManyLightsRestirArena })
    {
        showcaseScene.scene = scene;
        tests.Expect(CapabilityTable::Evaluate(showcaseScene).IsSupported(),
            "built Wave 5 experiment spaces must accept the production Wave 2 tuple");
    }
    showcaseScene.scene = ScenePreset::SponzaTraversalHall;
    tests.Expect(CapabilityTable::Evaluate(showcaseScene).status
            == CapabilityStatus::Unsupported,
        "Sponza must fail closed without its pinned licensed production asset");
    showcaseScene.scene = ScenePreset::ManyLightsRestirArena;
    showcaseScene.restir.manyLightsTier = ManyLightsTier::Lights1000;
    tests.Expect(CapabilityTable::Evaluate(showcaseScene).IsSupported(),
        "Many Lights 1k/10k must share the production mixed-runtime path");

    const CommandLineParseResult initialOnly = Parse({
        "--restir-neighbors", "9", "--restir-stage", "initial"
    });
    tests.Expect(initialOnly.config.restir.reuseStage == RestirReuseStage::Initial
            && initialOnly.config.restir.spatialNeighbors == 0u,
        "initial-only must disable spatial reuse explicitly");
    const CommandLineParseResult selectedVariant = Parse({
        "--scene", "environment-dome", "--scene-variant", "polar-sun"
    });
    tests.Expect(selectedVariant.config.sceneVariant == "polar-sun",
        "CLI must retain the exact scene-local experiment ID");
    tests.ExpectCommandLineError([] {
        (void)Parse({ "--scene-variant", "" });
    }, "an explicitly empty variant ID must fail parsing");
    const CommandLineParseResult spatialOnly = Parse({
        "--restir-stage", "spatial", "--restir-neighbors", "5"
    });
    tests.Expect(spatialOnly.config.restir.reuseStage == RestirReuseStage::Spatial
            && spatialOnly.config.restir.spatialNeighbors == 5u
            && CapabilityTable::Evaluate(spatialOnly.config).IsSupported(),
        "spatial-only must be an independently supported stage with its neighbor budget");
    static_assert(!UsesRestirTemporalReuse(RestirReuseStage::Initial));
    static_assert(!UsesRestirSpatialReuse(RestirReuseStage::Initial));
    static_assert(UsesRestirTemporalReuse(RestirReuseStage::Temporal));
    static_assert(!UsesRestirSpatialReuse(RestirReuseStage::Temporal));
    static_assert(!UsesRestirTemporalReuse(RestirReuseStage::Spatial));
    static_assert(UsesRestirSpatialReuse(RestirReuseStage::Spatial));
    static_assert(UsesRestirTemporalReuse(RestirReuseStage::TemporalSpatial));
    static_assert(UsesRestirSpatialReuse(RestirReuseStage::TemporalSpatial));
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--many-lights-tier", "999" }); },
        "an undeclared Many Lights tier must fail parsing");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--restir-candidates", "65" }); },
        "a candidate budget above the published bound must fail parsing");

    const CommandLineParseResult canonicalOptions = Parse({
        "--max-depth", "5",
        "--direct-lighting", "nee",
        "--resolution", "640x360",
        "--seed", "123456789"
    });
    tests.Expect(canonicalOptions.config.render.maximumBounce == 5, "--max-depth must set maximum bounce");
    tests.Expect(canonicalOptions.config.directLightingEstimator == DirectLightingEstimator::NextEventEstimation,
        "--direct-lighting must set the estimator");
    tests.Expect(canonicalOptions.config.render.width == 640 && canonicalOptions.config.render.height == 360,
        "--resolution must populate both dimensions");
    tests.Expect(canonicalOptions.config.render.baseSeed == 123456789ull, "--seed must preserve all parsed bits");
    tests.Expect(CapabilityTable::Evaluate(canonicalOptions.config).IsSupported(),
        "NEE must remain a first-class mixed-runtime estimator");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--integrator", "megakernel" }); },
        "the removed mixed integrator option must be rejected");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--light-proposal", "power" }); },
        "the removed mixed light-proposal option must be rejected");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--light-sampler", "nee" }); },
        "the removed light-sampler alias must be rejected");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--backend", "legacy-analytic-gpu" }); },
        "the removed legacy analytic backend spelling must be rejected");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--direct-lighting", "legacy-analytic-direct" }); },
        "the removed legacy analytic direct-lighting spelling must be rejected");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--light-selection", "legacy-analytic" }); },
        "the removed legacy analytic light-selection spelling must be rejected");

    RuntimeConfig unsupportedBackend;
    unsupportedBackend.backend = TraversalBackend::CpuSahBvh;
    tests.Expect(CapabilityTable::Evaluate(unsupportedBackend).status == CapabilityStatus::Unsupported,
        "declared but unbuilt traversal backend must be unsupported");

    RuntimeConfig invalidSamples;
    invalidSamples.render.samplesPerFrame = 0;
    tests.Expect(CapabilityTable::Evaluate(invalidSamples).status == CapabilityStatus::InvalidConfiguration,
        "zero samples-per-frame must be invalid");

    RuntimeConfig debugTermination;
    debugTermination.render.targetSamplesPerPixel = 1;
    debugTermination.debugView = DebugView::Normal;
    tests.Expect(CapabilityTable::Evaluate(debugTermination).IsSupported(),
        "interactive debug changes must preserve a dormant film target");
    tests.ExpectCommandLineError([] {
        (void)Parse({ "--debug-view", "normal", "--spp", "1" });
    }, "startup must reject film SPP termination outside Final");

    const RuntimeConfig headlessMock = MakeHeadlessMockRuntimeConfig();
    tests.Expect(headlessMock.run.headless, "headless mock fixture must identify itself as headless");
    tests.Expect(CapabilityTable::Evaluate(headlessMock).status == CapabilityStatus::Unsupported,
        "production capability evaluation must reject the headless mock");

    const CommandLineParseResult capture = Parse({
        "--capture", "artifacts/capture-run",
        "--artifact-root", "artifacts/capture-run"
    });
    tests.Expect(capture.config.run.captureDirectory.has_value(), "capture request must retain its directory");
    tests.Expect(capture.config.run.captureDirectory == capture.config.run.artifactRoot,
        "matching capture and artifact roots must normalize to the same path");
    tests.Expect(CapabilityTable::Evaluate(capture.config).IsSupported(),
        "the Wave 1 production capture provider must accept a canonical CLI capture request");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--capture", "capture-a", "--artifact-root", "capture-b" }); },
        "conflicting capture and artifact roots must fail parsing");

    tests.Expect(NormalizeRunIdentifier("../bad id") == "_bad_id",
        "run identifiers must be reduced to the ASCII-safe contract");
    tests.Expect(NormalizeRunIdentifier("...") == "manual",
        "an empty normalized run identifier must fall back to manual");
    const ArtifactLayout layout = ResolveArtifactLayout("artifacts", "../bad id");
    const std::filesystem::path expectedRunDirectory = std::filesystem::path("artifacts") / "_bad_id";
    tests.Expect(layout.runDirectory == expectedRunDirectory, "artifact run directory must use the normalized identifier");
    tests.Expect(layout.metadataFile == expectedRunDirectory / "metadata.json", "artifact metadata path must be deterministic");

    tests.Expect(Parse({ "--help" }).action == CommandLineAction::ShowHelp, "--help must short-circuit to an information action");
    tests.Expect(Parse({ "--version" }).action == CommandLineAction::ShowVersion, "--version must short-circuit to an information action");
    tests.Expect(Parse({ "--integration-status" }).action == CommandLineAction::ShowIntegrationStatus,
        "--integration-status must short-circuit to an information action");

    const auto modules = IntegratedModules();
    tests.Expect(modules.size() == 11u, "the central integration registry must cover L0 through L10");
    tests.Expect(GetIntegratedModuleStatus(IntegratedModule::L0Foundation).IsProductionAttached(),
        "the canonical staged Vulkan runtime must remain the attached production provider");
    tests.Expect(GetIntegratedModuleStatus(IntegratedModule::L1Platform).IsProductionAttached(),
        "the Wave 1 GLFW host must be the attached production platform provider");
    tests.Expect(GetIntegratedModuleStatus(IntegratedModule::L2SceneAssets).IsProductionAttached(),
        "the canonical Wave 2 experiment scenes must remain attached to the production renderer");
    tests.Expect(GetIntegratedModuleStatus(IntegratedModule::L10Showcase).IsProductionAttached(),
        "the Wave 1 ActionMap/RuntimeConfig harness must be attached at production frame start");
    tests.Expect(GetIntegratedModuleStatus(IntegratedModule::L6Megakernel).IsProductionAttached(),
        "the Wave 2 Megakernel must remain attached while Wave 3 stays fail-closed");
    tests.Expect(ProviderOwner(ExecutionArchitecture::Wavefront) == IntegratedModule::L7Wavefront,
        "wavefront ownership must resolve to L7");
    tests.Expect(IntegrationStatusText().find("central-build means solution composition only") != std::string::npos,
        "integration status must print its evidence boundary");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--spp-per-frame", "0" }); },
        "--spp-per-frame 0 must fail parsing");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--not-a-real-option" }); },
        "unknown options must fail parsing");

    if (tests.Passed())
    {
        std::cout << "Runtime config v2, CLI, capability, and artifact checks passed.\n";
    }
    return tests.Passed();
}
