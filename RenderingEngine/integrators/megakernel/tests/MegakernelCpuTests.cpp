#include "../MegakernelBridge.hpp"
#include "../include/ReferenceBsdf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
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

        constexpr std::uint32_t aliasSampleCount = 131072u;
        std::array<std::uint32_t, 3u> weightedCounts{};
        std::array<std::uint32_t, 4u> uniformCounts{};
        std::array<std::uint32_t, 8u> environmentCounts{};
        for (std::uint32_t sampleIndex = 0u; sampleIndex < aliasSampleCount; ++sampleIndex)
        {
            const float selection = CounterRandomFloat(
                2u, sampleIndex, 0u, 0x57a20000u, 0x2468ace013579bdfull);
            const AliasSample weightedSample = SampleAliasTable(alias.entries, selection);
            const std::size_t weightedBin = weightedSample.item == 4u
                ? 0u : (weightedSample.item == 7u ? 1u : 2u);
            ++weightedCounts[weightedBin];

            const float uniformSelection = CounterRandomFloat(
                2u, sampleIndex, 1u, 0x57a20000u, 0x2468ace013579bdfull);
            const AliasSample uniformSample = SampleAliasTable(uniform.entries, uniformSelection);
            Require(uniformSample.item < uniformCounts.size(), "uniform alias item range");
            ++uniformCounts[uniformSample.item];

            const float rowSelection = CounterRandomFloat(
                2u, sampleIndex, 2u, 0x57a20000u, 0x2468ace013579bdfull);
            const float columnSelection = CounterRandomFloat(
                2u, sampleIndex, 3u, 0x57a20000u, 0x2468ace013579bdfull);
            const AliasSample row = SampleAliasTable(environment.rowAlias, rowSelection);
            Require(row.item < environment.height, "environment row alias range");
            const std::span<const AliasEntryGpu> columns{
                environment.columnAlias.data() + row.item * environment.width,
                environment.width };
            const AliasSample column = SampleAliasTable(columns, columnSelection);
            Require(column.item < environment.width, "environment column alias range");
            ++environmentCounts[row.item * environment.width + column.item];
        }
        const auto ChiSquare = [](const auto& observed, const auto& expected)
        {
            double value = 0.0;
            for (std::size_t index = 0u; index < observed.size(); ++index)
            {
                if (expected[index] > 0.0)
                {
                    const double difference = static_cast<double>(observed[index]) - expected[index];
                    value += difference * difference / expected[index];
                }
                else
                {
                    Require(observed[index] == 0u, "zero-probability alias item selected");
                }
            }
            return value;
        };
        const std::array weightedExpected{
            aliasSampleCount * 0.25, aliasSampleCount * 0.75, 0.0 };
        const std::array uniformExpected{
            aliasSampleCount * 0.25, aliasSampleCount * 0.25,
            aliasSampleCount * 0.25, aliasSampleCount * 0.25 };
        std::array<double, 8u> environmentExpected{};
        environmentExpected.fill(aliasSampleCount / 8.0);
        Require(ChiSquare(weightedCounts, weightedExpected) <= 20.0,
            "weighted alias chi-square gate");
        Require(ChiSquare(uniformCounts, uniformExpected) <= 25.0,
            "uniform alias chi-square gate");
        Require(ChiSquare(environmentCounts, environmentExpected) <= 35.0,
            "environment alias chi-square gate");
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
            lights, LightSelection::Uniform, 10.0f, 0.0);
        Require(distribution.alias.size() == 2u, "top-level light table size");
        Require(distribution.selectionPmfByLight.size() == 2u,
            "top-level light PMF map size");
        Require(Near(distribution.selectionPmfByLight[0], 0.5, 1.0e-7),
            "first light PMF");
        Require(Near(distribution.selectionPmfByLight[1], 0.5, 1.0e-7),
            "second light PMF");

        lights[1].radianceScale = { 4.0f, 4.0f, 4.0f, 1.0f };
        const TopLevelLightDistribution powerDistribution =
            BuildTopLevelLightDistribution(
                lights, LightSelection::PowerWeighted, 10.0f, 0.0);
        Require(powerDistribution.selectionPmfByLight[1]
                > powerDistribution.selectionPmfByLight[0],
            "power-weighted selection favors the brighter light");

        std::array<PbrLightGpu, 3> emitterLights{};
        emitterLights[0].identity = {
            static_cast<std::uint32_t>(LightType::EmissiveTriangle),
            LightFlagEnabled, 17u, 9u};
        emitterLights[0].payload.w = 5u;
        emitterLights[1].identity = {
            static_cast<std::uint32_t>(LightType::Point),
            LightFlagEnabled | LightFlagDelta, 18u, kInvalidIndex};
        emitterLights[2].identity = {
            static_cast<std::uint32_t>(LightType::EmissiveTriangle),
            LightFlagEnabled, 19u, 9u};
        emitterLights[2].payload.w = 2u;
        const std::vector<EmitterMapEntryGpu> emitterMap = BuildEmitterMap(emitterLights);
        Require(emitterMap.size() == 2u, "emitter map includes finite triangle emitters");
        Require(emitterMap[0].instanceId == 2u && emitterMap[0].primitiveId == 9u &&
                emitterMap[0].lightIndex == 2u,
            "emitter map lexicographic first key");
        Require(emitterMap[1].instanceId == 5u && emitterMap[1].primitiveId == 9u &&
                emitterMap[1].lightIndex == 0u,
            "same primitive ID remains distinct across instances");

        emitterLights[2].payload.w = 5u;
        bool duplicateRejected = false;
        try
        {
            (void)BuildEmitterMap(emitterLights);
        }
        catch (const std::invalid_argument&)
        {
            duplicateRejected = true;
        }
        Require(duplicateRejected, "duplicate emitter instance/primitive key rejected");

        emitterLights[2].payload.w = kInvalidIndex;
        bool invalidIdentityRejected = false;
        try
        {
            (void)BuildEmitterMap(emitterLights);
        }
        catch (const std::invalid_argument&)
        {
            invalidIdentityRejected = true;
        }
        Require(invalidIdentityRejected, "invalid emitter instance identity rejected");

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
            1u, 2u, static_cast<std::uint32_t>(LightSelection::PowerWeighted),
            static_cast<std::uint32_t>(DirectLightingEstimator::Mis) };
        frame.environment.w = static_cast<std::uint32_t>(
            EnvironmentSampler::ImportanceMap);
        frame.russianRoulette = { 3.0f, 0.05f, 0.95f, 0.001f };
        Require(ValidateMegakernelFrameConstants(frame),
            "valid megakernel frame");

        frame.sampling.z = static_cast<std::uint32_t>(LightSelection::Uniform);
        frame.sampling.w = static_cast<std::uint32_t>(DirectLightingEstimator::NextEventEstimation);
        Require(ValidateMegakernelFrameConstants(frame),
            "light selection and estimator are independently selectable");
        frame.sampling.w = static_cast<std::uint32_t>(DirectLightingEstimator::BsdfOnly);
        Require(ValidateMegakernelFrameConstants(frame),
            "BSDF-only estimator frame");
        frame.environment.w = static_cast<std::uint32_t>(
            EnvironmentSampler::UniformSphere);
        Require(ValidateMegakernelFrameConstants(frame),
            "uniform-sphere environment sampler frame");
        frame.sampling.z = 2u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "unknown light-selection strategy rejected");
        frame.sampling.z = static_cast<std::uint32_t>(LightSelection::Uniform);
        frame.environment.w = 2u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "unknown environment sampler rejected");
        frame.environment.w = static_cast<std::uint32_t>(
            EnvironmentSampler::UniformSphere);
        frame.sampling.w = static_cast<std::uint32_t>(
            DirectLightingEstimator::RestirPrimary);
        Require(ValidateMegakernelFrameConstants(frame),
            "ReSTIR primary direct-ownership frame");
        frame.sampling.w = 4u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "unknown direct-light estimator rejected");
        frame.sampling.w = static_cast<std::uint32_t>(DirectLightingEstimator::Mis);

        frame.traversal = {
            static_cast<std::uint32_t>(TraversalBackend::Fixture), 0u, 0u, 0u };
        Require(ValidateMegakernelFrameConstants(frame),
            "fixture traversal frame");
        frame.traversal = {
            static_cast<std::uint32_t>(TraversalBackend::FlattenedSah), 7u, 4u, 0u };
        Require(ValidateMegakernelFrameConstants(frame),
            "flattened-SAH traversal frame with explicit counts");
        frame.traversal.y = 0u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "flattened-SAH traversal rejects a zero node count");
        frame.traversal = {
            static_cast<std::uint32_t>(TraversalBackend::HardwareRayQuery), 0u, 0u, 1u };
        Require(ValidateMegakernelFrameConstants(frame),
            "Ray Query traversal frame");
        frame.traversal = {
            static_cast<std::uint32_t>(TraversalBackend::CanonicalLinear),
            0u, 4u, 0u };
        Require(ValidateMegakernelFrameConstants(frame),
            "canonical linear traversal frame");
        frame.traversal.x = 4u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "unknown traversal backend rejected");
        frame.traversal = {};

        frame.image.w = 0u;
        Require(!ValidateMegakernelFrameConstants(frame),
            "zero maximum depth rejected");
        frame.image.w = 8u;
        frame.image.z = std::numeric_limits<std::uint32_t>::max();
        Require(!ValidateMegakernelFrameConstants(frame),
            "sample index that wraps the online-mean divisor is rejected");
        frame.image.z = 0u;
        frame.russianRoulette.y = 0.0f;
        Require(!ValidateMegakernelFrameConstants(frame),
            "zero roulette minimum rejected");
        frame.russianRoulette.y = 0.05f;
        frame.russianRoulette.w = 0.0f;
        Require(!ValidateMegakernelFrameConstants(frame),
            "zero ray epsilon rejected");
    }

    void TestEstimatorAndAbiMappings()
    {
        using AbiMeasure = RenderingEngine::Contracts::AbiV1::SampleMeasure;

        Require(static_cast<std::uint32_t>(DirectLightingEstimator::BsdfOnly) == 0u,
            "BSDF-only estimator value");
        Require(static_cast<std::uint32_t>(DirectLightingEstimator::NextEventEstimation) == 1u,
            "NEE estimator value");
        Require(static_cast<std::uint32_t>(DirectLightingEstimator::Mis) == 2u,
            "MIS estimator value");
        Require(static_cast<std::uint32_t>(DirectLightingEstimator::RestirPrimary) == 3u,
            "ReSTIR primary ownership estimator value");

        Require(ToAbiSampleMeasure(PrivateLightSampleMeasure::Discrete)
                == RenderingEngine::Contracts::AbiV1::SampleMeasureDiscrete,
            "private light discrete measure mapping");
        Require(ToAbiSampleMeasure(PrivateLightSampleMeasure::Area)
                == RenderingEngine::Contracts::AbiV1::SampleMeasureArea,
            "private light area measure mapping");
        Require(ToAbiSampleMeasure(PrivateLightSampleMeasure::SolidAngle)
                == RenderingEngine::Contracts::AbiV1::SampleMeasureSolidAngle,
            "private light solid-angle measure mapping");
        Require(FromAbiSampleMeasure(
                    RenderingEngine::Contracts::AbiV1::SampleMeasureDiscrete)
                == PrivateLightSampleMeasure::Discrete,
            "ABI light discrete measure mapping");
        Require(FromAbiSampleMeasure(
                    RenderingEngine::Contracts::AbiV1::SampleMeasureArea)
                == PrivateLightSampleMeasure::Area,
            "ABI light area measure mapping");
        Require(FromAbiSampleMeasure(
                    RenderingEngine::Contracts::AbiV1::SampleMeasureSolidAngle)
                == PrivateLightSampleMeasure::SolidAngle,
            "ABI light solid-angle measure mapping");
        Require(ToAbiSampleMeasure(PrivateBsdfMeasure::SolidAngle)
                == RenderingEngine::Contracts::AbiV1::SampleMeasureSolidAngle,
            "private BSDF solid-angle measure mapping");
        Require(ToAbiSampleMeasure(PrivateBsdfMeasure::Discrete)
                == RenderingEngine::Contracts::AbiV1::SampleMeasureDiscrete,
            "private BSDF discrete measure mapping");
        Require(FromAbiBsdfSampleMeasure(
                    RenderingEngine::Contracts::AbiV1::SampleMeasureSolidAngle)
                == PrivateBsdfMeasure::SolidAngle,
            "ABI BSDF solid-angle measure mapping");
        Require(FromAbiBsdfSampleMeasure(
                    RenderingEngine::Contracts::AbiV1::SampleMeasureDiscrete)
                == PrivateBsdfMeasure::Discrete,
            "ABI BSDF discrete measure mapping");
        Require(ToAbiSampleMeasure(PrivateLightSampleMeasure::Invalid) ==
                RenderingEngine::Contracts::AbiV1::SampleMeasureInvalid,
            "invalid private light measure mapping");
        Require(FromAbiSampleMeasure(
                    RenderingEngine::Contracts::AbiV1::SampleMeasureInvalid) ==
                PrivateLightSampleMeasure::Invalid,
            "invalid ABI light measure mapping");

        Require(static_cast<std::uint32_t>(Counter::InvalidMaterial) == 22u,
            "invalid-material counter slot");
        Require(static_cast<std::uint32_t>(Counter::InvalidBsdfSample) == 24u,
            "invalid-BSDF-sample counter slot");
        Require(static_cast<std::uint32_t>(Counter::Count) == 26u,
            "counter ABI count");

        const EmitterHitWeightResult bsdfOnly = ComputeEmitterHitWeight(
            DirectLightingEstimator::BsdfOnly,
            EmitterHitKind::NonDeltaBsdf,
            0.25f,
            0.75f);
        Require(bsdfOnly.valid && Near(bsdfOnly.weight, 1.0),
            "BSDF-only keeps non-delta emitter hits");

        const EmitterHitWeightResult nee = ComputeEmitterHitWeight(
            DirectLightingEstimator::NextEventEstimation,
            EmitterHitKind::NonDeltaBsdf,
            0.25f,
            0.75f);
        Require(nee.valid && Near(nee.weight, 0.0),
            "NEE suppresses non-delta emitter hits");

        const EmitterHitWeightResult mis = ComputeEmitterHitWeight(
            DirectLightingEstimator::Mis,
            EmitterHitKind::NonDeltaBsdf,
            0.25f,
            0.75f);
        Require(mis.valid && Near(mis.weight, 0.1, 1.0e-6),
            "MIS applies power heuristic to non-delta emitter hits");

        const EmitterHitWeightResult restir = ComputeEmitterHitWeight(
            DirectLightingEstimator::RestirPrimary,
            EmitterHitKind::NonDeltaBsdf,
            0.25f,
            0.75f);
        Require(restir.valid && Near(restir.weight, 0.0),
            "ReSTIR suppresses the primary non-delta emitter hit");

        for (const DirectLightingEstimator estimator : {
                 DirectLightingEstimator::BsdfOnly,
                 DirectLightingEstimator::NextEventEstimation,
                 DirectLightingEstimator::Mis,
                 DirectLightingEstimator::RestirPrimary })
        {
            Require(ComputeEmitterHitWeight(
                        estimator, EmitterHitKind::Camera).weight == 1.0f,
                "camera emitter hits stay visible");
            Require(ComputeEmitterHitWeight(
                        estimator, EmitterHitKind::DeltaBsdf).weight == 1.0f,
                "delta emitter hits stay visible");
        }

        const EmitterHitWeightResult badPdf = ComputeEmitterHitWeight(
            DirectLightingEstimator::Mis,
            EmitterHitKind::NonDeltaBsdf,
            std::numeric_limits<float>::quiet_NaN(),
            0.5f);
        Require(!badPdf.valid && badPdf.weight == 0.0f,
            "non-finite MIS PDF is rejected");
        const EmitterHitWeightResult negativePdf = ComputeEmitterHitWeight(
            DirectLightingEstimator::Mis,
            EmitterHitKind::NonDeltaBsdf,
            -0.5f,
            0.5f);
        Require(!negativePdf.valid && negativePdf.weight == 0.0f,
            "negative MIS PDF is rejected");
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

    void TestSamplingStatisticsAndEnergy()
    {
        Bsdf::BsdfContextL6 context{};
        context.geometricNormal = { 0.0, 0.0, 1.0 };
        context.shadingNormal = context.geometricNormal;
        context.tangent = { 1.0, 0.0, 0.0 };
        context.etaIncident = 1.0;
        context.etaTransmitted = 1.5;
        context.transportMode = Bsdf::kTransportRadiance;
        const Bsdf::Vec3 outgoing{ 0.0, 0.0, 1.0 };

        Bsdf::BsdfParamsL6 lambert{};
        lambert.model = Bsdf::kModelLambert;
        lambert.baseColor = { 0.8, 0.35, 0.1 };
        constexpr std::uint32_t cosineBins = 16u;
        constexpr std::uint32_t cosineSamples = 131072u;
        std::array<std::uint32_t, cosineBins> histogram{};
        for (std::uint32_t sampleIndex = 0u; sampleIndex < cosineSamples; ++sampleIndex)
        {
            const double u0 = CounterRandomFloat(
                0u, sampleIndex,
                BounceSampleDimension(0u, BounceDimension::BsdfU),
                0x57a20002u, 0x2468ace013579bdfull);
            const double u1 = CounterRandomFloat(
                0u, sampleIndex,
                BounceSampleDimension(0u, BounceDimension::BsdfV),
                0x57a20002u, 0x2468ace013579bdfull);
            const Bsdf::BsdfSampleL6 sampled = Bsdf::SampleBsdfL6(
                context, lambert, outgoing, { 0.5, u0, u1 });
            Require(sampled.isValid != 0u, "Lambert statistical sample validity");
            Require(Near(sampled.pdf, Bsdf::PdfBsdfL6(
                context, lambert, outgoing, sampled.direction), 1.0e-12),
                "Lambert statistical Sample/PDF consistency");
            const std::size_t bin = std::min<std::size_t>(
                static_cast<std::size_t>(sampled.direction.z * cosineBins),
                cosineBins - 1u);
            ++histogram[bin];
        }
        double cosineChiSquare = 0.0;
        for (std::uint32_t bin = 0u; bin < cosineBins; ++bin)
        {
            const double low = static_cast<double>(bin) / cosineBins;
            const double high = static_cast<double>(bin + 1u) / cosineBins;
            const double expected = cosineSamples * (high * high - low * low);
            const double difference = static_cast<double>(histogram[bin]) - expected;
            cosineChiSquare += difference * difference / expected;
        }
        Require(cosineChiSquare <= 45.0,
            "Lambert cosine-hemisphere chi-square gate");

        constexpr std::uint32_t zSteps = 192u;
        constexpr std::uint32_t phiSteps = 384u;
        constexpr double dOmega = 2.0 * Bsdf::kPi /
            static_cast<double>(zSteps * phiSteps);
        double lambertPdfIntegral = 0.0;
        Bsdf::Vec3 lambertFurnace{};
        for (std::uint32_t zIndex = 0u; zIndex < zSteps; ++zIndex)
        {
            const double z = (static_cast<double>(zIndex) + 0.5) / zSteps;
            for (std::uint32_t phiIndex = 0u; phiIndex < phiSteps; ++phiIndex)
            {
                const double phiSample =
                    (static_cast<double>(phiIndex) + 0.5) / phiSteps;
                const Bsdf::Vec3 incoming =
                    Bsdf::SampleUniformHemisphere(z, phiSample);
                const Bsdf::BsdfEvalL6 evaluation = Bsdf::EvaluateBsdfL6(
                    context, lambert, outgoing, incoming);
                Require(evaluation.isValid != 0u,
                    "Lambert quadrature evaluation validity");
                lambertPdfIntegral += evaluation.pdf * dOmega;
                lambertFurnace = lambertFurnace +
                    evaluation.value * (incoming.z * dOmega);
            }
        }
        Require(Near(lambertPdfIntegral, 1.0, 5.0e-4),
            "Lambert PDF hemisphere normalization");
        Require(Near(lambertFurnace.x, lambert.baseColor.x, 5.0e-4) &&
                Near(lambertFurnace.y, lambert.baseColor.y, 5.0e-4) &&
                Near(lambertFurnace.z, lambert.baseColor.z, 5.0e-4),
            "Lambert white-furnace response");

        // For VNDF-based finite BSDFs, the integral of Pdf over the sphere is
        // the valid-direction probability; below-macrosurface reflections are
        // represented by the remaining zero-contribution mass. Compare that
        // independent quadrature against empirical samples and two bounded
        // moments at six standard errors.
        struct StatisticalMaterial final
        {
            Bsdf::BsdfParamsL6 parameters;
        };
        std::array<StatisticalMaterial, 4u> statisticalMaterials{};
        statisticalMaterials[0].parameters.model = Bsdf::kModelGgxConductor;
        statisticalMaterials[0].parameters.f0 = { 0.9, 0.7, 0.5 };
        statisticalMaterials[0].parameters.perceptualRoughness = 0.45;
        statisticalMaterials[1].parameters.model = Bsdf::kModelGgxDielectricReflection;
        statisticalMaterials[1].parameters.perceptualRoughness = 0.45;
        statisticalMaterials[2].parameters.model = Bsdf::kModelRoughDielectric;
        statisticalMaterials[2].parameters.baseColor = { 1.0, 1.0, 1.0 };
        statisticalMaterials[2].parameters.transmission = 0.85;
        statisticalMaterials[2].parameters.perceptualRoughness = 0.45;
        statisticalMaterials[3].parameters.model = Bsdf::kModelMetallicRoughness;
        statisticalMaterials[3].parameters.baseColor = { 0.8, 0.6, 0.3 };
        statisticalMaterials[3].parameters.f0 = { 0.04, 0.04, 0.04 };
        statisticalMaterials[3].parameters.metallic = 0.35;
        statisticalMaterials[3].parameters.perceptualRoughness = 0.45;

        // Rough transmission can concentrate very close to z=-1 even at a
        // moderate microfacet roughness. Uniform-z quadrature is uniform in
        // solid angle, so spend resolution on z and only four azimuth samples
        // (the normal-incidence test cases are isotropic).
        constexpr std::uint32_t sphereZSteps = 65536u;
        constexpr std::uint32_t spherePhiSteps = 4u;
        constexpr double sphereDOmega = 4.0 * Bsdf::kPi /
            static_cast<double>(sphereZSteps * spherePhiSteps);
        constexpr std::uint32_t distributionSamples = 131072u;
        for (std::size_t materialIndex = 0u;
             materialIndex < statisticalMaterials.size(); ++materialIndex)
        {
            const Bsdf::BsdfParamsL6& material = statisticalMaterials[materialIndex].parameters;
            double pdfMass = 0.0;
            double expectedZ = 0.0;
            double expectedZ2 = 0.0;
            for (std::uint32_t zIndex = 0u; zIndex < sphereZSteps; ++zIndex)
            {
                const double z = -1.0 + 2.0 *
                    (static_cast<double>(zIndex) + 0.5) / sphereZSteps;
                const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
                for (std::uint32_t phiIndex = 0u; phiIndex < spherePhiSteps; ++phiIndex)
                {
                    const double phi = 2.0 * Bsdf::kPi *
                        (static_cast<double>(phiIndex) + 0.5) / spherePhiSteps;
                    const Bsdf::Vec3 incoming{
                        radius * std::cos(phi), radius * std::sin(phi), z };
                    const double pdf = Bsdf::PdfBsdfL6(
                        context, material, outgoing, incoming);
                    Require(std::isfinite(pdf) && pdf >= 0.0,
                        "finite non-negative sphere-quadrature PDF");
                    pdfMass += pdf * sphereDOmega;
                    expectedZ += z * pdf * sphereDOmega;
                    expectedZ2 += z * z * pdf * sphereDOmega;
                }
            }
            Require(pdfMass > 0.05 && pdfMass <= 1.005,
                "finite BSDF PDF mass bound");

            std::uint32_t validCount = 0u;
            double observedZ = 0.0;
            double observedZ2 = 0.0;
            double observedZSquare = 0.0;
            double observedZ2Square = 0.0;
            for (std::uint32_t sampleIndex = 0u;
                 sampleIndex < distributionSamples; ++sampleIndex)
            {
                const Bsdf::Vec3 randomSample{
                    CounterRandomFloat(static_cast<std::uint32_t>(materialIndex + 3u),
                        sampleIndex, BounceSampleDimension(0u, BounceDimension::BsdfLobe),
                        0x57a20001u, 0x2468ace013579bdfull),
                    CounterRandomFloat(static_cast<std::uint32_t>(materialIndex + 3u),
                        sampleIndex, BounceSampleDimension(0u, BounceDimension::BsdfU),
                        0x57a20001u, 0x2468ace013579bdfull),
                    CounterRandomFloat(static_cast<std::uint32_t>(materialIndex + 3u),
                        sampleIndex, BounceSampleDimension(0u, BounceDimension::BsdfV),
                        0x57a20001u, 0x2468ace013579bdfull) };
                const Bsdf::BsdfSampleL6 sampled = Bsdf::SampleBsdfL6(
                    context, material, outgoing, randomSample);
                double z = 0.0;
                double z2 = 0.0;
                if (sampled.isValid != 0u)
                {
                    Require(sampled.isDelta == 0u && sampled.measure == Bsdf::kMeasureSolidAngle,
                        "finite statistical BSDF sample measure");
                    Require(Near(sampled.pdf, Bsdf::PdfBsdfL6(
                        context, material, outgoing, sampled.direction), 1.0e-10),
                        "finite statistical Sample/PDF consistency");
                    ++validCount;
                    z = sampled.direction.z;
                    z2 = z * z;
                }
                observedZ += z;
                observedZ2 += z2;
                observedZSquare += z * z;
                observedZ2Square += z2 * z2;
            }
            const double sampleCount = static_cast<double>(distributionSamples);
            const double observedMass = static_cast<double>(validCount) / sampleCount;
            const double observedMeanZ = observedZ / sampleCount;
            const double observedMeanZ2 = observedZ2 / sampleCount;
            const double massSigma = std::sqrt(std::max(
                0.0, observedMass * (1.0 - observedMass)) / sampleCount);
            const double zSigma = std::sqrt(std::max(
                0.0, observedZSquare / sampleCount - observedMeanZ * observedMeanZ) /
                sampleCount);
            const double z2Sigma = std::sqrt(std::max(
                0.0, observedZ2Square / sampleCount - observedMeanZ2 * observedMeanZ2) /
                sampleCount);
            std::cout << "L6_BSDF_STAT model=" << material.model
                << " pdf_mass=" << pdfMass
                << " observed_mass=" << observedMass
                << " expected_z=" << expectedZ
                << " observed_z=" << observedMeanZ
                << " expected_z2=" << expectedZ2
                << " observed_z2=" << observedMeanZ2 << '\n';
            Require(std::abs(observedMass - pdfMass) <= 0.005 * pdfMass + 6.0 * massSigma,
                "finite BSDF PDF integral/acceptance confidence gate");
            Require(std::abs(observedMeanZ - expectedZ) <= 0.002 + 6.0 * zSigma,
                "finite BSDF first-moment confidence gate");
            Require(std::abs(observedMeanZ2 - expectedZ2) <= 0.002 + 6.0 * z2Sigma,
                "finite BSDF second-moment confidence gate");
        }

        Bsdf::BsdfParamsL6 smoothGlass{};
        smoothGlass.model = Bsdf::kModelSmoothGlass;
        smoothGlass.baseColor = { 1.0, 1.0, 1.0 };
        smoothGlass.transmission = 1.0;
        constexpr double normalIncidenceFresnel = 0.04;
        std::uint32_t reflectionCount = 0u;
        for (std::uint32_t sampleIndex = 0u; sampleIndex < distributionSamples; ++sampleIndex)
        {
            const Bsdf::BsdfSampleL6 sampled = Bsdf::SampleBsdfL6(
                context, smoothGlass, outgoing,
                { CounterRandomFloat(9u, sampleIndex,
                      BounceSampleDimension(0u, BounceDimension::BsdfLobe),
                      0x57a20005u, 0x2468ace013579bdfull),
                  0.5, 0.5 });
            Require(sampled.isValid != 0u && sampled.isDelta != 0u
                    && sampled.measure == Bsdf::kMeasureDiscrete,
                "smooth-glass statistical delta sample");
            reflectionCount += (sampled.lobeFlags & Bsdf::kLobeReflection) != 0u ? 1u : 0u;
        }
        const double reflectionFrequency =
            static_cast<double>(reflectionCount) / distributionSamples;
        const double reflectionSigma = std::sqrt(
            normalIncidenceFresnel * (1.0 - normalIncidenceFresnel) / distributionSamples);
        Require(std::abs(reflectionFrequency - normalIncidenceFresnel) <= 6.0 * reflectionSigma,
            "smooth-glass Fresnel branch-frequency confidence gate");

        constexpr std::array roughnessValues{ 0.15, 0.35, 0.70, 1.0 };
        constexpr std::array metallicValues{ 0.0, 0.5, 1.0 };
        constexpr std::uint32_t consistencySamples = 16384u;
        for (const double roughness : roughnessValues)
        {
            for (const double metallic : metallicValues)
            {
                Bsdf::BsdfParamsL6 material{};
                material.model = Bsdf::kModelMetallicRoughness;
                material.baseColor = { 1.0, 1.0, 1.0 };
                material.f0 = { 0.04, 0.04, 0.04 };
                material.metallic = metallic;
                material.perceptualRoughness = roughness;

                Bsdf::Vec3 furnace{};
                for (std::uint32_t zIndex = 0u; zIndex < zSteps; ++zIndex)
                {
                    const double z = (static_cast<double>(zIndex) + 0.5) / zSteps;
                    for (std::uint32_t phiIndex = 0u; phiIndex < phiSteps; ++phiIndex)
                    {
                        const double phiSample =
                            (static_cast<double>(phiIndex) + 0.5) / phiSteps;
                        const Bsdf::Vec3 incoming =
                            Bsdf::SampleUniformHemisphere(z, phiSample);
                        const Bsdf::BsdfEvalL6 evaluation = Bsdf::EvaluateBsdfL6(
                            context, material, outgoing, incoming);
                        Require(evaluation.isValid != 0u,
                            "metallic-roughness quadrature evaluation validity");
                        furnace = furnace + evaluation.value * (incoming.z * dOmega);
                    }
                }
                Require(Bsdf::IsFinite(furnace) && furnace.x >= 0.0 &&
                        furnace.y >= 0.0 && furnace.z >= 0.0 &&
                        furnace.x <= 1.005 && furnace.y <= 1.005 &&
                        furnace.z <= 1.005,
                    "metallic-roughness white-furnace energy bound");

                std::uint32_t validSamples = 0u;
                for (std::uint32_t sampleIndex = 0u;
                     sampleIndex < consistencySamples; ++sampleIndex)
                {
                    const Bsdf::Vec3 randomSample{
                        CounterRandomFloat(1u, sampleIndex,
                            BounceSampleDimension(0u, BounceDimension::BsdfLobe),
                            0x57a20003u, 0x2468ace013579bdfull),
                        CounterRandomFloat(1u, sampleIndex,
                            BounceSampleDimension(0u, BounceDimension::BsdfU),
                            0x57a20003u, 0x2468ace013579bdfull),
                        CounterRandomFloat(1u, sampleIndex,
                            BounceSampleDimension(0u, BounceDimension::BsdfV),
                            0x57a20003u, 0x2468ace013579bdfull) };
                    const Bsdf::BsdfSampleL6 sampled = Bsdf::SampleBsdfL6(
                        context, material, outgoing, randomSample);
                    if (sampled.isValid == 0u)
                    {
                        continue;
                    }
                    ++validSamples;
                    Require(sampled.measure == Bsdf::kMeasureSolidAngle &&
                            sampled.isDelta == 0u && sampled.pdf > 0.0 &&
                            Bsdf::IsFinite(sampled.value) &&
                            Bsdf::IsFinite(sampled.direction),
                        "metallic-roughness sampled-event invariants");
                    Require(Near(sampled.pdf, Bsdf::PdfBsdfL6(
                        context, material, outgoing, sampled.direction), 1.0e-10),
                        "metallic-roughness Sample/Evaluate/PDF consistency");
                }
                // Visible-normal GGX can produce a reflected direction below
                // the macrosurface; that is a zero-contribution rejection and
                // its rate grows for rough, reflection-only configurations.
                // Keep a loose non-degeneracy gate instead of inventing a
                // material-independent rejection-rate limit.
                Require(validSamples * 4u >= consistencySamples,
                    "metallic-roughness sampler is degenerate");
            }
        }
    }

    void TestDirectLightingEstimatorVariance()
    {
        // A unit-radiance spherical cap viewed by a unit-albedo Lambertian
        // surface is a compact direct-lighting oracle.  The exact integral is
        // sin^2(thetaMax), so BSDF, light, and one-sample-per-technique MIS can
        // be compared at the same two-proposal-ray budget without a scene or
        // visibility approximation.
        constexpr double cosineMaximum = 0.99;
        constexpr double capSolidAngle =
            2.0 * Bsdf::kPi * (1.0 - cosineMaximum);
        constexpr double lightPdf = 1.0 / capSolidAngle;
        constexpr double expectedIntegral =
            1.0 - cosineMaximum * cosineMaximum;
        constexpr std::uint32_t trialCount = 262144u;

        struct Moments final
        {
            double sum{};
            double squaredSum{};

            void Add(const double value) noexcept
            {
                sum += value;
                squaredSum += value * value;
            }

            [[nodiscard]] double Mean() const noexcept
            {
                return sum / trialCount;
            }

            [[nodiscard]] double Variance() const noexcept
            {
                const double mean = Mean();
                return std::max(0.0, squaredSum / trialCount - mean * mean);
            }
        };

        Moments bsdfOnly;
        Moments nextEvent;
        Moments mis;
        for (std::uint32_t trial = 0u; trial < trialCount; ++trial)
        {
            const auto random = [trial](const std::uint32_t dimension)
            {
                return static_cast<double>(CounterRandomFloat(
                    7u, trial, dimension, 0x57a20004u,
                    0x2468ace013579bdfull));
            };
            const auto bsdfContribution = [](const double uniform)
            {
                const double cosine = std::sqrt(std::max(0.0, 1.0 - uniform));
                return cosine >= cosineMaximum ? 1.0 : 0.0;
            };
            const double bsdfA = bsdfContribution(random(0u));
            const double bsdfB = bsdfContribution(random(1u));
            bsdfOnly.Add(0.5 * (bsdfA + bsdfB));

            const auto lightDirectionCosine = [](const double uniform)
            {
                return cosineMaximum + (1.0 - cosineMaximum) * uniform;
            };
            const auto lightContribution = [](const double cosine)
            {
                return Bsdf::kInversePi * cosine / lightPdf;
            };
            const double lightCosineA = lightDirectionCosine(random(2u));
            const double lightCosineB = lightDirectionCosine(random(3u));
            nextEvent.Add(0.5 * (lightContribution(lightCosineA)
                + lightContribution(lightCosineB)));

            double misEstimate = 0.0;
            const double bsdfCosine = std::sqrt(std::max(0.0, 1.0 - random(4u)));
            if (bsdfCosine >= cosineMaximum)
            {
                const double bsdfPdf = bsdfCosine * Bsdf::kInversePi;
                const double denominator = bsdfPdf * bsdfPdf + lightPdf * lightPdf;
                misEstimate += bsdfPdf * bsdfPdf / denominator;
            }
            const double lightCosine = lightDirectionCosine(random(5u));
            const double bsdfPdf = lightCosine * Bsdf::kInversePi;
            const double denominator = lightPdf * lightPdf + bsdfPdf * bsdfPdf;
            misEstimate += lightContribution(lightCosine)
                * (lightPdf * lightPdf / denominator);
            mis.Add(misEstimate);
        }

        Require(Near(bsdfOnly.Mean(), expectedIntegral, 7.5e-4),
            "BSDF-only direct estimator mean");
        Require(Near(nextEvent.Mean(), expectedIntegral, 7.5e-4),
            "NEE direct estimator mean");
        Require(Near(mis.Mean(), expectedIntegral, 7.5e-4),
            "MIS direct estimator mean");
        Require(nextEvent.Variance() < bsdfOnly.Variance()
                && mis.Variance() < bsdfOnly.Variance(),
            "NEE/MIS equal-ray-budget variance improvement");
    }
}

int main()
{
    try
    {
        TestAliasAndEnvironmentBuilders();
        TestLightDistributionAndRng();
        TestFrameValidation();
        TestEstimatorAndAbiMappings();
        TestBsdfApiInvariants();
        TestSamplingStatisticsAndEnergy();
        TestDirectLightingEstimatorVariance();
        std::cout << "L6 megakernel CPU self-tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "L6 megakernel CPU self-tests failed: " << exception.what() << '\n';
        return 1;
    }
}
