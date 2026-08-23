// Physically based Vulkan compute path tracer.
//
// Material semantics follow glTF 2.0 metallic-roughness. The opaque BRDF uses
// Cook-Torrance with GGX/Trowbridge-Reitz, height-correlated Smith visibility,
// and Schlick Fresnel. GGX reflection directions are sampled from the visible
// normal distribution described by Heitz (JCGT 2018). Sphere emitters are
// sampled uniformly in solid angle for next-event estimation.

// See PBR_IMPLEMENTATION.md and THIRD_PARTY_NOTICES.md for references.

static const float kPi = 3.14159265358979323846f;
static const float kInversePi = 0.31830988618379067154f;
static const float kRayEpsilon = 1.0e-3f;
static const float kMinimumAlpha = 2.0e-3f;
static const float kThroughputCutoff = 1.0e-4f;
static const uint kMaximumAccumulationSamples = 4096u;
static const uint kShadowMethodPcf = 0u;
static const uint kShadowMethodPcss = 1u;
static const uint kShadowMethodPhysical = 2u;
static const uint kPcfSampleCount = 16u;
static const uint kPcssBlockerSampleCount = 8u;
static const uint kPcssFilterSampleCount = 16u;

// Preserved from the renderer's legacy ray-tested PCF/PCSS preview modes.
static const float2 kShadowDiskSamples[16] =
{
    float2( 0.000000f,  0.000000f),
    float2(-0.613392f,  0.617481f),
    float2( 0.170019f, -0.040254f),
    float2(-0.299417f,  0.791925f),
    float2( 0.645680f,  0.493210f),
    float2(-0.651784f, -0.717887f),
    float2( 0.421003f,  0.027070f),
    float2(-0.817194f, -0.271096f),
    float2(-0.705374f, -0.668203f),
    float2( 0.977050f, -0.108615f),
    float2( 0.063326f,  0.142369f),
    float2( 0.203528f,  0.214331f),
    float2(-0.667531f,  0.326090f),
    float2(-0.098422f, -0.295755f),
    float2(-0.885922f,  0.215369f),
    float2( 0.566637f,  0.605213f)
};

struct FrameConstants
{
    float4 cameraPositionTanHalfFov;
    float4 cameraForwardAspect;
    float4 cameraRightTime;
    float4 cameraUpExposure;
    uint4 imageAndScene; // width, height, sphere count, plane count
    uint4 lightAndTrace; // light count, max depth, shadow method, reserved
    uint4 samplingAndDebug; // accumulation sample, debug view, reserved, reserved
};

struct Material
{
    float4 baseColorMetallic;
    float4 emissiveRoughness;
    float4 transmissionIor;
    float4 attenuationColorDistance;
};

struct Sphere
{
    float4 centerRadius;
    uint4 metadata;
};

struct Plane
{
    float4 normalDistance;
    uint4 metadata;
};

struct Light
{
    float4 positionRadius;
    float4 radiance;
};

struct Ray
{
    float3 origin;
    float3 direction;
};

struct Hit
{
    float t;
    float3 position;
    float3 normal;
    uint materialIndex;
    uint flags;
    bool frontFace;
};

struct BsdfSample
{
    float3 direction;
    float3 value;
    float pdf;
    bool valid;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> gFrame;
[[vk::binding(1, 0)]] StructuredBuffer<Sphere> gSpheres;
[[vk::binding(2, 0)]] StructuredBuffer<Plane> gPlanes;
[[vk::binding(3, 0)]] StructuredBuffer<Material> gMaterials;
[[vk::binding(4, 0)]] StructuredBuffer<Light> gLights;
[[vk::image_format("rgba32f")]]
[[vk::binding(5, 0)]] RWTexture2D<float4> gOutput;

uint Hash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float RandomFloat(inout uint state)
{
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word) * (1.0f / 4294967296.0f);
}

