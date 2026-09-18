#ifndef RENDERING_ENGINE_RESTIR_ABI_V3_HLSLI
#define RENDERING_ENGINE_RESTIR_ABI_V3_HLSLI

static const uint kRestirCandidateInvalidV3 = 0u;
static const uint kRestirCandidateUniformLightV3 = 1u;
static const uint kRestirCandidatePowerWeightedLightV3 = 2u;
static const uint kRestirCandidateEmissiveTriangleV3 = 3u;
static const uint kRestirCandidateEnvironmentV3 = 4u;
static const uint kRestirCandidateTemporalReuseV3 = 5u;
static const uint kRestirCandidateSpatialReuseV3 = 6u;

static const uint kRestirReuseNoneV3 = 0u;
static const uint kRestirReuseTemporalV3 = 1u;
static const uint kRestirReuseSpatialV3 = 2u;

static const uint kRestirShadowPcfV3 = 0u;
static const uint kRestirShadowPcssV3 = 1u;
static const uint kRestirShadowPhysicalV3 = 2u;
static const uint kRestirPcfFilterRayCountV3 = 8u;
static const uint kRestirPcssBlockerRayCountV3 = 4u;
static const uint kRestirPcssFilterRayCountV3 = 12u;
static const uint kRestirPcssVisibilityRayCountV3 =
    kRestirPcssBlockerRayCountV3 + kRestirPcssFilterRayCountV3;
static const uint kRestirPhysicalVisibilityRayCountV3 = 1u;

static const uint kRestirSampleFlagNoneV3 = 0u;
static const uint kRestirSampleFlagValidV3 = 1u << 0u;
static const uint kRestirSampleFlagDeltaV3 = 1u << 1u;
static const uint kRestirSampleFlagHasAreaPdfV3 = 1u << 2u;
static const uint kRestirSampleFlagHasSolidAnglePdfV3 = 1u << 3u;

static const uint kRestirReservoirFlagNoneV3 = 0u;
static const uint kRestirReservoirFlagValidV3 = 1u << 0u;
static const uint kRestirReservoirFlagMClampedV3 = 1u << 1u;
static const uint kRestirReservoirFlagTemporalAcceptedV3 = 1u << 2u;
static const uint kRestirReservoirFlagSpatialAcceptedV3 = 1u << 3u;
static const uint kRestirReservoirFlagReferenceModeV3 = 1u << 4u;
static const uint kRestirReservoirFlagFinalVisibilityEvaluatedV3 = 1u << 5u;
static const uint kRestirReservoirFlagVisibilityValidV3 = 1u << 6u;

static const uint kRestirHistoryFlagNoneV3 = 0u;
static const uint kRestirHistoryFlagValidV3 = 1u << 0u;
static const uint kRestirHistoryFlagResetV3 = 1u << 1u;
static const uint kRestirHistoryFlagDisoccludedV3 = 1u << 2u;
static const uint kRestirHistoryFlagCameraCutV3 = 1u << 3u;
static const uint kRestirHistoryFlagSceneChangedV3 = 1u << 4u;
static const uint kRestirHistoryFlagLightSetChangedV3 = 1u << 5u;
static const uint kRestirHistoryFlagResolutionChangedV3 = 1u << 6u;

static const uint kRestirRejectNoneV3 = 0u;
static const uint kRestirRejectInvalidCandidateV3 = 1u;
static const uint kRestirRejectReprojectionOutsideV3 = 2u;
static const uint kRestirRejectMotionInvalidV3 = 3u;
static const uint kRestirRejectCameraCutV3 = 4u;
static const uint kRestirRejectResizeV3 = 5u;
static const uint kRestirRejectDepthV3 = 6u;
static const uint kRestirRejectNormalV3 = 7u;
static const uint kRestirRejectInstanceV3 = 8u;
static const uint kRestirRejectThinGeometryV3 = 9u;
static const uint kRestirRejectSceneGenerationV3 = 10u;
static const uint kRestirRejectLightGenerationV3 = 11u;
static const uint kRestirRejectAgeV3 = 12u;
static const uint kRestirRejectMaterialV3 = 13u;

struct GpuPersistentLightSampleV3
{
    float4 positionDistance;
    float4 directionCombinedPdf;
    float4 radianceDiscretePdf;
    float4 conditionalPdf;
    uint4 identity;
    uint4 generation;
    uint4 metadata;
    uint4 sourceIdentity;
};

struct GpuRestirCandidateV3
{
    GpuPersistentLightSampleV3 sample;
    float4 targetProposalSupportCorrection;
    uint4 provenance;
};

