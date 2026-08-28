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
            if (text == "legacy-analytic-gpu") return TraversalBackend::LegacyAnalyticGpu;
            if (text == "cpu-brute-force") return TraversalBackend::CpuBruteForce;
            if (text == "cpu-sah" || text == "cpu-sah-bvh") return TraversalBackend::CpuSahBvh;
            if (text == "gpu-flattened-sah") return TraversalBackend::GpuFlattenedSahBvh;
            if (text == "gpu-lbvh") return TraversalBackend::GpuLbvh;
            if (text == "ray-query" || text == "vulkan-ray-query") return TraversalBackend::VulkanRayQuery;
            if (text == "rt-pipeline" || text == "vulkan-rt-pipeline") return TraversalBackend::VulkanRayTracingPipeline;
            throw CommandLineError("Unknown backend: " + std::string(text) + '.');
        }

        [[nodiscard]] Integrator ParseIntegrator(std::string_view text)
        {
            if (text == "pbr") return Integrator::Pbr;
            if (text == "whitted") return Integrator::Whitted;
            if (text == "cpu-reference" || text == "cpu-reference-path") return Integrator::CpuReferencePathTracer;
            if (text == "megakernel" || text == "gpu-megakernel-path") return Integrator::GpuMegakernelPathTracer;
            if (text == "wavefront" || text == "gpu-wavefront-path") return Integrator::GpuWavefrontPathTracer;
            throw CommandLineError(
                "Unknown integrator: " + std::string(text)
                + " (expected pbr, whitted, or a declared path-tracer token).");
        }

        [[nodiscard]] DirectLightingEstimator ParseDirectLighting(std::string_view text)
        {
            if (text == "legacy-analytic-direct") return DirectLightingEstimator::LegacyAnalyticDirect;
            if (text == "bsdf-only") return DirectLightingEstimator::BsdfOnly;
            if (text == "nee") return DirectLightingEstimator::NextEventEstimation;
            if (text == "mis") return DirectLightingEstimator::MultipleImportanceSampling;
            if (text == "restir-di") return DirectLightingEstimator::RestirDirectIllumination;
            throw CommandLineError("Unknown direct-lighting estimator: " + std::string(text) + '.');
        }

        [[nodiscard]] LightProposalDistribution ParseLightProposal(std::string_view text)
        {
            if (text == "legacy-analytic") return LightProposalDistribution::LegacyAnalyticLights;
            if (text == "uniform") return LightProposalDistribution::UniformLights;
            if (text == "power") return LightProposalDistribution::PowerWeightedLights;
            if (text == "environment") return LightProposalDistribution::EnvironmentImportance;
            throw CommandLineError("Unknown light proposal: " + std::string(text) + '.');
        }

        [[nodiscard]] ReconstructionMode ParseReconstruction(std::string_view text)
        {
            if (text == "raw") return ReconstructionMode::Raw;
            if (text == "temporal") return ReconstructionMode::TemporalAccumulation;
            if (text == "temporal-atrous") return ReconstructionMode::TemporalFixedAtrous;
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
            throw CommandLineError("Unknown debug view: " + std::string(text) + '.');
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
            if (argument == "--backend")
            {
                result.config.backend = ParseBackend(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--integrator")
            {
                result.config.integrator = ParseIntegrator(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--direct-lighting" || argument == "--light-sampler")
            {
                result.config.directLightingEstimator =
                    ParseDirectLighting(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--light-proposal")
            {
                result.config.lightProposalDistribution =
                    ParseLightProposal(RequireValue(index, argumentCount, arguments, argument));
                continue;
            }
            if (argument == "--reconstruction")
            {
                result.config.reconstruction =
                    ParseReconstruction(RequireValue(index, argumentCount, arguments, argument));
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
            "Vulkan HLSL Rendering Engine\n"
            "Legacy-compatible options:\n"
            "  --frames N                 Render N frames and exit; 0 runs until exit.\n"
            "  --integrator MODE          pbr (default), whitted, cpu-reference, megakernel, wavefront.\n"
            "  --max-depth N              Trace depth from 1 to 12 (default 8).\n"
            "  --exposure X               Linear exposure from 0.01 to 64.\n"
            "  --shadow MODE              physical, pcf, or pcss.\n"
            "  --debug-view VIEW          final, base-color, normal, roughness, metallic, emissive.\n"
            "  --resize-test              Exercise swapchain recreation in a finite run.\n"
            "Wave 0 control contract:\n"
            "  --scene NAME               baseline, intersection-bvh, whitted-optics, cornell, ...\n"
            "  --backend NAME             legacy-analytic-gpu, cpu-sah, ray-query, rt-pipeline, ...\n"
            "  --direct-lighting MODE     legacy-analytic-direct, bsdf-only, nee, mis, restir-di.\n"
            "  --light-sampler MODE       Compatibility alias for --direct-lighting.\n"
            "  --light-proposal MODE      legacy-analytic, uniform, power, environment.\n"
            "  --reconstruction MODE      raw, temporal, temporal-atrous, svgf.\n"
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
            "  --headless                  Request headless execution.\n"
            "  --capture DIRECTORY         Request capture; DIRECTORY becomes artifact root.\n"
            "  --benchmark PRESET          Request a benchmark preset.\n"
            "  --reference IMAGE.exr       Request comparison with a reference image.\n"
            "                              These four requests are rejected until implemented.\n"
            "  --help | --version         Print information without creating a window.\n"
            "  --integration-status       List central-build and production-runtime boundaries.\n"
            "Known roadmap modes that are not built return exit code 4 without fallback.\n"
            "Controls: WASD move, Space/Ctrl vertical, Shift sprint, mouse look, "
            "wheel zoom, 1 PCF, 2 PCSS, 3 physical, Tab release mouse, Esc exit.\n";
    }

    std::string_view RuntimeVersionText() noexcept
    {
        return "RenderingEngine runtime-config-v0";
    }
}
