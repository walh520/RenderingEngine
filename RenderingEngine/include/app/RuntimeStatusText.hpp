#pragma once

#include "app/RuntimeConfig.hpp"

#include <string>
#include <string_view>

namespace RenderingEngine
{
    // Stable UTF-8 display names for the RuntimeConfig enum values. Unknown
    // values return an explicit fallback instead of an empty string.
    [[nodiscard]] std::string_view ScenePresetName(ScenePreset value) noexcept;
    [[nodiscard]] std::string_view TraversalBackendName(TraversalBackend value) noexcept;
    [[nodiscard]] std::string_view TransportModelName(TransportModel value) noexcept;
    [[nodiscard]] std::string_view ExecutionArchitectureName(
        ExecutionArchitecture value) noexcept;
    [[nodiscard]] std::string_view DirectLightingEstimatorName(
        DirectLightingEstimator value) noexcept;
    [[nodiscard]] std::string_view LightSelectionStrategyName(
        LightSelectionStrategy value) noexcept;
    [[nodiscard]] std::string_view EnvironmentDirectionSamplerName(
        EnvironmentDirectionSampler value) noexcept;
    [[nodiscard]] std::string_view ReconstructionModeName(
        ReconstructionMode value) noexcept;
    [[nodiscard]] std::string_view DebugViewName(DebugView value) noexcept;

    // Formats every field in RuntimeConfig as one UTF-8, semicolon-delimited
    // status line. The result has no trailing newline.
    [[nodiscard]] std::string FormatRuntimeConfigStatus(const RuntimeConfig& config);

    // Formats a concise study card for the active scene and complete runtime
    // algorithm tuple. The result is intended for the debug console.
    [[nodiscard]] std::string FormatRuntimeReview(const RuntimeConfig& config);

    // Static UTF-8 help text for the GLFW showcase controls.
    [[nodiscard]] std::string_view GlfwKeyHelpText() noexcept;
}
