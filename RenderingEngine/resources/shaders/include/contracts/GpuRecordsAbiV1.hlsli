#ifndef RENDERING_ENGINE_GPU_RECORDS_ABI_V1_HLSLI
#define RENDERING_ENGINE_GPU_RECORDS_ABI_V1_HLSLI

static const uint kSampleMeasureInvalidV1 = 0u;
static const uint kSampleMeasureDiscreteV1 = 1u;
static const uint kSampleMeasureSolidAngleV1 = 2u;
static const uint kSampleMeasureAreaV1 = 3u;

static const uint kBsdfLobeNoneV1 = 0u;
static const uint kBsdfLobeDiffuseV1 = 1u << 0u;
static const uint kBsdfLobeGlossyV1 = 1u << 1u;
static const uint kBsdfLobeSpecularV1 = 1u << 2u;
static const uint kBsdfLobeReflectionV1 = 1u << 3u;
static const uint kBsdfLobeTransmissionV1 = 1u << 4u;

static const uint kPathFlagNoneV1 = 0u;
static const uint kPathFlagActiveV1 = 1u << 0u;
static const uint kPathFlagLastEventDeltaV1 = 1u << 1u;
static const uint kPathFlagTerminatedV1 = 1u << 2u;
static const uint kPathFlagInvalidV1 = 1u << 3u;

static const uint kShadowFlagNoneV1 = 0u;
static const uint kShadowFlagFiniteDistanceV1 = 1u << 0u;
static const uint kShadowFlagAlphaTestV1 = 1u << 1u;

struct GpuBsdfSampleV1
{
    float4 directionPdf;
    float4 valueEta;
    uint4 metadata;
    uint4 reserved0;
};

struct GpuLightSampleV1
{
    float4 positionDistance;
    float4 directionCombinedPdf;
    float4 radianceDiscretePdf;
    float4 conditionalPdf;
    uint4 identity;
    uint4 metadata;
};

struct GpuRayQueueRecordV1
{
    float4 originTMin;
    float4 directionTMax;
    uint4 identity;
    uint4 rng;
};

struct GpuHitQueueRecordV1
{
    float4 positionT;
    float4 geometricNormalBaryU;
    float4 shadingNormalBaryV;
    uint4 ids;
    uint4 metadata;
    uint4 reserved0;
};

struct GpuShadowQueueRecordV1
{
    float4 originTMin;
    float4 directionTMax;
    float4 contribution;
    uint4 identity;
    uint4 metadata;
};

struct GpuPathStateV1
{
    float4 radiance;
    float4 throughput;
    float4 previousPositionPdf;
    uint4 rng;
    uint4 identity;
    uint4 metadata;
};

#endif
