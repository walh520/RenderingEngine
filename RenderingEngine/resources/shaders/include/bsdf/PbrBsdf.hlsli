#ifndef RENDERING_ENGINE_PBR_BSDF_L6_HLSLI
#define RENDERING_ENGINE_PBR_BSDF_L6_HLSLI

// L6-private BSDF reference implementation.
//
// Direction convention: wo and wi are normalized world-space directions that
// point away from the surface.  Every finite PDF is with respect to solid
// angle.  Smooth dielectric events use the discrete measure and are never
// exposed through Evaluate/Pdf as finite-density events.

static const float kBsdfPiL6 = 3.14159265358979323846f;
static const float kBsdfInversePiL6 = 0.31830988618379067154f;
static const float kBsdfDirectionEpsilonL6 = 1.0e-7f;
static const float kBsdfMinimumAlphaL6 = 1.0e-4f;

static const uint kBsdfTransportRadianceL6 = 0u;
static const uint kBsdfTransportImportanceL6 = 1u;

static const uint kBsdfMeasureInvalidL6 = 0u;
static const uint kBsdfMeasureSolidAngleL6 = 1u;
static const uint kBsdfMeasureDiscreteL6 = 2u;

static const uint kBsdfLobeDiffuseL6 = 1u << 0u;
static const uint kBsdfLobeGlossyL6 = 1u << 1u;
static const uint kBsdfLobeSpecularL6 = 1u << 2u;
static const uint kBsdfLobeReflectionL6 = 1u << 3u;
static const uint kBsdfLobeTransmissionL6 = 1u << 4u;

static const uint kBsdfLobeDiffuseReflectionL6 =
    kBsdfLobeDiffuseL6 | kBsdfLobeReflectionL6;
static const uint kBsdfLobeGlossyReflectionL6 =
    kBsdfLobeGlossyL6 | kBsdfLobeReflectionL6;
static const uint kBsdfLobeGlossyTransmissionL6 =
    kBsdfLobeGlossyL6 | kBsdfLobeTransmissionL6;
static const uint kBsdfLobeSpecularReflectionL6 =
    kBsdfLobeSpecularL6 | kBsdfLobeReflectionL6;
static const uint kBsdfLobeSpecularTransmissionL6 =
    kBsdfLobeSpecularL6 | kBsdfLobeTransmissionL6;
static const uint kBsdfLobeAllL6 =
    kBsdfLobeDiffuseL6 | kBsdfLobeGlossyL6 | kBsdfLobeSpecularL6 |
    kBsdfLobeReflectionL6 | kBsdfLobeTransmissionL6;

static const uint kBsdfModelLambertL6 = 0u;
static const uint kBsdfModelGgxConductorL6 = 1u;
static const uint kBsdfModelGgxDielectricReflectionL6 = 2u;
static const uint kBsdfModelSmoothGlassL6 = 3u;
static const uint kBsdfModelRoughDielectricL6 = 4u;
static const uint kBsdfModelMetallicRoughnessL6 = 5u;

struct BsdfContextL6
{
    float3 geometricNormal;
    float etaIncident;
    float3 shadingNormal;
    float etaTransmitted;
    float3 tangent;
    uint transportMode;
};

struct BsdfParamsL6
{
    float3 baseColor;
    float metallic;
    float3 f0;
    float perceptualRoughness;
    float3 conductorEta;
    float transmission;
    float3 conductorK;
    uint model;
    uint allowedLobes;
    uint useComplexConductorFresnel;
    uint reserved0;
    uint reserved1;
};

// value is the total regular BSDF.  The split values add exactly to value and
// are intended for the L6 first-response AOVs.  pdf is the total mixture PDF.
struct BsdfEvalL6
{
    float3 value;
    float pdf;
    float3 diffuseValue;
    float reserved0;
    float3 specularValue;
    float reserved1;
    uint measure;
    uint lobeFlags;
    uint isValid;
    uint reserved2;
};

struct BsdfSampleL6
{
    float3 direction;
    float pdf;
    float3 value;
    float eta;
    float3 diffuseValue;
    float reserved0;
    float3 specularValue;
    float reserved1;
    uint measure;
    uint lobeFlags;
    uint isDelta;
    uint isValid;
};

BsdfEvalL6 InvalidBsdfEvalL6()
{
    BsdfEvalL6 result;
    result.value = 0.0f;
    result.pdf = 0.0f;
    result.diffuseValue = 0.0f;
    result.reserved0 = 0.0f;
    result.specularValue = 0.0f;
    result.reserved1 = 0.0f;
    result.measure = kBsdfMeasureInvalidL6;
    result.lobeFlags = 0u;
    result.isValid = 0u;
    result.reserved2 = 0u;
    return result;
}

BsdfSampleL6 InvalidBsdfSampleL6()
{
    BsdfSampleL6 result;
    result.direction = 0.0f;
    result.pdf = 0.0f;
    result.value = 0.0f;
    result.eta = 1.0f;
    result.diffuseValue = 0.0f;
    result.reserved0 = 0.0f;
    result.specularValue = 0.0f;
    result.reserved1 = 0.0f;
    result.measure = kBsdfMeasureInvalidL6;
    result.lobeFlags = 0u;
    result.isDelta = 0u;
    result.isValid = 0u;
    return result;
}

bool BsdfFiniteFloatL6(float value)
{
    return isfinite(value);
}

bool BsdfFiniteFloat3L6(float3 value)
{
    return all(isfinite(value));
}

bool BsdfSafeNormalizeL6(float3 value, out float3 normalizedValue)
{
    normalizedValue = 0.0f;
    const float lengthSquared = dot(value, value);
    if (!BsdfFiniteFloatL6(lengthSquared) ||
        lengthSquared <= kBsdfDirectionEpsilonL6 * kBsdfDirectionEpsilonL6)
    {
        return false;
    }
    normalizedValue = value * rsqrt(lengthSquared);
    return BsdfFiniteFloat3L6(normalizedValue);
}

