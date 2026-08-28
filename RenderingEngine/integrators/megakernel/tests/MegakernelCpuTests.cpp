#include "../MegakernelBridge.hpp"
#include "../include/ReferenceBsdf.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace RenderingEngine::Integrators::Megakernel;
    namespace Bsdf = RenderingEngine::Integrators::Megakernel::ReferenceBsdfL6;

    void Require(const bool condition, const char* const expression)
    {
        if (!condition)
        {
            throw std::runtime_error(expression);
        }
    }

    [[nodiscard]] bool Near(
        const double left,
        const double right,
        const double tolerance = 1.0e-10)
    {
        return std::abs(left - right) <= tolerance;
    }

    void TestAliasAndEnvironmentBuilders()
    {
        const std::array weights{ 1.0, 3.0, 0.0 };
        const std::array<std::uint32_t, 3> items{ 4u, 7u, 9u };
        const AliasTable alias = BuildVoseAliasTable(weights, items);
        Require(alias.entries.size() == weights.size(), "alias table size");
        Require(Near(alias.weightSum, 4.0), "alias table weight sum");
        Require(!alias.usedUniformFallback, "weighted alias fallback");

        double pmfSum = 0.0;
        for (const AliasEntryGpu& entry : alias.entries)
        {
            Require(entry.q >= 0.0f && entry.q <= 1.0f, "alias q range");
            Require(entry.alias < alias.entries.size(), "alias index range");
            pmfSum += entry.pmf;
        }
        Require(Near(pmfSum, 1.0, 1.0e-6), "alias PMF normalization");

        for (const float sample : { 0.0f, 0.11f, 0.49f, 0.999999f })
        {
            const AliasSample selected = SampleAliasTable(alias.entries, sample);
            Require(selected.item == 4u || selected.item == 7u, "zero-weight item selected");
            Require(selected.pmf > 0.0f, "selected alias PMF");
        }

        const std::array zeroWeights{ 0.0, 0.0, 0.0, 0.0 };
        const AliasTable uniform = BuildVoseAliasTable(zeroWeights);
        Require(uniform.usedUniformFallback, "uniform alias fallback flag");
        for (const AliasEntryGpu& entry : uniform.entries)
        {
            Require(Near(entry.pmf, 0.25, 1.0e-7), "uniform alias PMF");
        }

        const std::array<float, 8> constantEnvironment{
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const EnvironmentDistribution environment = BuildEnvironmentDistribution(
            4u, 2u, constantEnvironment);
        Require(environment.rowAlias.size() == 2u, "environment row table size");
        Require(environment.columnAlias.size() == 8u, "environment column table size");
        Require(Near(environment.integratedLuminance, 4.0 * Bsdf::kPi),
            "environment solid-angle integral");
        Require(!environment.usedUniformSphereFallback, "lit environment fallback");
    }

    void TestLightDistributionAndRng()
    {
        std::array<PbrLightGpu, 2> lights{};
        for (PbrLightGpu& light : lights)
        {
            light.identity.x = static_cast<std::uint32_t>(LightType::Point);
            light.identity.y = LightFlagEnabled | LightFlagDelta;
            light.radianceScale = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        const TopLevelLightDistribution distribution = BuildTopLevelLightDistribution(
            lights, LightProposal::Uniform, 10.0f, 0.0);
        Require(distribution.alias.size() == 2u, "top-level light table size");
        Require(distribution.selectionPmfByLight.size() == 2u,
            "top-level light PMF map size");
        Require(Near(distribution.selectionPmfByLight[0], 0.5, 1.0e-7),
            "first light PMF");
        Require(Near(distribution.selectionPmfByLight[1], 0.5, 1.0e-7),
            "second light PMF");

        const auto first = Philox4x32TenRounds(
            { 1u, 2u, 3u, 4u }, { 5u, 6u });
        const auto repeated = Philox4x32TenRounds(
            { 1u, 2u, 3u, 4u }, { 5u, 6u });
        const auto changed = Philox4x32TenRounds(
            { 1u, 2u, 3u, 5u }, { 5u, 6u });
        const auto knownAnswer = Philox4x32TenRounds(
            { 0u, 0u, 0u, 0u }, { 0u, 0u });
        Require(first == repeated, "Philox determinism");
        Require(first != changed, "Philox counter separation");
        Require(knownAnswer == std::array<std::uint32_t, 4>{
            0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u },
            "Random123 Philox4x32-10 known-answer vector");
        Require(Uint32ToUnitFloat(0u) == 0.0f,
            "counter RNG lower endpoint");
        Require(Uint32ToUnitFloat(0xffffffffu) < 1.0f,
            "counter RNG upper endpoint");

        const float randomA = CounterRandomFloat(13u, 7u, 11u, 3u, 0x123456789abcdef0ull);
        const float randomB = CounterRandomFloat(13u, 7u, 11u, 3u, 0x123456789abcdef0ull);
        const float randomC = CounterRandomFloat(13u, 7u, 12u, 3u, 0x123456789abcdef0ull);
        Require(randomA >= 0.0f && randomA < 1.0f, "counter RNG range");
        Require(randomA == randomB, "counter RNG determinism");
        Require(randomA != randomC, "counter RNG dimension separation");
        Require(BounceSampleDimension(3u, BounceDimension::BsdfU)
            == kCameraSampleDimensionCount + 3u * kDimensionsPerBounce + 6u,
            "L6 RNG dimension mapping");
    }

    void TestFrameValidation()
    {
        MegakernelFrameConstantsGpu frame{};
        frame.image = { 64u, 32u, 0u, 8u };
        frame.sampling = {
            1u, 2u, static_cast<std::uint32_t>(LightProposal::Power), 1u };
        frame.russianRoulette = { 3.0f, 0.05f, 0.95f, 0.001f };
        Require(ValidateMegakernelFrameConstants(frame),
            "valid megakernel frame");

        frame.image.w = 0u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "zero maximum depth rejected");
        frame.image.w = 8u;
        frame.russianRoulette.y = 0.0f;
        Require(!ValidateMegakernelFrameConstants(frame),
            "zero roulette minimum rejected");
        frame.russianRoulette.y = 0.05f;
        frame.russianRoulette.w = 0.0f;
        Require(!ValidateMegakernelFrameConstants(frame),
            "zero ray epsilon rejected");
    }

    void TestBsdfApiInvariants()
    {
        const Bsdf::Vec3 uniformDirection = Bsdf::SampleUniformHemisphere(0.25, 0.75);
        Require(Near(Bsdf::Dot(uniformDirection, uniformDirection), 1.0, 1.0e-12),
            "uniform hemisphere unit direction");
        Require(Near(uniformDirection.z, 0.25),
            "uniform hemisphere cosine mapping");
        Require(Near(Bsdf::UniformHemispherePdf(uniformDirection),
            0.5 * Bsdf::kInversePi),
            "uniform hemisphere PDF");

        Bsdf::BsdfContextL6 context{};
        context.geometricNormal = { 0.0, 0.0, 1.0 };
        context.shadingNormal = { 0.0, 0.0, 1.0 };
        context.tangent = { 1.0, 0.0, 0.0 };
        context.etaIncident = 1.0;
        context.etaTransmitted = 1.5;
        context.transportMode = Bsdf::kTransportRadiance;
        const Bsdf::Vec3 outgoing{ 0.0, 0.0, 1.0 };

        Bsdf::BsdfParamsL6 lambert{};
        lambert.model = Bsdf::kModelLambert;
        lambert.baseColor = { 0.8, 0.2, 0.1 };
        const Bsdf::BsdfEvalL6 lambertEval = Bsdf::EvaluateBsdfL6(
            context, lambert, outgoing, { 0.0, 0.0, 1.0 });
        Require(lambertEval.isValid != 0u, "Lambert Evaluate validity");
        Require(lambertEval.measure == Bsdf::kMeasureSolidAngle,
            "Lambert measure");
        Require(Near(lambertEval.pdf, Bsdf::kInversePi), "Lambert PDF");
        Require(Near(lambertEval.value.x, 0.8 * Bsdf::kInversePi),
            "Lambert value");
        Require(Near(lambertEval.diffuseValue.x + lambertEval.specularValue.x,
            lambertEval.value.x), "Lambert signal split");

        const Bsdf::BsdfSampleL6 lambertSample = Bsdf::SampleBsdfL6(
            context, lambert, outgoing, { 0.2, 0.3, 0.7 });
        Require(lambertSample.isValid != 0u, "Lambert Sample validity");
        Require(Near(lambertSample.pdf, Bsdf::PdfBsdfL6(
            context, lambert, outgoing, lambertSample.direction)),
            "Lambert Sample/PDF consistency");

        Bsdf::BsdfParamsL6 conductor = lambert;
        conductor.model = Bsdf::kModelGgxConductor;
        conductor.f0 = { 0.9, 0.7, 0.5 };
        conductor.perceptualRoughness = 0.45;
        const Bsdf::BsdfSampleL6 conductorSample = Bsdf::SampleBsdfL6(
            context, conductor, outgoing, { 0.25, 0.37, 0.73 });
        Require(conductorSample.isValid != 0u, "GGX conductor Sample validity");
        Require(Near(conductorSample.pdf, Bsdf::PdfBsdfL6(
            context, conductor, outgoing, conductorSample.direction), 1.0e-9),
            "GGX conductor Sample/PDF consistency");

        Bsdf::BsdfParamsL6 glass = lambert;
        glass.model = Bsdf::kModelSmoothGlass;
        glass.baseColor = { 1.0, 1.0, 1.0 };
        glass.transmission = 1.0;
        const Bsdf::BsdfSampleL6 reflected = Bsdf::SampleBsdfL6(
            context, glass, outgoing, { 0.0, 0.2, 0.8 });
        const Bsdf::BsdfSampleL6 transmitted = Bsdf::SampleBsdfL6(
            context, glass, outgoing, { 0.5, 0.2, 0.8 });
        Require(reflected.isValid != 0u && reflected.isDelta != 0u,
            "smooth-glass reflection delta");
        Require(transmitted.isValid != 0u && transmitted.isDelta != 0u,
            "smooth-glass transmission delta");
        Require(reflected.measure == Bsdf::kMeasureDiscrete
            && transmitted.measure == Bsdf::kMeasureDiscrete,
            "smooth-glass discrete measure");
        Require((reflected.lobeFlags & Bsdf::kLobeReflection) != 0u,
            "smooth-glass reflection event");
        Require((transmitted.lobeFlags & Bsdf::kLobeTransmission) != 0u,
            "smooth-glass transmission event");
        Require(reflected.direction.z > 0.0 && transmitted.direction.z < 0.0,
            "smooth-glass event hemispheres");
        Require(Bsdf::PdfBsdfL6(context, glass, outgoing, reflected.direction) == 0.0,
            "delta finite-direction PDF");

        Bsdf::BsdfParamsL6 roughGlass = glass;
        roughGlass.model = Bsdf::kModelRoughDielectric;
        roughGlass.perceptualRoughness = 0.35;
        const Bsdf::BsdfEvalL6 roughTransmission = Bsdf::EvaluateBsdfL6(
            context, roughGlass, outgoing, { 0.0, 0.0, -1.0 });
        Require(roughTransmission.isValid != 0u, "rough dielectric transmission Evaluate");
        Require(roughTransmission.measure == Bsdf::kMeasureSolidAngle,
            "rough dielectric measure");

        roughGlass.perceptualRoughness = 0.001;
        Require(Bsdf::EvaluateBsdfL6(
            context, roughGlass, outgoing, { 0.0, 0.0, -1.0 }).isValid == 0u,
            "sub-threshold roughness is explicit invalid");
    }
}

int main()
{
    try
    {
        TestAliasAndEnvironmentBuilders();
        TestLightDistributionAndRng();
        TestFrameValidation();
        TestBsdfApiInvariants();
        std::cout << "L6 megakernel CPU self-tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "L6 megakernel CPU self-tests failed: " << exception.what() << '\n';
        return 1;
    }
}
