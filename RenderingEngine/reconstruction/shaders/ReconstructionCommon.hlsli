#ifndef RENDERING_ENGINE_L8_RECONSTRUCTION_COMMON_HLSLI
#define RENDERING_ENGINE_L8_RECONSTRUCTION_COMMON_HLSLI

// L8-private records. They intentionally do not claim compatibility with a
// shared renderer ABI; the host adapter owns packing these resources.
struct L8GBufferRecord
{
    float linearDepth;
    float3 worldNormal;
    float3 diffuseAlbedo;
    float diffuseReserved;
    float3 specularAlbedo;
    // Positive view-space depth of this same point in the previous camera.
    float expectedPreviousLinearDepth;
    float2 motion; // previous jittered UV - current jittered UV
    uint materialId;
    uint objectId;
    uint valid;
    uint motionValid;
    uint2 reserved;
};

struct L8SignalRecord
{
    float3 diffuse;
    float diffuseReserved;
    float3 specular;
    float specularReserved;
};

struct L8HistoryRecord
{
    float3 demodulatedDiffuse;
    float diffuseReserved;
    float3 demodulatedSpecular;
    float specularReserved;
    float2 moments;
    float variance;
    float linearDepth;
    float3 worldNormal;
    uint historyLength;
    uint materialId;
    uint objectId;
    uint valid;
    uint reserved;
};

struct L8TemporalDebugRecord
{
    uint accepted;
    uint rejectReasons;
};

static const uint L8_REJECT_NO_HISTORY = 1u << 0u;
static const uint L8_REJECT_RESET = 1u << 1u;
static const uint L8_REJECT_SCREEN_BOUNDS = 1u << 2u;
static const uint L8_REJECT_INVALID_CURRENT = 1u << 3u;
static const uint L8_REJECT_INVALID_HISTORY = 1u << 4u;
static const uint L8_REJECT_NON_FINITE = 1u << 5u;
static const uint L8_REJECT_DEPTH = 1u << 6u;
static const uint L8_REJECT_NORMAL = 1u << 7u;
static const uint L8_REJECT_MATERIAL_ID = 1u << 8u;
static const uint L8_REJECT_OBJECT_ID = 1u << 9u;
static const uint L8_REJECT_INVALID_MOTION = 1u << 10u;

static const uint L8_OUTPUT_RAW = 0u;
static const uint L8_OUTPUT_TEMPORAL = 1u;
static const uint L8_OUTPUT_ATROUS = 2u;
static const uint L8_OUTPUT_SVGF = 3u;
static const uint L8_OUTPUT_MOTION = 4u;
static const uint L8_OUTPUT_HISTORY_LENGTH = 5u;
static const uint L8_OUTPUT_MOMENTS = 6u;
static const uint L8_OUTPUT_VARIANCE = 7u;
static const uint L8_OUTPUT_ACCEPTANCE = 8u;
static const uint L8_OUTPUT_REJECT_REASONS = 9u;

uint L8LinearIndex(uint2 pixel, uint2 extent)
{
    return pixel.y * extent.x + pixel.x;
}

float L8Luminance(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 L8SafeAlbedo(float3 albedo, float minimumAlbedo)
{
    const float safeMinimum = isfinite(minimumAlbedo) && minimumAlbedo > 0.0f
        ? minimumAlbedo
        : 1.0e-3f;
    return float3(
        isfinite(albedo.x) ? max(albedo.x, safeMinimum) : safeMinimum,
        isfinite(albedo.y) ? max(albedo.y, safeMinimum) : safeMinimum,
        isfinite(albedo.z) ? max(albedo.z, safeMinimum) : safeMinimum);
}

float3 L8SafeNormalize(float3 value)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-20f ? value * rsqrt(lengthSquared) : float3(0.0f, 0.0f, 1.0f);
}

bool L8IsUsableNormal(float3 value)
{
    return all(isfinite(value)) && dot(value, value) > 1.0e-20f;
}

bool L8IsFiniteSignal(L8SignalRecord signal)
{
    return all(isfinite(signal.diffuse)) && all(isfinite(signal.specular));
}

L8SignalRecord L8Demodulate(
    L8SignalRecord signal,
    L8GBufferRecord gbuffer,
    float minimumAlbedo,
    uint demodulateSpecular)
{
    L8SignalRecord result = (L8SignalRecord)0;
    result.diffuse = signal.diffuse / L8SafeAlbedo(gbuffer.diffuseAlbedo, minimumAlbedo);
    result.specular = demodulateSpecular != 0u
        ? signal.specular / L8SafeAlbedo(gbuffer.specularAlbedo, minimumAlbedo)
        : signal.specular;
    return result;
}

L8SignalRecord L8Remodulate(
    L8SignalRecord signal,
    L8GBufferRecord gbuffer,
    float minimumAlbedo,
    uint demodulateSpecular)
{
    L8SignalRecord result = (L8SignalRecord)0;
    result.diffuse = signal.diffuse * L8SafeAlbedo(gbuffer.diffuseAlbedo, minimumAlbedo);
    result.specular = demodulateSpecular != 0u
        ? signal.specular * L8SafeAlbedo(gbuffer.specularAlbedo, minimumAlbedo)
        : signal.specular;
    return result;
}

float3 L8RejectReasonColor(uint reasons)
{
    if (reasons == 0u) return float3(0.0f, 1.0f, 0.0f);
    if ((reasons & L8_REJECT_RESET) != 0u) return float3(1.0f, 1.0f, 0.0f);
    if ((reasons & L8_REJECT_SCREEN_BOUNDS) != 0u) return float3(1.0f, 0.0f, 1.0f);
    if ((reasons & L8_REJECT_DEPTH) != 0u) return float3(0.0f, 0.25f, 1.0f);
    if ((reasons & L8_REJECT_NORMAL) != 0u) return float3(0.0f, 1.0f, 1.0f);
    if ((reasons & L8_REJECT_MATERIAL_ID) != 0u) return float3(0.0f, 0.6f, 0.0f);
    if ((reasons & L8_REJECT_OBJECT_ID) != 0u) return float3(0.6f, 0.0f, 1.0f);
    if ((reasons & L8_REJECT_INVALID_MOTION) != 0u) return float3(1.0f, 0.2f, 0.2f);
    if ((reasons & L8_REJECT_NON_FINITE) != 0u) return float3(1.0f, 0.4f, 0.0f);
    if ((reasons & (L8_REJECT_INVALID_CURRENT | L8_REJECT_INVALID_HISTORY)) != 0u)
        return float3(1.0f, 0.0f, 0.0f);
    return 0.35f.xxx;
}

#endif
