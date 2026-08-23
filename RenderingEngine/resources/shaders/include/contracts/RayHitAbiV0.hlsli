#ifndef RENDERING_ENGINE_RAY_HIT_ABI_V0_HLSLI
#define RENDERING_ENGINE_RAY_HIT_ABI_V0_HLSLI

#include "AbiTypesV0.hlsli"

static const uint kRayFlagNoneV0 = 0u;
static const uint kRayFlagCullFrontFaceV0 = 1u << 0u;
static const uint kRayFlagCullBackFaceV0 = 1u << 1u;
static const uint kRayFlagForceOpaqueV0 = 1u << 2u;

static const uint kHitKindMissV0 = 0u;
static const uint kHitKindTriangleV0 = 1u;
static const uint kHitKindLegacyAnalyticV0 = 2u;
static const uint kHitKindInvalidV0 = 3u;

static const uint kHitFlagNoneV0 = 0u;
static const uint kHitFlagFrontFaceV0 = 1u << 0u;
static const uint kHitFlagAlphaTestedV0 = 1u << 1u;

struct GpuRayV0
{
    float4 originTMin;
    float4 directionTMax;
    uint4 query;
    uint4 reserved0;
};

struct GpuHitV0
{
    float4 positionT;
    float4 geometricNormalBaryU;
    float4 shadingNormalBaryV;
    uint4 ids;
    uint4 metadata;
    uint4 reserved0;
};

#endif