float BsdfLuminanceL6(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

bool BsdfContainsFlagsL6(uint mask, uint required)
{
    return (mask & required) == required;
}

bool BsdfUnitRandom3L6(float3 sampleValue)
{
    return BsdfFiniteFloat3L6(sampleValue) &&
        all(sampleValue >= 0.0f) && all(sampleValue < 1.0f);
}

bool BsdfColorInUnitRangeL6(float3 value)
{
    return BsdfFiniteFloat3L6(value) && all(value >= 0.0f) && all(value <= 1.0f);
}

bool BsdfPositiveEtaContextL6(BsdfContextL6 context)
{
    return BsdfFiniteFloatL6(context.etaIncident) &&
        BsdfFiniteFloatL6(context.etaTransmitted) &&
        context.etaIncident > 0.0f && context.etaTransmitted > 0.0f;
}

bool BsdfRoughnessIsFiniteModelL6(float perceptualRoughness)
{
    if (!BsdfFiniteFloatL6(perceptualRoughness) ||
        perceptualRoughness <= 0.0f || perceptualRoughness > 1.0f)
    {
        return false;
    }
    const float alpha = perceptualRoughness * perceptualRoughness;
    // Values below this documented boundary must be routed to an explicit
    // smooth/delta model by the caller; they are never silently clamped.
    return alpha >= kBsdfMinimumAlphaL6;
}

bool ValidateBsdfParamsL6(BsdfContextL6 context, BsdfParamsL6 parameters)
{
    if (parameters.allowedLobes == 0u ||
        context.transportMode > kBsdfTransportImportanceL6)
    {
        return false;
    }

    if (parameters.model == kBsdfModelLambertL6)
    {
        return BsdfColorInUnitRangeL6(parameters.baseColor);
    }

    if (parameters.model == kBsdfModelGgxConductorL6)
    {
        if (!BsdfRoughnessIsFiniteModelL6(parameters.perceptualRoughness))
        {
            return false;
        }
        if (parameters.useComplexConductorFresnel != 0u)
        {
            return BsdfFiniteFloat3L6(parameters.conductorEta) &&
                BsdfFiniteFloat3L6(parameters.conductorK) &&
                all(parameters.conductorEta > 0.0f) &&
                all(parameters.conductorK >= 0.0f);
        }
        return BsdfColorInUnitRangeL6(parameters.f0);
    }

    if (parameters.model == kBsdfModelGgxDielectricReflectionL6)
    {
        return BsdfRoughnessIsFiniteModelL6(parameters.perceptualRoughness) &&
            BsdfPositiveEtaContextL6(context);
    }

    if (parameters.model == kBsdfModelSmoothGlassL6)
    {
        return BsdfPositiveEtaContextL6(context) &&
            BsdfFiniteFloatL6(parameters.transmission) &&
            parameters.transmission >= 0.0f && parameters.transmission <= 1.0f;
    }

    if (parameters.model == kBsdfModelRoughDielectricL6)
    {
        return BsdfRoughnessIsFiniteModelL6(parameters.perceptualRoughness) &&
            BsdfPositiveEtaContextL6(context) &&
            BsdfFiniteFloatL6(parameters.transmission) &&
            parameters.transmission >= 0.0f && parameters.transmission <= 1.0f;
    }

    if (parameters.model == kBsdfModelMetallicRoughnessL6)
    {
        return BsdfColorInUnitRangeL6(parameters.baseColor) &&
            BsdfColorInUnitRangeL6(parameters.f0) &&
            BsdfRoughnessIsFiniteModelL6(parameters.perceptualRoughness) &&
            BsdfFiniteFloatL6(parameters.metallic) &&
            parameters.metallic >= 0.0f && parameters.metallic <= 1.0f &&
            BsdfFiniteFloatL6(parameters.transmission) &&
            parameters.transmission >= 0.0f && parameters.transmission <= 1.0f;
    }

    return false;
}

bool BsdfBuildFrameL6(
    BsdfContextL6 context,
    float3 woWorldInput,
    out float3 geometricNormal,
    out float3 shadingNormal,
    out float3 tangent,
    out float3 bitangent,
    out float3 woWorld,
    out float3 woLocal)
{
    geometricNormal = 0.0f;
    shadingNormal = 0.0f;
    tangent = 0.0f;
    bitangent = 0.0f;
    woWorld = 0.0f;
    woLocal = 0.0f;

    if (!BsdfSafeNormalizeL6(context.geometricNormal, geometricNormal) ||
        !BsdfSafeNormalizeL6(context.shadingNormal, shadingNormal) ||
        !BsdfSafeNormalizeL6(woWorldInput, woWorld))
    {
        return false;
    }

    if (dot(shadingNormal, geometricNormal) < 0.0f)
    {
        shadingNormal = -shadingNormal;
    }
    if (dot(woWorld, geometricNormal) < 0.0f)
    {
        geometricNormal = -geometricNormal;
        shadingNormal = -shadingNormal;
    }
    if (dot(woWorld, geometricNormal) <= kBsdfDirectionEpsilonL6 ||
        dot(woWorld, shadingNormal) <= kBsdfDirectionEpsilonL6)
    {
        return false;
    }

    float3 tangentCandidate = context.tangent -
        shadingNormal * dot(context.tangent, shadingNormal);
    if (!BsdfSafeNormalizeL6(tangentCandidate, tangent))
    {
        const float3 helper = abs(shadingNormal.z) < 0.999f
            ? float3(0.0f, 0.0f, 1.0f)
            : float3(0.0f, 1.0f, 0.0f);
        if (!BsdfSafeNormalizeL6(cross(helper, shadingNormal), tangent))
        {
            return false;
        }
    }
    const float3 bitangentCandidate = cross(shadingNormal, tangent);
    if (!BsdfSafeNormalizeL6(bitangentCandidate, bitangent))
    {
        return false;
    }

    woLocal = float3(
        dot(woWorld, tangent),
        dot(woWorld, bitangent),
        dot(woWorld, shadingNormal));
    return BsdfFiniteFloat3L6(woLocal) &&
        woLocal.z > kBsdfDirectionEpsilonL6;
}

float3 BsdfToLocalL6(
    float3 direction,
    float3 tangent,
    float3 bitangent,
    float3 normal)
{
    return float3(
        dot(direction, tangent),
        dot(direction, bitangent),
        dot(direction, normal));
}

float3 BsdfToWorldL6(
    float3 direction,
    float3 tangent,
    float3 bitangent,
    float3 normal)
{
    return tangent * direction.x + bitangent * direction.y + normal * direction.z;
}

bool BsdfValidateWorldEventL6(
    float3 woWorld,
    float3 wiWorld,
    float3 geometricNormal,
    uint lobeFlags)
{
    const float sideProduct = dot(woWorld, geometricNormal) *
        dot(wiWorld, geometricNormal);
    const bool isReflection = BsdfContainsFlagsL6(lobeFlags, kBsdfLobeReflectionL6);
    const bool isTransmission = BsdfContainsFlagsL6(lobeFlags, kBsdfLobeTransmissionL6);
    if (isReflection == isTransmission)
    {
        return false;
    }
    return isReflection
        ? sideProduct > 0.0f
        : sideProduct < 0.0f;
}

bool BsdfShadingNormalCorrectionL6(
    BsdfContextL6 context,
    float3 geometricNormal,
    float3 shadingNormal,
    float3 woWorld,
    float3 wiWorld,
    out float correction)
{
    correction = 0.0f;
    const float numerator = context.transportMode == kBsdfTransportRadianceL6
        ? abs(dot(wiWorld, shadingNormal))
        : abs(dot(woWorld, shadingNormal));
    const float denominator = context.transportMode == kBsdfTransportRadianceL6
        ? abs(dot(wiWorld, geometricNormal))
        : abs(dot(woWorld, geometricNormal));
    if (!BsdfFiniteFloatL6(numerator) || !BsdfFiniteFloatL6(denominator) ||
        denominator <= kBsdfDirectionEpsilonL6)
    {
        return false;
    }
    correction = numerator / denominator;
    return BsdfFiniteFloatL6(correction) && correction >= 0.0f;
}

bool BsdfFresnelDielectricL6(
    float cosineIncidentInput,
    float etaIncident,
    float etaTransmitted,
    out float fresnel)
{
    fresnel = 0.0f;
    if (!BsdfFiniteFloatL6(cosineIncidentInput) ||
        !BsdfFiniteFloatL6(etaIncident) || !BsdfFiniteFloatL6(etaTransmitted) ||
        etaIncident <= 0.0f || etaTransmitted <= 0.0f)
    {
        return false;
    }

    const float cosineIncident = min(abs(cosineIncidentInput), 1.0f);
    const float etaRatio = etaIncident / etaTransmitted;
    const float sineTransmittedSquared = etaRatio * etaRatio *
        max(0.0f, 1.0f - cosineIncident * cosineIncident);
    if (sineTransmittedSquared >= 1.0f)
    {
        fresnel = 1.0f;
        return true;
    }

    const float cosineTransmitted = sqrt(max(0.0f, 1.0f - sineTransmittedSquared));
    const float parallelDenominator =
        etaTransmitted * cosineIncident + etaIncident * cosineTransmitted;
    const float perpendicularDenominator =
        etaIncident * cosineIncident + etaTransmitted * cosineTransmitted;
    if (abs(parallelDenominator) <= kBsdfDirectionEpsilonL6 ||
        abs(perpendicularDenominator) <= kBsdfDirectionEpsilonL6)
    {
        return false;
    }

    const float parallel =
        (etaTransmitted * cosineIncident - etaIncident * cosineTransmitted) /
        parallelDenominator;
    const float perpendicular =
        (etaIncident * cosineIncident - etaTransmitted * cosineTransmitted) /
        perpendicularDenominator;
    fresnel = 0.5f * (parallel * parallel + perpendicular * perpendicular);
    return BsdfFiniteFloatL6(fresnel) && fresnel >= 0.0f && fresnel <= 1.0f;
}

float3 BsdfFresnelSchlickL6(float cosine, float3 f0)
{
    const float oneMinusCosine = 1.0f - min(abs(cosine), 1.0f);
    const float factor = oneMinusCosine * oneMinusCosine * oneMinusCosine *
        oneMinusCosine * oneMinusCosine;
    return f0 + (1.0f - f0) * factor;
}

bool BsdfFresnelConductorL6(
    float cosineIncidentInput,
    float3 eta,
    float3 extinction,
    out float3 fresnel)
{
    fresnel = 0.0f;
    if (!BsdfFiniteFloatL6(cosineIncidentInput) ||
        !BsdfFiniteFloat3L6(eta) || !BsdfFiniteFloat3L6(extinction) ||
        any(eta <= 0.0f) || any(extinction < 0.0f))
    {
        return false;
    }

    const float cosineIncident = min(abs(cosineIncidentInput), 1.0f);
    const float cosineSquared = cosineIncident * cosineIncident;
    const float sineSquared = max(0.0f, 1.0f - cosineSquared);
    const float3 etaSquared = eta * eta;
    const float3 extinctionSquared = extinction * extinction;
    const float3 t0 = etaSquared - extinctionSquared - sineSquared;
    const float3 a2PlusB2 = sqrt(max(
        t0 * t0 + 4.0f * etaSquared * extinctionSquared,
        0.0f));
    const float3 t1 = a2PlusB2 + cosineSquared;
    const float3 a = sqrt(max(0.5f * (a2PlusB2 + t0), 0.0f));
    const float3 t2 = 2.0f * cosineIncident * a;
    const float3 rsDenominator = t1 + t2;
    const float3 t3 = cosineSquared * a2PlusB2 + sineSquared * sineSquared;
    const float3 t4 = t2 * sineSquared;
    const float3 rpDenominator = t3 + t4;
    if (any(abs(rsDenominator) <= kBsdfDirectionEpsilonL6) ||
        any(abs(rpDenominator) <= kBsdfDirectionEpsilonL6))
    {
        return false;
    }

    const float3 rs = (t1 - t2) / rsDenominator;
    const float3 rp = rs * (t3 - t4) / rpDenominator;
    fresnel = 0.5f * (rs + rp);
    return BsdfFiniteFloat3L6(fresnel) &&
        all(fresnel >= 0.0f) && all(fresnel <= 1.0f);
}

float BsdfGgxDL6(float3 microNormal, float alpha)
{
    if (microNormal.z <= 0.0f || alpha < kBsdfMinimumAlphaL6)
    {
        return 0.0f;
    }
    const float alphaSquared = alpha * alpha;
    const float cosineSquared = microNormal.z * microNormal.z;
    const float denominator = cosineSquared * (alphaSquared - 1.0f) + 1.0f;
    if (denominator <= 0.0f)
    {
        return 0.0f;
    }
    return alphaSquared /
        (kBsdfPiL6 * denominator * denominator);
}

float BsdfGgxLambdaL6(float3 direction, float alpha)
{
    const float absoluteCosine = abs(direction.z);
    if (absoluteCosine <= kBsdfDirectionEpsilonL6)
    {
        return 1.0e30f;
    }
    const float sineSquared = max(0.0f, 1.0f - absoluteCosine * absoluteCosine);
    const float tangentSquared = sineSquared / (absoluteCosine * absoluteCosine);
    return 0.5f * (sqrt(1.0f + alpha * alpha * tangentSquared) - 1.0f);
}

float BsdfGgxG1L6(float3 direction, float alpha)
{
    return 1.0f / (1.0f + BsdfGgxLambdaL6(direction, alpha));
}

float BsdfGgxG2L6(float3 wo, float3 wi, float alpha)
{
    return 1.0f /
        (1.0f + BsdfGgxLambdaL6(wo, alpha) + BsdfGgxLambdaL6(wi, alpha));
}

float BsdfGgxVisibleNormalPdfL6(float3 wo, float3 microNormal, float alpha)
{
    if (wo.z <= kBsdfDirectionEpsilonL6 || microNormal.z <= 0.0f ||
        dot(wo, microNormal) <= 0.0f)
    {
        return 0.0f;
    }
    return BsdfGgxDL6(microNormal, alpha) * BsdfGgxG1L6(wo, alpha) *
        abs(dot(wo, microNormal)) / abs(wo.z);
}

bool BsdfSampleGgxVisibleNormalL6(
    float3 wo,
    float alpha,
    float2 randomSample,
    out float3 microNormal)
{
    microNormal = 0.0f;
    if (wo.z <= kBsdfDirectionEpsilonL6 || alpha < kBsdfMinimumAlphaL6 ||
        !BsdfFiniteFloat3L6(wo) || !all(isfinite(randomSample)) ||
        any(randomSample < 0.0f) || any(randomSample >= 1.0f))
    {
        return false;
    }

    float3 stretchedView;
    if (!BsdfSafeNormalizeL6(float3(alpha * wo.x, alpha * wo.y, wo.z), stretchedView))
    {
        return false;
    }
    if (stretchedView.z < 0.0f)
    {
        stretchedView = -stretchedView;
    }

    float3 tangent1;
    const float lensSquared = stretchedView.x * stretchedView.x +
        stretchedView.y * stretchedView.y;
    if (lensSquared > kBsdfDirectionEpsilonL6 * kBsdfDirectionEpsilonL6)
    {
        tangent1 = float3(-stretchedView.y, stretchedView.x, 0.0f) *
            rsqrt(lensSquared);
    }
    else
    {
        tangent1 = float3(1.0f, 0.0f, 0.0f);
    }
    const float3 tangent2 = cross(stretchedView, tangent1);

    const float radius = sqrt(randomSample.x);
    const float phi = 2.0f * kBsdfPiL6 * randomSample.y;
    const float diskX = radius * cos(phi);
    float diskY = radius * sin(phi);
    const float blend = 0.5f * (1.0f + stretchedView.z);
    diskY = lerp(
        sqrt(max(0.0f, 1.0f - diskX * diskX)),
        diskY,
        blend);

    const float projectedZ = sqrt(max(
        0.0f,
        1.0f - diskX * diskX - diskY * diskY));
    const float3 hemisphereNormal = diskX * tangent1 +
        diskY * tangent2 + projectedZ * stretchedView;
    if (!BsdfSafeNormalizeL6(
        float3(alpha * hemisphereNormal.x,
               alpha * hemisphereNormal.y,
               hemisphereNormal.z),
        microNormal))
    {
        return false;
    }
    return microNormal.z > 0.0f && dot(wo, microNormal) > 0.0f;
}

bool BsdfGgxReflectionTermsL6(
    float3 wo,
    float3 wi,
    float alpha,
    out float3 microNormal,
    out float distribution,
    out float maskingShadowing,
    out float directionPdf)
{
    microNormal = 0.0f;
    distribution = 0.0f;
    maskingShadowing = 0.0f;
    directionPdf = 0.0f;
    if (wo.z <= kBsdfDirectionEpsilonL6 || wi.z <= kBsdfDirectionEpsilonL6 ||
        !BsdfSafeNormalizeL6(wo + wi, microNormal))
    {
        return false;
    }
    if (microNormal.z < 0.0f)
    {
        microNormal = -microNormal;
    }
    const float woDotMicro = abs(dot(wo, microNormal));
    if (woDotMicro <= kBsdfDirectionEpsilonL6)
    {
        return false;
    }
    distribution = BsdfGgxDL6(microNormal, alpha);
    maskingShadowing = BsdfGgxG2L6(wo, wi, alpha);
    directionPdf = BsdfGgxVisibleNormalPdfL6(wo, microNormal, alpha) /
        (4.0f * woDotMicro);
    return distribution > 0.0f && maskingShadowing >= 0.0f &&
        directionPdf > 0.0f && BsdfFiniteFloatL6(directionPdf);
}

bool BsdfReflectionFresnelL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float cosine,
    out float3 fresnel)
{
    fresnel = 0.0f;
    if (parameters.model == kBsdfModelGgxConductorL6)
    {
        if (parameters.useComplexConductorFresnel != 0u)
        {
            return BsdfFresnelConductorL6(
                cosine,
                parameters.conductorEta,
                parameters.conductorK,
                fresnel);
        }
        fresnel = BsdfFresnelSchlickL6(cosine, parameters.f0);
        return BsdfFiniteFloat3L6(fresnel);
    }
    if (parameters.model == kBsdfModelGgxDielectricReflectionL6)
    {
        float scalarFresnel;
        if (!BsdfFresnelDielectricL6(
            cosine,
            context.etaIncident,
            context.etaTransmitted,
            scalarFresnel))
        {
            return false;
        }
        fresnel = scalarFresnel.xxx;
        return true;
    }
    fresnel = BsdfFresnelSchlickL6(cosine, parameters.f0);
    return BsdfFiniteFloat3L6(fresnel);
}

