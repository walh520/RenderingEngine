#pragma once

#include "contracts/GpuRecordsAbiV1.hpp"

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

    enum class LightSelection : std::uint32_t
    {
        Uniform = 0u,
        PowerWeighted = 1u
    };

    enum class EnvironmentSampler : std::uint32_t
    {
        UniformSphere = 0u,
        ImportanceMap = 1u
    };

    // This is the L6-private shader encoding. It deliberately starts at zero
    // because the field is a private frame payload, not the public
    // RuntimeConfig enum (which reserves a legacy value at zero).
    enum class DirectLightingEstimator : std::uint32_t
    {
        BsdfOnly = 0u,
        NextEventEstimation = 1u,
        Mis = 2u,
        MultipleImportanceSampling = Mis,
        // Wave 4: L9 owns primary-hit direct lighting. L6 keeps camera/delta
        // emission and uses MIS only for secondary non-primary vertices.
        RestirPrimary = 3u
    };

    enum class TraversalBackend : std::uint32_t
    {
        Fixture = 0u,
        FlattenedSah = 1u,
        HardwareRayQuery = 2u,
        // Canonical-scene compatibility implementation for the public
        // CanonicalLinearGpu selection.  It linearly tests every canonical
        // triangle and therefore remains algorithmically distinct from SAH.
        CanonicalLinear = 3u
    };

    enum class EmitterHitKind : std::uint32_t
    {
        Camera = 0u,
        DeltaBsdf = 1u,
        NonDeltaBsdf = 2u
    };

    // The light-sampling private measure encoding predates abi-v1 and is kept
    // stable for the private shader records. Do not reinterpret it as the
    // public AbiV1::SampleMeasure enum: its discrete value is zero and its
    // area value precedes solid-angle.
    enum class PrivateLightSampleMeasure : std::uint32_t
    {
        Discrete = 0u,
        Area = 1u,
        SolidAngle = 2u,
        Invalid = 0xffffffffu
    };

    // PbrBsdf.hlsli has its own historical encoding (invalid=0,
    // solid-angle=1, discrete=2). It also needs an explicit bridge when its
    // result is exported through the shared ABI.
    enum class PrivateBsdfMeasure : std::uint32_t
    {
        Invalid = 0u,
        SolidAngle = 1u,
        Discrete = 2u
    };

    [[nodiscard]] constexpr Contracts::AbiV1::SampleMeasure ToAbiSampleMeasure(
        const PrivateLightSampleMeasure value) noexcept
    {
        switch (value)
        {
        case PrivateLightSampleMeasure::Discrete:
            return Contracts::AbiV1::SampleMeasureDiscrete;
        case PrivateLightSampleMeasure::Area:
            return Contracts::AbiV1::SampleMeasureArea;
        case PrivateLightSampleMeasure::SolidAngle:
            return Contracts::AbiV1::SampleMeasureSolidAngle;
        case PrivateLightSampleMeasure::Invalid:
        default:
            return Contracts::AbiV1::SampleMeasureInvalid;
        }
    }

    [[nodiscard]] constexpr PrivateLightSampleMeasure FromAbiSampleMeasure(
        const Contracts::AbiV1::SampleMeasure value) noexcept
    {
        switch (value)
        {
        case Contracts::AbiV1::SampleMeasureDiscrete:
            return PrivateLightSampleMeasure::Discrete;
        case Contracts::AbiV1::SampleMeasureArea:
            return PrivateLightSampleMeasure::Area;
        case Contracts::AbiV1::SampleMeasureSolidAngle:
            return PrivateLightSampleMeasure::SolidAngle;
        case Contracts::AbiV1::SampleMeasureInvalid:
        default:
            return PrivateLightSampleMeasure::Invalid;
        }
    }

    [[nodiscard]] constexpr Contracts::AbiV1::SampleMeasure ToAbiSampleMeasure(
        const PrivateBsdfMeasure value) noexcept
    {
        switch (value)
        {
        case PrivateBsdfMeasure::SolidAngle:
            return Contracts::AbiV1::SampleMeasureSolidAngle;
        case PrivateBsdfMeasure::Discrete:
            return Contracts::AbiV1::SampleMeasureDiscrete;
        case PrivateBsdfMeasure::Invalid:
        default:
            return Contracts::AbiV1::SampleMeasureInvalid;
        }
    }

    [[nodiscard]] constexpr PrivateBsdfMeasure FromAbiBsdfSampleMeasure(
        const Contracts::AbiV1::SampleMeasure value) noexcept
    {
        switch (value)
        {
        case Contracts::AbiV1::SampleMeasureSolidAngle:
            return PrivateBsdfMeasure::SolidAngle;
        case Contracts::AbiV1::SampleMeasureDiscrete:
            return PrivateBsdfMeasure::Discrete;
        case Contracts::AbiV1::SampleMeasureInvalid:
        case Contracts::AbiV1::SampleMeasureArea:
        default:
            return PrivateBsdfMeasure::Invalid;
        }
    }

    struct EmitterHitWeightResult
    {
        float weight = 0.0f;
        bool valid = false;
    };

    // Host-side oracle for the shader's emitter-hit policy. It is intentionally
    // kept next to the frame contract so CPU tests can exercise all estimator
    // modes without requiring a Vulkan device.
    [[nodiscard]] EmitterHitWeightResult ComputeEmitterHitWeight(
        DirectLightingEstimator estimator,
        EmitterHitKind kind,
        float previousBsdfPdf = 0.0f,
        float lightPdf = 0.0f) noexcept;

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
        UInt4 payload;      // fixture geometry, texture, distribution offset, stable instance ID.
    };

    struct alignas(16) AliasEntryGpu
    {
        float q;
        float pmf;
        std::uint32_t alias;
        std::uint32_t item;
    };

    // Sorted lexicographically by (instanceId, primitiveId). L6 performs a
    // bounded binary lookup on emitter hits instead of assuming primitive IDs
    // remain unique when canonical geometry is instanced.
    struct alignas(16) EmitterMapEntryGpu
    {
        std::uint32_t instanceId{kInvalidIndex};
        std::uint32_t primitiveId{kInvalidIndex};
        std::uint32_t lightIndex{kInvalidIndex};
        std::uint32_t reserved{};
    };

    struct alignas(16) FixtureTriangleGpu
    {
        Float4 p0;
        Float4 p1;
        Float4 p2;
        UInt4 metadata; // material, primitive, stable instance ID, flags.
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
        UInt4 sampling;     // seed low/high, light selection, direct estimator.
        UInt4 environment;  // light index, width, height, environment sampler.
        UInt4 distribution; // light alias, env rows, env columns, emitter-map entries.
        Float4 russianRoulette; // start depth, minimum/maximum continuation, epsilon.
        Float4 sceneCenterRadius;
        Float4 environmentToWorld0;
        Float4 environmentToWorld1;
        Float4 environmentToWorld2;
        Float4 worldToEnvironment0;
        Float4 worldToEnvironment1;
        Float4 worldToEnvironment2;
        UInt4 traversal; // backend, flattened node count, triangle count, alpha-atlas layers.
        UInt4 output; // stream tag, alpha sampler ID, transport model, shadow method.
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
        LightSelection selection,
        float sceneRadius,
        double environmentIntegratedLuminance);

    // Builds the unique, shader-searchable mapping from an instanced emissive
    // primitive to its finite-light record. Emissive-triangle light records
    // publish the stable instance ID in payload.w and primitive ID in identity.w.
    [[nodiscard]] std::vector<EmitterMapEntryGpu> BuildEmitterMap(
        std::span<const PbrLightGpu> lights);

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
    static_assert(sizeof(EmitterMapEntryGpu) == 16u);
    static_assert(sizeof(FixtureTriangleGpu) == 64u);
    static_assert(sizeof(FixtureSphereGpu) == 32u);
    static_assert(sizeof(MegakernelFrameConstantsGpu) == 304u);
    static_assert(static_cast<std::uint32_t>(Counter::Count) == 26u);
    static_assert(static_cast<std::uint32_t>(DirectLightingEstimator::BsdfOnly) == 0u);
    static_assert(static_cast<std::uint32_t>(DirectLightingEstimator::NextEventEstimation) == 1u);
    static_assert(static_cast<std::uint32_t>(DirectLightingEstimator::Mis) == 2u);
    static_assert(static_cast<std::uint32_t>(DirectLightingEstimator::RestirPrimary) == 3u);
    static_assert(static_cast<std::uint32_t>(TraversalBackend::Fixture) == 0u);
    static_assert(static_cast<std::uint32_t>(TraversalBackend::FlattenedSah) == 1u);
    static_assert(static_cast<std::uint32_t>(TraversalBackend::HardwareRayQuery) == 2u);
    static_assert(static_cast<std::uint32_t>(TraversalBackend::CanonicalLinear) == 3u);
}
