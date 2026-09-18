#pragma once

#include "contracts/RestirAbiV3.hpp"
#include "renderers/ReSTIRDIRuntime.hpp"

#include <cstdint>

namespace RenderingEngine::Renderers
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
    struct ReSTIRFrameParameterSources final
    {
        std::uint32_t currentLightCount = 0u;
        std::uint32_t previousLightCount = 0u;
        std::uint32_t historyGeneration = 0u;
        Contracts::AbiV3::AbiFloat4 validation{
            0.8f, 0.05f, 0.02f, 0.005f};
        Contracts::AbiV3::AbiFloat4 cameraPosition{0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct ReSTIRFrameParameterResult final
    {
        ReSTIRRuntimeStatus status{};
        Contracts::AbiV3::GpuRestirParametersV3 parameters{};

        [[nodiscard]] bool IsReady() const noexcept
        {
            return static_cast<bool>(status);
        }
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    // Converts the validated host request and provider-owned scene facts into
    // the sole abi-v3 set-5 constant-buffer record. No renderer may invent a
    // private parameter layout or silently drift from RuntimeConfig.
    [[nodiscard]] ReSTIRFrameParameterResult BuildReSTIRFrameParameters(
        const ReSTIRFrameRequest& request,
        const ReSTIRFramePlan& plan,
        const ReSTIRFrameParameterSources& sources);
}
