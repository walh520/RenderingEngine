#ifndef RENDERING_ENGINE_RESTIR_PRIVATE_TYPES_HLSLI
#define RENDERING_ENGINE_RESTIR_PRIVATE_TYPES_HLSLI

// L9-private provisional records. They intentionally do not claim a shared ABI.
// Candidate identity is stable across proposal remapping. originSource records
// uniform/power/emissive/environment; reuseSource records temporal/spatial use.
static const uint kRestirInvalidId = 0xffffffffu;

static const uint kRestirSourceUniform = 0u;
static const uint kRestirSourcePower = 1u;
static const uint kRestirSourceEmissiveTriangle = 2u;
static const uint kRestirSourceEnvironment = 3u;
static const uint kRestirSourceTemporal = 4u;
static const uint kRestirSourceSpatial = 5u;
static const uint kRestirSourceInvalid = 0xffffffffu;

static const uint kRestirModeBiased = 0u;
static const uint kRestirModeUnbiasedReference = 1u;

static const uint kRestirReservoirValid = 1u << 0u;
static const uint kRestirReservoirMClamped = 1u << 1u;
static const uint kRestirReservoirTemporalAccepted = 1u << 2u;
static const uint kRestirReservoirSpatialAccepted = 1u << 3u;
static const uint kRestirReservoirReferenceMode = 1u << 4u;
static const uint kRestirReservoirFinalVisibility = 1u << 5u;

static const uint kRestirRejectEmpty = 1u << 0u;
static const uint kRestirRejectOutside = 1u << 1u;
static const uint kRestirRejectMotion = 1u << 2u;
static const uint kRestirRejectCameraCut = 1u << 3u;
static const uint kRestirRejectResize = 1u << 4u;
static const uint kRestirRejectDepth = 1u << 5u;
static const uint kRestirRejectNormal = 1u << 6u;
static const uint kRestirRejectInstance = 1u << 7u;
static const uint kRestirRejectThinGeometry = 1u << 8u;
static const uint kRestirRejectSceneGeneration = 1u << 9u;
static const uint kRestirRejectLightGeneration = 1u << 10u;
static const uint kRestirRejectLightDeleted = 1u << 11u;
static const uint kRestirRejectAge = 1u << 12u;
static const uint kRestirRejectCandidate = 1u << 13u;
static const uint kRestirRejectMaterial = 1u << 14u;
static const uint kRestirRejectPosition = 1u << 15u;

static const uint kRestirCounterCandidate = 0u;
static const uint kRestirCounterInvalidCandidate = 1u;
static const uint kRestirCounterZeroTarget = 2u;
static const uint kRestirCounterZeroPdf = 3u;
static const uint kRestirCounterZeroSupport = 4u;
static const uint kRestirCounterTemporalAccepted = 5u;
static const uint kRestirCounterTemporalRejected = 6u;
static const uint kRestirCounterSpatialAccepted = 7u;
static const uint kRestirCounterSpatialRejected = 8u;
static const uint kRestirCounterReferenceVisibility = 9u;
static const uint kRestirCounterFinalVisibility = 10u;
static const uint kRestirCounterDuplicateFinalVisibility = 11u;
static const uint kRestirCounterMClamp = 12u;

struct RestirCandidate
{
    uint4 identity; // stable light ID, primitive ID, sample ID, light generation.
    uint4 source;   // origin source, reuse source, source-surface index, reserved.
    float4 directionDistance;
    float4 contributionTarget; // unshadowed RGB, scalar target.
    float4 proposalSupportCorrection; // proposal PDF, support, correction, reserved.
};

struct RestirReservoir
{
    RestirCandidate selected;
    float4 weights; // weightSum, normalization W, reserved, reserved.
    uint4 metadata; // M, age, flags, reserved.
};

struct RestirSurface
{
    float4 positionDepth;
    float4 normalThin; // normal xyz, thin geometry bool.
    uint4 identity;    // instance, primitive, material, scene generation.
};

struct RestirDebug
{
    uint4 selectedIdentity;
    uint4 sourceAndState; // origin source, reuse source, M, age.
    float4 weights;       // weightSum, target, proposal PDF, normalization W.
    uint4 validation;    // temporal reject, spatial reject, flags, visibility count.
};

uint RestirHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float RestirRandom(inout uint state)
{
    state = RestirHash(state + 0x9e3779b9u);
    return (float)(state >> 8u) * (1.0f / 16777216.0f);
}

