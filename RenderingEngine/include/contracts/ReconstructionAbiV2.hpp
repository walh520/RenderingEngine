#pragma once

#include "contracts/AbiTypesV0.hpp"
#include "contracts/AbiVersionV2.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RenderingEngine::Contracts::AbiV2
{
    using AbiFloat4 = AbiV0::AbiFloat4;
    using AbiUInt4 = AbiV0::AbiUInt4;

    enum PrimarySurfaceFlags : std::uint32_t
    {
        PrimarySurfaceFlagNone = 0u,
        PrimarySurfaceFlagValid = 1u << 0u,
        PrimarySurfaceFlagFrontFace = 1u << 1u,
        PrimarySurfaceFlagHasDiffuse = 1u << 2u,
        PrimarySurfaceFlagHasSpecular = 1u << 3u
    };

    enum MotionFlags : std::uint32_t
    {
        MotionFlagNone = 0u,
        MotionFlagValid = 1u << 0u,
        MotionFlagCameraOnly = 1u << 1u,
        MotionFlagRigidInstance = 1u << 2u,
        MotionFlagDisocclusionCandidate = 1u << 3u
    };

    enum HistoryFlags : std::uint32_t
    {
        HistoryFlagNone = 0u,
        HistoryFlagValid = 1u << 0u,
        HistoryFlagReset = 1u << 1u,
        HistoryFlagReprojected = 1u << 2u,
        HistoryFlagSpatialFallback = 1u << 3u
    };

    // Motion is previous jittered UV minus current jittered UV. Linear depth
    // is positive view-space depth. identity = material, instance, primitive,
    // PrimarySurfaceFlags. The two albedo records reserve w for future use.
    struct alignas(16) GpuPrimarySurfaceV2
    {
        AbiFloat4 worldPositionLinearDepth;
        AbiFloat4 geometricNormalRoughness;
        AbiFloat4 shadingNormalMetallic;
        AbiFloat4 diffuseAlbedo;
        AbiFloat4 specularAlbedo;
        AbiUInt4 identity;
    };

    // motionExpectedDepth.xy = previousUV-currentUV; z is the same surface
    // point's positive depth in the previous camera. currentPreviousUv stores
    // current UV in xy and previous UV in zw. identity.w contains MotionFlags.
    struct alignas(16) GpuMotionVectorV2
    {
        AbiFloat4 motionExpectedDepth;
        AbiFloat4 currentPreviousUv;
        AbiUInt4 identity; // material, instance, primitive, MotionFlags.
    };

    // Split lighting signals remain independent so temporal reconstruction can
    // demodulate/filter diffuse and specular terms without changing estimators.
    struct alignas(16) GpuReconstructionSignalV2
    {
        AbiFloat4 directDiffuse;
        AbiFloat4 directSpecular;
        AbiFloat4 indirectDiffuse;
        AbiFloat4 indirectSpecular;
    };

    // momentsVarianceHistory = first luminance moment, second moment,
    // variance, and bounded history length. frameIdentity stores published
    // frame low/high, logical history generation, and HistoryFlags.
    struct alignas(16) GpuHistoryMetadataV2
    {
        AbiFloat4 momentsVarianceHistory;
        AbiUInt4 surfaceIdentity; // material, instance, primitive, reserved.
        AbiUInt4 frameIdentity;
    };

    struct alignas(16) GpuGBufferRecordV2
    {
        GpuPrimarySurfaceV2 primary;
        GpuMotionVectorV2 motion;
        GpuReconstructionSignalV2 signal;
    };

    static_assert(sizeof(GpuPrimarySurfaceV2) == 96u);
    static_assert(sizeof(GpuMotionVectorV2) == 48u);
    static_assert(sizeof(GpuReconstructionSignalV2) == 64u);
    static_assert(sizeof(GpuHistoryMetadataV2) == 48u);
    static_assert(sizeof(GpuGBufferRecordV2) == 208u);
    static_assert(alignof(GpuPrimarySurfaceV2) == 16u);
    static_assert(alignof(GpuMotionVectorV2) == 16u);
    static_assert(alignof(GpuReconstructionSignalV2) == 16u);
    static_assert(alignof(GpuHistoryMetadataV2) == 16u);
    static_assert(alignof(GpuGBufferRecordV2) == 16u);
    static_assert(std::is_standard_layout_v<GpuPrimarySurfaceV2>);
    static_assert(std::is_trivially_copyable_v<GpuPrimarySurfaceV2>);
    static_assert(std::is_standard_layout_v<GpuMotionVectorV2>);
    static_assert(std::is_trivially_copyable_v<GpuMotionVectorV2>);
    static_assert(std::is_standard_layout_v<GpuReconstructionSignalV2>);
    static_assert(std::is_trivially_copyable_v<GpuReconstructionSignalV2>);
    static_assert(std::is_standard_layout_v<GpuHistoryMetadataV2>);
    static_assert(std::is_trivially_copyable_v<GpuHistoryMetadataV2>);
    static_assert(std::is_standard_layout_v<GpuGBufferRecordV2>);
    static_assert(std::is_trivially_copyable_v<GpuGBufferRecordV2>);
    static_assert(offsetof(GpuPrimarySurfaceV2, identity) == 80u);
    static_assert(offsetof(GpuMotionVectorV2, identity) == 32u);
    static_assert(offsetof(GpuHistoryMetadataV2, frameIdentity) == 32u);
    static_assert(offsetof(GpuGBufferRecordV2, motion) == 96u);
    static_assert(offsetof(GpuGBufferRecordV2, signal) == 144u);
}
