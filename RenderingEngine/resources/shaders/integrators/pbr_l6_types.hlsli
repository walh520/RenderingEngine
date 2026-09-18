#ifndef RENDERING_ENGINE_PBR_L6_TYPES_HLSLI
#define RENDERING_ENGINE_PBR_L6_TYPES_HLSLI

#include "../include/contracts/GpuRecordsAbiV1.hlsli"

static const float PBR_L6_PI = 3.14159265358979323846f;
static const float PBR_L6_TWO_PI = 6.28318530717958647692f;
static const float PBR_L6_INV_PI = 0.31830988618379067154f;
static const uint PBR_L6_INVALID_INDEX = 0xffffffffu;

static const uint PBR_L6_LIGHT_POINT = 0u;
static const uint PBR_L6_LIGHT_DIRECTIONAL = 1u;
static const uint PBR_L6_LIGHT_SPOT = 2u;
static const uint PBR_L6_LIGHT_SPHERE_AREA = 3u;
static const uint PBR_L6_LIGHT_EMISSIVE_TRIANGLE = 4u;
static const uint PBR_L6_LIGHT_ENVIRONMENT = 5u;

static const uint PBR_L6_LIGHT_ENABLED = 1u << 0u;
static const uint PBR_L6_LIGHT_TWO_SIDED = 1u << 1u;
static const uint PBR_L6_LIGHT_ANIMATED = 1u << 2u;
static const uint PBR_L6_LIGHT_DELTA = 1u << 3u;

static const uint PBR_L6_LIGHT_SELECTION_UNIFORM = 0u;
static const uint PBR_L6_LIGHT_SELECTION_POWER_WEIGHTED = 1u;
static const uint PBR_L6_ENVIRONMENT_SAMPLER_UNIFORM_SPHERE = 0u;
static const uint PBR_L6_ENVIRONMENT_SAMPLER_IMPORTANCE_MAP = 1u;

static const uint PBR_L6_MATERIAL_PURE_EMITTER = 1u << 0u;
static const uint PBR_L6_MATERIAL_THIN_WALLED = 1u << 1u;

static const uint PBR_L6_MEASURE_DISCRETE = 0u;
static const uint PBR_L6_MEASURE_AREA = 1u;
static const uint PBR_L6_MEASURE_SOLID_ANGLE = 2u;
static const uint PBR_L6_MEASURE_INVALID = 0xffffffffu;

// Direct-light estimator values are private to the L6 frame payload. They are
// deliberately a dense 0..3 range and are independent from the public
// RuntimeConfig enum, whose zero value is reserved for the legacy estimator.
static const uint PBR_L6_ESTIMATOR_BSDF_ONLY = 0u;
static const uint PBR_L6_ESTIMATOR_NEE = 1u;
static const uint PBR_L6_ESTIMATOR_MIS = 2u;
static const uint PBR_L6_ESTIMATOR_RESTIR_PRIMARY = 3u;

static const uint PBR_L6_TRAVERSAL_FIXTURE = 0u;
static const uint PBR_L6_TRAVERSAL_FLATTENED_SAH = 1u;
static const uint PBR_L6_TRAVERSAL_HARDWARE_RAY_QUERY = 2u;
static const uint PBR_L6_TRAVERSAL_CANONICAL_LINEAR = 3u;

static const uint PBR_L6_TRANSPORT_PBR = 0u;
static const uint PBR_L6_TRANSPORT_WHITTED = 1u;

static const uint PBR_L6_SHADOW_PCF = 0u;
static const uint PBR_L6_SHADOW_PCSS = 1u;
static const uint PBR_L6_SHADOW_PHYSICAL = 2u;

static const uint PBR_L6_SAMPLE_VALID = 1u << 0u;
static const uint PBR_L6_SAMPLE_DELTA = 1u << 1u;
static const uint PBR_L6_SAMPLE_INFINITE = 1u << 2u;
static const uint PBR_L6_SAMPLE_TWO_SIDED = 1u << 3u;