float MaxComponent(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

float Luminance(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

void BuildBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    const float3 helper = abs(normal.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(0.0f, 1.0f, 0.0f);
    tangent = normalize(cross(helper, normal));
    bitangent = cross(normal, tangent);
}

float3 ToLocal(float3 direction, float3 tangent, float3 bitangent, float3 normal)
{
    return float3(dot(direction, tangent), dot(direction, bitangent), dot(direction, normal));
}

float3 ToWorld(float3 direction, float3 tangent, float3 bitangent, float3 normal)
{
    return tangent * direction.x + bitangent * direction.y + normal * direction.z;
}

void CommitHit(
    Ray ray,
    float t,
    float3 outwardNormal,
    uint materialIndex,
    uint flags,
    inout Hit hit)
{
    hit.t = t;
    hit.position = ray.origin + ray.direction * t;
    hit.frontFace = dot(ray.direction, outwardNormal) < 0.0f;
    hit.normal = hit.frontFace ? outwardNormal : -outwardNormal;
    hit.materialIndex = materialIndex;
    hit.flags = flags;
}

bool TraceClosest(Ray ray, float tMinimum, float tMaximum, out Hit hit)
{
    bool found = false;
    hit.t = tMaximum;
    hit.position = 0.0f;
    hit.normal = 0.0f;
    hit.materialIndex = 0u;
    hit.flags = 0u;
    hit.frontFace = true;

    [loop]
    for (uint sphereIndex = 0u; sphereIndex < gFrame.imageAndScene.z; ++sphereIndex)
    {
        const Sphere sphere = gSpheres[sphereIndex];
        const float3 center = sphere.centerRadius.xyz;
        const float radius = sphere.centerRadius.w;
        const float3 originToCenter = ray.origin - center;
        const float halfB = dot(originToCenter, ray.direction);
        const float c = dot(originToCenter, originToCenter) - radius * radius;
        const float discriminant = halfB * halfB - c;

        if (discriminant < 0.0f)
        {
            continue;
        }

        const float squareRoot = sqrt(discriminant);
        float root = -halfB - squareRoot;
        if (root <= tMinimum || root >= hit.t)
        {
            root = -halfB + squareRoot;
            if (root <= tMinimum || root >= hit.t)
            {
                continue;
            }
        }

        const float3 hitPosition = ray.origin + ray.direction * root;
        CommitHit(
            ray,
            root,
            normalize(hitPosition - center),
            sphere.metadata.x,
            sphere.metadata.y,
            hit);
        found = true;
    }

    [loop]
    for (uint planeIndex = 0u; planeIndex < gFrame.imageAndScene.w; ++planeIndex)
    {
        const Plane plane = gPlanes[planeIndex];
        const float3 normal = normalize(plane.normalDistance.xyz);
        const float denominator = dot(normal, ray.direction);
        if (abs(denominator) < 1.0e-6f)
        {
            continue;
        }

        const float t = -(dot(normal, ray.origin) + plane.normalDistance.w) / denominator;
        if (t <= tMinimum || t >= hit.t)
        {
            continue;
        }

        CommitHit(ray, t, normal, plane.metadata.x, plane.metadata.y, hit);
        found = true;
    }

    return found;
}

bool TraceAny(Ray ray, float tMinimum, float tMaximum)
{
    Hit ignoredHit;
    return TraceClosest(ray, tMinimum, tMaximum, ignoredHit);
}

float3 EnvironmentRadiance(float3 direction)
{
    const float elevation = saturate(direction.y * 0.5f + 0.5f);
    const float3 horizon = float3(0.055f, 0.070f, 0.105f);
    const float3 zenith = float3(0.30f, 0.47f, 0.78f);
    const float3 ground = float3(0.018f, 0.014f, 0.012f);
    const float skyWeight = smoothstep(0.0f, 0.52f, elevation);
    return lerp(ground, lerp(horizon, zenith, elevation * elevation), skyWeight);
}

float3 SurfaceBaseColor(Material material, Hit hit)
{
    float3 baseColor = saturate(material.baseColorMetallic.xyz);
    if ((hit.flags & 1u) != 0u)
    {
        const float tileSum = floor(hit.position.x * 0.75f) + floor(hit.position.z * 0.75f);
        const float checker = fmod(abs(tileSum), 2.0f);
        baseColor *= lerp(0.24f, 1.0f, checker);
    }
    return baseColor;
}

float DielectricF0(float indexOfRefraction)
{
    const float ratio = (max(indexOfRefraction, 1.0001f) - 1.0f)
        / (max(indexOfRefraction, 1.0001f) + 1.0f);
    return ratio * ratio;
}

float3 FresnelSchlick(float cosine, float3 f0)
{
    const float oneMinusCosine = 1.0f - saturate(cosine);
    const float factor = oneMinusCosine * oneMinusCosine * oneMinusCosine
        * oneMinusCosine * oneMinusCosine;
    return f0 + (1.0f - f0) * factor;
}

float DistributionGGX(float normalDotHalf, float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float factor = normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(kPi * factor * factor, 1.0e-7f);
}

float VisibilitySmithGGXCorrelated(float normalDotView, float normalDotLight, float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float ggxView = normalDotLight * sqrt(
        normalDotView * normalDotView * (1.0f - alphaSquared) + alphaSquared);
    const float ggxLight = normalDotView * sqrt(
        normalDotLight * normalDotLight * (1.0f - alphaSquared) + alphaSquared);
    return 0.5f / max(ggxView + ggxLight, 1.0e-6f);
}

float MaskingSmithGGX(float normalDotDirection, float alpha)
{
    const float alphaSquared = alpha * alpha;
    return (2.0f * normalDotDirection) / max(
        normalDotDirection
            + sqrt(alphaSquared + (1.0f - alphaSquared)
                * normalDotDirection * normalDotDirection),
        1.0e-6f);
}

float3 MaterialF0(Material material, float3 baseColor)
{
    const float metallic = saturate(material.baseColorMetallic.w);
    const float dielectric = DielectricF0(material.transmissionIor.y);
    return lerp(dielectric.xxx, baseColor, metallic);
}

float SpecularSamplingProbability(Material material, float3 baseColor, float normalDotView)
{
    const float metallic = saturate(material.baseColorMetallic.w);
    const float transmission = saturate(material.transmissionIor.x);
    const float3 viewFresnel = FresnelSchlick(normalDotView, MaterialF0(material, baseColor));
    const float specularWeight = max(Luminance(viewFresnel), 1.0e-4f);
    const float diffuseWeight = Luminance(baseColor) * (1.0f - metallic) * (1.0f - transmission);

    if (diffuseWeight <= 1.0e-5f)
    {
        return 1.0f;
    }
    return clamp(specularWeight / (specularWeight + diffuseWeight), 0.05f, 0.95f);
}

float3 EvaluateOpaqueBrdf(
    Material material,
    float3 baseColor,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    out float pdf)
{
    pdf = 0.0f;
    const float normalDotView = saturate(dot(normal, viewDirection));
    const float normalDotLight = saturate(dot(normal, lightDirection));
    if (normalDotView <= 0.0f || normalDotLight <= 0.0f)
    {
        return 0.0f;
    }

    const float3 halfVectorUnnormalized = viewDirection + lightDirection;
    const float halfVectorLengthSquared = dot(halfVectorUnnormalized, halfVectorUnnormalized);
    if (halfVectorLengthSquared <= 1.0e-10f)
    {
        return 0.0f;
    }

    const float3 halfVector = halfVectorUnnormalized * rsqrt(halfVectorLengthSquared);
    const float normalDotHalf = saturate(dot(normal, halfVector));
    const float viewDotHalf = saturate(dot(viewDirection, halfVector));
    const float perceptualRoughness = clamp(material.emissiveRoughness.w, 0.0f, 1.0f);
    const float alpha = max(perceptualRoughness * perceptualRoughness, kMinimumAlpha);
    const float distribution = DistributionGGX(normalDotHalf, alpha);
    const float visibility = VisibilitySmithGGXCorrelated(normalDotView, normalDotLight, alpha);
    const float3 fresnel = FresnelSchlick(viewDotHalf, MaterialF0(material, baseColor));

    const float metallic = saturate(material.baseColorMetallic.w);
    const float transmission = saturate(material.transmissionIor.x);
    const float3 diffuse = (1.0f - fresnel) * baseColor
        * ((1.0f - metallic) * (1.0f - transmission) * kInversePi);
    const float3 specular = fresnel * (distribution * visibility);

    const float diffusePdf = normalDotLight * kInversePi;
    const float specularPdf = distribution * MaskingSmithGGX(normalDotView, alpha)
        / max(4.0f * normalDotView, 1.0e-6f);
    const float specularProbability = SpecularSamplingProbability(
        material, baseColor, normalDotView);
    pdf = lerp(diffusePdf, specularPdf, specularProbability);
    return diffuse + specular;
}

float3 SampleCosineHemisphere(float2 randomSample)
{
    const float radius = sqrt(randomSample.x);
    const float phi = 2.0f * kPi * randomSample.y;
    return float3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(0.0f, 1.0f - randomSample.x)));
}

