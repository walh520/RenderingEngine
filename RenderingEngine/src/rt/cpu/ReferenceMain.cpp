#include "contracts/AbiVersion.hpp"
#include "integrators/reference_cpu/CornellReference.hpp"
#include "rt/cpu/ImageOutput.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
    using RenderingEngine::Integrators::ReferenceCpu::CornellReferenceOptions;
    using RenderingEngine::Integrators::ReferenceCpu::CornellReferenceResult;

    struct HarnessOptions final
    {
        CornellReferenceOptions render;
        std::filesystem::path outputPrefix = L"cornell-wave1";
        float exposure = 1.0f;
    };

    void PrintUsage()
    {
        std::cout
            << "L3 private CPU reference harness (not the production CLI)\n"
            << "Usage: RenderingEngine.CpuReference [options]\n"
            << "  --output-prefix <path>  Output prefix for .exr/.bmp/.json\n"
            << "  --width <1..4096>       Image width (default 256)\n"
            << "  --height <1..4096>      Image height (default 256)\n"
            << "  --spp <1..65536>        Samples per pixel (default 64)\n"
            << "  --max-bounces <1..64>   Maximum path depth (default 8)\n"
            << "  --seed <uint64>         Base seed (default 1)\n"
            << "  --threads <0..256>      Worker count; zero selects hardware concurrency\n"
            << "  --exposure <positive>   Preview exposure only (default 1)\n"
            << "  --help                  Show this message\n";
    }

    template <typename Integer>
    [[nodiscard]] bool ParseUnsigned(
        const std::string_view text,
        Integer& value) noexcept
    {
        static_assert(std::is_unsigned_v<Integer>);
        if (text.empty())
        {
            return false;
        }

        Integer parsed = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        {
            return false;
        }
        value = parsed;
        return true;
    }

    [[nodiscard]] bool ParsePositiveFloat(
        const std::string_view text,
        float& value) noexcept
    {
        if (text.empty())
        {
            return false;
        }

        float parsed = 0.0f;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
            !std::isfinite(parsed) || parsed <= 0.0f)
        {
            return false;
        }
        value = parsed;
        return true;
    }

    [[nodiscard]] bool ParseArguments(
        const int argc,
        char** argv,
        HarnessOptions& options,
        bool& helpRequested,
        std::string& error)
    {
        helpRequested = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h")
            {
                helpRequested = true;
                return true;
            }

            if (index + 1 >= argc)
            {
                error = "missing value for option: " + std::string(argument);
                return false;
            }
            const std::string_view value(argv[++index]);

            if (argument == "--output-prefix")
            {
                if (value.empty())
                {
                    error = "--output-prefix cannot be empty";
                    return false;
                }
                std::u8string utf8Value;
                utf8Value.reserve(value.size());
                for (const char byte : value)
                {
                    utf8Value.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
                }
                options.outputPrefix = std::filesystem::path(utf8Value);
            }
            else if (argument == "--width")
            {
                if (!ParseUnsigned(value, options.render.width) || options.render.width == 0u ||
                    options.render.width > 4096u)
                {
                    error = "--width must be in [1, 4096]";
                    return false;
                }
            }
            else if (argument == "--height")
            {
                if (!ParseUnsigned(value, options.render.height) || options.render.height == 0u ||
                    options.render.height > 4096u)
                {
                    error = "--height must be in [1, 4096]";
                    return false;
                }
            }
            else if (argument == "--spp")
            {
                if (!ParseUnsigned(value, options.render.spp) || options.render.spp == 0u ||
                    options.render.spp > 65536u)
                {
                    error = "--spp must be in [1, 65536]";
                    return false;
                }
            }
            else if (argument == "--max-bounces")
            {
                if (!ParseUnsigned(value, options.render.maxBounces) ||
                    options.render.maxBounces == 0u ||
                    options.render.maxBounces > 64u)
                {
                    error = "--max-bounces must be in [1, 64]";
                    return false;
                }
            }
            else if (argument == "--seed")
            {
                if (!ParseUnsigned(value, options.render.seed))
                {
                    error = "--seed must be an unsigned 64-bit integer";
                    return false;
                }
            }
            else if (argument == "--threads")
            {
                if (!ParseUnsigned(value, options.render.threads) || options.render.threads > 256u)
                {
                    error = "--threads must be in [0, 256]";
                    return false;
                }
            }
            else if (argument == "--exposure")
            {
                if (!ParsePositiveFloat(value, options.exposure))
                {
                    error = "--exposure must be finite and positive";
                    return false;
                }
            }
            else
            {
                error = "unknown option: " + std::string(argument);
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string EscapeJson(const std::string_view value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value)
        {
            switch (character)
            {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += character; break;
            }
        }
        return escaped;
    }

    [[nodiscard]] bool WriteReport(
        const std::filesystem::path& path,
        const HarnessOptions& options,
        const CornellReferenceResult& result,
        const std::uint64_t imageHash,
        const std::filesystem::path& exrPath,
        const std::filesystem::path& bmpPath,
        std::string& error)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "failed to open report: " + path.string();
            return false;
        }

        stream << "{\n"
            << "  \"scope\": \"l3-private-cornell-fixture-v0\",\n"
            << "  \"contract\": \"abi-v0\",\n"
            << "  \"contract_numeric\": "
            << RenderingEngine::Contracts::AbiV0::kAbiVersion << ",\n"
            << "  \"width\": " << options.render.width << ",\n"
            << "  \"height\": " << options.render.height << ",\n"
            << "  \"spp\": " << options.render.spp << ",\n"
            << "  \"max_bounces\": " << options.render.maxBounces << ",\n"
            << "  \"seed\": " << options.render.seed << ",\n"
            << "  \"threads_requested\": " << options.render.threads << ",\n"
            << "  \"scene_hash_fnv1a64\": \"0x" << std::hex << std::setw(16)
            << std::setfill('0') << result.sceneHash << "\",\n"
            << "  \"linear_rgb_hash_fnv1a64\": \"0x" << std::setw(16)
            << imageHash << "\",\n" << std::dec
            << "  \"ray_count\": " << result.rayCount << ",\n"
            << "  \"shadow_ray_count\": " << result.shadowRayCount << ",\n"
            << "  \"non_finite_count\": " << result.nonFiniteCount << ",\n"
            << "  \"elapsed_ms\": " << std::fixed << std::setprecision(3)
            << result.elapsedMilliseconds << ",\n"
            << "  \"exr\": \"" << EscapeJson(exrPath.generic_string()) << "\",\n"
            << "  \"preview_bmp\": \"" << EscapeJson(bmpPath.generic_string()) << "\"\n"
            << "}\n";
        if (!stream)
        {
            error = "failed while writing report: " + path.string();
            return false;
        }
        return true;
    }
}

