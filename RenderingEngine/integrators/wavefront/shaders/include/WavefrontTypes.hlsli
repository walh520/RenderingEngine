#ifndef RENDERING_ENGINE_WAVEFRONT_TYPES_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_TYPES_HLSLI

static const uint kWfQueueModeAtomicAppend = 0u;
static const uint kWfQueueModePrefixScan = 1u;
static const uint kWfQueueRayA = 0u;
static const uint kWfQueueRayB = 1u;
static const uint kWfQueueNext = 2u;
static const uint kWfQueueShadow = 3u;
static const uint kWfQueueMaterial = 4u;
static const uint kWfQueueGlobal = 5u;
static const uint kWfQueueCount = 6u;

static const uint kWfFatalRayAOverflow = 1u << 0u;
static const uint kWfFatalRayBOverflow = 1u << 1u;
static const uint kWfFatalNextOverflow = 1u << 2u;
static const uint kWfFatalShadowOverflow = 1u << 3u;
static const uint kWfFatalMaterialOverflow = 1u << 4u;
static const uint kWfFatalDispatchOverflow = 1u << 5u;
static const uint kWfFatalInvalidCapacity = 1u << 6u;
static const uint kWfFatalScanOverflow = 1u << 7u;
static const uint kWfFatalRayGenPathCapacity = 1u << 8u;
static const uint kWfFatalRayGenPbrFrame = 1u << 9u;
static const uint kWfFatalRayGenImageContract = 1u << 10u;
static const uint kWfFatalRayGenSeedContract = 1u << 11u;
static const uint kWfFatalRayGenQueueMode = 1u << 12u;
static const uint kWfFatalRayGenDispatchContract = 1u << 13u;
static const uint kWfFatalRayGenStreamContract = 1u << 14u;
static const uint kWfFatalResetFrameCapacity = 1u << 15u;
static const uint kWfFatalShadeSourceQueue = 1u << 16u;
static const uint kWfFatalShadePathIndex = 1u << 17u;
static const uint kWfFatalNextPathIndex = 1u << 18u;
static const uint kWfFatalShadowPathIndex = 1u << 19u;
static const uint kWfFatalShadowMethod = 1u << 20u;

static const uint kWfShadowMethodPcf = 0u;
static const uint kWfShadowMethodPcss = 1u;
static const uint kWfShadowMethodPhysical = 2u;
static const uint kWfShadowQueueMetadataMethodLane = 3u;

static const uint kWfPathActive = 1u << 0u;
static const uint kWfPathPreviousDelta = 1u << 1u;
static const uint kWfPathPreviousSpecular = 1u << 2u;
static const uint kWfPathTerminated = 1u << 3u;
static const uint kWfPathError = 1u << 4u;
// The previous non-delta vertex was an eligible primary surface whose direct
// lighting is owned by ReSTIR. Bit 5 was previously unused; ABI layout stays fixed.
static const uint kWfPathPreviousRestirOwned = 1u << 5u;

static const uint kWfHitMiss = 0u;
static const uint kWfHitFixtureSphere = 1u;
static const uint kWfHitFixturePlane = 2u;

static const uint kWfSignalDirectDiffuse = 0u;
static const uint kWfSignalDirectSpecular = 1u;
static const uint kWfSignalIndirectDiffuse = 2u;
static const uint kWfSignalIndirectSpecular = 3u;

static const uint kWfScanInputIsFlags = 1u << 0u;
static const uint kWfScanPrefixIsBase = 1u << 1u;
static const uint kWfScanTopLevel = 1u << 2u;
static const uint kWfScanChildIsBase = 1u << 3u;

static const uint kWfResetCounters = 1u << 0u;
static const uint kWfResetIndirectArguments = 1u << 1u;
static const uint kWfResetValidateFrame = 1u << 2u;

struct WfFrameConstants
{
    float4 cameraPositionTanHalfFov;
    float4 cameraForwardAspect;
    float4 cameraRightTime;
    float4 cameraUpExposure;
    uint4 imageSample;       // width, height, sample index, maximum bounce count.
    uint4 capacityModeSeed;  // path capacity, queue mode, base-seed low/high.
    uint4 dispatchLimits;    // max groups X/Y, queue thread count, RNG stream tag.
    float4 fixtureLight;     // xyz surface-to-light direction, w radiance scale.
    float4 fixtureAlbedo;    // rgb albedo, w reserved.
};

struct WfPassConstants
{
    uint4 pass;    // bounce, read queue, write queue, indirect command slot.
    uint4 params0; // reset mask, element count, input offset, prefix offset.
    uint4 params1; // sums offset, parent prefix offset, scan flags, source queue.
    uint4 params2; // source count override, profiler slot, reset flags, indirect slots.
};

struct WfRayItem
{
    float4 originTMin;
    float4 directionTMax;
    uint4 identity;
    uint4 rng;
};

struct WfMaterialWorkItem
{
    float4 positionT;
    float4 geometricNormalBaryU;
    float4 shadingNormalBaryV;
    uint4 ids;
    uint4 metadata;
    uint4 reserved0;
};

struct WfNextBounceCandidate
{
    float4 originTMin;
    float4 directionTMax;
    float4 throughputEta;
    float4 diffuseThroughput;
    float4 specularThroughput;
    float4 previousPositionPdf;
    float4 previousGeometricNormal;
    float4 previousShadingNormal;
    uint4 identity;
};

struct WfShadowWorkItem
{
    float4 originTMin;
    float4 directionTMax;
    float4 diffuseContributionPdf;
    float4 specularContributionLight;
    uint4 identity;
    uint4 sampling;
};

struct WfShadowQueueItem
{
    float4 originTMin;
    float4 directionTMax;
    float4 contribution;
    uint4 identity;
    uint4 metadata;
};

struct WfShadowAovItem
{
    float4 diffuseContributionPdf;
    float4 specularContributionLight;
};

struct WfSharedPathState
{
    float4 radiance;
    float4 throughput;
    float4 previousPositionPdf;
    uint4 rng;
    uint4 identity;
    uint4 metadata;
};

struct WfPathState
{
    float4 throughputEta;
    float4 diffuseThroughput;
    float4 specularThroughput;
    float4 cameraEmission;
    float4 directDiffuse;
    float4 directSpecular;
    float4 indirectDiffuse;
    float4 indirectSpecular;
    float4 previousPositionPdf;
    float4 previousGeometricNormal;
    float4 previousShadingNormal;
    uint4 identity;
};

struct WfQueueHeader
{
    uint attemptedCount;
    uint activeCount;
    uint capacity;
    uint overflowCount;
};

struct WfBounceCounters
{
    uint4 work;
    uint4 errors;
};

#endif
