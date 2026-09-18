#ifndef RENDERING_ENGINE_RESTIR_PRODUCTION_V3_HLSLI
#define RENDERING_ENGINE_RESTIR_PRODUCTION_V3_HLSLI

#include "../include/contracts/AbiV3.hlsli"
#include "../include/bsdf/PbrBsdf.hlsli"

static const uint kRestirEstimatorBiasedV3 = 0u;
// This mode evaluates the existing source-aware visibility correction.  Its
// numeric value is ABI-v3, but the implementation is not by itself a proof of
// an unbiased estimator for every reuse configuration.
static const uint kRestirEstimatorReferenceCorrectionV3 = 1u;
static const uint kRestirInvalidIndexV3 = 0xffffffffu;

[[vk::binding(0, 5)]] ConstantBuffer<GpuRestirParametersV3> gRestirParametersV3;

uint RestirHashV3(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float RestirRandomV3(inout uint state)
{
    state = RestirHashV3(state + 0x9e3779b9u);
    return (float)(state >> 8u) * (1.0f / 16777216.0f);
}

bool RestirSurfaceValidV3(GpuPrimarySurfaceV2 surface);

uint RestirShadowRayCountV3()
{
    const uint shadowMode = gRestirParametersV3.historyGenerations.w;
    return shadowMode == kRestirShadowPcfV3
        ? kRestirPcfFilterRayCountV3
        : (shadowMode == kRestirShadowPcssV3
            ? kRestirPcssVisibilityRayCountV3
            : kRestirPhysicalVisibilityRayCountV3);
}

void RestirBuildShadowBasisV3(
    float3 direction,
    out float3 tangent,
    out float3 bitangent)
{
    const float3 referenceAxis = abs(direction.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(0.0f, 1.0f, 0.0f);
    tangent = normalize(cross(referenceAxis, direction));
    bitangent = cross(direction, tangent);
}

float RestirShadowRotationV3(
    uint queryIndex,
    GpuPersistentLightSampleV3 sample)
{
    uint state = RestirHashV3(queryIndex
        ^ RestirHashV3(sample.identity.x)
        ^ RestirHashV3(sample.identity.y + 0x9e3779b9u)
        ^ RestirHashV3(gRestirParametersV3.generations.w));
    return 6.28318530718f * RestirRandomV3(state);
}

float RestirShadowKernelAngularRadiusV3(
    GpuPersistentLightSampleV3 sample,
    float maximumDistance)
{
    float angularRadius = 0.0f;
    if (maximumDistance < 1.0e20f)
    {
        if ((sample.metadata.y & kRestirSampleFlagHasAreaPdfV3) != 0u
            && sample.conditionalPdf.x > 0.0f)
        {
            const float equivalentWorldRadius = sqrt(
                rcp(sample.conditionalPdf.x * 3.14159265359f));
            angularRadius = equivalentWorldRadius
                / max(maximumDistance, 1.0e-4f);
        }
        else if ((sample.metadata.y
                & kRestirSampleFlagHasSolidAnglePdfV3) != 0u
            && sample.conditionalPdf.y > 0.0f)
        {
            const float solidAngle = min(
                rcp(sample.conditionalPdf.y), 6.28318530718f);
            angularRadius = acos(clamp(
                1.0f - solidAngle / 6.28318530718f, -1.0f, 1.0f));
        }
    }
    // Delta/infinite lights have no finite emitter radius in the persistent
    // sample. PCF/PCSS remain explicit teaching modes via this small kernel.
    return clamp(max(angularRadius, 0.0025f), 0.0005f, 0.08f);
}

float RestirPcssFilterKernelRatioV3(uint filterTap)
{
    if (filterTap == 0u) return 0.0f;
    const float radial = sqrt(
        (float(filterTap) - 0.5f)
        / float(kRestirPcssFilterRayCountV3 - 1u));
    return 4.0f * radial;
}

float3 RestirShadowTapDirectionV3(
    float3 centerDirection,
    float3 tangent,
    float3 bitangent,
    uint tap,
    uint tapCount,
    float kernelRatio,
    float rotation)
{
    const float radial = sqrt((float(tap) + 0.5f) / float(tapCount));
    const float angle = rotation
        + 6.28318530718f * (float(tap) * 0.61803398875f);
    const float2 disk = radial * float2(cos(angle), sin(angle));
    return normalize(centerDirection
        + kernelRatio * (disk.x * tangent + disk.y * bitangent));
}

float3 RestirPcssFilterDirectionV3(
    float3 centerDirection,
    float3 tangent,
    float3 bitangent,
    uint filterTap,
    float angularRadius,
    float rotation)
{
    if (filterTap == 0u) return centerDirection;
    const float angle = rotation
        + 6.28318530718f * (float(filterTap) * 0.61803398875f);
    const float2 disk = float2(cos(angle), sin(angle));
    return normalize(centerDirection
        + angularRadius * RestirPcssFilterKernelRatioV3(filterTap)
            * (disk.x * tangent + disk.y * bitangent));
}

GpuRayQueueRecordV1 RestirMakeVisibilityRayV3(
    uint rayIndex,
    uint queryIndex,
    uint tapIndex,
    GpuRestirReservoirV3 reservoir,
    GpuPrimarySurfaceV2 surface)
{
    GpuRayQueueRecordV1 ray = (GpuRayQueueRecordV1)0;
    ray.identity = uint4(rayIndex, queryIndex, 0u, 0u);
    if ((reservoir.state.z & kRestirReservoirFlagValidV3) == 0u
        || !RestirSurfaceValidV3(surface))
        return ray;

    const float3 normal = normalize(surface.geometricNormalRoughness.xyz);
    float3 direction = normalize(reservoir.selected.directionCombinedPdf.xyz);
    float maximumDistance = 1.0e30f;
    if (isfinite(reservoir.selected.positionDistance.w)
        && reservoir.selected.positionDistance.w > 0.0f)
    {
        const float3 displacement = reservoir.selected.positionDistance.xyz
            - surface.worldPositionLinearDepth.xyz;
        const float distance = length(displacement);
        if (distance > 2.0e-4f)
        {
            direction = displacement / distance;
            maximumDistance = max(distance - 1.0e-4f, 1.0e-4f);
        }
    }

    const uint shadowMode = gRestirParametersV3.historyGenerations.w;
    if (shadowMode != kRestirShadowPhysicalV3)
    {
        float3 tangent;
        float3 bitangent;
        RestirBuildShadowBasisV3(direction, tangent, bitangent);
        const float rotation = RestirShadowRotationV3(
            queryIndex, reservoir.selected);
        const float angularRadius = RestirShadowKernelAngularRadiusV3(
            reservoir.selected, maximumDistance);
        if (shadowMode == kRestirShadowPcfV3)
        {
            direction = RestirShadowTapDirectionV3(
                direction, tangent, bitangent,
                tapIndex, kRestirPcfFilterRayCountV3,
                angularRadius, rotation);
        }
        else
        {
            direction = tapIndex < kRestirPcssBlockerRayCountV3
                ? RestirShadowTapDirectionV3(
                    direction, tangent, bitangent,
                    tapIndex, kRestirPcssBlockerRayCountV3,
                    angularRadius * 0.5f, rotation)
                : RestirPcssFilterDirectionV3(
                    direction, tangent, bitangent,
                    tapIndex - kRestirPcssBlockerRayCountV3,
                    angularRadius, rotation);
        }
    }

    ray.originTMin = float4(
        surface.worldPositionLinearDepth.xyz + normal * 1.0e-4f, 1.0e-4f);
    ray.directionTMax = float4(direction, maximumDistance);
    ray.identity.w = 0xffu;
    return ray;
}

bool RestirVisibilityHitMatchesV3(
    GpuHitQueueRecordV1 hit,
    uint rayIndex,
    uint queryIndex)
{
    return hit.metadata.x == rayIndex && hit.metadata.w == queryIndex
        && hit.metadata.y != kHitKindInvalidV0;
}

float RestirBinaryVisibilityV3(
    GpuHitQueueRecordV1 hit,
    uint rayIndex,
    uint queryIndex)
{
    return RestirVisibilityHitMatchesV3(hit, rayIndex, queryIndex)
        && hit.metadata.y == kHitKindMissV0 ? 1.0f : 0.0f;
}

float RestirLuminanceV3(float3 value)
{
    return dot(max(value, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

bool RestirFiniteNonNegativeV3(float value)
{
    return isfinite(value) && value >= 0.0f;
}

bool RestirSurfaceValidV3(GpuPrimarySurfaceV2 surface)
{
    return (surface.identity.w & kPrimarySurfaceFlagValidV2) != 0u
        && all(isfinite(surface.worldPositionLinearDepth))
        && all(isfinite(surface.geometricNormalRoughness))
        && all(isfinite(surface.shadingNormalMetallic))
        && all(isfinite(surface.diffuseAlbedo))
        && all(isfinite(surface.specularAlbedo))
        && all(surface.diffuseAlbedo.xyz >= 0.0f)
        && all(surface.diffuseAlbedo.xyz <= 1.0f)
        && all(surface.specularAlbedo.xyz >= 0.0f)
        && all(surface.specularAlbedo.xyz <= 1.0f)
        && surface.shadingNormalMetallic.w >= 0.0f
        && surface.shadingNormalMetallic.w <= 1.0f
        && BsdfRoughnessIsFiniteModelL6(
            surface.geometricNormalRoughness.w)
        && dot(surface.geometricNormalRoughness.xyz,
            surface.geometricNormalRoughness.xyz) > 1.0e-12f
        && dot(surface.shadingNormalMetallic.xyz,
            surface.shadingNormalMetallic.xyz) > 1.0e-12f;
}

bool RestirSampleValidV3(GpuPersistentLightSampleV3 sample)
{
    return (sample.metadata.y & kRestirSampleFlagValidV3) != 0u
        && sample.identity.x != kRestirInvalidIndexV3
        && sample.generation.x != 0u
        && sample.generation.y == gRestirParametersV3.generations.x
        && RestirFiniteNonNegativeV3(sample.directionCombinedPdf.w)
        && RestirFiniteNonNegativeV3(sample.radianceDiscretePdf.w)
        && all(isfinite(sample.directionCombinedPdf.xyz))
        && all(isfinite(sample.radianceDiscretePdf.xyz));
}

void RestirEvaluateUnshadowedV3(
    GpuPersistentLightSampleV3 sample,
    GpuPrimarySurfaceV2 surface,
    out float3 diffuse,
    out float3 specular,
    out float target)
{
    diffuse = 0.0f;
    specular = 0.0f;
    target = 0.0f;
    if (!RestirSampleValidV3(sample) || !RestirSurfaceValidV3(surface)) return;

    const float3 n = normalize(surface.shadingNormalMetallic.xyz);
    const float3 l = normalize(sample.directionCombinedPdf.xyz);
    const float3 v = normalize(
        gRestirParametersV3.cameraPosition.xyz - surface.worldPositionLinearDepth.xyz);
    const float noL = saturate(dot(n, l));
    const float noV = saturate(dot(n, v));
    if (!(noL > 0.0f) || !(noV > 0.0f)) return;

    const float3 h = normalize(l + v);
    const float noH = saturate(dot(n, h));
    const float voH = saturate(dot(v, h));
    const float roughness = surface.geometricNormalRoughness.w;
    // ABI-v2 publishes material-decomposed albedos. Re-applying metallic here
    // would suppress diffuse twice and would lerp a fully metallic F0 to zero.
    const float3 diffuseAlbedo = max(surface.diffuseAlbedo.xyz, 0.0f);
    const float3 f0 = max(surface.specularAlbedo.xyz, 0.0f);
    const float3 f = BsdfFresnelSchlickL6(voH, f0);
    const float alpha = roughness * roughness;
    // Reuse the same GGX distribution and height-correlated Smith masking as
    // the production metallic-roughness BSDF.  PrimarySurfaceV2 already owns
    // the diffuse metallic/transmission decomposition.
    const float d = BsdfGgxDL6(float3(0.0f, 0.0f, noH), alpha);
    const float g = BsdfGgxG2L6(
        float3(sqrt(max(0.0f, 1.0f - noV * noV)), 0.0f, noV),
        float3(sqrt(max(0.0f, 1.0f - noL * noL)), 0.0f, noL),
        alpha);
    const float3 radiance = max(sample.radianceDiscretePdf.xyz, 0.0f);
    diffuse = radiance * diffuseAlbedo * (1.0f - f)
        * (noL * kBsdfInversePiL6);
    specular = radiance * f * (d * g / max(4.0f * noV, 1.0e-6f));
    if (!all(isfinite(diffuse)) || !all(isfinite(specular)))
    {
        diffuse = 0.0f;
        specular = 0.0f;
        return;
    }
    target = RestirLuminanceV3(diffuse + specular);
}

GpuRestirCandidateV3 RestirEvaluateCandidateV3(
    GpuPersistentLightSampleV3 sample,
    GpuPrimarySurfaceV2 surface,
    uint candidateSource,
    uint reuseSource,
    uint sourceSurfaceIndex)
{
    GpuRestirCandidateV3 candidate = (GpuRestirCandidateV3)0;
    candidate.sample = sample;
    float3 diffuse;
    float3 specular;
    float target;
    RestirEvaluateUnshadowedV3(sample, surface, diffuse, specular, target);
    const float proposalPdf = sample.directionCombinedPdf.w;
    const bool surfaceValid = RestirSurfaceValidV3(surface);
    const float support = RestirSampleValidV3(sample) && surfaceValid
        && target > 0.0f
        && proposalPdf > 0.0f ? 1.0f : 0.0f;
    candidate.targetProposalSupportCorrection =
        float4(target, proposalPdf, support, surfaceValid ? 1.0f : 0.0f);
    candidate.provenance = uint4(
        candidateSource, reuseSource, sourceSurfaceIndex, sample.metadata.y);
    return candidate;
}

bool RestirCandidateValidV3(GpuRestirCandidateV3 candidate)
{
    const float4 terms = candidate.targetProposalSupportCorrection;
    const bool targetSupportConsistent = terms.x > 0.0f
        ? terms.z == 1.0f : terms.z == 0.0f;
    return RestirSampleValidV3(candidate.sample)
        && RestirFiniteNonNegativeV3(terms.x)
        && isfinite(terms.y) && terms.y > 0.0f
        && (terms.z == 0.0f || terms.z == 1.0f)
        && RestirFiniteNonNegativeV3(terms.w)
        && terms.w > 0.0f && targetSupportConsistent;
}

float RestirCandidateWeightV3(GpuRestirCandidateV3 candidate)
{
    if (!RestirCandidateValidV3(candidate)) return 0.0f;
    const float4 terms = candidate.targetProposalSupportCorrection;
    return terms.x * terms.z * terms.w / terms.y;
}

GpuRestirReservoirV3 RestirEmptyReservoirV3()
{
    GpuRestirReservoirV3 reservoir = (GpuRestirReservoirV3)0;
    reservoir.selected.identity.x = kRestirInvalidIndexV3;
    reservoir.selected.identity.y = kRestirInvalidIndexV3;
    return reservoir;
}

void RestirUpdateReservoirV3(
    inout GpuRestirReservoirV3 reservoir,
    GpuRestirCandidateV3 candidate,
    float weight,
    uint multiplicity,
    float randomValue)
{
    if (!RestirCandidateValidV3(candidate) || !isfinite(weight)
        || weight < 0.0f || multiplicity == 0u)
        return;
    const uint oldM = reservoir.state.x;
    const uint newM = oldM > 0xffffffffu - multiplicity
        ? 0xffffffffu : oldM + multiplicity;
    // A well-formed zero-target proposal is still one attempted RIS sample.
    // It contributes to M but cannot select a winner or create 0/0 state.
    if (weight == 0.0f)
    {
        reservoir.state.x = newM;
        return;
    }
    const float oldSum = reservoir.weightState.x;
    const float newSum = oldSum + weight;
    if (!(newSum > 0.0f) || !isfinite(newSum)) return;
    reservoir.weightState.x = newSum;
    reservoir.state.x = newM;
    if (randomValue * newSum < weight)
    {
        reservoir.selected = candidate.sample;
        reservoir.weightState.z = candidate.targetProposalSupportCorrection.x;
        reservoir.weightState.w = candidate.targetProposalSupportCorrection.y;
        reservoir.selectedTerms = float4(
            candidate.targetProposalSupportCorrection.z,
            candidate.targetProposalSupportCorrection.w,
            weight,
            0.0f);
        reservoir.provenance = candidate.provenance;
    }
}

void RestirFinalizeReservoirV3(inout GpuRestirReservoirV3 reservoir)
{
    if (reservoir.state.x == 0u || !(reservoir.weightState.z > 0.0f)
        || !(reservoir.weightState.x > 0.0f)
        || !isfinite(reservoir.weightState.z)
        || !isfinite(reservoir.weightState.x))
    {
        reservoir.state.z &= ~kRestirReservoirFlagValidV3;
        reservoir.weightState.y = 0.0f;
        return;
    }
    const float denominator = (float)reservoir.state.x * reservoir.weightState.z;
    if (!(denominator > 0.0f) || !isfinite(denominator))
    {
        reservoir.state.z &= ~kRestirReservoirFlagValidV3;
        reservoir.weightState.y = 0.0f;
        return;
    }
    reservoir.weightState.y = reservoir.weightState.x / denominator;
    if (!(reservoir.weightState.y > 0.0f)
        || !isfinite(reservoir.weightState.y))
    {
        reservoir.state.z &= ~kRestirReservoirFlagValidV3;
        reservoir.weightState.y = 0.0f;
        return;
    }
    reservoir.selectedTerms.w = reservoir.weightState.y;
    reservoir.state.z |= kRestirReservoirFlagValidV3;
}

void RestirClampMV3(inout GpuRestirReservoirV3 reservoir)
{
    const uint maxM = gRestirParametersV3.reuseLimits.y;
    if (reservoir.state.x > maxM)
    {
        const float scale = (float)maxM / (float)reservoir.state.x;
        reservoir.weightState.x *= scale;
        reservoir.state.x = maxM;
        reservoir.state.z |= kRestirReservoirFlagMClampedV3;
    }
}

uint RestirValidateSurfacePairV3(
    GpuPrimarySurfaceV2 currentSurface,
    GpuPrimarySurfaceV2 sourceSurface)
{
    if (!RestirSurfaceValidV3(currentSurface)
        || !RestirSurfaceValidV3(sourceSurface))
        return kRestirRejectInvalidCandidateV3;
    if (currentSurface.identity.x != sourceSurface.identity.x)
        return kRestirRejectMaterialV3;
    if (currentSurface.identity.y != sourceSurface.identity.y)
        return kRestirRejectInstanceV3;
    const float depthScale = max(1.0f, max(
        abs(currentSurface.worldPositionLinearDepth.w),
        abs(sourceSurface.worldPositionLinearDepth.w)));
    if (abs(currentSurface.worldPositionLinearDepth.w
            - sourceSurface.worldPositionLinearDepth.w) / depthScale
        > gRestirParametersV3.validation.y)
        return kRestirRejectDepthV3;
    if (dot(normalize(currentSurface.shadingNormalMetallic.xyz),
            normalize(sourceSurface.shadingNormalMetallic.xyz))
        < gRestirParametersV3.validation.x)
        return kRestirRejectNormalV3;
    const float positionDistance = length(
        currentSurface.worldPositionLinearDepth.xyz
        - sourceSurface.worldPositionLinearDepth.xyz);
    if (positionDistance > gRestirParametersV3.validation.z)
        return kRestirRejectThinGeometryV3;
    return kRestirRejectNoneV3;
}

#endif
