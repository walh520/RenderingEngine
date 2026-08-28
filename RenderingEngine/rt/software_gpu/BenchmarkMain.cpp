#include "rt/software_gpu/SoftwareGpu.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace L4 = RenderingEngine::Rt::SoftwareGpu;

namespace
{
    struct Options
    {
        std::uint32_t primitiveCount{4096u};
        std::uint32_t rayCount{100000u};
        std::uint32_t iterations{3u};
    };

    [[nodiscard]] bool ParsePositive(
        const std::string_view text,
        std::uint32_t& value) noexcept
    {
        std::uint32_t parsed{};
        const auto conversion = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() ||
            parsed == 0u)
        {
            return false;
        }
        value = parsed;
        return true;
    }

    [[nodiscard]] bool ParseOptions(const int argc, char** const argv, Options& options)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view option = argv[index];
            if (option == "--help")
            {
                std::cout << "Usage: RenderingEngine.SoftwareGpu.Benchmark "
                             "[--primitives N] [--rays N] [--iterations N]\n";
                return false;
            }
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value after " << option << '\n';
                return false;
            }
            const std::string_view value = argv[++index];
            std::uint32_t* destination = nullptr;
            if (option == "--primitives")
            {
                destination = &options.primitiveCount;
            }
            else if (option == "--rays")
            {
                destination = &options.rayCount;
            }
            else if (option == "--iterations")
            {
                destination = &options.iterations;
            }
            else
            {
                std::cerr << "Unknown option: " << option << '\n';
                return false;
            }
            if (!ParsePositive(value, *destination))
            {
                std::cerr << "Expected a positive integer after " << option << '\n';
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<L4::SoftwarePrimitiveRecord> MakeGrid(
        const std::uint32_t count)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(
            std::ceil(std::sqrt(static_cast<double>(count))));
        std::vector<L4::SoftwarePrimitiveRecord> result{};
        result.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const float x = static_cast<float>(index % width);
            const float y = static_cast<float>(index / width);
            result.push_back(L4::MakeTriangle(
                {x - 0.45f, y - 0.45f, 0.0f},
                {x + 0.45f, y - 0.45f, 0.0f},
                {x, y + 0.45f, 0.0f},
                index));
        }
        return result;
    }

    [[nodiscard]] std::vector<L4::SoftwareRayRecord> MakeRays(
        const std::uint32_t rayCount,
        const std::uint32_t primitiveCount)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(
            std::ceil(std::sqrt(static_cast<double>(primitiveCount))));
        std::vector<L4::SoftwareRayRecord> result{};
        result.reserve(rayCount);
        for (std::uint32_t index = 0u; index < rayCount; ++index)
        {
            const std::uint32_t primitive = index % primitiveCount;
            float x = static_cast<float>(primitive % width);
            float y = static_cast<float>(primitive / width);
            if ((index % 10u) == 0u)
            {
                x += static_cast<float>(width) + 10.0f;
                y += static_cast<float>(width) + 10.0f;
            }
            result.push_back(L4::MakeRay(
                {x, y, 4.0f},
                {0.0f, 0.0f, -1.0f},
                0.001f,
                1000.0f,
                index));
        }
        return result;
    }

    void PrintPath(const char* const name, const L4::BenchmarkPathResult& result)
    {
        std::cout << "    \"" << name << "\": {\n"
                  << "      \"measurementDomain\": \"cpu-mirror\",\n"
                  << "      \"gpuMeasured\": false,\n"
                  << "      \"buildStatus\": " << static_cast<std::uint32_t>(result.buildStatus)
                  << ",\n"
                  << "      \"buildMilliseconds\": " << result.buildMilliseconds << ",\n"
                  << "      \"traceMilliseconds\": " << result.traceMilliseconds << ",\n"
                  << "      \"millionRaysPerSecond\": " << result.millionRaysPerSecond << ",\n"
                  << "      \"memoryBytes\": " << result.memoryBytes << ",\n"
                  << "      \"nodeTests\": " << result.counters.nodeTests << ",\n"
                  << "      \"triangleTests\": " << result.counters.triangleTests << ",\n"
                  << "      \"leafVisits\": " << result.counters.leafVisits << ",\n"
                  << "      \"accumulatedLeafPrimitives\": "
                  << result.counters.accumulatedLeafPrimitives << ",\n"
                  << "      \"maximumLeafOccupancy\": "
                  << result.counters.maximumLeafOccupancy << ",\n"
                  << "      \"maximumStackDepth\": " << result.counters.maximumStackDepth << ",\n"
                  << "      \"stackOverflows\": " << result.counters.stackOverflows << ",\n"
                  << "      \"invalidRays\": " << result.counters.invalidRays << ",\n"
                  << "      \"invalidHits\": " << result.counters.invalidHits << ",\n"
                  << "      \"checksum\": " << result.resultChecksum << "\n"
                  << "    }";
    }
}

int main(const int argc, char** const argv)
{
    Options options{};
    if (!ParseOptions(argc, argv, options))
    {
        return argc > 1 && std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    const std::vector<L4::SoftwarePrimitiveRecord> primitives =
        MakeGrid(options.primitiveCount);
    const std::vector<L4::SoftwareRayRecord> rays =
        MakeRays(options.rayCount, options.primitiveCount);
    const L4::BenchmarkResult result = L4::RunCpuMirrorBenchmark(
        primitives,
        rays,
        options.iterations,
        {4u, 64u});

    std::cout << "{\n"
              << "  \"schema\": \"l4-software-gpu-benchmark-v0-private\",\n"
              << "  \"primitiveCount\": " << result.primitiveCount << ",\n"
              << "  \"rayCount\": " << result.rayCount << ",\n"
              << "  \"iterations\": " << result.iterations << ",\n"
              << "  \"gpuMeasurement\": {\"measured\": false, "
                 "\"buildGpuMilliseconds\": null, \"traceGpuMilliseconds\": null, "
                 "\"uploadBytes\": null, \"readbackBytes\": null},\n";
    PrintPath("flattenedSah", result.flattenedSah);
    std::cout << ",\n";
    PrintPath("lbvh", result.lbvh);
    std::cout << "\n}\n";

    return result.flattenedSah.buildStatus == L4::BuildStatus::Success &&
                   result.lbvh.buildStatus == L4::BuildStatus::Success
               ? 0
               : 1;
}