static const uint PBR_L6_COUNTER_CAMERA_RAYS = 0u;
static const uint PBR_L6_COUNTER_PATH_RAYS = 1u;
static const uint PBR_L6_COUNTER_SHADOW_RAYS = 2u;
static const uint PBR_L6_COUNTER_SURFACE_HITS = 3u;
static const uint PBR_L6_COUNTER_MISSES = 4u;
static const uint PBR_L6_COUNTER_VALID_LIGHT_SAMPLES = 5u;
static const uint PBR_L6_COUNTER_INVALID_LIGHT_SAMPLES = 6u;
static const uint PBR_L6_COUNTER_OCCLUDED_LIGHT_SAMPLES = 7u;
static const uint PBR_L6_COUNTER_CAMERA_EMITTER_HITS = 8u;
static const uint PBR_L6_COUNTER_DELTA_EMITTER_HITS = 9u;
static const uint PBR_L6_COUNTER_MIS_EMITTER_HITS = 10u;
static const uint PBR_L6_COUNTER_RR_TESTS = 11u;
static const uint PBR_L6_COUNTER_RR_TERMINATIONS = 12u;
static const uint PBR_L6_COUNTER_MAXIMUM_DEPTH = 13u;
static const uint PBR_L6_COUNTER_ZERO_PDF = 14u;
static const uint PBR_L6_COUNTER_NEGATIVE_PDF = 15u;
static const uint PBR_L6_COUNTER_NONFINITE_PDF = 16u;
static const uint PBR_L6_COUNTER_NONFINITE_BSDF = 17u;
static const uint PBR_L6_COUNTER_NONFINITE_THROUGHPUT = 18u;
static const uint PBR_L6_COUNTER_NONFINITE_RADIANCE = 19u;
static const uint PBR_L6_COUNTER_NEGATIVE_CONTRIBUTION = 20u;
static const uint PBR_L6_COUNTER_ALIAS_FALLBACKS = 21u;
static const uint PBR_L6_COUNTER_INVALID_MATERIAL = 22u;
static const uint PBR_L6_COUNTER_INVALID_BSDF_EVALUATION = 23u;
static const uint PBR_L6_COUNTER_INVALID_BSDF_SAMPLE = 24u;
static const uint PBR_L6_COUNTER_INVALID_FRAME = 25u;
static const uint PBR_L6_COUNTER_COUNT = 26u;

static const uint PBR_L6_CAMERA_DIMENSION_COUNT = 4u;
static const uint PBR_L6_DIMENSIONS_PER_BOUNCE = 16u;
static const uint PBR_L6_DIM_LIGHT_SELECTION = 0u;
static const uint PBR_L6_DIM_LIGHT_SHAPE_0 = 1u;
static const uint PBR_L6_DIM_LIGHT_SHAPE_1 = 2u;
static const uint PBR_L6_DIM_LIGHT_SHAPE_2 = 3u;
static const uint PBR_L6_DIM_LIGHT_SHAPE_3 = 4u;
static const uint PBR_L6_DIM_BSDF_LOBE = 5u;
static const uint PBR_L6_DIM_BSDF_U = 6u;
static const uint PBR_L6_DIM_BSDF_V = 7u;
static const uint PBR_L6_DIM_DIELECTRIC_EVENT = 8u;
static const uint PBR_L6_DIM_RUSSIAN_ROULETTE = 9u;
static const uint PBR_L6_DIM_ALPHA_MASK = 10u;

struct PbrMaterialGpuL6
{
    float4 baseColorMetallic;
    float4 emissiveRoughness;
    float4 transmissionIor;
    float4 attenuationColorDistance;
    float4 f0;
    float4 conductorEta;
    float4 conductorK;
    uint4 metadata;
};

struct PbrLightGpuL6
{
    float4 positionRange;
    float4 directionCosOuter;
    float4 radianceScale;
    float4 shapeParams;
    uint4 identity;
    uint4 payload;
};

struct PbrAliasEntryGpuL6
{
    float q;
    float pmf;
    uint alias;
    uint item;
};

struct PbrEmitterMapEntryGpuL6
{
    uint instanceId;
    uint primitiveId;
    uint lightIndex;
    uint reserved;
};

struct PbrFixtureTriangleGpuL6
{
    float4 p0;
    float4 p1;
    float4 p2;
    uint4 metadata;
};

struct PbrFixtureSphereGpuL6
{
    float4 centerRadius;
    uint4 metadata;
};

struct PbrFrameConstantsGpuL6
{
    float4 cameraPositionTanHalfFov;
    float4 cameraForwardAspect;
    float4 cameraRightLensRadius;
    float4 cameraUpExposure;
    uint4 image;
    uint4 trace;
    uint4 sampling;    // seed low/high, light selection, direct estimator.
    uint4 environment; // light index, width, height, environment sampler.
    uint4 distribution;
    float4 russianRoulette;
    float4 sceneCenterRadius;
    float4 environmentToWorld0;
    float4 environmentToWorld1;
    float4 environmentToWorld2;
    float4 worldToEnvironment0;
    float4 worldToEnvironment1;
    float4 worldToEnvironment2;
    uint4 traversal; // mode, flattened node count, triangle count, alpha-atlas layers.
    uint4 output;    // stream tag, alpha sampler ID, transport model, shadow method.
};

struct PbrRayL6
{
    float3 origin;
    float3 direction;
};

struct PbrHitL6
{
    float t;
    float3 position;
    float3 geometricNormal;
    float3 shadingNormal;
    uint materialIndex;
    uint instanceId;
    uint primitiveId;
    uint emitterLightIndex;
    uint frontFace;
};