BsdfEvalL6 BsdfEvaluateLambertLocalL6(
    BsdfParamsL6 parameters,
    float3 wo,
    float3 wi)
{
    BsdfEvalL6 result = InvalidBsdfEvalL6();
    if (!BsdfContainsFlagsL6(parameters.allowedLobes, kBsdfLobeDiffuseReflectionL6) ||
        wo.z <= kBsdfDirectionEpsilonL6 || wi.z <= kBsdfDirectionEpsilonL6)
    {
        return result;
    }
    result.diffuseValue = parameters.baseColor * kBsdfInversePiL6;
    result.value = result.diffuseValue;
    result.pdf = wi.z * kBsdfInversePiL6;
    result.measure = kBsdfMeasureSolidAngleL6;
    result.lobeFlags = kBsdfLobeDiffuseReflectionL6;
    result.isValid = BsdfFiniteFloat3L6(result.value) &&
        BsdfFiniteFloatL6(result.pdf) && result.pdf > 0.0f ? 1u : 0u;
    if (result.isValid == 0u)
    {
        return InvalidBsdfEvalL6();
    }
    return result;
}

BsdfEvalL6 BsdfEvaluateGgxReflectionLocalL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 wo,
    float3 wi)
{
    BsdfEvalL6 result = InvalidBsdfEvalL6();
    if (!BsdfContainsFlagsL6(parameters.allowedLobes, kBsdfLobeGlossyReflectionL6))
    {
        return result;
    }
    const float alpha = parameters.perceptualRoughness *
        parameters.perceptualRoughness;
    float3 microNormal;
    float distribution;
    float maskingShadowing;
    float directionPdf;
    if (!BsdfGgxReflectionTermsL6(
        wo, wi, alpha, microNormal, distribution, maskingShadowing, directionPdf))
    {
        return result;
    }

    float3 fresnel;
    if (!BsdfReflectionFresnelL6(
        context, parameters, abs(dot(wo, microNormal)), fresnel))
    {
        return result;
    }
    const float denominator = 4.0f * abs(wo.z * wi.z);
    if (denominator <= kBsdfDirectionEpsilonL6)
    {
        return result;
    }
    result.specularValue = fresnel *
        (distribution * maskingShadowing / denominator);
    result.value = result.specularValue;
    result.pdf = directionPdf;
    result.measure = kBsdfMeasureSolidAngleL6;
    result.lobeFlags = kBsdfLobeGlossyReflectionL6;
    result.isValid = BsdfFiniteFloat3L6(result.value) &&
        BsdfFiniteFloatL6(result.pdf) && result.pdf > 0.0f ? 1u : 0u;
    if (result.isValid == 0u)
    {
        return InvalidBsdfEvalL6();
    }
    return result;
}

