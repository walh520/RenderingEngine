#pragma once

#include "contracts/AbiTypesV0.hpp"
#include "contracts/AbiVersionV1.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RenderingEngine::Contracts::AbiV1
{
    using AbiFloat4 = AbiV0::AbiFloat4;
    using AbiUInt4 = AbiV0::AbiUInt4;

    enum SampleMeasure : std::uint32_t
    {
        SampleMeasureInvalid = 0u,
        SampleMeasureDiscrete = 1u,
        SampleMeasureSolidAngle = 2u,
        SampleMeasureArea = 3u
    };

    enum BsdfLobeFlags : std::uint32_t
    {
        BsdfLobeNone = 0u,
        BsdfLobeDiffuse = 1u << 0u,
        BsdfLobeGlossy = 1u << 1u,
        BsdfLobeSpecular = 1u << 2u,
        BsdfLobeReflection = 1u << 3u,
        BsdfLobeTransmission = 1u << 4u
    };

    enum PathFlags : std::uint32_t
    {
        PathFlagNone = 0u,
        PathFlagActive = 1u << 0u,
        PathFlagLastEventDelta = 1u << 1u,
        PathFlagTerminated = 1u << 2u,
        PathFlagInvalid = 1u << 3u
    };

    enum ShadowFlags : std::uint32_t
    {
        ShadowFlagNone = 0u,
        ShadowFlagFiniteDistance = 1u << 0u,
        ShadowFlagAlphaTest = 1u << 1u
    };

    // Sampled direction and PDF are expressed in the measure named by
    // metadata.x. valueEta.xyz is the BSDF value and valueEta.w is eta.
    // metadata = (measure, lobe flags, isDelta, isValid).
    struct alignas(16) GpuBsdfSampleV1
    {
        AbiFloat4 directionPdf;
        AbiFloat4 valueEta;
        AbiUInt4 metadata;
        AbiUInt4 reserved0;
    };

    // directionCombinedPdf.xyz points from the shading point toward the light;
    // w is the combined solid-angle PDF. radianceDiscretePdf.w is the discrete
    // light-selection PDF. conditionalPdf.x stores the conditional density and
    // conditionalPdf.y stores its SampleMeasure value.
    struct alignas(16) GpuLightSampleV1
    {
        AbiFloat4 positionDistance;
        AbiFloat4 directionCombinedPdf;
        AbiFloat4 radianceDiscretePdf;
        AbiFloat4 conditionalPdf;
        AbiUInt4 identity; // light ID, primitive ID, sample identity low/high.
        AbiUInt4 metadata; // isDelta, isValid, light flags, reserved.
    };

    // Queue ray identity = (ray ID, path ID, bounce, visibility mask). rng =
    // (base-seed low/high, dimension, stream tag). tMin/tMax are exclusive.
    struct alignas(16) GpuRayQueueRecordV1
    {
        AbiFloat4 originTMin;
        AbiFloat4 directionTMax;
        AbiUInt4 identity;
        AbiUInt4 rng;
    };

    // Barycentric convention matches abi-v0. metadata =
    // (ray ID, HitKind v0 value, HitFlags v0 value, path ID).
    struct alignas(16) GpuHitQueueRecordV1
    {
        AbiFloat4 positionT;
        AbiFloat4 geometricNormalBaryU;
        AbiFloat4 shadingNormalBaryV;
        AbiUInt4 ids; // instance, primitive, geometry, material IDs.
        AbiUInt4 metadata;
        AbiUInt4 reserved0;
    };

    // contribution.xyz is the unoccluded contribution owned by the integrator;
    // contribution.w is the estimator weight. identity =
    // (shadow ray ID, path ID, light ID, visibility mask).
    struct alignas(16) GpuShadowQueueRecordV1
    {
        AbiFloat4 originTMin;
        AbiFloat4 directionTMax;
        AbiFloat4 contribution;
        AbiUInt4 identity;
        AbiUInt4 metadata; // bounce, ShadowFlags, reserved, reserved.
    };

    // previousPositionPdf.xyz is the previous scattering position and w is the
    // previous directional PDF. identity = (pixel, path, bounce, sample).
    struct alignas(16) GpuPathStateV1
    {
        AbiFloat4 radiance;
        AbiFloat4 throughput;
        AbiFloat4 previousPositionPdf;
        AbiUInt4 rng; // base-seed low/high, next dimension, stream tag.
        AbiUInt4 identity;
        AbiUInt4 metadata; // PathFlags, previous lobe flags, medium ID, reserved.
    };

    static_assert(sizeof(GpuBsdfSampleV1) == 64u);
    static_assert(sizeof(GpuLightSampleV1) == 96u);
    static_assert(sizeof(GpuRayQueueRecordV1) == 64u);
    static_assert(sizeof(GpuHitQueueRecordV1) == 96u);
    static_assert(sizeof(GpuShadowQueueRecordV1) == 80u);
    static_assert(sizeof(GpuPathStateV1) == 96u);
    static_assert(alignof(GpuBsdfSampleV1) == 16u);
    static_assert(alignof(GpuLightSampleV1) == 16u);
    static_assert(alignof(GpuRayQueueRecordV1) == 16u);
    static_assert(alignof(GpuHitQueueRecordV1) == 16u);
    static_assert(alignof(GpuShadowQueueRecordV1) == 16u);
    static_assert(alignof(GpuPathStateV1) == 16u);
    static_assert(std::is_standard_layout_v<GpuBsdfSampleV1>);
    static_assert(std::is_trivially_copyable_v<GpuBsdfSampleV1>);
    static_assert(std::is_standard_layout_v<GpuLightSampleV1>);
    static_assert(std::is_trivially_copyable_v<GpuLightSampleV1>);
    static_assert(std::is_standard_layout_v<GpuRayQueueRecordV1>);
    static_assert(std::is_trivially_copyable_v<GpuRayQueueRecordV1>);
    static_assert(std::is_standard_layout_v<GpuHitQueueRecordV1>);
    static_assert(std::is_trivially_copyable_v<GpuHitQueueRecordV1>);
    static_assert(std::is_standard_layout_v<GpuShadowQueueRecordV1>);
    static_assert(std::is_trivially_copyable_v<GpuShadowQueueRecordV1>);
    static_assert(std::is_standard_layout_v<GpuPathStateV1>);
    static_assert(std::is_trivially_copyable_v<GpuPathStateV1>);
    static_assert(offsetof(GpuRayQueueRecordV1, identity) == 32u);
    static_assert(offsetof(GpuHitQueueRecordV1, metadata) == 64u);
    static_assert(offsetof(GpuShadowQueueRecordV1, identity) == 48u);
    static_assert(offsetof(GpuPathStateV1, metadata) == 80u);
}