struct PbrLightContextL6
{
    float3 position;
    float3 geometricNormal;
    float3 shadingNormal;
};

struct PbrLightSampleL6
{
    float3 wi;
    float distance;
    float3 Li;
    float selectionPmf;
    float conditionalPdf;
    float combinedPdfW;
    uint lightIndex;
    uint instanceId;
    uint primitiveId;
    float3 position;
    uint measure;
    float3 normal;
    uint flags;
};

struct PbrLightRandomL6
{
    float selection;
    float shape0;
    float shape1;
    float shape2;
    float shape3;
};

struct PbrPathSignalsL6
{
    float3 raw;
    float3 cameraEmission;
    float3 directDiffuse;
    float3 directSpecular;
    float3 indirectDiffuse;
    float3 indirectSpecular;
};

uint PbrPrivateLightMeasureToAbiV1L6(uint privateMeasure)
{
    if (privateMeasure == PBR_L6_MEASURE_DISCRETE)
    {
        return kSampleMeasureDiscreteV1;
    }
    if (privateMeasure == PBR_L6_MEASURE_AREA)
    {
        return kSampleMeasureAreaV1;
    }
    if (privateMeasure == PBR_L6_MEASURE_SOLID_ANGLE)
    {
        return kSampleMeasureSolidAngleV1;
    }
    return kSampleMeasureInvalidV1;
}

uint PbrAbiV1MeasureToPrivateLightMeasureL6(uint abiMeasure)
{
    if (abiMeasure == kSampleMeasureDiscreteV1)
    {
        return PBR_L6_MEASURE_DISCRETE;
    }
    if (abiMeasure == kSampleMeasureAreaV1)
    {
        return PBR_L6_MEASURE_AREA;
    }
    if (abiMeasure == kSampleMeasureSolidAngleV1)
    {
        return PBR_L6_MEASURE_SOLID_ANGLE;
    }
    return PBR_L6_MEASURE_INVALID;
}

bool PbrIsFiniteFloatL6(float value)
{
    return isfinite(value);
}

bool PbrIsFinite3L6(float3 value)
{
    return all(isfinite(value));
}

bool PbrFrameSamplingIsValidL6(PbrFrameConstantsGpuL6 frame)
{
    const float rouletteStart = frame.russianRoulette.x;
    const float rouletteMinimum = frame.russianRoulette.y;
    const float rouletteMaximum = frame.russianRoulette.z;
    const float rayEpsilon = frame.russianRoulette.w;
    // RuntimeConfig constrains production dimensions to 16384. Express that
    // invariant directly: the former dynamic uint division is rejected by
    // NVIDIA's NVVM compiler in Wavefront kernels that also use atomics.
    const bool pixelCountOverflows =
        frame.image.x > 16384u || frame.image.y > 16384u;
    bool traversalValid =
        frame.traversal.x <= PBR_L6_TRAVERSAL_CANONICAL_LINEAR;
#if defined(PBR_L6_TRAVERSAL_SOFTWARE)
    traversalValid = traversalValid &&
        ((frame.traversal.x == PBR_L6_TRAVERSAL_FLATTENED_SAH &&
                frame.traversal.y > 0u && frame.traversal.z > 0u) ||
            (frame.traversal.x == PBR_L6_TRAVERSAL_CANONICAL_LINEAR &&
                frame.traversal.z > 0u));
#elif defined(PBR_L6_TRAVERSAL_RAY_QUERY)
    traversalValid = traversalValid &&
        frame.traversal.x == PBR_L6_TRAVERSAL_HARDWARE_RAY_QUERY;
#elif defined(PBR_L6_TRAVERSAL_WAVEFRONT)
    traversalValid = traversalValid &&
        ((frame.traversal.x == PBR_L6_TRAVERSAL_FLATTENED_SAH
                && frame.traversal.y > 0u && frame.traversal.z > 0u)
            || frame.traversal.x == PBR_L6_TRAVERSAL_HARDWARE_RAY_QUERY
            || (frame.traversal.x == PBR_L6_TRAVERSAL_CANONICAL_LINEAR
                && frame.traversal.z > 0u));
#else
    traversalValid = traversalValid && frame.traversal.x == PBR_L6_TRAVERSAL_FIXTURE;
#endif
    return frame.image.x != 0u && frame.image.y != 0u &&
        frame.image.z != 0xffffffffu && frame.image.w != 0u &&
        !pixelCountOverflows
        && frame.sampling.z <= PBR_L6_LIGHT_SELECTION_POWER_WEIGHTED &&
        frame.sampling.w <= PBR_L6_ESTIMATOR_RESTIR_PRIMARY &&
        frame.environment.w <= PBR_L6_ENVIRONMENT_SAMPLER_IMPORTANCE_MAP &&
        PbrIsFiniteFloatL6(rouletteStart) && rouletteStart >= 0.0f &&
        floor(rouletteStart) == rouletteStart &&
        PbrIsFiniteFloatL6(rouletteMinimum) &&
        PbrIsFiniteFloatL6(rouletteMaximum) &&
        rouletteMinimum > 0.0f && rouletteMinimum <= rouletteMaximum &&
        rouletteMaximum <= 1.0f &&
        PbrIsFiniteFloatL6(rayEpsilon) && rayEpsilon > 0.0f &&
        traversalValid;
}

