#ifndef RENDERING_ENGINE_RECONSTRUCTION_ABI_V2_HLSLI
#define RENDERING_ENGINE_RECONSTRUCTION_ABI_V2_HLSLI

static const uint kPrimarySurfaceFlagNoneV2 = 0u;
static const uint kPrimarySurfaceFlagValidV2 = 1u << 0u;
static const uint kPrimarySurfaceFlagFrontFaceV2 = 1u << 1u;
static const uint kPrimarySurfaceFlagHasDiffuseV2 = 1u << 2u;
static const uint kPrimarySurfaceFlagHasSpecularV2 = 1u << 3u;
static const uint kMotionFlagNoneV2 = 0u;
static const uint kMotionFlagValidV2 = 1u << 0u;
static const uint kMotionFlagCameraOnlyV2 = 1u << 1u;
static const uint kMotionFlagRigidInstanceV2 = 1u << 2u;
static const uint kMotionFlagDisocclusionCandidateV2 = 1u << 3u;
static const uint kHistoryFlagNoneV2 = 0u;
static const uint kHistoryFlagValidV2 = 1u << 0u;
static const uint kHistoryFlagResetV2 = 1u << 1u;
static const uint kHistoryFlagReprojectedV2 = 1u << 2u;
static const uint kHistoryFlagSpatialFallbackV2 = 1u << 3u;

struct GpuPrimarySurfaceV2
{
    float4 worldPositionLinearDepth;
    float4 geometricNormalRoughness;
    float4 shadingNormalMetallic;
    float4 diffuseAlbedo;
    float4 specularAlbedo;
    uint4 identity;
};

struct GpuMotionVectorV2
{
    float4 motionExpectedDepth;
    float4 currentPreviousUv;
    uint4 identity;
};

struct GpuReconstructionSignalV2
{
    float4 directDiffuse;
    float4 directSpecular;
    float4 indirectDiffuse;
    float4 indirectSpecular;
};

struct GpuHistoryMetadataV2
{
    float4 momentsVarianceHistory;
    uint4 surfaceIdentity;
    uint4 frameIdentity;
};

struct GpuGBufferRecordV2
{
    GpuPrimarySurfaceV2 primary;
    GpuMotionVectorV2 motion;
    GpuReconstructionSignalV2 signal;
};

#endif
