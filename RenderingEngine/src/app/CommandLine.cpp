#include "app/CommandLine.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace RenderingEngine
{
    namespace
    {
        [[nodiscard]] std::string_view RequireValue(
            int& index,
            int argumentCount,
            char** arguments,
            std::string_view option)
        {
            if (index + 1 >= argumentCount)
            {
                throw CommandLineError(std::string(option) + " requires a value.");
            }
            if (arguments[index + 1] == nullptr)
            {
                throw CommandLineError(std::string(option) + " value may not be null.");
            }
            ++index;
            return arguments[index];
        }

        template <typename Integer>
        [[nodiscard]] Integer ParseInteger(std::string_view text, std::string_view option)
        {
            Integer value{};
            const char* const begin = text.data();
            const char* const end = begin + text.size();
            const std::from_chars_result result = std::from_chars(begin, end, value, 10);
            if (result.ec != std::errc{} || result.ptr != end)
            {
                throw CommandLineError(std::string(option) + " requires a decimal integer.");
            }
            return value;
        }

        [[nodiscard]] float ParseFloat(std::string_view text, std::string_view option)
        {
            float value = 0.0f;
            const char* const begin = text.data();
            const char* const end = begin + text.size();
            const std::from_chars_result result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
            {
                throw CommandLineError(std::string(option) + " requires a finite number.");
            }
            return value;
        }

        [[nodiscard]] RuntimeToggle ParseToggle(std::string_view text, std::string_view option)
        {
            if (text == "renderer-default")
            {
                return RuntimeToggle::RendererDefault;
            }
            if (text == "on")
            {
                return RuntimeToggle::Enabled;
            }
            if (text == "off")
            {
                return RuntimeToggle::Disabled;
            }
            throw CommandLineError(
                std::string(option) + " requires renderer-default, on, or off.");
        }

        [[nodiscard]] ScenePreset ParseScene(std::string_view text)
        {
            if (text == "baseline" || text == "baseline-gallery") return ScenePreset::BaselineGallery;
            if (text == "intersection-bvh" || text == "intersection-bvh-lab") return ScenePreset::IntersectionBvhLab;
            if (text == "whitted-optics" || text == "whitted-optics-room") return ScenePreset::WhittedOpticsRoom;
            if (text == "cornell" || text == "cornell-box") return ScenePreset::CornellBox;
            if (text == "ggx-mis" || text == "ggx-mis-material-lab") return ScenePreset::GgxMisMaterialLab;
            if (text == "environment-dome" || text == "environment-sampling-dome") return ScenePreset::EnvironmentSamplingDome;
            if (text == "sponza" || text == "sponza-traversal-hall") return ScenePreset::SponzaTraversalHall;
            if (text == "backend-parity" || text == "backend-parity-benchmark") return ScenePreset::BackendParityBenchmark;
            if (text == "temporal-stability" || text == "temporal-stability-corridor") return ScenePreset::TemporalStabilityCorridor;
            if (text == "many-lights" || text == "many-lights-restir-arena") return ScenePreset::ManyLightsRestirArena;
            throw CommandLineError("Unknown scene: " + std::string(text) + '.');
        }

        [[nodiscard]] TraversalBackend ParseBackend(std::string_view text)
        {
            if (text == "canonical-linear-gpu") return TraversalBackend::CanonicalLinearGpu;
            if (text == "cpu-brute-force") return TraversalBackend::CpuBruteForce;
            if (text == "cpu-sah" || text == "cpu-sah-bvh") return TraversalBackend::CpuSahBvh;
            if (text == "gpu-flattened-sah") return TraversalBackend::GpuFlattenedSahBvh;
            if (text == "gpu-lbvh") return TraversalBackend::GpuLbvh;
            if (text == "ray-query" || text == "vulkan-ray-query") return TraversalBackend::VulkanRayQuery;
            if (text == "rt-pipeline" || text == "vulkan-rt-pipeline") return TraversalBackend::VulkanRayTracingPipeline;
            throw CommandLineError("Unknown backend: " + std::string(text) + '.');
        }

        [[nodiscard]] TransportModel ParseTransportModel(std::string_view text)
        {
            if (text == "pbr") return TransportModel::Pbr;
            if (text == "whitted") return TransportModel::Whitted;
            throw CommandLineError(
                "Unknown transport model: " + std::string(text)
                + " (expected pbr or whitted).");
        }

        [[nodiscard]] ExecutionArchitecture ParseExecutionArchitecture(
            std::string_view text)
        {
            if (text == "staged") return ExecutionArchitecture::Staged;
            if (text == "cpu-reference") return ExecutionArchitecture::CpuReference;
            if (text == "megakernel") return ExecutionArchitecture::Megakernel;
            if (text == "wavefront") return ExecutionArchitecture::Wavefront;
            throw CommandLineError(
                "Unknown execution architecture: " + std::string(text)
                + " (expected staged, cpu-reference, megakernel, or wavefront).");
        }

        [[nodiscard]] DirectLightingEstimator ParseDirectLighting(std::string_view text)
        {
            if (text == "bsdf-only") return DirectLightingEstimator::BsdfOnly;
            if (text == "nee") return DirectLightingEstimator::NextEventEstimation;
            if (text == "mis") return DirectLightingEstimator::MultipleImportanceSampling;
            if (text == "restir-di") return DirectLightingEstimator::RestirDirectIllumination;
            throw CommandLineError("Unknown direct-lighting estimator: " + std::string(text) + '.');
        }

        [[nodiscard]] LightSelectionStrategy ParseLightSelection(
            std::string_view text)
        {
            if (text == "uniform") return LightSelectionStrategy::Uniform;
            if (text == "power") return LightSelectionStrategy::PowerWeighted;
            throw CommandLineError(
                "Unknown light-selection strategy: " + std::string(text) + '.');
        }

        [[nodiscard]] EnvironmentDirectionSampler ParseEnvironmentSampler(
            std::string_view text)
        {
            if (text == "uniform-sphere")
            {
                return EnvironmentDirectionSampler::UniformSphere;
            }
            if (text == "importance-map")
            {
                return EnvironmentDirectionSampler::ImportanceMap;
            }
            throw CommandLineError(
                "Unknown environment direction sampler: " + std::string(text) + '.');
        }

        [[nodiscard]] ReconstructionMode ParseReconstruction(std::string_view text)
        {
            if (text == "progressive-mean") return ReconstructionMode::ProgressiveMean;
            if (text == "current-frame") return ReconstructionMode::CurrentFrame;
            if (text == "temporal") return ReconstructionMode::TemporalAccumulation;
            if (text == "atrous-spatial") return ReconstructionMode::SpatialFixedAtrous;
            if (text == "svgf") return ReconstructionMode::Svgf;
            throw CommandLineError("Unknown reconstruction mode: " + std::string(text) + '.');
        }

        [[nodiscard]] ShadowMethod ParseShadow(std::string_view text)
        {
            if (text == "pcf") return ShadowMethod::Pcf;
            if (text == "pcss") return ShadowMethod::Pcss;
            if (text == "physical") return ShadowMethod::Physical;
            throw CommandLineError(
                "Unknown shadow method: " + std::string(text) + " (expected physical, pcf, or pcss).");
        }

        [[nodiscard]] DebugView ParseDebugView(std::string_view text)
        {
            if (text == "final") return DebugView::Final;
            if (text == "base-color") return DebugView::BaseColor;
            if (text == "normal") return DebugView::Normal;
            if (text == "roughness") return DebugView::Roughness;
            if (text == "metallic") return DebugView::Metallic;
            if (text == "emissive") return DebugView::Emissive;
            if (text == "motion") return DebugView::Motion;
            if (text == "history-length") return DebugView::HistoryLength;
            if (text == "moments") return DebugView::Moments;
            if (text == "variance") return DebugView::Variance;
            if (text == "temporal-acceptance") return DebugView::TemporalAcceptance;
            if (text == "temporal-reject-reasons") return DebugView::TemporalRejectReasons;
            if (text == "reservoir-m") return DebugView::ReservoirM;
            if (text == "reservoir-weight") return DebugView::ReservoirWeight;
            if (text == "reservoir-light-id") return DebugView::ReservoirLightId;
            if (text == "reservoir-source") return DebugView::ReservoirSource;
            if (text == "reservoir-reuse") return DebugView::ReservoirReuse;
            if (text == "reservoir-rejection") return DebugView::ReservoirRejection;
            if (text == "winner-visibility") return DebugView::WinnerVisibility;
            throw CommandLineError("Unknown debug view: " + std::string(text) + '.');
        }

        [[nodiscard]] ManyLightsTier ParseManyLightsTier(std::string_view text)
        {
            if (text == "100") return ManyLightsTier::Lights100;
            if (text == "1000" || text == "1k") return ManyLightsTier::Lights1000;
            if (text == "10000" || text == "10k") return ManyLightsTier::Lights10000;
            throw CommandLineError("--many-lights-tier requires 100, 1000, or 10000.");
        }

        [[nodiscard]] RestirReuseStage ParseRestirStage(std::string_view text)
        {
            if (text == "initial") return RestirReuseStage::Initial;
            if (text == "spatial") return RestirReuseStage::Spatial;
            if (text == "temporal") return RestirReuseStage::Temporal;
            if (text == "spatial" || text == "temporal-spatial")
                return RestirReuseStage::TemporalSpatial;
            throw CommandLineError(
                "--restir-stage requires initial, temporal, spatial, or temporal-spatial.");
        }

        [[nodiscard]] RestirBiasMode ParseRestirBias(std::string_view text)
        {
            if (text == "biased" || text == "explicitly-biased")
                return RestirBiasMode::ExplicitlyBiased;
            if (text == "reference-correction")
                return RestirBiasMode::ReferenceCorrection;
            throw CommandLineError(
                "--restir-bias requires explicitly-biased or reference-correction.");
        }

        void ParseResolution(std::string_view text, RuntimeConfig& config)
        {
            const std::size_t separator = text.find_first_of("xX");
            if (separator == std::string_view::npos || separator == 0 || separator + 1 >= text.size())
            {
                throw CommandLineError("--resolution requires WIDTHxHEIGHT.");
            }
            const std::uint32_t width = ParseInteger<std::uint32_t>(text.substr(0, separator), "--resolution");
            const std::uint32_t height = ParseInteger<std::uint32_t>(text.substr(separator + 1), "--resolution");
            if (width < 64 || width > 16384 || height < 64 || height > 16384)
            {
                throw CommandLineError("--resolution must be between 64x64 and 16384x16384.");
            }
            config.render.width = width;
            config.render.height = height;
        }
    }

    CommandLineParseResult ParseCommandLine(int argumentCount, char** arguments)
    {
        CommandLineParseResult result;
        bool artifactRootWasExplicitlySet = false;
        if (argumentCount < 0 || (argumentCount > 0 && arguments == nullptr))
        {
            throw CommandLineError("Invalid process argument array.");
        }

        for (int index = 1; index < argumentCount; ++index)
        {
            if (arguments[index] == nullptr)
            {
                throw CommandLineError("Command-line argument may not be null.");
            }
            const std::string_view argument = arguments[index];

            if (argument == "--help")
            {
                result.action = CommandLineAction::ShowHelp;
                return result;
            }
            if (argument == "--version")
            {
                result.action = CommandLineAction::ShowVersion;
                return result;
            }
            if (argument == "--integration-status")
            {
                result.action = CommandLineAction::ShowIntegrationStatus;
                return result;
            }
            if (argument == "--scene")
            {
                result.config.scene = ParseScene(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--scene-variant")
            {
                const std::string_view value = RequireValue(index, argumentCount, arguments, argument);
                if (value.empty()) throw CommandLineError("--scene-variant requires a non-empty scene-local ID.");
                result.config.sceneVariant = value;
                continue;
            }
            if (argument == "--backend")
            {
                result.config.backend = ParseBackend(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--transport")
            {
                result.config.transportModel = ParseTransportModel(
                    RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--execution")
            {
                result.config.executionArchitecture = ParseExecutionArchitecture(
                    RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--direct-lighting")
            {
                result.config.directLightingEstimator =
                    ParseDirectLighting(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--light-selection")
            {
                result.config.lightSelection = ParseLightSelection(
                    RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--environment-sampler")
            {
                result.config.environmentSampler = ParseEnvironmentSampler(
                    RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--reconstruction")
            {
                result.config.reconstruction =
                    ParseReconstruction(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--many-lights-tier")
            {
                result.config.restir.manyLightsTier = ParseManyLightsTier(
                    RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--restir-stage")
            {
                result.config.restir.reuseStage = ParseRestirStage(
                    RequireValue(index, argumentCount, arguments, argument));
                if (!UsesRestirSpatialReuse(result.config.restir.reuseStage))
                    result.config.restir.spatialNeighbors = 0u;
                continue;
            }
            if (argument == "--restir-bias")
            {
                result.config.restir.biasMode = ParseRestirBias(
                    RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--restir-candidates"
                || argument == "--comparison-candidate-budget")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (value == 0u || value > 64u)
                {
                    throw CommandLineError(
                        std::string(argument) + " must be from 1 to 64.");
                }
                if (argument == "--restir-candidates")
                    result.config.restir.initialCandidatesPerPixel = value;
                else
                    result.config.restir.comparisonCandidateBudgetPerPixel = value;
                continue;
            }
            if (argument == "--restir-neighbors")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (value > 30u)
                    throw CommandLineError("--restir-neighbors must be from 0 to 30.");
                result.config.restir.spatialNeighbors = value;
                continue;
            }
            if (argument == "--restir-max-m" || argument == "--restir-history-age")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (value == 0u || value > 4096u)
                    throw CommandLineError(
                        std::string(argument) + " must be from 1 to 4096.");
                if (argument == "--restir-max-m")
                    result.config.restir.maximumReservoirM = value;
                else
                    result.config.restir.maximumHistoryAge = value;
                continue;
            }
            if (argument == "--comparison-visibility-budget")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (value == 0u || value > 64u)
                    throw CommandLineError(
                        "--comparison-visibility-budget must be from 1 to 64.");
                result.config.restir.comparisonVisibilityBudgetPerPixel = value;
                continue;
            }
            if (argument == "--animate-many-lights")
            {
                const RuntimeToggle toggle = ParseToggle(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (toggle == RuntimeToggle::RendererDefault)
                    throw CommandLineError("--animate-many-lights requires on or off.");
                result.config.restir.animateLights = toggle == RuntimeToggle::Enabled;
                continue;
            }
            if (argument == "--animate-rigid-occluders")
            {
                const RuntimeToggle toggle = ParseToggle(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (toggle == RuntimeToggle::RendererDefault)
                    throw CommandLineError("--animate-rigid-occluders requires on or off.");
                result.config.restir.animateRigidOccluders =
                    toggle == RuntimeToggle::Enabled;
                continue;
            }
            if (argument == "--frames")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                result.config.run.frameLimit = value;
                continue;
            }
            if (argument == "--resize-test")
            {
                result.config.run.resizeTest = true;
                continue;
            }
            if (argument == "--max-depth" || argument == "--max-bounce")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (value < 1 || value > 12)
                {
                    throw CommandLineError(std::string(argument) + " must be from 1 to 12.");
                }
                result.config.render.maximumBounce = value;
                continue;
            }
            if (argument == "--exposure")
            {
                const float value = ParseFloat(RequireValue(index, argumentCount, arguments, argument), argument);
                if (value < 0.01f || value > 64.0f)
                {
                    throw CommandLineError("--exposure must be from 0.01 to 64.");
                }
                result.config.render.exposure = value;
                continue;
            }
            if (argument == "--shadow")
            {
                result.config.shadowMethod = ParseShadow(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--debug-view")
            {
                result.config.debugView = ParseDebugView(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--resolution")
            {
                ParseResolution(RequireValue(index, argumentCount, arguments, argument), result.config);
                continue;
            }
            if (argument == "--render-scale")
            {
                const float value = ParseFloat(RequireValue(index, argumentCount, arguments, argument), argument);
                if (value < 0.0625f || value > 2.0f)
                {
                    throw CommandLineError("--render-scale must be from 0.0625 to 2.");
                }
                result.config.render.renderScale = value;
                continue;
            }
            if (argument == "--spp-per-frame")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                if (value == 0)
                {
                    throw CommandLineError("--spp-per-frame requires a positive integer.");
                }
                result.config.render.samplesPerFrame = value;
                continue;
            }
            if (argument == "--spp")
            {
                const std::uint32_t value = ParseInteger<std::uint32_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                result.config.render.targetSamplesPerPixel = value;
                continue;
            }
            if (argument == "--seed")
            {
                result.config.render.baseSeed = ParseInteger<std::uint64_t>(
                    RequireValue(index, argumentCount, arguments, argument), argument);
                continue;
            }
            if (argument == "--fov")
            {
                const float value = ParseFloat(RequireValue(index, argumentCount, arguments, argument), argument);
                if (value < 25.0f || value > 80.0f)
                {
                    throw CommandLineError("--fov must be from 25 to 80 degrees.");
                }
                result.config.render.verticalFovDegrees = value;
                continue;
            }
            if (argument == "--vsync")
            {
                result.config.render.vsync = ParseToggle(RequireValue(index, argumentCount, arguments, argument), argument);
                continue;
            }
            if (argument == "--validation")
            {
                result.config.run.validation = ParseToggle(RequireValue(index, argumentCount, arguments, argument), argument);
                continue;
            }
            if (argument == "--headless")
            {
                result.config.run.headless = true;
                continue;
            }
            if (argument == "--capture")
            {
                const std::string_view value = RequireValue(index, argumentCount, arguments, argument);
                if (value.empty())
                {
                    throw CommandLineError("--capture directory may not be empty.");
                }
                result.config.run.captureDirectory = std::string(value);
                continue;
            }
            if (argument == "--benchmark")
            {
                const std::string_view value = RequireValue(index, argumentCount, arguments, argument);
                if (value.empty())
                {
                    throw CommandLineError("--benchmark preset may not be empty.");
                }
                result.config.run.benchmarkPreset = value;
                continue;
            }
            if (argument == "--reference")
            {
                const std::string_view value = RequireValue(index, argumentCount, arguments, argument);
                if (value.empty())
                {
                    throw CommandLineError("--reference image path may not be empty.");
                }
                result.config.run.referenceImage = std::string(value);
                continue;
            }
            if (argument == "--artifact-root")
            {
                const std::string_view value = RequireValue(index, argumentCount, arguments, argument);
                if (value.empty())
                {
                    throw CommandLineError("--artifact-root may not be empty.");
                }
                result.config.run.artifactRoot = std::string(value);
                artifactRootWasExplicitlySet = true;
                continue;
            }
            if (argument == "--run-id")
            {
                const std::string_view value = RequireValue(index, argumentCount, arguments, argument);
                if (value.empty())
                {
                    throw CommandLineError("--run-id may not be empty.");
                }
                result.config.run.runIdentifier = value;
                continue;
            }

            throw CommandLineError("Unknown command-line argument: " + std::string(argument));
        }

        // A startup stop condition must be attainable. During interactive axis
        // changes the stored film target is retained but inactive outside Final
        // Progressive Mean; it is not an algorithm capability restriction.
        if (result.config.render.targetSamplesPerPixel > 0
            && (result.config.debugView != DebugView::Final
                || result.config.reconstruction != ReconstructionMode::ProgressiveMean))
        {
            throw CommandLineError(
                "--spp requires progressive-mean and the final debug view; use --frames for other outputs.");
        }
        if (result.config.run.captureDirectory.has_value())
        {
            const std::filesystem::path captureRoot =
                result.config.run.captureDirectory->lexically_normal();
            const std::filesystem::path configuredRoot =
                result.config.run.artifactRoot.lexically_normal();
            if (artifactRootWasExplicitlySet && configuredRoot != captureRoot)
            {
                throw CommandLineError(
                    "--capture DIRECTORY and --artifact-root PATH must resolve to the same lexical path.");
            }
            result.config.run.captureDirectory = captureRoot;
            result.config.run.artifactRoot = captureRoot;
        }
        return result;
    }

    std::string_view CommandLineHelpText() noexcept
    {
        return
            "Vulkan RT Wave 5 Debug Showcase\n"
            "RuntimeConfig v2 options:\n"
            "  --frames N                 Render N frames and exit; 0 runs until exit.\n"
            "  --transport MODE           pbr (default) or whitted.\n"
            "  --execution MODE           staged (default), cpu-reference, megakernel, or wavefront.\n"
            "  --max-depth N              Trace depth from 1 to 12 (default 8).\n"
            "  --exposure X               Linear exposure from 0.01 to 64.\n"
            "  --shadow MODE              physical, pcf, or pcss.\n"
            "  --debug-view VIEW          final, material, motion/history/variance, reservoir views.\n"
            "  --resize-test              Exercise swapchain recreation in a finite run.\n"
            "Independent algorithm axes:\n"
            "  --scene NAME               baseline, intersection-bvh, whitted-optics, cornell, ...\n"
            "  --scene-variant ID         Select an existing scene-local experiment; unknown IDs are rejected.\n"
            "  --backend NAME             canonical-linear-gpu, cpu-sah, ray-query, ...\n"
            "  --direct-lighting MODE     bsdf-only, nee, mis, or restir-di.\n"
            "  --light-selection MODE     uniform or power.\n"
            "  --environment-sampler MODE uniform-sphere or importance-map.\n"
            "  --reconstruction MODE      current-frame, progressive-mean, temporal, atrous-spatial, svgf.\n"
            "ReSTIR / Many Lights settings:\n"
            "  --many-lights-tier N       100, 1000, or 10000 lights.\n"
            "  --restir-stage MODE        initial, temporal, spatial, or temporal-spatial.\n"
            "  --restir-bias MODE         explicitly-biased or reference-correction.\n"
            "  --restir-candidates N      Initial candidates per pixel, 1..64.\n"
            "  --restir-neighbors N       Spatial neighbors per pixel, 0..30.\n"
            "  --restir-max-m N           Reservoir M clamp, 1..4096.\n"
            "  --restir-history-age N     Maximum retained history age, 1..4096.\n"
            "  --comparison-candidate-budget N   Equal-budget comparison candidates.\n"
            "  --comparison-visibility-budget N  Equal-budget comparison visibility rays.\n"
            "  --animate-many-lights STATE       Select animated/static initial light layout.\n"
            "  --animate-rigid-occluders STATE   Select animated/static initial occluder layout.\n"
            "  --resolution WxH           Initial client resolution.\n"
            "  --render-scale X           Internal render scale.\n"
            "  --spp-per-frame N          Samples per rendered frame.\n"
            "  --spp N                    Target samples per pixel; 0 means no target.\n"
            "  --max-bounce N             Canonical alias for --max-depth.\n"
            "  --seed N                   Base random seed.\n"
            "  --fov X                    Initial vertical FOV in degrees.\n"
            "  --vsync STATE              renderer-default, on, or off.\n"
            "  --validation STATE         renderer-default, on, or off.\n"
            "  --artifact-root PATH       Artifact planning root (default .artifacts).\n"
            "  --run-id ID                Artifact run directory name (default manual).\n"
            "  --headless                  Use the supported CPU-reference headless path.\n"
            "  --capture DIRECTORY         Capture live linear EXR, PNG preview, and metadata.\n"
            "                              DIRECTORY becomes the artifact root.\n"
            "  --benchmark PRESET          Request a benchmark preset (not yet attached).\n"
            "  --reference IMAGE.exr       Request comparison (not yet attached).\n"
            "  --help | --version         Print information without creating a window.\n"
            "  --integration-status       List central-build and production-runtime boundaries.\n"
            "Known roadmap modes that are not built return exit code 4 without fallback.\n"
            "Wave 5 Debug controls:\n"
            "  W/A/S/D move, Q/E descend/ascend, Left Shift fast, Left Ctrl fine.\n"
            "  Mouse look; wheel changes speed; Alt+wheel changes field of view.\n"
            "  Tab captures/releases the cursor; Esc releases only; Alt+F4 exits.\n"
            "  Home fixed camera; P pause; O single-step; K locks camera/base seed/origin.\n"
            "  B backend, I transport, Ctrl+I execution, L direct-lighting; Shift reverses.\n"
            "  Ctrl+L light selection, Alt+L environment sampler, Ctrl+Alt+L shadow.\n"
            "  N reconstruction, V debug view.\n"
            "  0-9 select scenes only; unsupported scenes keep the current scene and report why.\n"
            "  R reset; [/] bounce; -/= exposure; PageDown/PageUp render scale.\n"
            "  F1 help, F2 algorithm, F3 profiler, F4 capture, F5 shader reload,\n"
            "  F6 fixed-seed A/B, F7 legend, F8 Benchmark entry, F9 reference.\n"
            "  F10 current/Presentation study card; F11 restore Presentation; F12 cycles Diagnosis variants.\n"
            "Unsupported actions never mutate the current tuple and show an explicit reason.\n";
    }

    std::string_view RuntimeVersionText() noexcept
    {
        return "RenderingEngine runtime-config-v2 abi-v3";
    }
}