// Isotropic form of Heitz's exact GGX visible-normal sampling routine.
float3 SampleGGXVisibleNormal(float3 viewLocal, float alpha, float2 randomSample)
{
    const float3 stretchedView = normalize(float3(
        alpha * viewLocal.x,
        alpha * viewLocal.y,
        max(viewLocal.z, 1.0e-6f)));

    const float lensSquared = stretchedView.x * stretchedView.x
        + stretchedView.y * stretchedView.y;
    const float3 tangent1 = lensSquared > 1.0e-8f
        ? float3(-stretchedView.y, stretchedView.x, 0.0f) * rsqrt(lensSquared)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent2 = cross(stretchedView, tangent1);

    const float radius = sqrt(randomSample.x);
    const float phi = 2.0f * kPi * randomSample.y;
    const float diskX = radius * cos(phi);
    float diskY = radius * sin(phi);
    const float blend = 0.5f * (1.0f + stretchedView.z);
    diskY = lerp(sqrt(max(0.0f, 1.0f - diskX * diskX)), diskY, blend);

    const float projectedZ = sqrt(max(0.0f, 1.0f - diskX * diskX - diskY * diskY));
    const float3 hemisphereNormal = diskX * tangent1
        + diskY * tangent2
        + projectedZ * stretchedView;
    return normalize(float3(
        alpha * hemisphereNormal.x,
        alpha * hemisphereNormal.y,
        max(hemisphereNormal.z, 1.0e-6f)));
}

