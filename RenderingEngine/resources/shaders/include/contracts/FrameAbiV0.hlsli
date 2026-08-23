#ifndef RENDERING_ENGINE_FRAME_ABI_V0_HLSLI
#define RENDERING_ENGINE_FRAME_ABI_V0_HLSLI

#include "AbiTypesV0.hlsli"

static const uint kFrameFlagNoneV0 = 0u;
static const uint kFrameFlagCameraCutV0 = 1u << 0u;
static const uint kFrameFlagFixedSeedComparisonV0 = 1u << 1u;
static const uint kFrameFlagHeadlessV0 = 1u << 2u;

struct GpuFrameConstantsV0
{
    AbiMat4Rows clipFromWorld;
    AbiMat4Rows worldFromClip;
    float4 cameraPositionExposure;
    float4 renderExtentInvExtent;
    float4 timeAndEpsilon;
    uint4 frameInfo;
    uint4 renderInfo;
    uint4 modeInfo;
    uint4 sceneInfo;
    uint4 reserved0;
};

#endif