bool BsdfMetallicLobeProbabilitiesL6(
    BsdfParamsL6 parameters,
    float3 wo,
    out float diffuseProbability,
    out float specularProbability)
{
    diffuseProbability = 0.0f;
    specularProbability = 0.0f;
    const bool allowDiffuse = BsdfContainsFlagsL6(
        parameters.allowedLobes, kBsdfLobeDiffuseReflectionL6);
    const bool allowSpecular = BsdfContainsFlagsL6(
        parameters.allowedLobes, kBsdfLobeGlossyReflectionL6);
    const float diffuseScale = (1.0f - parameters.metallic) *
        (1.0f - parameters.transmission);
    const float diffuseWeight = allowDiffuse
        ? BsdfLuminanceL6(parameters.baseColor) * diffuseScale
        : 0.0f;
    const float specularWeight = allowSpecular
        ? BsdfLuminanceL6(BsdfFresnelSchlickL6(abs(wo.z), parameters.f0))
        : 0.0f;
    const float totalWeight = diffuseWeight + specularWeight;
    if (!BsdfFiniteFloatL6(totalWeight) || totalWeight <= 0.0f)
    {
        return false;
    }
    diffuseProbability = diffuseWeight / totalWeight;
    specularProbability = specularWeight / totalWeight;
    return BsdfFiniteFloatL6(diffuseProbability) &&
        BsdfFiniteFloatL6(specularProbability);
}