BsdfSample SampleOpaqueBsdf(
    Material material,
    float3 baseColor,
    float3 normal,
    float3 viewDirection,
    inout uint randomState)
{
    BsdfSample sample;
    sample.direction = 0.0f;
    sample.value = 0.0f;
    sample.pdf = 0.0f;
    sample.valid = false;

    float3 tangent;
    float3 bitangent;
    BuildBasis(normal, tangent, bitangent);
    const float3 viewLocal = ToLocal(viewDirection, tangent, bitangent, normal);
    if (viewLocal.z <= 0.0f)
    {
        return sample;
    }

    const float specularProbability = SpecularSamplingProbability(
        material, baseColor, viewLocal.z);
    const float2 directionSample = float2(
        RandomFloat(randomState),
        RandomFloat(randomState));

    float3 lightLocal;
    if (RandomFloat(randomState) < specularProbability)
    {
        const float perceptualRoughness = clamp(material.emissiveRoughness.w, 0.0f, 1.0f);
        const float alpha = max(perceptualRoughness * perceptualRoughness, kMinimumAlpha);
        const float3 halfLocal = SampleGGXVisibleNormal(viewLocal, alpha, directionSample);
        lightLocal = reflect(-viewLocal, halfLocal);
    }
    else
    {
        lightLocal = SampleCosineHemisphere(directionSample);
    }

    if (lightLocal.z <= 0.0f)
    {
        return sample;
    }

    sample.direction = normalize(ToWorld(lightLocal, tangent, bitangent, normal));
    sample.value = EvaluateOpaqueBrdf(
        material,
        baseColor,
        normal,
        viewDirection,
        sample.direction,
        sample.pdf);
    sample.valid = sample.pdf > 1.0e-7f && MaxComponent(sample.value) > 0.0f;
    return sample;
}

float3 OffsetRayOrigin(float3 position, float3 orientedNormal, float3 direction)
{
    const float side = dot(direction, orientedNormal) >= 0.0f ? 1.0f : -1.0f;
    return position + orientedNormal * (side * kRayEpsilon);
}

float3 AreaLightDiskSamplePosition(
    Light light,
    float3 tangent,
    float3 bitangent,
    uint sampleIndex,
    float sampleRadius)
{
    const float2 diskOffset = kShadowDiskSamples[sampleIndex] * sampleRadius;
    return light.positionRadius.xyz + tangent * diskOffset.x + bitangent * diskOffset.y;
}

