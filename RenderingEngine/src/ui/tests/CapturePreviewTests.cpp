#include "demos/CapturePreview.hpp"

#include <array>
#include <limits>
#include <ostream>
#include <span>
#include <string_view>

bool RunCapturePreviewTests(std::ostream& errors)
{
    using namespace RenderingEngine::Demos;

    int failureCount = 0;
    const auto expect = [&errors, &failureCount](
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            errors << "L10 capture preview test failed: " << message << '\n';
            ++failureCount;
        }
    };

    constexpr std::array<float, 12> linear{
        0.0f, 0.0f, 0.0f, 0.25f,
        0.18f, 0.18f, 0.18f, 0.5f,
        4.0f, 1.0f, 0.25f, 0.0f
    };
    const CapturePreviewResult preview = BuildCapturePreviewRgba8(linear, 1.0f);
    expect(static_cast<bool>(preview) && preview.rgba8.size() == linear.size(),
        "valid RGBA32F input must produce one RGBA8 component per input component");
    expect(preview.rgba8.size() == linear.size()
            && preview.rgba8[0] == 0u
            && preview.rgba8[3] == 255u
            && preview.rgba8[7] == 255u
            && preview.rgba8[11] == 255u,
        "black must remain black and presentation alpha must always be opaque");
    expect(preview.rgba8.size() == linear.size()
            && preview.rgba8[8] > preview.rgba8[9]
            && preview.rgba8[9] > preview.rgba8[10],
        "PBR Neutral preview must preserve the ordering of a warm HDR highlight");

    const CapturePreviewResult incomplete = BuildCapturePreviewRgba8(
        std::span<const float>(linear).first(11u),
        1.0f);
    expect(!incomplete && incomplete.rgba8.empty() && !incomplete.error.empty(),
        "an incomplete RGBA pixel must fail before allocating an output image");

    std::array<float, 4> nonFinite{ 0.0f, 0.0f, 0.0f, 1.0f };
    nonFinite[1] = std::numeric_limits<float>::quiet_NaN();
    const CapturePreviewResult invalidPixel = BuildCapturePreviewRgba8(
        nonFinite,
        1.0f);
    expect(!invalidPixel && invalidPixel.rgba8.empty(),
        "non-finite renderer pixels must fail closed");

    const CapturePreviewResult invalidExposure = BuildCapturePreviewRgba8(
        linear,
        std::numeric_limits<float>::infinity());
    expect(!invalidExposure && invalidExposure.rgba8.empty(),
        "non-finite exposure must fail closed");

    if (failureCount == 0)
    {
        errors << "L10 capture preview checks passed.\n";
    }
    return failureCount == 0;
}
