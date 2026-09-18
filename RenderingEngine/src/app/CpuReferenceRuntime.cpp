#include "app/CpuReferenceRuntime.hpp"

#include "integrators/reference_cpu/CornellReference.hpp"
#include "rt/cpu/ImageOutput.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace RenderingEngine
{
    namespace
    {
        using Integrators::ReferenceCpu::CornellReferenceOptions;
        using Integrators::ReferenceCpu::CornellReferenceResult;

        [[nodiscard]] ArtifactLayout ResolveAvailableCpuReferenceLayout(
            const ArtifactLayout& requested)
        {
            std::error_code filesystemError;
            if (!std::filesystem::exists(
                    requested.runDirectory, filesystemError))
            {
                if (filesystemError)
                {
                    throw std::runtime_error(
                        "Failed to query the CPU reference artifact directory.");
                }
                return requested;
            }

            const std::string baseRunIdentifier =
                requested.runDirectory.filename().string();
            for (std::uint32_t suffix = 1u; suffix <= 9999u; ++suffix)
            {
                ArtifactLayout candidate = ResolveArtifactLayout(
                    requested.artifactRoot,
                    baseRunIdentifier + "-" + std::to_string(suffix));
                filesystemError.clear();
                const bool candidateExists = std::filesystem::exists(
                    candidate.runDirectory, filesystemError);
                if (filesystemError)
                {
                    throw std::runtime_error(
                        "Failed to query a CPU reference artifact directory candidate.");
                }
                if (!candidateExists)
                {
                    std::cout << "CPU reference run already exists; using "
                        << candidate.runDirectory.string() << " instead.\n";
                    return candidate;
                }
            }
            throw std::runtime_error(
                "No available CPU reference artifact run directory remains.");
        }

        void WriteMetadata(
            const std::filesystem::path& path,
            const RuntimeConfig& config,
            const CornellReferenceResult& result,
            const std::uint64_t pixelHash)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("Failed to create CPU reference metadata.");
            }
            output
                << "{\n"
                << "  \"schema_version\": \"artifact-layout-v0\",\n"
                << "  \"contract_versions\": {"
                << "\"scene_frame_abi\": \"abi-v0-numeric-1\", "
                << "\"gpu_traversal_abi\": \"abi-v1-numeric-2\", "
                << "\"runtime_config\": \"runtime-config-v2\"},\n"
                << "  \"provider\": \"L3-cpu-reference\",\n"
                << "  \"scene\": \"cornell-box-private-fixture\",\n"
                << "  \"width\": " << config.render.width << ",\n"
                << "  \"height\": " << config.render.height << ",\n"
                << "  \"spp\": " << config.render.targetSamplesPerPixel << ",\n"
                << "  \"max_bounces\": " << config.render.maximumBounce << ",\n"
                << "  \"seed\": " << config.render.baseSeed << ",\n"
                << "  \"scene_hash\": " << result.sceneHash << ",\n"
                << "  \"pixel_hash\": " << pixelHash << ",\n"
                << "  \"ray_count\": " << result.rayCount << ",\n"
                << "  \"shadow_ray_count\": " << result.shadowRayCount << ",\n"
                << "  \"non_finite_count\": " << result.nonFiniteCount << ",\n"
                << "  \"elapsed_ms\": " << result.elapsedMilliseconds << "\n"
                << "}\n";
            if (!output)
            {
                throw std::runtime_error("Failed while writing CPU reference metadata.");
            }
        }
    }

    void RunCpuReferenceRuntime(
        const RuntimeConfig& config,
        const ArtifactLayout& artifactLayout)
    {
        const ArtifactLayout outputLayout =
            ResolveAvailableCpuReferenceLayout(artifactLayout);

        CornellReferenceOptions options;
        options.width = config.render.width;
        options.height = config.render.height;
        options.spp = config.render.targetSamplesPerPixel;
        options.maxBounces = config.render.maximumBounce;
        options.seed = config.render.baseSeed;

        CornellReferenceResult result;
        std::string error;
        if (!Integrators::ReferenceCpu::RenderCornellReference(options, result, error))
        {
            throw std::runtime_error(error);
        }
        if (result.nonFiniteCount != 0u)
        {
            throw std::runtime_error("CPU reference completed with non-finite samples.");
        }

        std::error_code filesystemError;
        if (!std::filesystem::create_directories(
                outputLayout.referencesDirectory, filesystemError) || filesystemError)
        {
            throw std::runtime_error("Failed to create the CPU reference artifact directory.");
        }

        try
        {
            if (!Rt::Cpu::WriteLinearRgbExr(
                    outputLayout.referencesDirectory / "cornell-reference.exr",
                    options.width, options.height, result.pixels, error))
            {
                throw std::runtime_error(error);
            }
            if (!Rt::Cpu::WriteSrgbBmpPreview(
                    outputLayout.referencesDirectory / "cornell-reference.bmp",
                    options.width, options.height, result.pixels,
                    config.render.exposure, error))
            {
                throw std::runtime_error(error);
            }
            const std::uint64_t pixelHash = Rt::Cpu::HashLinearRgbPixels(
                options.width, options.height, result.pixels, error);
            if (pixelHash == 0u)
            {
                throw std::runtime_error(error);
            }
            WriteMetadata(outputLayout.metadataFile, config, result, pixelHash);
        }
        catch (...)
        {
            std::filesystem::remove_all(outputLayout.runDirectory, filesystemError);
            throw;
        }

        std::cout << "L3 CPU reference complete: " << options.width << 'x'
            << options.height << ", spp=" << options.spp
            << ", rays=" << result.rayCount
            << ", artifacts=" << outputLayout.runDirectory.string() << '\n';
    }
}