int main(const int argc, char** argv)
{
    HarnessOptions options;
    bool helpRequested = false;
    std::string error;
    if (!ParseArguments(argc, argv, options, helpRequested, error))
    {
        std::cerr << "Configuration error: " << error << '\n';
        PrintUsage();
        return 2;
    }
    if (helpRequested)
    {
        PrintUsage();
        return 0;
    }

    const std::filesystem::path parent = options.outputPrefix.parent_path();
    if (!parent.empty())
    {
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError)
        {
            std::cerr << "Runtime error: failed to create output directory: "
                << directoryError.message() << '\n';
            return 10;
        }
    }

    CornellReferenceResult result;
    if (!RenderingEngine::Integrators::ReferenceCpu::RenderCornellReference(
        options.render,
        result,
        error))
    {
        std::cerr << "Runtime error: " << error << '\n';
        return 10;
    }

    std::filesystem::path exrPath = options.outputPrefix;
    std::filesystem::path bmpPath = options.outputPrefix;
    std::filesystem::path reportPath = options.outputPrefix;
    exrPath += L".exr";
    bmpPath += L".bmp";
    reportPath += L".json";
    if (!RenderingEngine::Rt::Cpu::WriteLinearRgbExr(
        exrPath,
        options.render.width,
        options.render.height,
        result.pixels,
        error))
    {
        std::cerr << "Runtime error: " << error << '\n';
        return 10;
    }
    if (!RenderingEngine::Rt::Cpu::WriteSrgbBmpPreview(
        bmpPath,
        options.render.width,
        options.render.height,
        result.pixels,
        options.exposure,
        error))
    {
        std::cerr << "Runtime error: " << error << '\n';
        return 10;
    }

    const std::uint64_t imageHash = RenderingEngine::Rt::Cpu::HashLinearRgbPixels(
        options.render.width,
        options.render.height,
        result.pixels,
        error);
    if (!error.empty())
    {
        std::cerr << "Runtime error: " << error << '\n';
        return 10;
    }
    if (!WriteReport(reportPath, options, result, imageHash, exrPath, bmpPath, error))
    {
        std::cerr << "Runtime error: " << error << '\n';
        return 10;
    }

    std::cout << "L3 Cornell reference complete\n"
        << "  linear EXR: " << exrPath.string() << '\n'
        << "  preview BMP: " << bmpPath.string() << '\n'
        << "  report: " << reportPath.string() << '\n'
        << "  image hash: 0x" << std::hex << std::setw(16) << std::setfill('0')
        << imageHash << std::dec << '\n'
        << "  non-finite values: " << result.nonFiniteCount << '\n'
        << "  elapsed ms: " << std::fixed << std::setprecision(3)
        << result.elapsedMilliseconds << '\n';
    return result.nonFiniteCount == 0u ? 0 : 10;
}