bool FindPreviewShadowBlocker(
    Hit receiver,
    Light light,
    float3 lightSamplePosition,
    out float blockerDepthFromLight)
{
    blockerDepthFromLight = 0.0f;
    const float3 shadowOrigin = OffsetRayOrigin(receiver.position, receiver.normal, receiver.normal);
    const float3 toLightSample = lightSamplePosition - shadowOrigin;
    const float sampleDistance = length(toLightSample);
    if (sampleDistance <= 2.0f * kRayEpsilon)
    {
        return false;
    }

    Ray shadowRay;
    shadowRay.origin = shadowOrigin;
    shadowRay.direction = toLightSample / sampleDistance;

    Hit blocker;
    if (!TraceClosest(shadowRay, kRayEpsilon, sampleDistance - kRayEpsilon, blocker))
    {
        return false;
    }

    const float emitterRadius = max(light.positionRadius.w, 1.0e-4f);
    const bool reachedThisEmitter = length(blocker.position - light.positionRadius.xyz)
        <= emitterRadius * 1.01f;
    if (reachedThisEmitter)
    {
        return false;
    }

    blockerDepthFromLight = max(sampleDistance - blocker.t, kRayEpsilon);
    return true;
}

float EvaluatePcfVisibility(Hit receiver, Light light)
{
    const float3 receiverToLight = light.positionRadius.xyz - receiver.position;
    const float receiverDepth = length(receiverToLight);
    if (receiverDepth <= 2.0f * kRayEpsilon)
    {
        return 1.0f;
    }

    float3 tangent;
    float3 bitangent;
    BuildBasis(receiverToLight / receiverDepth, tangent, bitangent);
    const float filterRadius = max(light.positionRadius.w, 0.0f);
    float visibleSamples = 0.0f;

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < kPcfSampleCount; ++sampleIndex)
    {
        const float3 samplePosition = AreaLightDiskSamplePosition(
            light, tangent, bitangent, sampleIndex, filterRadius);
        float ignoredBlockerDepth;
        visibleSamples += FindPreviewShadowBlocker(
            receiver, light, samplePosition, ignoredBlockerDepth) ? 0.0f : 1.0f;
    }
    return visibleSamples / float(kPcfSampleCount);
}

float EvaluatePcssVisibility(Hit receiver, Light light)
{
    const float3 receiverToLight = light.positionRadius.xyz - receiver.position;
    const float receiverDepth = length(receiverToLight);
    if (receiverDepth <= 2.0f * kRayEpsilon)
    {
        return 1.0f;
    }

    float3 tangent;
    float3 bitangent;
    BuildBasis(receiverToLight / receiverDepth, tangent, bitangent);
    const float lightRadius = max(light.positionRadius.w, 0.0f);
    float blockerDepthSum = 0.0f;
    uint blockerCount = 0u;

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < kPcssBlockerSampleCount; ++sampleIndex)
    {
        const float3 samplePosition = AreaLightDiskSamplePosition(
            light, tangent, bitangent, sampleIndex, lightRadius);
        float blockerDepth;
        if (FindPreviewShadowBlocker(receiver, light, samplePosition, blockerDepth))
        {
            blockerDepthSum += blockerDepth;
            ++blockerCount;
        }
    }

    if (blockerCount == 0u)
    {
        return 1.0f;
    }

    const float averageBlockerDepth = blockerDepthSum / float(blockerCount);
    const float penumbraRatio = max(
        (receiverDepth - averageBlockerDepth) / max(averageBlockerDepth, kRayEpsilon),
        0.0f);
    const float filterRadius = lightRadius * clamp(penumbraRatio, 0.05f, 1.0f);
    float visibleSamples = 0.0f;

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < kPcssFilterSampleCount; ++sampleIndex)
    {
        const float3 samplePosition = AreaLightDiskSamplePosition(
            light, tangent, bitangent, sampleIndex, filterRadius);
        float ignoredBlockerDepth;
        visibleSamples += FindPreviewShadowBlocker(
            receiver, light, samplePosition, ignoredBlockerDepth) ? 0.0f : 1.0f;
    }
    return visibleSamples / float(kPcssFilterSampleCount);
}

float EvaluatePreviewShadowVisibility(Hit receiver, Light light)
{
    return gFrame.lightAndTrace.z == kShadowMethodPcf
        ? EvaluatePcfVisibility(receiver, light)
        : EvaluatePcssVisibility(receiver, light);
}

