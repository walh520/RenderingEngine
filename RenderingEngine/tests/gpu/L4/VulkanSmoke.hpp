#pragma once

#include "rt/software_gpu/SoftwareGpu.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace RenderingEngine::Rt::SoftwareGpu::VulkanSmoke
{
    enum class Status : std::uint32_t
    {
        Passed = 0u,
        Skipped,
        Failed,
    };

    struct PathEvidence
    {
        bool buildPassed{};
        bool tracePassed{};
        GpuTraversalCounterReadback counters{};
        std::uint32_t rayCount{};
        std::uint32_t hitCount{};
        std::uint32_t maximumDepth{};
        std::uint64_t uploadBytes{};
        std::uint64_t readbackBytes{};
        double buildGpuMilliseconds{};
        double traceGpuMilliseconds{};
        bool buildGpuTimestampMeasured{};
        bool traceGpuTimestampMeasured{};
    };

    struct Report
    {
        Status status{Status::Failed};
        std::string reason{};
        std::string deviceName{};
        bool validationLayerAvailable{};
        bool validationLayerEnabled{};
        bool synchronizationValidationEnabled{};
        std::uint32_t validationWarnings{};
        std::uint32_t validationErrors{};
        PathEvidence flattenedSah{};
        PathEvidence gpuLbvh{};
        std::vector<std::string> validationMessages{};
    };

    [[nodiscard]] Report Run(const std::filesystem::path& shaderDirectory) noexcept;
}
