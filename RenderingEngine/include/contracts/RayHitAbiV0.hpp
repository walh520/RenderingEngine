#pragma once

#include "contracts/AbiTypesV0.hpp"

namespace RenderingEngine::Contracts::AbiV0
{
    enum RayFlags : std::uint32_t
    {
        RayFlagNone = 0u,
        RayFlagCullFrontFace = 1u << 0u,
        RayFlagCullBackFace = 1u << 1u,
        RayFlagForceOpaque = 1u << 2u
    };

    enum HitKind : std::uint32_t
    {
        HitKindMiss = 0u,
        HitKindTriangle = 1u,
        HitKindLegacyAnalytic = 2u,
        HitKindInvalid = 3u
    };

    enum HitFlags : std::uint32_t
    {
        HitFlagNone = 0u,
        HitFlagFrontFace = 1u << 0u,
        HitFlagAlphaTested = 1u << 1u
    };

    // Traversal-only query data. Integrator/path state intentionally belongs to
    // abi-v1 and must not be added to this record.
    struct alignas(16) GpuRayV0
    {
        AbiFloat4 originTMin;
        AbiFloat4 directionTMax;
        AbiUInt4 query; // ray ID, visibility mask, RayFlags, reserved.
        AbiUInt4 reserved0;
    };

    struct alignas(16) GpuHitV0
    {
        AbiFloat4 positionT;
        AbiFloat4 geometricNormalBaryU;
        AbiFloat4 shadingNormalBaryV;
        AbiUInt4 ids;      // instance, primitive, geometry, material IDs.
        AbiUInt4 metadata; // ray ID, HitKind, HitFlags, reserved.
        AbiUInt4 reserved0;
    };
}
