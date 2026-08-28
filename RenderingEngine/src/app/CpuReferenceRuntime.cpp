#include "app/CpuReferenceRuntime.hpp"

#include "integrators/reference_cpu/CornellReference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace RenderingEngine
{
    namespace
    {
        using Integrators::ReferenceCpu::CornellReferenceOptions;
        using Integrators::ReferenceCpu::CornellReferenceResult;

        [[nodiscard]] std::uint8_t LinearToSrgbByte(const float value, const float exposure)
        {
            const float linear = std::max(0.0f, value * exposure);
            const float mapped = linear / (1.0f + linear);
            const float srgb = mapped <= 0.0031308f
                ? 12.92f * mapped
                : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::clamp(srgb, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        void WritePfm(
            const std::filesystem::path& path,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::vector<float>& pixels)
        {
            static_assert(std::endian::native == std::endian::little);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("Failed to create CPU reference PFM output.");
            }
            output << "PF\n" << width << ' ' << height << "\n-1.0\n";
            for (std::uint32_t row = height; row-- > 0u;)
            {
                const std::size_t offset = static_cast<std::size_t>(row) * width * 3u;
                output.write(
                    reinterpret_cast<const char*>(pixels.data() + offset),
                    static_cast<std::streamsize>(width) * 3 * sizeof(float));
            }
            if (!output)
            {
                throw std::runtime_error("Failed while writing CPU reference PFM output.");
            }
        }

        void WritePpm(
            const std::filesystem::path& path,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::vector<float>& pixels,
            const float exposure)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("Failed to create CPU reference PPM preview.");
            }
            output << "P6\n" << width << ' ' << height << "\n255\n";
            for (const float component : pixels)
            {
                if (!std::isfinite(component))
                {
                    throw std::runtime_error("CPU reference produced a non-finite preview component.");
                }
                output.put(static_cast<char>(LinearToSrgbByte(component, exposure)));
            }
            if (!output)
            {
                throw std::runtime_error("Failed while writing CPU reference PPM preview.");
            }
        }

        void WriteMetadata(
            const std::filesystem::path& path,
            const RuntimeConfig& config,
            const CornellReferenceResult& result)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("Failed to create CPU reference metadata.");
            }
            output
                << "{\n"
                << "  \"provider\": \"L3-cpu-reference\",\n"
                << "  \"scene\": \"cornell-box-private-fixture\",\n"
                << "  \"width\": " << config.render.width << ",\n"
                << "  \"height\": " << config.render.height << ",\n"
                << "  \"spp\": " << config.render.targetSamplesPerPixel << ",\n"
                << "  \"max_bounces\": " << config.render.maximumBounce << ",\n"
                << "  \"seed\": " << config.render.baseSeed << ",\n"
                << "  \"scene_hash\": " << result.sceneHash << ",\n"
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
        const bool runDirectoryExists = std::filesystem::exists(
            artifactLayout.runDirectory, filesystemError);
        if (filesystemError)
        {
            throw std::runtime_error("Failed to query the CPU reference artifact directory.");
        }
        if (runDirectoryExists)
        {
            throw std::runtime_error("CPU reference artifact run already exists.");
        }
        if (!std::filesystem::create_directories(
                artifactLayout.referencesDirectory, filesystemError) || filesystemError)
        {
            throw std::runtime_error("Failed to create the CPU reference artifact directory.");
        }

        try
        {
            WritePfm(
                artifactLayout.referencesDirectory / "cpu-reference.pfm",
                options.width, options.height, result.pixels);
            WritePpm(
                artifactLayout.referencesDirectory / "cpu-reference.ppm",
                options.width, options.height, result.pixels, config.render.exposure);
            WriteMetadata(artifactLayout.metadataFile, config, result);
        }
        catch (...)
        {
            std::filesystem::remove_all(artifactLayout.runDirectory, filesystemError);
            throw;
        }

        std::cout << "L3 CPU reference complete: " << options.width << 'x'
            << options.height << ", spp=" << options.spp
            << ", rays=" << result.rayCount
            << ", artifacts=" << artifactLayout.runDirectory.string() << '\n';
    }
}