bool SampleSphereLight(
    float3 origin,
    Light light,
    inout uint randomState,
    out float3 direction,
    out float pdf,
    out float distanceToLight)
{
    direction = 0.0f;
    pdf = 0.0f;
    distanceToLight = 0.0f;

    const float3 center = light.positionRadius.xyz;
    const float radius = max(light.positionRadius.w, 1.0e-4f);
    const float3 toCenter = center - origin;
    const float distanceSquared = dot(toCenter, toCenter);
    const float radiusSquared = radius * radius;
    if (distanceSquared <= radiusSquared * 1.0001f)
    {
        return false;
    }

    const float centerDistance = sqrt(distanceSquared);
    const float3 centerDirection = toCenter / centerDistance;
    const float cosThetaMaximum = sqrt(max(0.0f, 1.0f - radiusSquared / distanceSquared));
    const float cosTheta = lerp(1.0f, cosThetaMaximum, RandomFloat(randomState));
    const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * kPi * RandomFloat(randomState);

    float3 tangent;
    float3 bitangent;
    BuildBasis(centerDirection, tangent, bitangent);
    direction = normalize(
        tangent * (cos(phi) * sinTheta)
        + bitangent * (sin(phi) * sinTheta)
        + centerDirection * cosTheta);

    const float3 originToCenter = origin - center;
    const float halfB = dot(originToCenter, direction);
    const float c = dot(originToCenter, originToCenter) - radiusSquared;
    const float discriminant = halfB * halfB - c;
    if (discriminant <= 0.0f)
    {
        return false;
    }

    distanceToLight = -halfB - sqrt(discriminant);
    if (distanceToLight <= kRayEpsilon)
    {
        distanceToLight = -halfB + sqrt(discriminant);
    }
    if (distanceToLight <= kRayEpsilon)
    {
        return false;
    }

    pdf = 1.0f / max(2.0f * kPi * (1.0f - cosThetaMaximum), 1.0e-8f);
    return true;
}

float3 EvaluateDirectLighting(
    Hit hit,
    Material material,
    float3 baseColor,
    float3 viewDirection,
    inout uint randomState)
{
    float3 result = 0.0f;
    const float3 shadowOrigin = OffsetRayOrigin(hit.position, hit.normal, hit.normal);

    [loop]
    for (uint lightIndex = 0u; lightIndex < gFrame.lightAndTrace.x; ++lightIndex)
    {
        const Light light = gLights[lightIndex];

        if (gFrame.lightAndTrace.z != kShadowMethodPhysical)
        {
            const float3 toLightCenter = light.positionRadius.xyz - hit.position;
            const float distanceSquared = dot(toLightCenter, toLightCenter);
            if (distanceSquared <= 1.0e-6f)
            {
                continue;
            }

            const float distanceToCenter = sqrt(distanceSquared);
            const float3 lightDirection = toLightCenter / distanceToCenter;
            const float normalDotLight = saturate(dot(hit.normal, lightDirection));
            if (normalDotLight <= 0.0f)
            {
                continue;
            }

            const float radius = min(max(light.positionRadius.w, 0.0f), distanceToCenter * 0.999f);
            const float cosThetaMaximum = sqrt(max(0.0f, 1.0f - radius * radius / distanceSquared));
            const float solidAngle = 2.0f * kPi * (1.0f - cosThetaMaximum);
            float ignoredBsdfPdf;
            const float3 brdf = EvaluateOpaqueBrdf(
                material,
                baseColor,
                hit.normal,
                viewDirection,
                lightDirection,
                ignoredBsdfPdf);
            result += light.radiance.xyz * brdf * normalDotLight * solidAngle
                * EvaluatePreviewShadowVisibility(hit, light);
            continue;
        }

        float3 lightDirection;
        float lightPdf;
        float lightDistance;
        if (!SampleSphereLight(
            shadowOrigin,
            light,
            randomState,
            lightDirection,
            lightPdf,
            lightDistance))
        {
            continue;
        }

        const float normalDotLight = saturate(dot(hit.normal, lightDirection));
        if (normalDotLight <= 0.0f)
        {
            continue;
        }

        Ray shadowRay;
        shadowRay.origin = shadowOrigin;
        shadowRay.direction = lightDirection;
        if (TraceAny(
            shadowRay,
            kRayEpsilon,
            max(kRayEpsilon, lightDistance - 2.0f * kRayEpsilon)))
        {
            continue;
        }

        float ignoredBsdfPdf;
        const float3 brdf = EvaluateOpaqueBrdf(
            material,
            baseColor,
            hit.normal,
            viewDirection,
            lightDirection,
            ignoredBsdfPdf);
        result += light.radiance.xyz * brdf * (normalDotLight / lightPdf);
    }

    return result;
}