BsdfEvalL6 BsdfEvaluateMetallicRoughnessLocalL6(
    BsdfParamsL6 parameters,
    float3 wo,
    float3 wi)
{
    BsdfEvalL6 result = InvalidBsdfEvalL6();
    if (wo.z <= kBsdfDirectionEpsilonL6 || wi.z <= kBsdfDirectionEpsilonL6)
    {
        return result;
    }
    float diffuseProbability;
    float specularProbability;
    if (!BsdfMetallicLobeProbabilitiesL6(
        parameters, wo, diffuseProbability, specularProbability))
    {
        return result;
    }

    const float alpha = parameters.perceptualRoughness *
        parameters.perceptualRoughness;
    float3 microNormal;
    float distribution;
    float maskingShadowing;
    float specularPdf;
    const bool validSpecularTerms = BsdfGgxReflectionTermsL6(
        wo, wi, alpha, microNormal, distribution, maskingShadowing, specularPdf);
    if (specularProbability > 0.0f && !validSpecularTerms)
    {
        return result;
    }

    const float3 fresnel = validSpecularTerms
        ? BsdfFresnelSchlickL6(abs(dot(wo, microNormal)), parameters.f0)
        : 0.0f;
    const float diffuseScale = (1.0f - parameters.metallic) *
        (1.0f - parameters.transmission);
    if (diffuseProbability > 0.0f)
    {
        result.diffuseValue = (1.0f - fresnel) * parameters.baseColor *
            (diffuseScale * kBsdfInversePiL6);
        result.lobeFlags |= kBsdfLobeDiffuseReflectionL6;
    }
    if (specularProbability > 0.0f)
    {
        const float denominator = 4.0f * abs(wo.z * wi.z);
        if (denominator <= kBsdfDirectionEpsilonL6)
        {
            return InvalidBsdfEvalL6();
        }
        result.specularValue = fresnel *
            (distribution * maskingShadowing / denominator);
        result.lobeFlags |= kBsdfLobeGlossyReflectionL6;
    }
    result.value = result.diffuseValue + result.specularValue;
    result.pdf = diffuseProbability * wi.z * kBsdfInversePiL6 +
        specularProbability * specularPdf;
    result.measure = kBsdfMeasureSolidAngleL6;
    result.isValid = BsdfFiniteFloat3L6(result.value) &&
        BsdfFiniteFloat3L6(result.diffuseValue) &&
        BsdfFiniteFloat3L6(result.specularValue) &&
        BsdfFiniteFloatL6(result.pdf) && result.pdf > 0.0f ? 1u : 0u;
    if (result.isValid == 0u)
    {
        return InvalidBsdfEvalL6();
    }
    return result;
}

bool BsdfRoughDielectricHalfVectorL6(
    float3 wo,
    float3 wi,
    float etaRelative,
    out bool isReflection,
    out float3 microNormal)
{
    isReflection = wo.z * wi.z > 0.0f;
    microNormal = 0.0f;
    const float3 halfVector = isReflection ? wo + wi : wo + wi * etaRelative;
    if (abs(wo.z) <= kBsdfDirectionEpsilonL6 ||
        abs(wi.z) <= kBsdfDirectionEpsilonL6 ||
        !BsdfSafeNormalizeL6(halfVector, microNormal))
    {
        return false;
    }
    if (microNormal.z < 0.0f)
    {
        microNormal = -microNormal;
    }
    return dot(microNormal, wi) * wi.z >= 0.0f &&
        dot(microNormal, wo) * wo.z >= 0.0f;
}

bool BsdfRoughDielectricEventProbabilitiesL6(
    BsdfParamsL6 parameters,
    float fresnel,
    out float reflectionProbability,
    out float transmissionProbability)
{
    reflectionProbability = BsdfContainsFlagsL6(
        parameters.allowedLobes, kBsdfLobeGlossyReflectionL6)
        ? fresnel
        : 0.0f;
    transmissionProbability = BsdfContainsFlagsL6(
        parameters.allowedLobes, kBsdfLobeGlossyTransmissionL6)
        ? (1.0f - fresnel) * parameters.transmission
        : 0.0f;
    const float total = reflectionProbability + transmissionProbability;
    if (!BsdfFiniteFloatL6(total) || total <= 0.0f)
    {
        return false;
    }
    reflectionProbability /= total;
    transmissionProbability /= total;
    return true;
}

