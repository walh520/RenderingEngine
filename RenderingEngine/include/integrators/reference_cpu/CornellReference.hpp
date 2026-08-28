#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RenderingEngine::Integrators::ReferenceCpu
{
    struct CornellReferenceOptions final
    {
        std::uint32_t width = 256u;
        std::uint32_t height = 256u;
        std::uint32_t spp = 64u;
        std::uint32_t maxBounces = 8u;
        std::uint64_t seed = 1u;
        // Zero selects std::thread::hardware_concurrency(), with a one-thread fallback.
        std::uint32_t threads = 0u;
    };

    struct CornellReferenceResult final
    {
        // Top-to-bottom, row-major, interleaved linear Rec.709 RGB.
        std::vector<float> pixels;
        std::uint64_t sceneHash = 0u;
        std::uint64_t rayCount = 0u;
        std::uint64_t shadowRayCount = 0u;
        std::uint64_t nonFiniteCount = 0u;
        double elapsedMilliseconds = 0.0;
    };

    // Renders an L3-private deterministic Cornell fixture. It is deliberately
    // not the L2 canonical scene; the production app exposes it only through
    // the fail-closed headless CPU-reference configuration.
    [[nodiscard]] bool RenderCornellReference(
        const CornellReferenceOptions& options,
        CornellReferenceResult& result,
        std::string& error);
}