struct GpuRestirHistoryIdentityV3
{
    uint4 surfaceIdentity;
    uint4 sceneIdentity;
    uint4 frameIdentity;
    uint4 reprojection;
};

struct GpuRestirReservoirV3
{
    GpuPersistentLightSampleV3 selected;
    float4 weightState;
    float4 selectedTerms;
    uint4 state;
    uint4 provenance;
    GpuRestirHistoryIdentityV3 history;
};

struct GpuRestirDebugV3
{
    uint4 identity;
    uint4 generation;
    float4 scalar;
    uint4 state;
};

struct GpuRestirStatisticsV3
{
    uint4 candidateCounts;
    uint4 reuseCounts;
    uint4 visibilityCounts;
    float4 errorMetrics;
};

struct GpuRestirParametersV3
{
    uint4 extentAndCandidates;
    uint4 reuseLimits;
    uint4 generations;
    uint4 historyGenerations;
    uint4 modeAndFlags;
    uint4 lightTableCounts;
    float4 validation;
    float4 cameraPosition;
};

// HLSL has no portable offsetof/static_assert facility. These constants are
// the shader-side layout probe values consumed by the C++ static test and the
// shader review; every field is a 16-byte scalar vector or nested record.
static const uint kGpuPersistentLightSampleV3Size = 128u;
static const uint kGpuRestirCandidateV3Size = 160u;
static const uint kGpuRestirHistoryIdentityV3Size = 64u;
static const uint kGpuRestirReservoirV3Size = 256u;
static const uint kGpuRestirDebugV3Size = 64u;
static const uint kGpuRestirStatisticsV3Size = 64u;
static const uint kGpuRestirParametersV3Size = 128u;

static const uint kGpuPersistentLightSampleV3OffsetPositionDistance = 0u;
static const uint kGpuPersistentLightSampleV3OffsetDirectionCombinedPdf = 16u;
static const uint kGpuPersistentLightSampleV3OffsetRadianceDiscretePdf = 32u;
static const uint kGpuPersistentLightSampleV3OffsetConditionalPdf = 48u;
static const uint kGpuPersistentLightSampleV3OffsetIdentity = 64u;
static const uint kGpuPersistentLightSampleV3OffsetGeneration = 80u;
static const uint kGpuPersistentLightSampleV3OffsetMetadata = 96u;
static const uint kGpuPersistentLightSampleV3OffsetSourceIdentity = 112u;
static const uint kGpuRestirCandidateV3OffsetSample = 0u;
static const uint kGpuRestirCandidateV3OffsetTerms = 128u;
static const uint kGpuRestirCandidateV3OffsetProvenance = 144u;
static const uint kGpuRestirHistoryIdentityV3OffsetSurface = 0u;
static const uint kGpuRestirHistoryIdentityV3OffsetScene = 16u;
static const uint kGpuRestirHistoryIdentityV3OffsetFrame = 32u;
static const uint kGpuRestirHistoryIdentityV3OffsetReprojection = 48u;
static const uint kGpuRestirReservoirV3OffsetSelected = 0u;
static const uint kGpuRestirReservoirV3OffsetWeightState = 128u;
static const uint kGpuRestirReservoirV3OffsetSelectedTerms = 144u;
static const uint kGpuRestirReservoirV3OffsetState = 160u;
static const uint kGpuRestirReservoirV3OffsetProvenance = 176u;
static const uint kGpuRestirReservoirV3OffsetHistory = 192u;
static const uint kGpuRestirDebugV3OffsetIdentity = 0u;
static const uint kGpuRestirDebugV3OffsetGeneration = 16u;
static const uint kGpuRestirDebugV3OffsetScalar = 32u;
static const uint kGpuRestirDebugV3OffsetState = 48u;
static const uint kGpuRestirStatisticsV3OffsetCandidateCounts = 0u;
static const uint kGpuRestirStatisticsV3OffsetReuseCounts = 16u;
static const uint kGpuRestirStatisticsV3OffsetVisibilityCounts = 32u;
static const uint kGpuRestirStatisticsV3OffsetErrorMetrics = 48u;
static const uint kGpuRestirParametersV3OffsetExtentAndCandidates = 0u;
static const uint kGpuRestirParametersV3OffsetReuseLimits = 16u;
static const uint kGpuRestirParametersV3OffsetGenerations = 32u;
static const uint kGpuRestirParametersV3OffsetHistoryGenerations = 48u;
static const uint kGpuRestirParametersV3OffsetModeAndFlags = 64u;
static const uint kGpuRestirParametersV3OffsetLightTableCounts = 80u;
static const uint kGpuRestirParametersV3OffsetValidation = 96u;
static const uint kGpuRestirParametersV3OffsetCameraPosition = 112u;

#endif