float PbrMaxComponentL6(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

float PbrLuminanceL6(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 PbrTransformDirectionRowsL6(float3 direction, float4 row0, float4 row1, float4 row2)
{
    return float3(dot(row0.xyz, direction), dot(row1.xyz, direction), dot(row2.xyz, direction));
}

uint PbrBounceDimensionL6(uint depth, uint slot)
{
    return PBR_L6_CAMERA_DIMENSION_COUNT + depth * PBR_L6_DIMENSIONS_PER_BOUNCE + slot;
}

uint4 PbrPhiloxRoundL6(uint4 counter, uint2 key)
{
    const uint multiplier0 = 0xd2511f53u;
    const uint multiplier1 = 0xcd9e8d57u;
    const uint a0Low = multiplier0 & 0xffffu;
    const uint a0High = multiplier0 >> 16u;
    const uint b0Low = counter.x & 0xffffu;
    const uint b0High = counter.x >> 16u;
    const uint product0LowLow = a0Low * b0Low;
    const uint product0LowHigh = a0Low * b0High;
    const uint product0HighLow = a0High * b0Low;
    const uint product0HighHigh = a0High * b0High;
    const uint middle0 = (product0LowLow >> 16u)
        + (product0LowHigh & 0xffffu)
        + (product0HighLow & 0xffffu);
    const uint low0 = (product0LowLow & 0xffffu) | (middle0 << 16u);
    const uint high0 = product0HighHigh
        + (product0LowHigh >> 16u)
        + (product0HighLow >> 16u)
        + (middle0 >> 16u);

    const uint a1Low = multiplier1 & 0xffffu;
    const uint a1High = multiplier1 >> 16u;
    const uint b1Low = counter.z & 0xffffu;
    const uint b1High = counter.z >> 16u;
    const uint product1LowLow = a1Low * b1Low;
    const uint product1LowHigh = a1Low * b1High;
    const uint product1HighLow = a1High * b1Low;
    const uint product1HighHigh = a1High * b1High;
    const uint middle1 = (product1LowLow >> 16u)
        + (product1LowHigh & 0xffffu)
        + (product1HighLow & 0xffffu);
    const uint low1 = (product1LowLow & 0xffffu) | (middle1 << 16u);
    const uint high1 = product1HighHigh
        + (product1LowHigh >> 16u)
        + (product1HighLow >> 16u)
        + (middle1 >> 16u);
    return uint4(
        high1 ^ counter.y ^ key.x,
        low1,
        high0 ^ counter.w ^ key.y,
        low0);
}

uint4 PbrPhilox4x32TenRoundsL6(uint4 counter, uint2 key)
{
    [unroll]
    for (uint round = 0u; round < 10u; ++round)
    {
        counter = PbrPhiloxRoundL6(counter, key);
        key += uint2(0x9e3779b9u, 0xbb67ae85u);
    }
    return counter;
}

[noinline]
float PbrCounterRandomL6(
    uint pixelIndex,
    uint sampleIndex,
    uint dimension,
    uint streamTag,
    uint2 baseSeed)
{
    const uint randomBits = PbrPhilox4x32TenRoundsL6(
        uint4(pixelIndex, sampleIndex, dimension, streamTag),
        baseSeed).x;
    // The top 24 bits are exactly representable as float. This mapping is
    // therefore strictly [0, 1), including randomBits == 0xffffffff.
    return float(randomBits >> 8u) * (1.0f / 16777216.0f);
}

float PbrPowerHeuristicL6(float pdfA, float pdfB)
{
    if (!PbrIsFiniteFloatL6(pdfA) || !PbrIsFiniteFloatL6(pdfB)
        || pdfA < 0.0f || pdfB < 0.0f)
    {
        return 0.0f;
    }
    const float maximumPdf = max(pdfA, pdfB);
    if (!(maximumPdf > 0.0f) || !PbrIsFiniteFloatL6(maximumPdf))
    {
        return 0.0f;
    }
    const float normalizedA = pdfA / maximumPdf;
    const float normalizedB = pdfB / maximumPdf;
    const float squaredA = normalizedA * normalizedA;
    const float squaredB = normalizedB * normalizedB;
    return squaredA / (squaredA + squaredB);
}

#endif
