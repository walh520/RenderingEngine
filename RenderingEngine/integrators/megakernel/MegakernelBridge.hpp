#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Integrators::Megakernel
{
    inline constexpr std::uint32_t kInvalidIndex = 0xffffffffu;
    inline constexpr std::uint32_t kCameraSampleDimensionCount = 4u;
    inline constexpr std::uint32_t kDimensionsPerBounce = 16u;

    enum class LightType : std::uint32_t
    {
        Point = 0u,
        Directional = 1u,
        Spot = 2u,
        SphereArea = 3u,
        EmissiveTriangle = 4u,
        Environment = 5u
    };

    enum LightFlags : std::uint32_t
    {
        LightFlagNone = 0u,
        LightFlagEnabled = 1u << 0u,
        LightFlagTwoSided = 1u << 1u,
        LightFlagAnimated = 1u << 2u,
        LightFlagDelta = 1u << 3u
    };

    enum MaterialFlags : std::uint32_t
    {
        MaterialFlagNone = 0u,
        MaterialFlagPureEmitter = 1u << 0u,
        MaterialFlagThinWalled = 1u << 1u
    };

    enum class LightProposal : std::uint32_t
    {
        Uniform = 0u,
        Power = 1u
    };

    // These identifiers are mirrored by pbr_l6_types.hlsli. Counters are reset
    // for every dispatch and accumulated into 64-bit host totals after readback.
    enum class Counter : std::uint32_t
    {
        CameraRays = 0u,
        PathRays,
        ShadowRays,
        SurfaceHits,
        Misses,
        ValidLightSamples,
        InvalidLightSamples,
        OccludedLightSamples,
        CameraEmitterHits,
        DeltaEmitterHits,
        MisEmitterHits,
        RussianRouletteTests,
        RussianRouletteTerminations,
        MaximumDepthTerminations,
        ZeroPdf,
        NegativePdf,
        NonFinitePdf,
        NonFiniteBsdf,
        NonFiniteThroughput,
        NonFiniteRadiance,
        NegativeContribution,
        AliasFallbacks,
        InvalidMaterial,
        InvalidBsdfEvaluation,
        InvalidBsdfSample,
        InvalidFrame,
        Count
    };

    enum class BounceDimension : std::uint32_t
    {
        LightSelection = 0u,
        LightShape0 = 1u,
        LightShape1 = 2u,
        LightShape2 = 3u,
        LightShape3 = 4u,
        BsdfLobe = 5u,
        BsdfU = 6u,
        BsdfV = 7u,
        DielectricEvent = 8u,
        RussianRoulette = 9u,
        AlphaMask = 10u,
        Reserved11 = 11u,
        Reserved12 = 12u,
        Reserved13 = 13u,
        Reserved14 = 14u,
        Reserved15 = 15u
    };

    struct alignas(16) Float4
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct alignas(16) UInt4
    {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t z;
        std::uint32_t w;
    };

    // Fixture material record. It is intentionally private to the L6 harness;
    // it is not a replacement for a published scene or material ABI.
    struct alignas(16) PbrMaterialGpu
    {
        Float4 baseColorMetallic;
        Float4 emissiveRoughness;
        Float4 transmissionIor;
        Float4 attenuationColorDistance;
        Float4 f0;
        Float4 conductorEta;
        Float4 conductorK;
        UInt4 metadata; // BSDF model, allowed lobes, complex Fresnel, MaterialFlags.
    };

    // radianceScale.xyz has type-dependent radiometric units: intensity for
    // point/spot lights, incident radiance for directional lights, and emitted
    // radiance for finite area/environment lights. radianceScale.w is a scalar.
    struct alignas(16) PbrLightGpu
    {
        Float4 positionRange;
        Float4 directionCosOuter;
        Float4 radianceScale;
        Float4 shapeParams; // radius, spot cos-inner, triangle area, reserved.
        UInt4 identity;     // LightType, LightFlags, stable light ID, primitive ID.
        UInt4 payload;      // fixture geometry, texture, distribution offset/count.
    };

    struct alignas(16) AliasEntryGpu
    {
        float q;
        float pmf;
        std::uint32_t alias;
        std::uint32_t item;
    };

    struct alignas(16) FixtureTriangleGpu
    {
        Float4 p0;
        Float4 p1;
        Float4 p2;
        UInt4 metadata; // material, primitive, emitter light, flags.
    };

    struct alignas(16) FixtureSphereGpu
    {
        Float4 centerRadius;
        UInt4 metadata; // material, primitive, emitter light, flags.
    };

    struct alignas(16) MegakernelFrameConstantsGpu
    {
        Float4 cameraPositionTanHalfFov;
        Float4 cameraForwardAspect;
        Float4 cameraRightLensRadius;
        Float4 cameraUpExposure;
        UInt4 image;        // width, height, sample index, maximum depth.
        UInt4 trace;        // triangles, spheres, materials, lights.
        UInt4 sampling;     // seed low/high, proposal, direct-lighting enabled.
        UInt4 environment;  // light index, width, height, flags.
        UInt4 distribution; // light alias, env rows, env columns, primitive map.
        Float4 russianRoulette; // start depth, minimum/maximum continuation, epsilon.
        Float4 sceneCenterRadius;
        Float4 environmentToWorld0;
        Float4 environmentToWorld1;
        Float4 environmentToWorld2;
        Float4 worldToEnvironment0;
        Float4 worldToEnvironment1;
        Float4 worldToEnvironment2;
        UInt4 output; // stream tag and private output flags.
    };

    struct AliasTable
    {
        std::vector<AliasEntryGpu> entries;
        double weightSum = 0.0;
        bool usedUniformFallback = false;
    };

    struct AliasSample
    {
        std::uint32_t item = kInvalidIndex;
        float pmf = 0.0f;
    };

    struct EnvironmentDistribution
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::vector<AliasEntryGpu> rowAlias;
        std::vector<AliasEntryGpu> columnAlias;
        double integratedLuminance = 0.0;
        bool usedUniformSphereFallback = false;
    };

    struct TopLevelLightDistribution
    {
        std::vector<AliasEntryGpu> alias;
        std::vector<float> selectionPmfByLight;
        double weightSum = 0.0;
        bool usedUniformFallback = false;
    };

    [[nodiscard]] AliasTable BuildVoseAliasTable(
        std::span<const double> weights,
        std::span<const std::uint32_t> items = {});

    [[nodiscard]] AliasSample SampleAliasTable(
        std::span<const AliasEntryGpu> table,
        float uniformSample);

    [[nodiscard]] EnvironmentDistribution BuildEnvironmentDistribution(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const float> linearLuminance);

    [[nodiscard]] TopLevelLightDistribution BuildTopLevelLightDistribution(
        std::span<const PbrLightGpu> lights,
        LightProposal proposal,
        float sceneRadius,
        double environmentIntegratedLuminance);

    [[nodiscard]] bool ValidateMegakernelFrameConstants(
        const MegakernelFrameConstantsGpu& frame) noexcept;

    [[nodiscard]] double TexelSolidAngle(
        std::uint32_t row,
        std::uint32_t width,
        std::uint32_t height);

    [[nodiscard]] double LightPowerWeight(
        const PbrLightGpu& light,
        float sceneRadius,
        double environmentIntegratedLuminance);

    [[nodiscard]] std::array<std::uint32_t, 4> Philox4x32TenRounds(
        std::array<std::uint32_t, 4> counter,
        std::array<std::uint32_t, 2> key) noexcept;

    [[nodiscard]] constexpr float Uint32ToUnitFloat(
        const std::uint32_t value) noexcept
    {
        return static_cast<float>(value >> 8u) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] float CounterRandomFloat(
        std::uint32_t pixelIndex,
        std::uint32_t sampleIndex,
        std::uint32_t dimension,
        std::uint32_t streamTag,
        std::uint64_t baseSeed) noexcept;

    [[nodiscard]] constexpr std::uint32_t BounceSampleDimension(
        const std::uint32_t depth,
        const BounceDimension slot) noexcept
    {
        return kCameraSampleDimensionCount
            + depth * kDimensionsPerBounce
            + static_cast<std::uint32_t>(slot);
    }

    static_assert(sizeof(Float4) == 16u);
    static_assert(sizeof(UInt4) == 16u);
    static_assert(sizeof(PbrMaterialGpu) == 128u);
    static_assert(sizeof(PbrLightGpu) == 96u);
    static_assert(sizeof(AliasEntryGpu) == 16u);
    static_assert(sizeof(FixtureTriangleGpu) == 64u);
    static_assert(sizeof(FixtureSphereGpu) == 32u);
    static_assert(sizeof(MegakernelFrameConstantsGpu) == 288u);
    static_assert(static_cast<std::uint32_t>(Counter::Count) == 26u);
}