float FresnelDielectric(float cosineIncident, float etaIncident, float etaTransmitted)
{
    cosineIncident = saturate(cosineIncident);
    const float eta = etaIncident / etaTransmitted;
    const float sinTransmittedSquared = eta * eta * max(0.0f, 1.0f - cosineIncident * cosineIncident);
    if (sinTransmittedSquared >= 1.0f)
    {
        return 1.0f;
    }

    const float cosineTransmitted = sqrt(max(0.0f, 1.0f - sinTransmittedSquared));
    const float parallel = (etaTransmitted * cosineIncident - etaIncident * cosineTransmitted)
        / max(etaTransmitted * cosineIncident + etaIncident * cosineTransmitted, 1.0e-6f);
    const float perpendicular = (etaIncident * cosineIncident - etaTransmitted * cosineTransmitted)
        / max(etaIncident * cosineIncident + etaTransmitted * cosineTransmitted, 1.0e-6f);
    return 0.5f * (parallel * parallel + perpendicular * perpendicular);
}

float3 ApplyVolumeAttenuation(float3 throughput, Material material, float distanceInMedium)
{
    const float attenuationDistance = material.attenuationColorDistance.w;
    if (attenuationDistance <= 0.0f)
    {
        return throughput;
    }

    const float3 attenuationColor = clamp(
        material.attenuationColorDistance.xyz,
        1.0e-4f,
        1.0f);
    return throughput * pow(attenuationColor, distanceInMedium / attenuationDistance);
}

float3 TracePath(Ray ray, inout uint randomState)
{
    float3 radiance = 0.0f;
    float3 throughput = 1.0f;
    bool previousEventWasDelta = true;
    const uint maximumDepth = clamp(gFrame.lightAndTrace.y, 1u, 12u);

    [loop]
    for (uint depth = 0u; depth < maximumDepth; ++depth)
    {
        Hit hit;
        if (!TraceClosest(ray, kRayEpsilon, 1.0e30f, hit))
        {
            radiance += throughput * EnvironmentRadiance(ray.direction);
            break;
        }

        const Material material = gMaterials[hit.materialIndex];
        const float3 baseColor = SurfaceBaseColor(material, hit);
        const float transmission = saturate(material.transmissionIor.x);

        if (!hit.frontFace && transmission > 0.0f)
        {
            throughput = ApplyVolumeAttenuation(throughput, material, hit.t);
        }

        const float3 emission = max(material.emissiveRoughness.xyz, 0.0f);
        if (MaxComponent(emission) > 0.0f)
        {
            // Non-delta direct-light hits are covered by next-event estimation.
            // Keeping only camera and delta-path hits avoids counting that energy twice.
            if (depth == 0u || previousEventWasDelta)
            {
                radiance += throughput * emission;
            }
            break;
        }

        const float3 viewDirection = -ray.direction;
        if (transmission > 0.0f)
        {
            // The transmission extension is intentionally a smooth dielectric.
            // Opaque rough surfaces use the full GGX model above.
            const float indexOfRefraction = max(material.transmissionIor.y, 1.0001f);
            const float etaIncident = hit.frontFace ? 1.0f : indexOfRefraction;
            const float etaTransmitted = hit.frontFace ? indexOfRefraction : 1.0f;
            const float fresnel = FresnelDielectric(
                saturate(dot(viewDirection, hit.normal)),
                etaIncident,
                etaTransmitted);
            const float reflectionProbability = fresnel;
            const float refractionProbability = (1.0f - fresnel) * transmission;
            const float eventSample = RandomFloat(randomState);

            float3 nextDirection;
            if (eventSample < reflectionProbability)
            {
                nextDirection = normalize(reflect(ray.direction, hit.normal));
            }
            else if (eventSample < reflectionProbability + refractionProbability)
            {
                nextDirection = refract(ray.direction, hit.normal, etaIncident / etaTransmitted);
                const float directionLengthSquared = dot(nextDirection, nextDirection);
                if (directionLengthSquared <= 1.0e-10f)
                {
                    break;
                }
                nextDirection *= rsqrt(directionLengthSquared);
            }
            else
            {
                break;
            }

            ray.origin = OffsetRayOrigin(hit.position, hit.normal, nextDirection);
            ray.direction = nextDirection;
            previousEventWasDelta = true;
            continue;
        }

        radiance += throughput * EvaluateDirectLighting(
            hit,
            material,
            baseColor,
            viewDirection,
            randomState);

        const BsdfSample sample = SampleOpaqueBsdf(
            material,
            baseColor,
            hit.normal,
            viewDirection,
            randomState);
        if (!sample.valid)
        {
            break;
        }

        const float normalDotDirection = saturate(dot(hit.normal, sample.direction));
        throughput *= sample.value * (normalDotDirection / sample.pdf);
        if (MaxComponent(throughput) < kThroughputCutoff)
        {
            break;
        }

        if (depth >= 2u)
        {
            const float continuationProbability = clamp(MaxComponent(throughput), 0.05f, 0.95f);
            if (RandomFloat(randomState) > continuationProbability)
            {
                break;
            }
            throughput /= continuationProbability;
        }

        ray.origin = OffsetRayOrigin(hit.position, hit.normal, sample.direction);
        ray.direction = sample.direction;
        previousEventWasDelta = false;
    }

    return max(radiance, 0.0f);
}

