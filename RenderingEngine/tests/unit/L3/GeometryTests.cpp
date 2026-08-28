#include "rt/cpu/Geometry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace
{
    using RenderingEngine::Rt::Cpu::Aabb;
    using RenderingEngine::Rt::Cpu::Hit;
    using RenderingEngine::Rt::Cpu::IntersectAabb;
    using RenderingEngine::Rt::Cpu::IntersectTriangle;
    using RenderingEngine::Rt::Cpu::Ray;
    using RenderingEngine::Rt::Cpu::Triangle;
    using RenderingEngine::Rt::Cpu::Vec3;
}

TEMPLATE_TEST_CASE(
    "Ray and AABB honor parallel slabs and the open ray interval",
    "[l3][geometry][aabb]",
    float,
    double)
{
    const Aabb<TestType> bounds{
        Vec3<TestType>{TestType{-1}, TestType{-1}, TestType{-1}},
        Vec3<TestType>{TestType{1}, TestType{1}, TestType{1}}};

    TestType nearDistance{};
    TestType farDistance{};
    const Ray<TestType> inside{
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{0}},
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{-1}},
        TestType{0},
        TestType{10}};
    REQUIRE(IntersectAabb(inside, bounds, nearDistance, farDistance));
    REQUIRE(nearDistance == TestType{0});
    REQUIRE(farDistance == TestType{1});

    const Ray<TestType> parallelOutside{
        Vec3<TestType>{TestType{2}, TestType{0}, TestType{0}},
        Vec3<TestType>{-TestType{0}, TestType{0}, TestType{-1}},
        TestType{0},
        TestType{10}};
    REQUIRE_FALSE(IntersectAabb(parallelOutside, bounds));

    const Ray<TestType> parallelBoundary{
        Vec3<TestType>{TestType{1}, TestType{0}, TestType{2}},
        Vec3<TestType>{-TestType{0}, TestType{0}, TestType{-1}},
        TestType{0},
        TestType{10}};
    REQUIRE(IntersectAabb(parallelBoundary, bounds));

    const Ray<TestType> intervalEndsAtEntry{
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{2}},
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{-1}},
        TestType{0},
        TestType{1}};
    REQUIRE_FALSE(IntersectAabb(intervalEndsAtEntry, bounds));

    const Ray<TestType> zeroDirection{
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{0}},
        Vec3<TestType>{},
        TestType{0},
        TestType{1}};
    REQUIRE_FALSE(zeroDirection.IsValid());
    REQUIRE_FALSE(IntersectAabb(zeroDirection, bounds));

    const Ray<TestType> nonUnitDirection{
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{0}},
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{-2}},
        TestType{0},
        TestType{1}};
    REQUIRE_FALSE(nonUnitDirection.IsValid());
    REQUIRE_FALSE(IntersectAabb(nonUnitDirection, bounds));
}

TEMPLATE_TEST_CASE(
    "Watertight triangle intersection returns contract barycentrics",
    "[l3][geometry][triangle]",
    float,
    double)
{
    const Triangle<TestType> triangle{
        Vec3<TestType>{TestType{-1}, TestType{-1}, TestType{-2}},
        Vec3<TestType>{TestType{1}, TestType{-1}, TestType{-2}},
        Vec3<TestType>{TestType{-1}, TestType{1}, TestType{-2}},
        41u};
    const Ray<TestType> ray{
        Vec3<TestType>{TestType{-0.5}, TestType{-0.5}, TestType{0}},
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{-1}},
        TestType{0},
        TestType{10}};

    Hit<TestType> hit{};
    REQUIRE(IntersectTriangle(ray, triangle, hit));
    REQUIRE(hit.IsHit());
    REQUIRE(hit.primitiveId == 41u);
    REQUIRE(static_cast<double>(hit.t) == Catch::Approx(2.0));
    REQUIRE(static_cast<double>(hit.barycentric.x) == Catch::Approx(0.5));
    REQUIRE(static_cast<double>(hit.barycentric.y) == Catch::Approx(0.25));
    REQUIRE(static_cast<double>(hit.barycentric.z) == Catch::Approx(0.25));
    REQUIRE(hit.frontFace);

    Hit<TestType> endpointHit{};
    REQUIRE_FALSE(IntersectTriangle(
        Ray<TestType>{ray.origin, ray.direction, TestType{2}, TestType{10}},
        triangle,
        endpointHit));
    REQUIRE_FALSE(IntersectTriangle(
        Ray<TestType>{ray.origin, ray.direction, TestType{0}, TestType{2}},
        triangle,
        endpointHit));
}

TEMPLATE_TEST_CASE(
    "Triangle corpus covers shared edges, degeneracy, and extreme distance",
    "[l3][geometry][corpus]",
    float,
    double)
{
    const Triangle<TestType> first{
        Vec3<TestType>{TestType{-1}, TestType{-1}, TestType{-2}},
        Vec3<TestType>{TestType{1}, TestType{-1}, TestType{-2}},
        Vec3<TestType>{TestType{1}, TestType{1}, TestType{-2}},
        7u};
    const Triangle<TestType> second{
        Vec3<TestType>{TestType{-1}, TestType{-1}, TestType{-2}},
        Vec3<TestType>{TestType{1}, TestType{1}, TestType{-2}},
        Vec3<TestType>{TestType{-1}, TestType{1}, TestType{-2}},
        3u};
    const Ray<TestType> sharedEdgeRay{
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{0}},
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{-1}},
        TestType{0},
        TestType{10}};
    REQUIRE(IntersectTriangle(sharedEdgeRay, first).IsHit());
    REQUIRE(IntersectTriangle(sharedEdgeRay, second).IsHit());

    const Triangle<TestType> degenerate{
        Vec3<TestType>{TestType{0}, TestType{0}, TestType{-1}},
        Vec3<TestType>{TestType{1}, TestType{0}, TestType{-1}},
        Vec3<TestType>{TestType{2}, TestType{0}, TestType{-1}},
        99u};
    REQUIRE_FALSE(IntersectTriangle(sharedEdgeRay, degenerate).IsHit());

    constexpr TestType farDistance = TestType{1000000};
    const Triangle<TestType> farTriangle{
        Vec3<TestType>{TestType{-10}, TestType{-10}, -farDistance},
        Vec3<TestType>{TestType{10}, TestType{-10}, -farDistance},
        Vec3<TestType>{TestType{0}, TestType{10}, -farDistance},
        100u};
    const Hit<TestType> farHit = IntersectTriangle(
        Ray<TestType>{
            Vec3<TestType>{},
            Vec3<TestType>{TestType{0}, TestType{0}, TestType{-1}},
            TestType{0},
            farDistance * TestType{2}},
        farTriangle);
    REQUIRE(farHit.IsHit());
    REQUIRE(std::abs(static_cast<double>(farHit.t - farDistance)) <=
        static_cast<double>(farDistance) * 1.0e-6);
}