BsdfEvalL6 BsdfEvaluateRoughDielectricLocalL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 wo,
    float3 wi)
{
    BsdfEvalL6 result = InvalidBsdfEvalL6();
    const float etaRelative = context.etaTransmitted / context.etaIncident;
    if (!BsdfFiniteFloatL6(etaRelative) || etaRelative <= 0.0f)
    {
        return result;
    }

    bool isReflection;
    float3 microNormal;
    if (!BsdfRoughDielectricHalfVectorL6(
        wo, wi, etaRelative, isReflection, microNormal))
    {
        return result;
    }

    float fresnel;
    if (!BsdfFresnelDielectricL6(
        dot(wo, microNormal),
        context.etaIncident,
        context.etaTransmitted,
        fresnel))
    {
        return result;
    }
    float reflectionProbability;
    float transmissionProbability;
    if (!BsdfRoughDielectricEventProbabilitiesL6(
        parameters, fresnel, reflectionProbability, transmissionProbability))
    {
        return result;
    }

    const float alpha = parameters.perceptualRoughness *
        parameters.perceptualRoughness;
    const float distribution = BsdfGgxDL6(microNormal, alpha);
    const float maskingShadowing = BsdfGgxG2L6(wo, wi, alpha);
    const float normalPdf = BsdfGgxVisibleNormalPdfL6(wo, microNormal, alpha);
    if (distribution <= 0.0f || maskingShadowing < 0.0f || normalPdf <= 0.0f)
    {
        return result;
    }

    if (isReflection)
    {
        if (reflectionProbability <= 0.0f)
        {
            return result;
        }
        const float woDotMicro = abs(dot(wo, microNormal));
        const float valueDenominator = 4.0f * abs(wo.z * wi.z);
        if (woDotMicro <= kBsdfDirectionEpsilonL6 ||
            valueDenominator <= kBsdfDirectionEpsilonL6)
        {
            return result;
        }
        result.specularValue = fresnel.xxx *
            (distribution * maskingShadowing / valueDenominator);
        result.pdf = normalPdf / (4.0f * woDotMicro) * reflectionProbability;
        result.lobeFlags = kBsdfLobeGlossyReflectionL6;
    }
    else
    {
        if (transmissionProbability <= 0.0f)
        {
            return result;
        }
        const float woDotMicro = dot(wo, microNormal);
        const float wiDotMicro = dot(wi, microNormal);
        const float denominatorTerm = wiDotMicro + woDotMicro / etaRelative;
        const float jacobianDenominator = denominatorTerm * denominatorTerm;
        const float cosineProduct = wo.z * wi.z;
        if (jacobianDenominator <= kBsdfDirectionEpsilonL6 ||
            abs(cosineProduct) <= kBsdfDirectionEpsilonL6)
        {
            return result;
        }
        const float dMicroDWi = abs(wiDotMicro) / jacobianDenominator;
        float transmissionValue = parameters.transmission * (1.0f - fresnel) *
            distribution * maskingShadowing *
            abs(wiDotMicro * woDotMicro /
                (cosineProduct * jacobianDenominator));
        if (context.transportMode == kBsdfTransportRadianceL6)
        {
            transmissionValue /= etaRelative * etaRelative;
        }
        result.specularValue = transmissionValue.xxx;
        result.pdf = normalPdf * dMicroDWi * transmissionProbability;
        result.lobeFlags = kBsdfLobeGlossyTransmissionL6;
    }

    result.value = result.specularValue;
    result.measure = kBsdfMeasureSolidAngleL6;
    result.isValid = BsdfFiniteFloat3L6(result.value) &&
        BsdfFiniteFloatL6(result.pdf) && result.pdf > 0.0f ? 1u : 0u;
    if (result.isValid == 0u)
    {
        return InvalidBsdfEvalL6();
    }
    return result;
}

BsdfEvalL6 EvaluateBsdfL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 woWorldInput,
    float3 wiWorldInput)
{
    BsdfEvalL6 result = InvalidBsdfEvalL6();
    if (!ValidateBsdfParamsL6(context, parameters))
    {
        return result;
    }

    float3 geometricNormal;
    float3 shadingNormal;
    float3 tangent;
    float3 bitangent;
    float3 woWorld;
    float3 woLocal;
    if (!BsdfBuildFrameL6(
        context,
        woWorldInput,
        geometricNormal,
        shadingNormal,
        tangent,
        bitangent,
        woWorld,
        woLocal))
    {
        return result;
    }

    float3 wiWorld;
    if (!BsdfSafeNormalizeL6(wiWorldInput, wiWorld))
    {
        return result;
    }
    const float3 wiLocal = BsdfToLocalL6(
        wiWorld, tangent, bitangent, shadingNormal);

    if (parameters.model == kBsdfModelLambertL6)
    {
        result = BsdfEvaluateLambertLocalL6(parameters, woLocal, wiLocal);
    }
    else if (parameters.model == kBsdfModelGgxConductorL6 ||
             parameters.model == kBsdfModelGgxDielectricReflectionL6)
    {
        result = BsdfEvaluateGgxReflectionLocalL6(
            context, parameters, woLocal, wiLocal);
    }
    else if (parameters.model == kBsdfModelRoughDielectricL6)
    {
        result = BsdfEvaluateRoughDielectricLocalL6(
            context, parameters, woLocal, wiLocal);
    }
    else if (parameters.model == kBsdfModelMetallicRoughnessL6)
    {
        result = BsdfEvaluateMetallicRoughnessLocalL6(
            parameters, woLocal, wiLocal);
    }
    else if (parameters.model == kBsdfModelSmoothGlassL6)
    {
        // Delta lobes have no finite-direction Evaluate/Pdf value.
        result.measure = kBsdfMeasureDiscreteL6;
        return result;
    }

    if (result.isValid == 0u ||
        !BsdfValidateWorldEventL6(
            woWorld, wiWorld, geometricNormal, result.lobeFlags))
    {
        return InvalidBsdfEvalL6();
    }

    float correction;
    if (!BsdfShadingNormalCorrectionL6(
        context,
        geometricNormal,
        shadingNormal,
        woWorld,
        wiWorld,
        correction))
    {
        return InvalidBsdfEvalL6();
    }
    result.value *= correction;
    result.diffuseValue *= correction;
    result.specularValue *= correction;
    if (!BsdfFiniteFloat3L6(result.value) ||
        !BsdfFiniteFloat3L6(result.diffuseValue) ||
        !BsdfFiniteFloat3L6(result.specularValue))
    {
        return InvalidBsdfEvalL6();
    }
    return result;
}

