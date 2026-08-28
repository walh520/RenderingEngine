#pragma once

#include <cstdint>
#include <string>

namespace RenderingEngine::Restir::Tests
{
    enum class VulkanSmokeStatus : std::uint32_t
    {
        Passed = 0u,
        SkippedUnavailable = 1u
    };

    struct VulkanSmokeReport
    {
        VulkanSmokeStatus status = VulkanSmokeStatus::SkippedUnavailable;
        std::string message{};
        std::string deviceName{};
        bool validationLayerEnabled = false;
        std::uint32_t validationErrorCount = 0u;
        std::uint32_t reservoirCount = 0u;
        std::uint32_t nonemptyReservoirCount = 0u;
        std::uint32_t finalVisibilityRays = 0u;
        std::uint32_t debugImagePixelCount = 0u;
        std::uint64_t readbackChecksum = 0u;
    };

    // Executes the five private L9 Compute stages on an actual Vulkan 1.3
    // compute queue. Loader/device absence is reported as a skip; API,
    // validation, dispatch, synchronization, or readback failures throw.
    [[nodiscard]] VulkanSmokeReport RunVulkanSmoke();
}
