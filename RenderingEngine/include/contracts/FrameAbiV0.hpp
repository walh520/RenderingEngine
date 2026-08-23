#pragma once

#include "contracts/AbiTypesV0.hpp"

namespace RenderingEngine::Contracts::AbiV0
{
    enum FrameFlags : std::uint32_t
    {
        FrameFlagNone = 0u,
        FrameFlagCameraCut = 1u << 0u,
        FrameFlagFixedSeedComparison = 1u << 1u,
        FrameFlagHeadless = 1u << 2u
    };

    struct alignas(16) GpuFrameConstantsV0
    {
        AbiMat4Rows clipFromWorld;
        AbiMat4Rows worldFromClip;
        AbiFloat4 cameraPositionExposure; // xyz = world position; w = exposure.
        AbiFloat4 renderExtentInvExtent;  // width, height, reciprocal width/height.
        AbiFloat4 timeAndEpsilon;         // time, delta time, ray t-min, shadow t-min.
        AbiUInt4 frameInfo;               // frame, sample, base seed low/high.
        AbiUInt4 renderInfo;              // max bounce, spp/frame, debug view, FrameFlags.
        AbiUInt4 modeInfo;                // backend, integrator, sampling, reconstruction IDs.
        AbiUInt4 sceneInfo;               // scene ID, scene generation, camera-cut generation, ABI.
        AbiUInt4 reserved0;               // Must be zero in abi-v0.
    };
}
