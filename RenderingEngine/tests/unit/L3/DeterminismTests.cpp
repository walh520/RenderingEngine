#include "bsdf/reference_cpu/Lambert.hpp"
#include "integrators/reference_cpu/CornellReference.hpp"
#include "rt/cpu/Random.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>

namespace
{
    using RenderingEngine::Bsdf::ReferenceCpu::Lambert;
    using RenderingEngine::Integrators::ReferenceCpu::CornellReferenceOptions;
    using RenderingEngine::Integrators::ReferenceCpu::CornellReferenceResult;
    using RenderingEngine::Rt::Cpu::Pcg32;
    using RenderingEngine::Rt::Cpu::Vec3;
}

TEST_CASE("PCG and per-sample seeding are deterministic", "[l3][rng]")
{
    Pcg32 first(123456789u, 987654321u);
    Pcg32 second(123456789u, 987654321u);
    for (std::size_t index = 0u; index < 64u; ++index)
    {
        REQUIRE(first.NextUInt() == second.NextUInt());
    }

    Pcg32 sampleA = RenderingEngine::Rt::Cpu::MakeSampleRng(1u, 42u, 7u);
    Pcg32 sampleB = RenderingEngine::Rt::Cpu::MakeSampleRng(1u, 42u, 7u);
    Pcg32 adjacent = RenderingEngine::Rt::Cpu::MakeSampleRng(1u, 43u, 7u);
    REQUIRE(sampleA.NextUInt() == sampleB.NextUInt());
    REQUIRE(sampleA.NextUInt() != adjacent.NextUInt());
}

TEST_CASE("Lambert PDF integrates to one and does not add furnace energy", "[l3][bsdf]")
{
    constexpr std::uint32_t thetaSteps = 512u;
    constexpr std::uint32_t phiSteps = 512u;
    constexpr double deltaTheta = (0.5 * std::numbers::pi_v<double>) /
        static_cast<double>(thetaSteps);
    constexpr double deltaPhi = (2.0 * std::numbers::pi_v<double>) /
        static_cast<double>(phiSteps);

    const Vec3<double> normal{0.0, 1.0, 0.0};
    const Vec3<double> outgoing{0.0, 1.0, 0.0};
    const Vec3<double> reflectance{0.73, 0.41, 0.19};
    double integratedPdf = 0.0;
    Vec3<double> reflectedEnergy{};
    for (std::uint32_t thetaIndex = 0u; thetaIndex < thetaSteps; ++thetaIndex)
    {
        const double theta = (static_cast<double>(thetaIndex) + 0.5) * deltaTheta;
        const double sinTheta = std::sin(theta);
        const double cosTheta = std::cos(theta);
        for (std::uint32_t phiIndex = 0u; phiIndex < phiSteps; ++phiIndex)
        {
            const double phi = (static_cast<double>(phiIndex) + 0.5) * deltaPhi;
            const Vec3<double> incoming{
                sinTheta * std::cos(phi),
                cosTheta,
                sinTheta * std::sin(phi)};
            const double differentialSolidAngle = sinTheta * deltaTheta * deltaPhi;
            integratedPdf += Lambert::Pdf(normal, outgoing, incoming) * differentialSolidAngle;
            reflectedEnergy += Lambert::Evaluate(reflectance, normal, outgoing, incoming) *
                (cosTheta * differentialSolidAngle);
        }
    }

    REQUIRE(integratedPdf == Catch::Approx(1.0).margin(5.0e-5));
    REQUIRE(reflectedEnergy.x == Catch::Approx(reflectance.x).margin(5.0e-5));
    REQUIRE(reflectedEnergy.y == Catch::Approx(reflectance.y).margin(5.0e-5));
    REQUIRE(reflectedEnergy.z == Catch::Approx(reflectance.z).margin(5.0e-5));
}

TEST_CASE("Cornell float output is independent of worker scheduling", "[l3][render][determinism]")
{
    CornellReferenceOptions options;
    options.width = 16u;
    options.height = 16u;
    options.spp = 4u;
    options.maxBounces = 6u;
    options.seed = 0x4c335741564531ull;
    options.threads = 1u;

    CornellReferenceResult serial;
    CornellReferenceResult parallel;
    std::string error;
    REQUIRE(RenderingEngine::Integrators::ReferenceCpu::RenderCornellReference(
        options,
        serial,
        error));
    INFO(error);
    options.threads = 4u;
    REQUIRE(RenderingEngine::Integrators::ReferenceCpu::RenderCornellReference(
        options,
        parallel,
        error));
    INFO(error);

    REQUIRE(serial.sceneHash == parallel.sceneHash);
    REQUIRE(serial.rayCount == parallel.rayCount);
    REQUIRE(serial.shadowRayCount == parallel.shadowRayCount);
    REQUIRE(serial.nonFiniteCount == 0u);
    REQUIRE(parallel.nonFiniteCount == 0u);
    REQUIRE(serial.pixels == parallel.pixels);
    std::cout << "L3_DETERMINISM pixels=" << serial.pixels.size()
        << " scene_hash=0x" << std::hex << serial.sceneHash << std::dec << '\n';
}
