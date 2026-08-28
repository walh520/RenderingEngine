#include "restir/Benchmark.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace RenderingEngine::Restir
{
    namespace
    {
        constexpr std::uint64_t kProvisionalGpuLightBytes = 48u;
        constexpr std::uint64_t kProvisionalGpuCandidateBytes = 80u;
        constexpr std::uint64_t kProvisionalGpuReservoirBytes = 112u;
        constexpr std::uint64_t kProvisionalGpuSurfaceBytes = 48u;
        constexpr std::uint64_t kProvisionalGpuDebugBytes = 64u;
        constexpr std::uint64_t kProvisionalGpuDirectLightingBytes = 16u;

        [[nodiscard]] std::uint64_t MixChecksum(std::uint64_t hash, const std::uint64_t value) noexcept
        {
            hash ^= value;
            hash *= 1099511628211ull;
            return hash;
        }
    }

    BenchmarkResult RunManyLightsBenchmark(const BenchmarkTier& tier)
    {
        BenchmarkResult result{};
        result.tier = tier;
        result.estimatorMode = EstimatorMode::Biased;
        result.proposal = BenchmarkProposal::RestirMixed;
        result.candidateBudget = tier.candidatesPerReservoir;
        result.visibilityBudget = 1u;

        const auto buildStart = std::chrono::steady_clock::now();
        std::vector<AnalyticLight> lights;
        lights.reserve(tier.lightCount);
        for (std::uint32_t index = 0u; index < tier.lightCount; ++index)
        {
            const float phase = static_cast<float>(index) * 0.61803398875f;
            const float height = 1.0f + static_cast<float>(index % 17u) * 0.05f;
            const float intensity = 1.0f + static_cast<float>((index * 13u) % 97u) * 0.1f;
            lights.push_back({
                index,
                1u,
                { std::cos(phase) * 5.0f, height, std::sin(phase) * 5.0f },
                { intensity, intensity * 0.8f, intensity * 0.6f },
                intensity
            });
        }
        const auto buildEnd = std::chrono::steady_clock::now();

        const SurfaceRecord surface{
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            1.0f,
            1u,
            2u,
            3u,
            1u,
            false
        };
        ReservoirStatistics statistics{};
        std::uint64_t checksum = 1469598103934665603ull;
        const auto traceStart = std::chrono::steady_clock::now();
        for (std::uint32_t reservoirIndex = 0u; reservoirIndex < tier.reservoirCount; ++reservoirIndex)
        {
            Pcg32 random(0x9e3779b97f4a7c15ull + reservoirIndex, 7u);
            std::vector<Candidate> candidates;
            candidates.reserve(tier.candidatesPerReservoir);
            for (std::uint32_t candidateIndex = 0u;
                candidateIndex < tier.candidatesPerReservoir;
                ++candidateIndex)
            {
                const float sample = random.NextFloat();
                candidates.push_back((candidateIndex & 1u) == 0u
                    ? GenerateUniformLightCandidate(lights, surface, sample)
                    : GeneratePowerWeightedLightCandidate(lights, surface, sample));
            }

            Reservoir reservoir = BuildInitialReservoir(
                candidates, random, std::max(1u, tier.candidatesPerReservoir), &statistics);
            const FinalLightingResult lighting = EvaluateFinalVisibilityOnce(
                reservoir,
                [](const Candidate&) { return true; },
                &statistics);
            checksum = MixChecksum(checksum, reservoir.selected.identity.lightId);
            checksum = MixChecksum(checksum, reservoir.M);
            checksum = MixChecksum(
                checksum,
                static_cast<std::uint64_t>(std::max(0.0f, Luminance(lighting.contribution)) * 1000000.0f));
        }
        const auto traceEnd = std::chrono::steady_clock::now();

        result.cpuBuildMilliseconds = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
        result.cpuTraceMilliseconds = std::chrono::duration<double, std::milli>(traceEnd - traceStart).count();
        result.candidateCount = static_cast<std::uint64_t>(tier.reservoirCount) *
            static_cast<std::uint64_t>(tier.candidatesPerReservoir);
        result.finalVisibilityRays = statistics.finalVisibilityRays;
        const std::uint64_t perReservoirBytes =
            static_cast<std::uint64_t>(tier.candidatesPerReservoir) * kProvisionalGpuCandidateBytes +
            2u * kProvisionalGpuReservoirBytes +
            kProvisionalGpuSurfaceBytes +
            kProvisionalGpuDebugBytes +
            kProvisionalGpuDirectLightingBytes;
        result.estimatedGpuBytes = static_cast<std::uint64_t>(lights.size()) * kProvisionalGpuLightBytes +
            static_cast<std::uint64_t>(tier.reservoirCount) * perReservoirBytes;
        result.deterministicChecksum = checksum;
        return result;
    }

    std::vector<BenchmarkResult> RunStandardManyLightsBenchmarks(
        const std::uint32_t reservoirCount,
        const std::uint32_t candidatesPerReservoir)
    {
        std::vector<BenchmarkResult> results;
        results.reserve(3u);
        for (const std::uint32_t lightCount : { 100u, 1000u, 10000u })
        {
            results.push_back(RunManyLightsBenchmark({
                lightCount,
                reservoirCount,
                candidatesPerReservoir
            }));
        }
        return results;
    }

    std::string FormatBenchmarkCsv(const std::span<const BenchmarkResult> results)
    {
        std::ostringstream stream;
        stream << "light_count,reservoir_count,estimator_mode,proposal,candidate_budget,visibility_budget,"
            "cpu_build_ms,cpu_trace_ms,reference_error_status,mean_absolute_error,rmse,"
            "gpu_timing_status,gpu_ms,candidate_count,visibility_rays,estimated_gpu_bytes,checksum\n";
        stream << std::fixed << std::setprecision(6);
        for (const BenchmarkResult& result : results)
        {
            stream << result.tier.lightCount << ','
                << result.tier.reservoirCount << ','
                << static_cast<std::uint32_t>(result.estimatorMode) << ','
                << static_cast<std::uint32_t>(result.proposal) << ','
                << result.candidateBudget << ','
                << result.visibilityBudget << ','
                << result.cpuBuildMilliseconds << ','
                << result.cpuTraceMilliseconds << ','
                << static_cast<std::uint32_t>(result.referenceErrorStatus) << ','
                << result.meanAbsoluteError << ','
                << result.rmse << ','
                << static_cast<std::uint32_t>(result.gpuTimingStatus) << ','
                << result.gpuMilliseconds << ','
                << result.candidateCount << ','
                << result.finalVisibilityRays << ','
                << result.estimatedGpuBytes << ','
                << result.deterministicChecksum << '\n';
        }
        return stream.str();
    }
}
