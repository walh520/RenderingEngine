#include "rt/cpu/Bvh.hpp"
#include "rt/cpu/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    using namespace RenderingEngine::Rt::Cpu;

    [[nodiscard]] std::vector<Triangle<float>> MakeFloatTestScene()
    {
        return {
            {{-1.0f, -1.0f, -2.0f}, {1.0f, -1.0f, -2.0f}, {1.0f, 1.0f, -2.0f}, 17u},
            {{-1.0f, -1.0f, -2.0f}, {1.0f, 1.0f, -2.0f}, {-1.0f, 1.0f, -2.0f}, 11u},
            {{-0.5f, -0.5f, -4.0f}, {0.5f, -0.5f, -4.0f}, {0.0f, 0.5f, -4.0f}, 29u},
            {{2.0f, -1.0f, -3.0f}, {3.0f, -1.0f, -3.0f}, {2.5f, 1.0f, -3.0f}, 31u},
            {{-3.0f, -1.0f, -3.0f}, {-2.0f, -1.0f, -3.0f}, {-2.5f, 1.0f, -3.0f}, 37u},
        };
    }
}

TEST_CASE("Median and binned SAH preserve stable closest-hit semantics", "[l3][bvh]")
{
    const std::vector<Triangle<float>> triangles = MakeFloatTestScene();
    const Bvh<float> median(triangles, BvhBuildMethod::Median, 2u);
    const Bvh<float> sah(triangles, BvhBuildMethod::BinnedSah, 2u);
    const Ray<float> sharedEdgeRay{
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        0.0f,
        100.0f};

    const Hit<float> brute = TraceClosestBruteForce<float>(triangles, sharedEdgeRay);
    REQUIRE(brute.IsHit());
    REQUIRE(brute.primitiveId == 11u);
    REQUIRE(median.TraceClosest(sharedEdgeRay).primitiveId == brute.primitiveId);
    REQUIRE(sah.TraceClosest(sharedEdgeRay).primitiveId == brute.primitiveId);
    REQUIRE(median.TraceAny(sharedEdgeRay));
    REQUIRE(sah.TraceAny(sharedEdgeRay));
    REQUIRE(median.PrimitiveCount() == triangles.size());
    REQUIRE(sah.PrimitiveCount() == triangles.size());
    REQUIRE(median.NodeCount() > 1u);
    REQUIRE(sah.NodeCount() > 1u);
}

TEST_CASE("Binned SAH is deterministic and handles coincident centroids", "[l3][bvh]")
{
    std::vector<Triangle<double>> triangles;
    triangles.reserve(64u);
    for (std::uint32_t primitive = 0u; primitive < 64u; ++primitive)
    {
        const double scale = 0.05 + static_cast<double>(primitive) * 0.002;
        triangles.emplace_back(
            Vec3<double>{-scale, -scale, -2.0},
            Vec3<double>{scale, -scale, -2.0},
            Vec3<double>{0.0, scale * 2.0, -2.0},
            1000u - primitive);
    }

    const Bvh<double> first(triangles, BvhBuildMethod::BinnedSah, 2u);
    const Bvh<double> second(triangles, BvhBuildMethod::BinnedSah, 2u);
    REQUIRE(first.PrimitiveCount() == 64u);
    REQUIRE(first.NodeCount() <= 127u);
    REQUIRE(first.MaximumDepth() <= 7u);
    REQUIRE(first.PrimitiveOrder().size() == second.PrimitiveOrder().size());
    REQUIRE(std::equal(
        first.PrimitiveOrder().begin(),
        first.PrimitiveOrder().end(),
        second.PrimitiveOrder().begin()));
}

TEST_CASE("Float SAH matches brute force for a deterministic smoke corpus", "[l3][bvh]")
{
    const std::vector<Triangle<float>> triangles = MakeFloatTestScene();
    const Bvh<float> sah(triangles, BvhBuildMethod::BinnedSah, 2u);
    Pcg32 rng(0x4c33534d4f4b4555ull, 0x46524159434f5250ull);

    std::uint64_t mismatches = 0u;
    for (std::uint32_t rayIndex = 0u; rayIndex < 10000u; ++rayIndex)
    {
        const Vec3<float> origin{
            rng.Uniform<float>() * 8.0f - 4.0f,
            rng.Uniform<float>() * 6.0f - 3.0f,
            rng.Uniform<float>() * 2.0f + 0.5f};
        const Vec3<float> target{
            rng.Uniform<float>() * 6.0f - 3.0f,
            rng.Uniform<float>() * 4.0f - 2.0f,
            -(rng.Uniform<float>() * 4.0f + 1.0f)};
        const Ray<float> ray{origin, NormalizeOrZero(target - origin), 1.0e-5f, 1000.0f};
        const Hit<float> brute = TraceClosestBruteForce<float>(triangles, ray);
        const Hit<float> accelerated = sah.TraceClosest(ray);
        if (brute.IsHit() != accelerated.IsHit() ||
            (brute.IsHit() && brute.primitiveId != accelerated.primitiveId))
        {
            ++mismatches;
        }
    }
    REQUIRE(mismatches == 0u);
}
