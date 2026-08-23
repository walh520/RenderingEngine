#ifndef RENDERING_ENGINE_SCENE_ABI_V0_HLSLI
#define RENDERING_ENGINE_SCENE_ABI_V0_HLSLI

#include "AbiTypesV0.hlsli"
#include "AbiVersionV0.hlsli"

static const uint kSceneFlagNoneV0 = 0u;
static const uint kSceneFlagHasEnvironmentV0 = 1u << 0u;
static const uint kSceneFlagHasAlphaMaskV0 = 1u << 1u;
static const uint kSceneFlagHasAnimatedRigidInstancesV0 = 1u << 2u;

static const uint kGeometryFlagNoneV0 = 0u;
static const uint kGeometryFlagOpaqueV0 = 1u << 0u;
static const uint kGeometryFlagAlphaMaskV0 = 1u << 1u;
static const uint kGeometryFlagDoubleSidedV0 = 1u << 2u;

static const uint kInstanceFlagNoneV0 = 0u;
static const uint kInstanceFlagVisibleV0 = 1u << 0u;
static const uint kInstanceFlagCastsShadowV0 = 1u << 1u;
static const uint kInstanceFlagAnimatedRigidV0 = 1u << 2u;

static const uint kMaterialModelMetallicRoughnessV0 = 0u;
static const uint kMaterialModelSmoothDielectricV0 = 1u;

static const uint kMaterialFlagNoneV0 = 0u;
static const uint kMaterialFlagDoubleSidedV0 = 1u << 0u;
static const uint kMaterialFlagAlphaMaskV0 = 1u << 1u;
static const uint kMaterialFlagEmissiveV0 = 1u << 2u;
static const uint kMaterialFlagThinWalledV0 = 1u << 3u;

static const uint kLightTypePointV0 = 0u;
static const uint kLightTypeDirectionalV0 = 1u;
static const uint kLightTypeSpotV0 = 2u;
static const uint kLightTypeSphereAreaV0 = 3u;
static const uint kLightTypeEmissiveTriangleV0 = 4u;
static const uint kLightTypeEnvironmentV0 = 5u;

static const uint kLightFlagNoneV0 = 0u;
static const uint kLightFlagEnabledV0 = 1u << 0u;
static const uint kLightFlagTwoSidedV0 = 1u << 1u;
static const uint kLightFlagAnimatedV0 = 1u << 2u;
static const uint kLightFlagDeltaV0 = 1u << 3u;

struct GpuSceneConstantsV0
{
    uint4 counts0;
    uint4 counts1;
    float4 sceneBoundsMin;
    float4 sceneBoundsMax;
    uint4 versionFlags;
    uint4 environment;
};

struct GpuVertexV0
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 texcoord0;
};

struct GpuGeometryV0
{
    uint4 indexRange;
    uint4 identity;
    float4 localBoundsMin;
    float4 localBoundsMax;
};

struct GpuInstanceV0
{
    AbiMat4Rows objectToWorld;
    AbiMat4Rows worldToObject;
    AbiMat4Rows previousObjectToWorld;
    uint4 metadata;
    uint4 reserved0;
};

struct GpuMaterialV0
{
    float4 baseColorFactor;
    float4 emissiveFactorStrength;
    float4 surfaceParams;
    float4 transmissionParams;
    float4 attenuationColorDistance;
    uint4 textureImageIndices;
    uint4 textureSamplerIndices;
    uint4 metadata;
};

struct GpuLightV0
{
    float4 positionRange;
    float4 directionCosOuter;
    float4 radianceScale;
    float4 shapeParams;
    uint4 identity;
    uint4 extra;
};

#endif