bool RestirCandidateValid(RestirCandidate candidate)
{
    return candidate.source.x != kRestirSourceInvalid &&
        candidate.identity.x != kRestirInvalidId &&
        all(isfinite(candidate.directionDistance)) &&
        all(isfinite(candidate.contributionTarget)) &&
        all(isfinite(candidate.proposalSupportCorrection.xyz)) &&
        candidate.contributionTarget.w >= 0.0f &&
        candidate.proposalSupportCorrection.x > 0.0f &&
        candidate.proposalSupportCorrection.y >= 0.0f &&
        candidate.proposalSupportCorrection.z >= 0.0f;
}

float RestirCandidateWeight(RestirCandidate candidate)
{
    if (!RestirCandidateValid(candidate)) return 0.0f;
    return candidate.contributionTarget.w * candidate.proposalSupportCorrection.y *
        candidate.proposalSupportCorrection.z / candidate.proposalSupportCorrection.x;
}

RestirReservoir RestirEmptyReservoir()
{
    RestirReservoir reservoir = (RestirReservoir)0;
    reservoir.selected.identity = kRestirInvalidId.xxxx;
    reservoir.selected.source = kRestirSourceInvalid.xxxx;
    return reservoir;
}

bool RestirUpdate(
    inout RestirReservoir reservoir,
    RestirCandidate candidate,
    float weight,
    uint multiplicity,
    float randomValue)
{
    if (!RestirCandidateValid(candidate) || multiplicity == 0u || !isfinite(weight) || weight < 0.0f)
        return false;
    // Match the CPU UpdateReservoir rejection rule: never allow the effective
    // sample count to wrap. Callers account this as an invalid candidate.
    if (multiplicity > 0xffffffffu - reservoir.metadata.x)
        return false;
    const float accumulated = reservoir.weights.x + weight;
    if (weight > 0.0f && randomValue * accumulated < weight)
        reservoir.selected = candidate;
    reservoir.weights.x = accumulated;
    reservoir.metadata.x += multiplicity;
    if (reservoir.selected.source.x != kRestirSourceInvalid)
        reservoir.metadata.z |= kRestirReservoirValid;
    return true;
}

void RestirClampM(inout RestirReservoir reservoir, uint maxM)
{
    if (maxM == 0u)
    {
        reservoir = RestirEmptyReservoir();
        return;
    }
    if (reservoir.metadata.x > maxM)
    {
        reservoir.weights.x *= (float)maxM / (float)reservoir.metadata.x;
        reservoir.metadata.x = maxM;
        reservoir.metadata.z |= kRestirReservoirMClamped;
    }
}

void RestirFinalizeNaiveBiased(inout RestirReservoir reservoir)
{
    const float denominator = (float)reservoir.metadata.x * reservoir.selected.contributionTarget.w;
    reservoir.weights.y = denominator > 0.0f ? reservoir.weights.x / denominator : 0.0f;
    if (!(reservoir.weights.y > 0.0f)) reservoir.metadata.z &= ~kRestirReservoirValid;
}

float RestirRelativeDepth(float a, float b)
{
    return abs(a - b) / max(1.0f, max(abs(a), abs(b)));
}

float3 RestirSafeNormal(float3 value)
{
    const float lengthSquared = dot(value, value);
    return all(isfinite(value)) && isfinite(lengthSquared) && lengthSquared > 0.0f
        ? value * rsqrt(lengthSquared)
        : 0.0f;
}

uint RestirValidateSurface(
    RestirSurface center,
    RestirSurface source,
    float normalCosThreshold,
    float depthThreshold,
    float positionThreshold,
    float thinGeometryThreshold,
    bool requireSameInstance)
{
    uint reason = 0u;
    if (RestirRelativeDepth(center.positionDepth.w, source.positionDepth.w) > depthThreshold)
        reason |= kRestirRejectDepth;
    if (dot(RestirSafeNormal(center.normalThin.xyz), RestirSafeNormal(source.normalThin.xyz)) < normalCosThreshold)
        reason |= kRestirRejectNormal;
    if (distance(center.positionDepth.xyz, source.positionDepth.xyz) > positionThreshold)
        reason |= kRestirRejectPosition;
    if (requireSameInstance && center.identity.x != source.identity.x)
        reason |= kRestirRejectInstance;
    if (center.identity.z != source.identity.z)
        reason |= kRestirRejectMaterial;
    if (center.identity.w != source.identity.w)
        reason |= kRestirRejectSceneGeneration;
    if ((center.normalThin.w != 0.0f || source.normalThin.w != 0.0f) &&
        (center.identity.y != source.identity.y ||
         distance(center.positionDepth.xyz, source.positionDepth.xyz) > thinGeometryThreshold))
        reason |= kRestirRejectThinGeometry;
    return reason;
}

#endif
