#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace RenderingEngine::Demos
{
    struct CapturePreviewResult
    {
        std::vector<std::uint8_t> rgba8;
        std::string error;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error.empty();
        }
    };

    // Mirrors resources/shaders/whitted/Present.hlsl: non-negative linear
    // Rec.709 -> exposure -> PBR Neutral -> sRGB OETF. Alpha is published as
    // opaque because the presentation pass does the same.
    [[nodiscard]] CapturePreviewResult BuildCapturePreviewRgba8(
        std::span<const float> linearRgba,
        float exposure);
}
