#include "rt/cpu/Bvh.hpp"
#include "rt/cpu/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
    using namespace RenderingEngine::Rt::Cpu;

    [[nodiscard]] std::vector<Triangle<double>> MakeParityScene()
    {
        std::vector<Triangle<double>> triangles;
        triangles.reserve(18u);
        std::uint32_t primitiveId = 100u;
        for (std::uint32_t layer = 0u; layer < 3u; ++layer)
        {
            const double z = -2.0 - static_cast<double>(layer) * 1.5;
            for (std::uint32_t cell = 0u; cell < 3u; ++cell)
            {
                const double x = -2.2 + static_cast<double>(cell) * 2.2;
                const double y = -0.8 + static_cast<double>(layer) * 0.7;
                triangles.emplace_back(
                    Vec3<double>{x - 0.55, y - 0.45, z},
                    Vec3<double>{x + 0.55, y - 0.45, z},
                    Vec3<double>{x + 0.35, y + 0.55, z - 0.05},
                    primitiveId++);
                triangles.emplace_back(
                    Vec3<double>{x - 0.45, y - 0.35, z - 0.35},
                    Vec3<double>{x + 0.30, y + 0.50, z - 0.40},
                    Vec3<double>{x - 0.60, y + 0.40, z - 0.50},
                    primitiveId++);
            }
        }
        return triangles;
    }

    [[nodiscard]] double RelativeError(const double reference, const double candidate)
    {
        const double denominator = (std::max)(1.0, std::abs(reference));
        return std::abs(reference - candidate) / denominator;
    }

    [[nodiscard]] double MaximumBarycentricError(
        const Hit<double>& reference,
        const Hit<double>& candidate)
    {
        return (std::max)({
            std::abs(reference.barycentric.x - candidate.barycentric.x),
            std::abs(reference.barycentric.y - candidate.barycentric.y),
            std::abs(reference.barycentric.z - candidate.barycentric.z)});
    }
}

TEST_CASE("One million deterministic rays give brute median and SAH hit parity", "[l3][statistical][parity]")
{
    constexpr std::uint32_t rayCount = 1'000'000u;
    constexpr double tolerance = 1.0e-5;
    const std::vector<Triangle<double>> triangles = MakeParityScene();
    const Bvh<double> median(triangles, BvhBuildMethod::Median, 4u);
    const Bvh<double> sah(triangles, BvhBuildMethod::BinnedSah, 4u);
    Pcg32 rng(0x4c33504152495459ull, 0x524159434f525055ull);

    std::uint64_t hitMissMismatches = 0u;
    std::uint64_t idMismatches = 0u;
    std::uint64_t invalidResults = 0u;
    double maximumRelativeTError = 0.0;
    double maximumBarycentricError = 0.0;

    for (std::uint32_t rayIndex = 0u; rayIndex < rayCount; ++rayIndex)
    {
        const Vec3<double> origin{
            rng.Uniform<double>() * 10.0 - 5.0,
            rng.Uniform<double>() * 7.0 - 3.5,
            rng.Uniform<double>() * 3.0 + 0.25};
        const Vec3<double> target{
            rng.Uniform<double>() * 7.0 - 3.5,
            rng.Uniform<double>() * 4.0 - 2.0,
            -(rng.Uniform<double>() * 6.0 + 1.0)};
        const Ray<double> ray{origin, NormalizeOrZero(target - origin), 1.0e-8, 1.0e6};
        const Hit<double> brute = TraceClosestBruteForce<double>(triangles, ray);
        const Hit<double> medianHit = median.TraceClosest(ray);
        const Hit<double> sahHit = sah.TraceClosest(ray);

        if (brute.IsHit() != medianHit.IsHit() || brute.IsHit() != sahHit.IsHit())
        {
            ++hitMissMismatches;
            continue;
        }
        if (!brute.IsHit())
        {
            continue;
        }
        if (!std::isfinite(brute.t) || !std::isfinite(medianHit.t) ||
            !std::isfinite(sahHit.t))
        {
            ++invalidResults;
            continue;
        }
        if (brute.primitiveId != medianHit.primitiveId ||
            brute.primitiveId != sahHit.primitiveId)
        {
            ++idMismatches;
        }
        maximumRelativeTError = (std::max)({
            maximumRelativeTError,
            RelativeError(brute.t, medianHit.t),
            RelativeError(brute.t, sahHit.t)});
        maximumBarycentricError = (std::max)({
            maximumBarycentricError,
            MaximumBarycentricError(brute, medianHit),
            MaximumBarycentricError(brute, sahHit)});
    }

    std::cout << "L3_PARITY rays=" << rayCount
        << " hit_miss_mismatches=" << hitMissMismatches
        << " id_mismatches=" << idMismatches
        << " invalid_results=" << invalidResults
        << " max_relative_t_error=" << std::scientific << maximumRelativeTError
        << " max_barycentric_error=" << maximumBarycentricError << std::defaultfloat
        << '\n';
    REQUIRE(hitMissMismatches == 0u);
    REQUIRE(idMismatches == 0u);
    REQUIRE(invalidResults == 0u);
    REQUIRE(maximumRelativeTError <= tolerance);
    REQUIRE(maximumBarycentricError <= tolerance);
}
