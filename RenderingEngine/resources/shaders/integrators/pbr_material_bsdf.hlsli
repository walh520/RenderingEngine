#ifndef RENDERING_ENGINE_PBR_MATERIAL_BSDF_L6_HLSLI
#define RENDERING_ENGINE_PBR_MATERIAL_BSDF_L6_HLSLI

// Requires pbr_l6_types.hlsli, pbr_light_sampling.hlsli (basis helper), and
// PbrBsdf.hlsli. Both L6 Megakernel and L7 Wavefront consume this exact bridge
// so material routing and eta semantics cannot drift between integrators.

BsdfParamsL6 PbrMaterialToBsdfParamsL6(PbrMaterialGpuL6 material)
{
    BsdfParamsL6 parameters;
    parameters.baseColor = material.baseColorMetallic.xyz;
    parameters.metallic = material.baseColorMetallic.w;
    parameters.f0 = material.f0.xyz;
    parameters.perceptualRoughness = material.emissiveRoughness.w;
    parameters.conductorEta = material.conductorEta.xyz;
    parameters.transmission = material.transmissionIor.x;
    parameters.conductorK = material.conductorK.xyz;
    parameters.model = material.metadata.x;
    parameters.allowedLobes = material.metadata.y;
    parameters.useComplexConductorFresnel = material.metadata.z;
    parameters.reserved0 = 0u;
    parameters.reserved1 = 0u;

    // A rough dielectric below the named microfacet resolution threshold is
    // the already-supported smooth dielectric model. This is an explicit
    // model transition, not a hidden roughness clamp.
    if (parameters.model == kBsdfModelRoughDielectricL6 &&
        BsdfFiniteFloatL6(parameters.perceptualRoughness) &&
        parameters.perceptualRoughness >= 0.0f &&
        parameters.perceptualRoughness * parameters.perceptualRoughness <
            kBsdfMinimumAlphaL6)
    {
        parameters.model = kBsdfModelSmoothGlassL6;
        if ((parameters.allowedLobes & kBsdfLobeGlossyL6) != 0u)
        {
            parameters.allowedLobes =
                (parameters.allowedLobes & ~kBsdfLobeGlossyL6) |
                kBsdfLobeSpecularL6;
        }
    }
    return parameters;
}

BsdfContextL6 PbrBuildBsdfContextL6(PbrHitL6 hit, PbrMaterialGpuL6 material)
{
    BsdfContextL6 context;
    context.geometricNormal = hit.geometricNormal;
    context.shadingNormal = hit.shadingNormal;
    float3 tangent;
    float3 bitangent;
    PbrBuildBasisL6(hit.shadingNormal, tangent, bitangent);
    context.tangent = tangent;
    const float ior = material.transmissionIor.y;
    context.etaIncident = hit.frontFace != 0u ? 1.0f : ior;
    context.etaTransmitted = hit.frontFace != 0u ? ior : 1.0f;
    context.transportMode = kBsdfTransportRadianceL6;
    return context;
}

bool PbrMaterialAuxiliaryIsValidL6(PbrMaterialGpuL6 material)
{
    if (!PbrIsFinite3L6(material.emissiveRoughness.xyz) ||
        any(material.emissiveRoughness.xyz < 0.0f) ||
        !all(isfinite(material.attenuationColorDistance)) ||
        material.attenuationColorDistance.w < 0.0f)
    {
        return false;
    }
    if (material.attenuationColorDistance.w > 0.0f &&
        (!all(material.attenuationColorDistance.xyz > 0.0f) ||
         !all(material.attenuationColorDistance.xyz <= 1.0f)))
    {
        return false;
    }
    return true;
}

bool PbrInteriorAttenuationFactorL6(
    PbrMaterialGpuL6 material,
    float distance,
    out float3 attenuation)
{
    attenuation = 1.0f;
    if (!(material.attenuationColorDistance.w > 0.0f))
    {
        return true;
    }
    const float3 color = material.attenuationColorDistance.xyz;
    if (!all(color > 0.0f) || !all(color <= 1.0f) ||
        !PbrIsFiniteFloatL6(distance) || distance < 0.0f)
    {
        return false;
    }
    attenuation = pow(color, distance / material.attenuationColorDistance.w);
    return PbrIsFinite3L6(attenuation);
}

bool PbrApplyInteriorAttenuationL6(
    PbrMaterialGpuL6 material,
    float distance,
    inout float3 beta)
{
    float3 attenuation;
    if (!PbrInteriorAttenuationFactorL6(material, distance, attenuation))
    {
        return false;
    }
    beta *= attenuation;
    return PbrIsFinite3L6(beta);
}

bool PbrApplyInteriorAttenuationL6(
    PbrMaterialGpuL6 material,
    float distance,
    inout float3 beta,
    inout float3 betaDiffuse,
    inout float3 betaSpecular)
{
    float3 attenuation;
    if (!PbrInteriorAttenuationFactorL6(material, distance, attenuation))
    {
        return false;
    }
    beta *= attenuation;
    betaDiffuse *= attenuation;
    betaSpecular *= attenuation;
    return PbrIsFinite3L6(beta) && PbrIsFinite3L6(betaDiffuse) &&
        PbrIsFinite3L6(betaSpecular);
}

#endif
