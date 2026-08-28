#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace RenderingEngine::Rt::Cpu
{
    // Writes interleaved, row-major linear RGB as three 32-bit float channels.
    [[nodiscard]] bool WriteLinearRgbExr(
        const std::filesystem::path& outputPath,
        std::uint32_t width,
        std::uint32_t height,
        std::span<const float> linearRgb,
        std::string& error);

    // Writes a top-down 24-bit BMP. Row zero in linearRgb remains the top row
    // in the preview. exposure is a linear multiplier applied before the
    // standard sRGB OETF.
    [[nodiscard]] bool WriteSrgbBmpPreview(
        const std::filesystem::path& outputPath,
        std::uint32_t width,
        std::uint32_t height,
        std::span<const float> linearRgb,
        float exposure,
        std::string& error);

    // FNV-1a over a versioned, little-endian encoding of the dimensions and
    // IEEE-754 component bits. Negative zero is normalized to positive zero.
    // Returns zero and sets error when dimensions, span size, or values fail
    // validation.
    [[nodiscard]] std::uint64_t HashLinearRgbPixels(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const float> linearRgb,
        std::string& error);
}