float PdfBsdfL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 woWorld,
    float3 wiWorld)
{
    const BsdfEvalL6 evaluation = EvaluateBsdfL6(
        context, parameters, woWorld, wiWorld);
    return evaluation.isValid != 0u &&
        evaluation.measure == kBsdfMeasureSolidAngleL6
        ? evaluation.pdf
        : 0.0f;
}

float3 BsdfSampleUniformHemisphereL6(float2 randomSample)
{
    const float cosineTheta = randomSample.x;
    const float sineTheta = sqrt(max(0.0f, 1.0f - cosineTheta * cosineTheta));
    const float phi = 2.0f * kBsdfPiL6 * randomSample.y;
    return float3(
        sineTheta * cos(phi),
        sineTheta * sin(phi),
        cosineTheta);
}

float BsdfUniformHemispherePdfL6(float3 direction)
{
    return direction.z >= 0.0f ? 0.5f * kBsdfInversePiL6 : 0.0f;
}

float3 BsdfSampleCosineHemisphereL6(float2 randomSample)
{
    const float radius = sqrt(randomSample.x);
    const float phi = 2.0f * kBsdfPiL6 * randomSample.y;
    return float3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(0.0f, 1.0f - randomSample.x)));
}

bool BsdfRefractLocalL6(
    float3 wo,
    float3 microNormalInput,
    float etaRelative,
    out float3 wi)
{
    wi = 0.0f;
    float3 microNormal = microNormalInput;
    float cosineIncident = dot(microNormal, wo);
    if (cosineIncident < 0.0f)
    {
        microNormal = -microNormal;
        cosineIncident = -cosineIncident;
    }
    if (cosineIncident <= kBsdfDirectionEpsilonL6 ||
        !BsdfFiniteFloatL6(etaRelative) || etaRelative <= 0.0f)
    {
        return false;
    }
    const float sineTransmittedSquared =
        max(0.0f, 1.0f - cosineIncident * cosineIncident) /
        (etaRelative * etaRelative);
    if (sineTransmittedSquared >= 1.0f)
    {
        return false;
    }
    const float cosineTransmitted = sqrt(max(
        0.0f, 1.0f - sineTransmittedSquared));
    const float3 candidate = -wo / etaRelative +
        (cosineIncident / etaRelative - cosineTransmitted) * microNormal;
    return BsdfSafeNormalizeL6(candidate, wi);
}

BsdfSampleL6 BsdfSampleSmoothGlassLocalL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 wo,
    float eventSample)
{
    BsdfSampleL6 result = InvalidBsdfSampleL6();
    float fresnel;
    if (!BsdfFresnelDielectricL6(
        wo.z,
        context.etaIncident,
        context.etaTransmitted,
        fresnel))
    {
        return result;
    }

    float reflectionWeight = BsdfContainsFlagsL6(
        parameters.allowedLobes, kBsdfLobeSpecularReflectionL6)
        ? fresnel
        : 0.0f;
    float transmissionWeight = BsdfContainsFlagsL6(
        parameters.allowedLobes, kBsdfLobeSpecularTransmissionL6)
        ? (1.0f - fresnel) * parameters.transmission
        : 0.0f;
    const float totalWeight = reflectionWeight + transmissionWeight;
    if (!BsdfFiniteFloatL6(totalWeight) || totalWeight <= 0.0f)
    {
        return result;
    }
    const float reflectionProbability = reflectionWeight / totalWeight;
    const float transmissionProbability = transmissionWeight / totalWeight;

    result.measure = kBsdfMeasureDiscreteL6;
    result.isDelta = 1u;
    result.eta = 1.0f;
    if (eventSample < reflectionProbability)
    {
        result.direction = float3(-wo.x, -wo.y, wo.z);
        const float cosine = abs(result.direction.z);
        if (cosine <= kBsdfDirectionEpsilonL6 || reflectionProbability <= 0.0f)
        {
            return InvalidBsdfSampleL6();
        }
        result.pdf = reflectionProbability;
        result.value = fresnel.xxx / cosine;
        result.specularValue = result.value;
        result.lobeFlags = kBsdfLobeSpecularReflectionL6;
    }
    else
    {
        const float etaRelative = context.etaTransmitted / context.etaIncident;
        if (transmissionProbability <= 0.0f ||
            !BsdfRefractLocalL6(
                wo, float3(0.0f, 0.0f, 1.0f), etaRelative, result.direction))
        {
            return InvalidBsdfSampleL6();
        }
        const float cosine = abs(result.direction.z);
        if (cosine <= kBsdfDirectionEpsilonL6)
        {
            return InvalidBsdfSampleL6();
        }
        result.pdf = transmissionProbability;
        float value = (1.0f - fresnel) * parameters.transmission / cosine;
        if (context.transportMode == kBsdfTransportRadianceL6)
        {
            value /= etaRelative * etaRelative;
        }
        result.value = value.xxx;
        result.specularValue = result.value;
        result.eta = etaRelative;
        result.lobeFlags = kBsdfLobeSpecularTransmissionL6;
    }
    result.isValid = BsdfFiniteFloat3L6(result.direction) &&
        BsdfFiniteFloat3L6(result.value) &&
        BsdfFiniteFloatL6(result.pdf) && result.pdf > 0.0f ? 1u : 0u;
    if (result.isValid == 0u)
    {
        return InvalidBsdfSampleL6();
    }
    return result;
}

