#ifndef RENDERING_ENGINE_RESTIR_CANDIDATE_GENERATION_HLSLI
#define RENDERING_ENGINE_RESTIR_CANDIDATE_GENERATION_HLSLI

#include "RestirPrivateTypes.hlsli"

float RestirLuminance(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

RestirCandidate RestirInvalidCandidate()
{
    RestirCandidate candidate = (RestirCandidate)0;
    candidate.identity = kRestirInvalidId.xxxx;
    candidate.source = kRestirSourceInvalid.xxxx;
    return candidate;
}

// Uniform and power-weighted analytic light proposals both pass a discrete PMF.
// Selection identity and generation are never inferred from array position.
RestirCandidate RestirMakeAnalyticCandidate(
    uint originSource,
    uint stableLightId,
    uint lightGeneration,
    uint sampleId,
    float3 lightPosition,
    float3 radiance,
    float proposalPmf,
    RestirSurface surface)
{
    RestirCandidate candidate = RestirInvalidCandidate();
    candidate.identity = uint4(stableLightId, kRestirInvalidId, sampleId, lightGeneration);
    candidate.source = uint4(originSource, kRestirSourceInvalid, 0u, 0u);
    const float3 offset = lightPosition - surface.positionDepth.xyz;
    const float distanceSquared = dot(offset, offset);
    if (!(distanceSquared > 1.0e-12f) || !(proposalPmf > 0.0f)) return candidate;
    const float distanceToLight = sqrt(distanceSquared);
    const float3 direction = offset / distanceToLight;
    const float cosine = max(0.0f, dot(normalize(surface.normalThin.xyz), direction));
    const float3 contribution = radiance * cosine / distanceSquared;
    candidate.directionDistance = float4(direction, distanceToLight);
    candidate.contributionTarget = float4(contribution, max(0.0f, RestirLuminance(contribution)));
    candidate.proposalSupportCorrection = float4(proposalPmf, cosine > 0.0f ? 1.0f : 0.0f, 1.0f, 0.0f);
    return candidate;
}

// Triangle sampling uses area measure: p_A = selectionPmf / area. The target
// contains receiver cosine * emitter cosine / distance^2 in the same measure.
RestirCandidate RestirMakeEmissiveTriangleCandidate(
    uint stableLightId,
    uint stablePrimitiveId,
    uint lightGeneration,
    float3 p0,
    float3 p1,
    float3 p2,
    float3 radiance,
    float selectionPmf,
    float2 uniformSample,
    bool twoSided,
    RestirSurface surface)
{
    RestirCandidate candidate = RestirInvalidCandidate();
    candidate.identity = uint4(stableLightId, stablePrimitiveId, 0u, lightGeneration);
    candidate.source = uint4(kRestirSourceEmissiveTriangle, kRestirSourceInvalid, 0u, 0u);
    const float3 crossValue = cross(p1 - p0, p2 - p0);
    const float doubleArea = length(crossValue);
    const float area = 0.5f * doubleArea;
    if (!(area > 1.0e-12f) || !(selectionPmf > 0.0f)) return candidate;
    const float root = sqrt(saturate(uniformSample.x));
    const float3 barycentric = float3(
        1.0f - root,
        root * (1.0f - saturate(uniformSample.y)),
        root * saturate(uniformSample.y));
    const float3 sampledPosition = p0 * barycentric.x + p1 * barycentric.y + p2 * barycentric.z;
    const float3 offset = sampledPosition - surface.positionDepth.xyz;
    const float distanceSquared = dot(offset, offset);
    candidate.proposalSupportCorrection = float4(selectionPmf / area, 0.0f, 1.0f, 0.0f);
    if (!(distanceSquared > 1.0e-12f)) return candidate;
    const float distanceToLight = sqrt(distanceSquared);
    const float3 direction = offset / distanceToLight;
    const float receiverCosine = max(0.0f, dot(normalize(surface.normalThin.xyz), direction));
    const float rawEmitterCosine = dot(crossValue / doubleArea, -direction);
    const float emitterCosine = twoSided ? abs(rawEmitterCosine) : max(0.0f, rawEmitterCosine);
    const float geometry = receiverCosine * emitterCosine / distanceSquared;
    const float3 contribution = radiance * geometry;
    candidate.directionDistance = float4(direction, distanceToLight);
    candidate.contributionTarget = float4(contribution, max(0.0f, RestirLuminance(contribution)));
    candidate.proposalSupportCorrection.y = geometry > 0.0f ? 1.0f : 0.0f;
    return candidate;
}

// Environment sampling uses solid-angle measure: p_omega = texelPmf / dOmega.
RestirCandidate RestirMakeEnvironmentCandidate(
    uint stableLightId,
    uint lightGeneration,
    uint cellId,
    float3 direction,
    float3 radiance,
    float texelPmf,
    float solidAngle,
    RestirSurface surface)
{
    RestirCandidate candidate = RestirInvalidCandidate();
    candidate.identity = uint4(stableLightId, kRestirInvalidId, cellId, lightGeneration);
    candidate.source = uint4(kRestirSourceEnvironment, kRestirSourceInvalid, 0u, 0u);
    if (!(solidAngle > 0.0f) || !(texelPmf > 0.0f)) return candidate;
    const float3 normalizedDirection = normalize(direction);
    const float cosine = max(0.0f, dot(normalize(surface.normalThin.xyz), normalizedDirection));
    const float3 contribution = radiance * cosine;
    candidate.directionDistance = float4(normalizedDirection, 3.402823466e+38f);
    candidate.contributionTarget = float4(contribution, max(0.0f, RestirLuminance(contribution)));
    candidate.proposalSupportCorrection = float4(
        texelPmf / solidAngle,
        cosine > 0.0f ? 1.0f : 0.0f,
        1.0f,
        0.0f);
    return candidate;
}

#endif
