#include "app/ArtifactLayout.hpp"
#include "app/CapabilityTable.hpp"
#include "app/CommandLine.hpp"
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

    TestContext tests;

    const CommandLineParseResult zeroLimits = Parse({ "--frames", "0", "--spp", "0" });
    tests.Expect(zeroLimits.config.run.frameLimit == 0, "--frames 0 must preserve interactive execution");
    tests.Expect(zeroLimits.config.render.targetSamplesPerPixel == 0, "--spp 0 must preserve no-target accumulation");
    tests.Expect(CapabilityTable::Evaluate(zeroLimits.config).IsSupported(), "default zero frame/SPP limits must remain supported");
    tests.Expect(zeroLimits.config.integrator == Integrator::Pbr,
        "omitting --integrator must select the PBR delivery path");

    const RuntimeConfig defaultConfig;
    tests.Expect(defaultConfig.integrator == Integrator::Pbr,
        "RuntimeConfig must default to the PBR integrator");
    const RunOptions defaultLegacyOptions;
    tests.Expect(defaultLegacyOptions.integrator == Integrator::Pbr,
        "legacy renderer projection storage must default to PBR");
    tests.Expect(CommandLineHelpText().find("pbr (default)") != std::string_view::npos,
        "help text must advertise the same PBR default as RuntimeConfig");

    const CommandLineParseResult aliases = Parse({
        "--max-depth", "5",
        "--light-sampler", "nee",
        "--resolution", "640x360",
        "--seed", "123456789"
    });
    tests.Expect(aliases.config.render.maximumBounce == 5, "--max-depth must alias --max-bounce");
    tests.Expect(aliases.config.directLightingEstimator == DirectLightingEstimator::NextEventEstimation,
        "--light-sampler must alias --direct-lighting");
    tests.Expect(aliases.config.render.width == 640 && aliases.config.render.height == 360,
        "--resolution must populate both dimensions");
    tests.Expect(aliases.config.render.baseSeed == 123456789ull, "--seed must preserve all parsed bits");
    tests.Expect(CapabilityTable::Evaluate(aliases.config).status == CapabilityStatus::Unsupported,
        "declared NEE mode must reject explicitly instead of falling back");

    const CommandLineParseResult duplicateOptions = Parse({
        "--max-depth", "3",
        "--max-bounce", "7",
        "--light-sampler", "nee",
        "--direct-lighting", "legacy-analytic-direct"
    });
    tests.Expect(duplicateOptions.config.render.maximumBounce == 7,
        "duplicate scalar aliases must use the final occurrence");
    tests.Expect(
        duplicateOptions.config.directLightingEstimator == DirectLightingEstimator::LegacyAnalyticDirect,
        "duplicate enum aliases must use the final occurrence");

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
    tests.Expect(CapabilityTable::Evaluate(debugTermination).status == CapabilityStatus::InvalidConfiguration,
        "target SPP with a non-final debug view must be invalid");

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

    RuntimeConfig canonical;
    canonical.render.width = 1920;
    canonical.render.height = 1080;
    canonical.render.exposure = 1.75f;
    canonical.render.maximumBounce = 11;
    canonical.render.targetSamplesPerPixel = 64;
    canonical.render.baseSeed = 0x12345678abcdef00ull;
    canonical.render.verticalFovDegrees = 67.0f;
    canonical.render.vsync = RuntimeToggle::Disabled;
    canonical.run.frameLimit = 17;
    canonical.run.resizeTest = true;
    canonical.run.validation = RuntimeToggle::Enabled;
    canonical.integrator = Integrator::Pbr;
    canonical.shadowMethod = ShadowMethod::Pcss;
    canonical.debugView = DebugView::Metallic;
    const RunOptions legacy = MakeLegacyRunOptions(canonical);
    tests.Expect(legacy.initialWidth == canonical.render.width && legacy.initialHeight == canonical.render.height,
        "legacy projection must preserve the initial extent");
    tests.Expect(legacy.frameLimit == canonical.run.frameLimit && legacy.resizeTest == canonical.run.resizeTest,
        "legacy projection must preserve run termination controls");
    tests.Expect(legacy.exposure == canonical.render.exposure
        && legacy.maximumTraceDepth == canonical.render.maximumBounce
        && legacy.targetSamplesPerPixel == canonical.render.targetSamplesPerPixel,
        "legacy projection must preserve sampling and exposure controls");
    tests.Expect(legacy.baseSeed == canonical.render.baseSeed
        && legacy.verticalFovDegrees == canonical.render.verticalFovDegrees,
        "legacy projection must preserve deterministic camera and seed controls");
    tests.Expect(legacy.vsync == canonical.render.vsync && legacy.validation == canonical.run.validation,
        "legacy projection must preserve runtime toggles");
    tests.Expect(legacy.integrator == canonical.integrator
        && legacy.shadowMethod == canonical.shadowMethod
        && legacy.debugView == canonical.debugView,
        "legacy projection must preserve renderer modes without fallback");

    tests.Expect(Parse({ "--help" }).action == CommandLineAction::ShowHelp, "--help must short-circuit to an information action");
    tests.Expect(Parse({ "--version" }).action == CommandLineAction::ShowVersion, "--version must short-circuit to an information action");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--spp-per-frame", "0" }); },
        "--spp-per-frame 0 must fail parsing");
    tests.ExpectCommandLineError(
        [] { (void)Parse({ "--not-a-real-option" }); },
        "unknown options must fail parsing");

    if (tests.Passed())
    {
        std::cout << "Runtime config, CLI, capability, artifact, and legacy projection checks passed.\n";
    }
    return tests.Passed();
}
