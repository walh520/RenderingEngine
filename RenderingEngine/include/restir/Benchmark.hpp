#pragma once

#include "restir/RestirDi.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace RenderingEngine::Restir
{
    enum class BenchmarkProposal : std::uint32_t
    {
        Uniform = 0u,
        PowerWeighted = 1u,
        RestirMixed = 2u,
        HighSppReference = 3u
    };

    enum class MeasurementStatus : std::uint32_t
    {
        NotMeasured = 0u,
        Measured = 1u
    };

    struct BenchmarkTier
    {
        std::uint32_t lightCount = 0u;
        std::uint32_t reservoirCount = 0u;
        std::uint32_t candidatesPerReservoir = 0u;
    };

    struct BenchmarkResult
    {
        BenchmarkTier tier{};
        EstimatorMode estimatorMode = EstimatorMode::Biased;
        BenchmarkProposal proposal = BenchmarkProposal::RestirMixed;
        std::uint32_t candidateBudget = 0u;
        std::uint32_t visibilityBudget = 0u;
        double cpuBuildMilliseconds = 0.0;
        double cpuTraceMilliseconds = 0.0;
        MeasurementStatus referenceErrorStatus = MeasurementStatus::NotMeasured;
        double meanAbsoluteError = std::numeric_limits<double>::quiet_NaN();
        double rmse = std::numeric_limits<double>::quiet_NaN();
        MeasurementStatus gpuTimingStatus = MeasurementStatus::NotMeasured;
        double gpuMilliseconds = std::numeric_limits<double>::quiet_NaN();
        std::uint64_t candidateCount = 0u;
        std::uint64_t finalVisibilityRays = 0u;
        std::uint64_t estimatedGpuBytes = 0u;
        std::uint64_t deterministicChecksum = 0u;
    };

    [[nodiscard]] BenchmarkResult RunManyLightsBenchmark(const BenchmarkTier& tier);
    [[nodiscard]] std::vector<BenchmarkResult> RunStandardManyLightsBenchmarks(
        std::uint32_t reservoirCount,
        std::uint32_t candidatesPerReservoir);
    [[nodiscard]] std::string FormatBenchmarkCsv(std::span<const BenchmarkResult> results);
}
