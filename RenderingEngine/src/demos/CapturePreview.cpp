#include "demos/CapturePreview.hpp"

#include <algorithm>
#include <cmath>

namespace RenderingEngine::Demos
{
    namespace
    {
        [[nodiscard]] float LinearToSrgb(const float value) noexcept
        {
            const float nonNegative = std::max(value, 0.0f);
            return nonNegative <= 0.0031308f
                ? nonNegative * 12.92f
                : 1.055f * std::pow(nonNegative, 1.0f / 2.4f) - 0.055f;
        }

        void ApplyPbrNeutralToneMap(float& red, float& green, float& blue) noexcept
        {
            constexpr float compressionStart = 0.76f;
            constexpr float desaturation = 0.15f;
            const float darkest = std::min(red, std::min(green, blue));
            const float offset = darkest < 0.08f
                ? darkest - 6.25f * darkest * darkest
                : 0.04f;
            red -= offset;
            green -= offset;
            blue -= offset;

            const float peak = std::max(red, std::max(green, blue));
            if (peak < compressionStart)
            {
                return;
            }
            constexpr float distanceToWhite = 1.0f - compressionStart;
            const float compressedPeak = 1.0f
                - distanceToWhite * distanceToWhite
                    / (peak + distanceToWhite - compressionStart);
            const float scale = compressedPeak / peak;
            red *= scale;
            green *= scale;
            blue *= scale;
            const float weight = 1.0f
                - 1.0f / (desaturation * (peak - compressedPeak) + 1.0f);
            red += (compressedPeak - red) * weight;
            green += (compressedPeak - green) * weight;
            blue += (compressedPeak - blue) * weight;
        }

        [[nodiscard]] std::uint8_t Quantize(const float value) noexcept
        {
            return static_cast<std::uint8_t>(std::lround(
                std::clamp(LinearToSrgb(value), 0.0f, 1.0f) * 255.0f));
        }
    }

    CapturePreviewResult BuildCapturePreviewRgba8(
        const std::span<const float> linearRgba,
        const float exposure)
    {
        CapturePreviewResult result;
        if (linearRgba.empty() || linearRgba.size() % 4u != 0u)
        {
            result.error = "Capture preview input must contain complete non-empty RGBA pixels.";
            return result;
        }
        if (!std::isfinite(exposure) || exposure < 0.01f || exposure > 64.0f)
        {
            result.error = "Capture preview exposure must be finite and from 0.01 to 64.";
            return result;
        }
        if (std::any_of(
                linearRgba.begin(),
                linearRgba.end(),
                [](const float value) { return !std::isfinite(value); }))
        {
            result.error = "Capture preview input contains a non-finite RGBA component.";
            return result;
        }

        result.rgba8.resize(linearRgba.size());
        for (std::size_t offset = 0; offset < linearRgba.size(); offset += 4u)
        {
            float red = std::max(linearRgba[offset] * exposure, 0.0f);
            float green = std::max(linearRgba[offset + 1u] * exposure, 0.0f);
            float blue = std::max(linearRgba[offset + 2u] * exposure, 0.0f);
            ApplyPbrNeutralToneMap(red, green, blue);
            result.rgba8[offset] = Quantize(red);
            result.rgba8[offset + 1u] = Quantize(green);
            result.rgba8[offset + 2u] = Quantize(blue);
            result.rgba8[offset + 3u] = 255u;
        }
        return result;
    }
}