BsdfSampleL6 BsdfSampleFiniteLocalL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 wo,
    float3 randomSample)
{
    BsdfSampleL6 result = InvalidBsdfSampleL6();
    result.measure = kBsdfMeasureSolidAngleL6;
    result.eta = 1.0f;

    if (parameters.model == kBsdfModelLambertL6)
    {
        if (!BsdfContainsFlagsL6(
            parameters.allowedLobes, kBsdfLobeDiffuseReflectionL6))
        {
            return InvalidBsdfSampleL6();
        }
        result.direction = BsdfSampleCosineHemisphereL6(randomSample.yz);
        result.lobeFlags = kBsdfLobeDiffuseReflectionL6;
        result.isValid = 1u;
        return result;
    }

    const float alpha = parameters.perceptualRoughness *
        parameters.perceptualRoughness;
    if (parameters.model == kBsdfModelGgxConductorL6 ||
        parameters.model == kBsdfModelGgxDielectricReflectionL6)
    {
        if (!BsdfContainsFlagsL6(
            parameters.allowedLobes, kBsdfLobeGlossyReflectionL6))
        {
            return InvalidBsdfSampleL6();
        }
        float3 microNormal;
        if (!BsdfSampleGgxVisibleNormalL6(wo, alpha, randomSample.yz, microNormal))
        {
            return InvalidBsdfSampleL6();
        }
        result.direction = -wo + 2.0f * dot(wo, microNormal) * microNormal;
        if (result.direction.z <= kBsdfDirectionEpsilonL6)
        {
            return InvalidBsdfSampleL6();
        }
        result.lobeFlags = kBsdfLobeGlossyReflectionL6;
        result.isValid = 1u;
        return result;
    }

    if (parameters.model == kBsdfModelMetallicRoughnessL6)
    {
        float diffuseProbability;
        float specularProbability;
        if (!BsdfMetallicLobeProbabilitiesL6(
            parameters, wo, diffuseProbability, specularProbability))
        {
            return InvalidBsdfSampleL6();
        }
        if (randomSample.x < diffuseProbability)
        {
            result.direction = BsdfSampleCosineHemisphereL6(randomSample.yz);
            result.lobeFlags = kBsdfLobeDiffuseReflectionL6;
        }
        else
        {
            float3 microNormal;
            if (specularProbability <= 0.0f ||
                !BsdfSampleGgxVisibleNormalL6(
                    wo, alpha, randomSample.yz, microNormal))
            {
                return InvalidBsdfSampleL6();
            }
            result.direction = -wo + 2.0f * dot(wo, microNormal) * microNormal;
            if (result.direction.z <= kBsdfDirectionEpsilonL6)
            {
                return InvalidBsdfSampleL6();
            }
            result.lobeFlags = kBsdfLobeGlossyReflectionL6;
        }
        result.isValid = 1u;
        return result;
    }

    if (parameters.model == kBsdfModelRoughDielectricL6)
    {
        float3 microNormal;
        if (!BsdfSampleGgxVisibleNormalL6(wo, alpha, randomSample.yz, microNormal))
        {
            return InvalidBsdfSampleL6();
        }
        float fresnel;
        if (!BsdfFresnelDielectricL6(
            dot(wo, microNormal),
            context.etaIncident,
            context.etaTransmitted,
            fresnel))
        {
            return InvalidBsdfSampleL6();
        }
        float reflectionProbability;
        float transmissionProbability;
        if (!BsdfRoughDielectricEventProbabilitiesL6(
            parameters, fresnel, reflectionProbability, transmissionProbability))
        {
            return InvalidBsdfSampleL6();
        }
        if (randomSample.x < reflectionProbability)
        {
            result.direction = -wo + 2.0f * dot(wo, microNormal) * microNormal;
            if (result.direction.z <= kBsdfDirectionEpsilonL6)
            {
                return InvalidBsdfSampleL6();
            }
            result.lobeFlags = kBsdfLobeGlossyReflectionL6;
        }
        else
        {
            const float etaRelative = context.etaTransmitted / context.etaIncident;
            if (transmissionProbability <= 0.0f ||
                !BsdfRefractLocalL6(
                    wo, microNormal, etaRelative, result.direction) ||
                result.direction.z >= -kBsdfDirectionEpsilonL6)
            {
                return InvalidBsdfSampleL6();
            }
            result.eta = etaRelative;
            result.lobeFlags = kBsdfLobeGlossyTransmissionL6;
        }
        result.isValid = 1u;
        return result;
    }

    return InvalidBsdfSampleL6();
}

BsdfSampleL6 SampleBsdfL6(
    BsdfContextL6 context,
    BsdfParamsL6 parameters,
    float3 woWorldInput,
    float3 randomSample)
{
    BsdfSampleL6 result = InvalidBsdfSampleL6();
    if (!ValidateBsdfParamsL6(context, parameters) ||
        !BsdfUnitRandom3L6(randomSample))
    {
        return result;
    }

    float3 geometricNormal;
    float3 shadingNormal;
    float3 tangent;
    float3 bitangent;
    float3 woWorld;
    float3 woLocal;
    if (!BsdfBuildFrameL6(
        context,
        woWorldInput,
        geometricNormal,
        shadingNormal,
        tangent,
        bitangent,
        woWorld,
        woLocal))
    {
        return result;
    }

    BsdfSampleL6 localSample;
    if (parameters.model == kBsdfModelSmoothGlassL6)
    {
        localSample = BsdfSampleSmoothGlassLocalL6(
            context, parameters, woLocal, randomSample.x);
    }
    else
    {
        localSample = BsdfSampleFiniteLocalL6(
            context, parameters, woLocal, randomSample);
    }
    if (localSample.isValid == 0u)
    {
        return result;
    }

    float3 wiWorld;
    if (!BsdfSafeNormalizeL6(
        BsdfToWorldL6(
            localSample.direction, tangent, bitangent, shadingNormal),
        wiWorld) ||
        !BsdfValidateWorldEventL6(
            woWorld, wiWorld, geometricNormal, localSample.lobeFlags))
    {
        return InvalidBsdfSampleL6();
    }

    if (localSample.measure == kBsdfMeasureSolidAngleL6)
    {
        const BsdfEvalL6 evaluation = EvaluateBsdfL6(
            context, parameters, woWorld, wiWorld);
        if (evaluation.isValid == 0u)
        {
            return InvalidBsdfSampleL6();
        }
        result.direction = wiWorld;
        result.pdf = evaluation.pdf;
        result.value = evaluation.value;
        result.eta = localSample.eta;
        result.diffuseValue = evaluation.diffuseValue;
        result.specularValue = evaluation.specularValue;
        result.measure = kBsdfMeasureSolidAngleL6;
        result.lobeFlags = localSample.lobeFlags;
        result.isDelta = 0u;
        result.isValid = 1u;
        return result;
    }

    float correction;
    if (!BsdfShadingNormalCorrectionL6(
        context,
        geometricNormal,
        shadingNormal,
        woWorld,
        wiWorld,
        correction))
    {
        return InvalidBsdfSampleL6();
    }
    result = localSample;
    result.direction = wiWorld;
    result.value *= correction;
    result.diffuseValue *= correction;
    result.specularValue *= correction;
    result.isValid = BsdfFiniteFloat3L6(result.value) &&
        BsdfFiniteFloat3L6(result.specularValue) ? 1u : 0u;
    if (result.isValid == 0u)
    {
        return InvalidBsdfSampleL6();
    }
    return result;
}

#endif
