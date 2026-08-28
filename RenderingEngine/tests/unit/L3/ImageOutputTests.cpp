#include "rt/cpu/ImageOutput.hpp"

#include <catch2/catch_test_macros.hpp>
#include <tinyexr.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>

namespace
{
    struct ExrError final
    {
        const char* message = nullptr;

        ~ExrError()
        {
            if (message != nullptr)
            {
                FreeEXRErrorMessage(message);
            }
        }
    };

    struct FloatPixels final
    {
        float* data = nullptr;

        ~FloatPixels()
        {
            std::free(data);
        }
    };
}

TEST_CASE("TinyEXR round-trips L3 linear RGB output", "[l3][image][exr]")
{
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "rendering-engine-l3-image-output.exr";
    const std::array<float, 12u> source = {
        0.0f, 0.5f, 1.0f,
        2.0f, 3.0f, 4.0f,
        0.25f, 0.125f, 0.0625f,
        10.0f, 0.0f, 0.75f,
    };
    std::string error;
    REQUIRE(RenderingEngine::Rt::Cpu::WriteLinearRgbExr(
        outputPath,
        2u,
        2u,
        source,
        error));
    INFO(error);

    FloatPixels loaded;
    int width = 0;
    int height = 0;
    ExrError exrError;
    const int result = LoadEXR(
        &loaded.data,
        &width,
        &height,
        outputPath.string().c_str(),
        &exrError.message);
    INFO(std::string(exrError.message == nullptr ? "" : exrError.message));
    REQUIRE(result == TINYEXR_SUCCESS);
    REQUIRE(loaded.data != nullptr);
    REQUIRE(width == 2);
    REQUIRE(height == 2);
    for (std::size_t pixel = 0u; pixel < 4u; ++pixel)
    {
        REQUIRE(loaded.data[pixel * 4u] == source[pixel * 3u]);
        REQUIRE(loaded.data[pixel * 4u + 1u] == source[pixel * 3u + 1u]);
        REQUIRE(loaded.data[pixel * 4u + 2u] == source[pixel * 3u + 2u]);
        REQUIRE(loaded.data[pixel * 4u + 3u] == 1.0f);
    }

    std::error_code removeError;
    static_cast<void>(std::filesystem::remove(outputPath, removeError));
    REQUIRE_FALSE(removeError);
}

TEST_CASE("L3 image hash is stable and rejects non-finite data", "[l3][image][hash]")
{
    std::string error;
    const std::array<float, 3u> reference = {0.0f, 0.5f, 1.0f};
    REQUIRE(RenderingEngine::Rt::Cpu::HashLinearRgbPixels(
        1u,
        1u,
        reference,
        error) == 0x4f870f75ae6f9d3dull);
    REQUIRE(error.empty());

    const std::array<float, 3u> invalid = {
        0.0f,
        (std::numeric_limits<float>::quiet_NaN)(),
        1.0f,
    };
    REQUIRE(RenderingEngine::Rt::Cpu::HashLinearRgbPixels(
        1u,
        1u,
        invalid,
        error) == 0u);
    REQUIRE_FALSE(error.empty());
}