float3 EvaluateDebugView(Ray ray, uint debugView)
{
    Hit hit;
    if (!TraceClosest(ray, kRayEpsilon, 1.0e30f, hit))
    {
        return EnvironmentRadiance(ray.direction);
    }

    const Material material = gMaterials[hit.materialIndex];
    if (debugView == 1u)
    {
        return SurfaceBaseColor(material, hit);
    }
    if (debugView == 2u)
    {
        return hit.normal * 0.5f + 0.5f;
    }
    if (debugView == 3u)
    {
        return material.emissiveRoughness.www;
    }
    if (debugView == 4u)
    {
        return material.baseColorMetallic.www;
    }
    if (debugView == 5u)
    {
        return saturate(material.emissiveRoughness.xyz);
    }
    return float3(1.0f, 0.0f, 1.0f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 imageSize = gFrame.imageAndScene.xy;
    if (pixel.x >= imageSize.x || pixel.y >= imageSize.y)
    {
        return;
    }

    const uint sampleIndex = gFrame.samplingAndDebug.x;
    const uint debugView = gFrame.samplingAndDebug.y;
    if (debugView == 0u && sampleIndex >= kMaximumAccumulationSamples)
    {
        return;
    }

    uint randomState = Hash(pixel.x + pixel.y * imageSize.x)
        ^ Hash(sampleIndex + 0x9e3779b9u);
    const float2 pixelOffset = debugView == 0u
        ? float2(RandomFloat(randomState), RandomFloat(randomState))
        : float2(0.5f, 0.5f);
    float2 screen = (float2(pixel) + pixelOffset) / float2(imageSize);
    screen = screen * 2.0f - 1.0f;
    screen.y = -screen.y;

    Ray primaryRay;
    primaryRay.origin = gFrame.cameraPositionTanHalfFov.xyz;
    primaryRay.direction = normalize(
        gFrame.cameraForwardAspect.xyz
        + gFrame.cameraRightTime.xyz
            * (screen.x * gFrame.cameraForwardAspect.w * gFrame.cameraPositionTanHalfFov.w)
        + gFrame.cameraUpExposure.xyz
            * (screen.y * gFrame.cameraPositionTanHalfFov.w));

    if (debugView != 0u)
    {
        gOutput[pixel] = float4(EvaluateDebugView(primaryRay, debugView), 1.0f);
        return;
    }

    const float3 sampleRadiance = TracePath(primaryRay, randomState);
    const float3 previousAverage = sampleIndex == 0u ? 0.0f : gOutput[pixel].xyz;
    const float3 accumulated = previousAverage
        + (sampleRadiance - previousAverage) / float(sampleIndex + 1u);
    gOutput[pixel] = float4(accumulated, 1.0f);
}
