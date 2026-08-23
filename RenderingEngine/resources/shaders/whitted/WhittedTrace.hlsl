static const float kRayEpsilon = 1.0e-3f;
static const float kThroughputCutoff = 1.0e-3f;
static const float kPi = 3.14159265358979323846f;
// A depth-first binary reflection/refraction tree needs at most one pending
// sibling per depth. Sixteen slots cover the public runtime depth cap of twelve
// without silently dropping a Whitted branch.
static const uint kMaximumPendingRays = 16;
static const uint kShadowMethodPcf = 0;
static const uint kShadowMethodPcss = 1;
static const uint kShadowMethodPhysical = 2;
static const uint kPcfSampleCount = 16;
static const uint kPcssBlockerSampleCount = 8;
static const uint kPcssFilterSampleCount = 16;
static const uint kMaximumAccumulationSamples = 4096;
static const uint kDebugViewFinal = 0;

// A deterministic low-discrepancy disk kernel keeps the result stable while the
// camera moves. The same ordered kernel is used for blocker search and filtering.
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
    uint4 samplingAndDebug; // accumulation sample, debug view, base-seed low/high
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

struct RayState
{
    float3 origin;
    float3 direction;
    float3 throughput;
    uint depth;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> gFrame;
[[vk::binding(1, 0)]] StructuredBuffer<Sphere> gSpheres;
[[vk::binding(2, 0)]] StructuredBuffer<Plane> gPlanes;
[[vk::binding(3, 0)]] StructuredBuffer<Material> gMaterials;
[[vk::binding(4, 0)]] StructuredBuffer<Light> gLights;
// The running average stays in 32-bit float so late samples are not rounded
// away once the progressive soft-shadow accumulation reaches high SPP.
[[vk::image_format("rgba32f")]]
[[vk::binding(5, 0)]] RWTexture2D<float4> gOutput;

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
    hit.materialIndex = 0;
    hit.flags = 0;
    hit.frontFace = true;

    [loop]
    for (uint sphereIndex = 0; sphereIndex < gFrame.imageAndScene.z; ++sphereIndex)
    {
        Sphere sphere = gSpheres[sphereIndex];
        float3 center = sphere.centerRadius.xyz;
        float radius = sphere.centerRadius.w;
        float3 originToCenter = ray.origin - center;
        float halfB = dot(originToCenter, ray.direction);
        float c = dot(originToCenter, originToCenter) - radius * radius;
        float discriminant = halfB * halfB - c;

        if (discriminant < 0.0f)
        {
            continue;
        }

        float squareRoot = sqrt(discriminant);
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
    for (uint planeIndex = 0; planeIndex < gFrame.imageAndScene.w; ++planeIndex)
    {
        Plane plane = gPlanes[planeIndex];
        // Plane normals are normalized once when the GPU scene is authored.
        const float3 normal = plane.normalDistance.xyz;
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

void BuildShadowBasis(float3 directionToLight, out float3 tangent, out float3 bitangent)
{
    const float3 referenceAxis = abs(directionToLight.y) < 0.999f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(referenceAxis, directionToLight));
    bitangent = cross(directionToLight, tangent);
}

float3 AreaLightSamplePosition(
    Light light,
    float3 tangent,
    float3 bitangent,
    uint sampleIndex,
    float sampleRadius)
{
    const float2 diskOffset = kShadowDiskSamples[sampleIndex] * sampleRadius;
    return light.positionRadius.xyz + tangent * diskOffset.x + bitangent * diskOffset.y;
}

uint HashUint(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float RandomFloat01(inout uint state)
{
    state = HashUint(state);
    return float(state & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float3 PhysicalLightSamplePosition(Hit receiver, Light light)
{
    const float radius = max(light.positionRadius.w, 0.0f);
    if (radius <= kRayEpsilon)
    {
        return light.positionRadius.xyz;
    }

    const float3 toCenter = light.positionRadius.xyz - receiver.position;
    const float3 directionToLight = normalize(toCenter);
    float3 tangent;
    float3 bitangent;
    BuildShadowBasis(directionToLight, tangent, bitangent);

    uint randomState = HashUint(gFrame.samplingAndDebug.x + 1u);
    if ((gFrame.samplingAndDebug.z | gFrame.samplingAndDebug.w) != 0u)
    {
        randomState ^= HashUint(gFrame.samplingAndDebug.z);
        randomState ^= HashUint(gFrame.samplingAndDebug.w + 0x9e3779b9u);
    }
    randomState ^= HashUint(asuint(receiver.position.x));
    randomState ^= HashUint(asuint(receiver.position.y) + 0x9e3779b9u);
    randomState ^= HashUint(asuint(receiver.position.z) + 0x85ebca6bu);
    randomState ^= HashUint(asuint(light.positionRadius.x) + asuint(light.positionRadius.z));
    const float radiusSample = sqrt(RandomFloat01(randomState));
    const float angle = 2.0f * kPi * RandomFloat01(randomState);
    const float2 diskOffset = radiusSample * float2(cos(angle), sin(angle)) * radius;
    return light.positionRadius.xyz + tangent * diskOffset.x + bitangent * diskOffset.y;
}

bool FindShadowBlocker(
    Hit receiver,
    float3 lightSamplePosition,
    float lightRadius,
    out float blockerDepthFromLight)
{
    blockerDepthFromLight = 0.0f;
    const float3 shadowOrigin = receiver.position + receiver.normal * kRayEpsilon;
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

    // The demo's warm area light also has visible emissive sphere geometry.
    // A hit on that geometry is the ray reaching its emitter, not an occluder.
    const Material blockerMaterial = gMaterials[blocker.materialIndex];
    const float distanceFromHitToSample = sampleDistance - blocker.t;
    const bool reachedEmitter = any(blockerMaterial.emissiveRoughness.xyz > 1.0e-5f)
        && distanceFromHitToSample <= max(lightRadius * 1.25f, 0.25f);
    if (reachedEmitter)
    {
        return false;
    }

    blockerDepthFromLight = max(distanceFromHitToSample, kRayEpsilon);
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

    const float3 directionToLight = receiverToLight / receiverDepth;
    float3 tangent;
    float3 bitangent;
    BuildShadowBasis(directionToLight, tangent, bitangent);

    const float filterRadius = max(light.positionRadius.w, 0.0f);
    if (filterRadius <= kRayEpsilon)
    {
        float ignoredBlockerDepth;
        return FindShadowBlocker(
            receiver, light.positionRadius.xyz, 0.0f, ignoredBlockerDepth) ? 0.0f : 1.0f;
    }
    float visibleSamples = 0.0f;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPcfSampleCount; ++sampleIndex)
    {
        const float3 samplePosition = AreaLightSamplePosition(
            light, tangent, bitangent, sampleIndex, filterRadius);
        float ignoredBlockerDepth;
        visibleSamples += FindShadowBlocker(receiver, samplePosition, filterRadius, ignoredBlockerDepth)
            ? 0.0f
            : 1.0f;
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

    const float3 directionToLight = receiverToLight / receiverDepth;
    float3 tangent;
    float3 bitangent;
    BuildShadowBasis(directionToLight, tangent, bitangent);

    const float lightRadius = max(light.positionRadius.w, 0.0f);
    if (lightRadius <= kRayEpsilon)
    {
        float ignoredBlockerDepth;
        return FindShadowBlocker(
            receiver, light.positionRadius.xyz, 0.0f, ignoredBlockerDepth) ? 0.0f : 1.0f;
    }
    float blockerDepthSum = 0.0f;
    uint blockerCount = 0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPcssBlockerSampleCount; ++sampleIndex)
    {
        const float3 samplePosition = AreaLightSamplePosition(
            light, tangent, bitangent, sampleIndex, lightRadius);
        float blockerDepth;
        if (FindShadowBlocker(receiver, samplePosition, lightRadius, blockerDepth))
        {
            blockerDepthSum += blockerDepth;
            ++blockerCount;
        }
    }

    if (blockerCount == 0)
    {
        return 1.0f;
    }

    // PCSS penumbra estimate: (receiverDepth - blockerDepth) / blockerDepth.
    // Here both depths are measured from the light, derived from exact ray hits.
    const float averageBlockerDepth = blockerDepthSum / float(blockerCount);
    const float penumbraRatio = max(
        (receiverDepth - averageBlockerDepth) / max(averageBlockerDepth, kRayEpsilon),
        0.0f);
    const float filterRadius = lightRadius * clamp(penumbraRatio, 0.05f, 1.0f);

    float visibleSamples = 0.0f;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPcssFilterSampleCount; ++sampleIndex)
    {
        const float3 samplePosition = AreaLightSamplePosition(
            light, tangent, bitangent, sampleIndex, filterRadius);
        float ignoredBlockerDepth;
        visibleSamples += FindShadowBlocker(receiver, samplePosition, lightRadius, ignoredBlockerDepth)
            ? 0.0f
            : 1.0f;
    }
    return visibleSamples / float(kPcssFilterSampleCount);
}

float EvaluateShadowVisibility(Hit receiver, Light light, float3 physicalSamplePosition)
{
    if (gFrame.lightAndTrace.z == kShadowMethodPcf)
    {
        return EvaluatePcfVisibility(receiver, light);
    }
    if (gFrame.lightAndTrace.z == kShadowMethodPcss)
    {
        return EvaluatePcssVisibility(receiver, light);
    }

    float ignoredBlockerDepth;
    return FindShadowBlocker(
        receiver,
        physicalSamplePosition,
        max(light.positionRadius.w, 0.0f),
        ignoredBlockerDepth) ? 0.0f : 1.0f;
}

float3 SkyColor(float3 direction)
{
    const float horizon = saturate(direction.y * 0.5f + 0.5f);
    const float3 lowerSky = float3(0.012f, 0.018f, 0.035f);
    const float3 upperSky = float3(0.26f, 0.43f, 0.72f);
    return lerp(lowerSky, upperSky, horizon * horizon);
}

float3 SurfaceBaseColor(Material material, Hit hit)
{
    float3 baseColor = material.baseColorMetallic.xyz;
    if ((hit.flags & 1u) != 0u)
    {
        const float tileSum = floor(hit.position.x * 0.75f) + floor(hit.position.z * 0.75f);
        const float checker = fmod(abs(tileSum), 2.0f);
        baseColor *= lerp(0.28f, 1.0f, checker);
    }
    return baseColor;
}

float3 FresnelSchlick(float cosine, float3 reflectanceAtNormal)
{
    return reflectanceAtNormal
        + (1.0f - reflectanceAtNormal) * pow(1.0f - saturate(cosine), 5.0f);
}

float DistributionGgx(float normalDotHalf, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominatorTerm = normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(kPi * denominatorTerm * denominatorTerm, 1.0e-6f);
}

float GeometrySchlickGgx(float normalDotDirection, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) * 0.125f;
    return normalDotDirection / max(normalDotDirection * (1.0f - k) + k, 1.0e-6f);
}

float GeometrySmith(float normalDotView, float normalDotLight, float roughness)
{
    return GeometrySchlickGgx(normalDotView, roughness)
        * GeometrySchlickGgx(normalDotLight, roughness);
}

float DielectricReflectanceAtNormal(float indexOfRefraction)
{
    float r0 = (1.0f - indexOfRefraction) / (1.0f + indexOfRefraction);
    return r0 * r0;
}

float3 EvaluateDirectLighting(Ray incomingRay, Hit hit, Material material, float3 baseColor)
{
    const float metallic = saturate(material.baseColorMetallic.w);
    const float transmission = saturate(material.transmissionIor.x);
    const float roughness = clamp(material.emissiveRoughness.w, 0.045f, 1.0f);
    const float indexOfRefraction = max(material.transmissionIor.y, 1.0001f);
    const float3 viewDirection = -incomingRay.direction;
    const float normalDotView = max(dot(hit.normal, viewDirection), 1.0e-4f);
    const float dielectricF0 = DielectricReflectanceAtNormal(indexOfRefraction);
    const float3 reflectanceAtNormal = lerp(dielectricF0.xxx, baseColor, metallic);
    float3 result = baseColor * ((1.0f - metallic) * (1.0f - transmission) * 0.018f);

    [loop]
    for (uint lightIndex = 0; lightIndex < gFrame.lightAndTrace.x; ++lightIndex)
    {
        Light light = gLights[lightIndex];
        const bool physicalSampling = gFrame.lightAndTrace.z == kShadowMethodPhysical;
        const float3 lightSamplePosition = physicalSampling
            ? PhysicalLightSamplePosition(hit, light)
            : light.positionRadius.xyz;
        const float3 toLight = lightSamplePosition - hit.position;
        const float distanceSquared = max(dot(toLight, toLight), 1.0e-4f);
        const float lightDistance = sqrt(distanceSquared);
        const float3 lightDirection = toLight / lightDistance;
        const float normalDotLight = saturate(dot(hit.normal, lightDirection));

        if (normalDotLight <= 0.0f)
        {
            continue;
        }

        const float visibility = EvaluateShadowVisibility(hit, light, lightSamplePosition);
        if (visibility <= 0.0f)
        {
            continue;
        }

        const float3 incidentRadiance = light.radiance.xyz / distanceSquared;
        const float3 halfVector = normalize(lightDirection + viewDirection);
        const float normalDotHalf = saturate(dot(hit.normal, halfVector));
        const float viewDotHalf = saturate(dot(viewDirection, halfVector));
        const float distribution = DistributionGgx(normalDotHalf, roughness);
        const float geometry = GeometrySmith(normalDotView, normalDotLight, roughness);
        const float3 fresnel = FresnelSchlick(viewDotHalf, reflectanceAtNormal);
        const float3 specular = distribution * geometry * fresnel
            / max(4.0f * normalDotView * normalDotLight, 1.0e-4f);
        const float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic) * (1.0f - transmission);
        const float3 diffuse = diffuseWeight * baseColor * (1.0f / kPi);
        result += (diffuse + specular) * incidentRadiance * normalDotLight * visibility;
    }

    return result;
}

float SchlickReflectance(float cosine, float indexOfRefraction)
{
    float r0 = (1.0f - indexOfRefraction) / (1.0f + indexOfRefraction);
    r0 *= r0;
    return r0 + (1.0f - r0) * pow(1.0f - saturate(cosine), 5.0f);
}

float3 OffsetRayOrigin(float3 position, float3 orientedNormal, float3 direction)
{
    const float side = dot(direction, orientedNormal) >= 0.0f ? 1.0f : -1.0f;
    return position + orientedNormal * (side * kRayEpsilon);
}

float3 TraceWhitted(Ray primaryRay)
{
    RayState pending[kMaximumPendingRays];
    uint pendingCount = 1;
    pending[0].origin = primaryRay.origin;
    pending[0].direction = primaryRay.direction;
    pending[0].throughput = 1.0f;
    pending[0].depth = 0;

    float3 result = 0.0f;
    const uint maximumDepth = clamp(gFrame.lightAndTrace.y, 1u, 12u);

    [loop]
    while (pendingCount > 0)
    {
        RayState state = pending[--pendingCount];
        const float maximumThroughput = max(state.throughput.x, max(state.throughput.y, state.throughput.z));
        if (maximumThroughput < kThroughputCutoff)
        {
            continue;
        }

        Ray ray;
        ray.origin = state.origin;
        ray.direction = normalize(state.direction);

        Hit hit;
        if (!TraceClosest(ray, kRayEpsilon, 1.0e30f, hit))
        {
            result += state.throughput * SkyColor(ray.direction);
            continue;
        }

        Material material = gMaterials[hit.materialIndex];
        const float3 baseColor = SurfaceBaseColor(material, hit);
        const float metallic = saturate(material.baseColorMetallic.w);
        const float transmission = saturate(material.transmissionIor.x);
        const float indexOfRefraction = max(material.transmissionIor.y, 1.0001f);

        // Apply Beer-Lambert attenuation when a transmitted ray reaches the
        // inside-to-outside surface. hit.t is the distance travelled in medium.
        float3 pathThroughput = state.throughput;
        const float attenuationDistance = material.attenuationColorDistance.w;
        if (!hit.frontFace && transmission > 0.0f && attenuationDistance > kRayEpsilon)
        {
            const float3 attenuationColor = clamp(
                material.attenuationColorDistance.xyz, 1.0e-3f, 1.0f);
            pathThroughput *= pow(attenuationColor, hit.t / attenuationDistance);
        }

        result += pathThroughput * material.emissiveRoughness.xyz;
        result += pathThroughput * EvaluateDirectLighting(ray, hit, material, baseColor);

        if (state.depth + 1u >= maximumDepth)
        {
            continue;
        }

        const float cosine = saturate(dot(-ray.direction, hit.normal));
        const float dielectricF0 = DielectricReflectanceAtNormal(indexOfRefraction);
        const float3 reflectanceAtNormal = lerp(dielectricF0.xxx, baseColor, metallic);
        float3 reflectionWeight = FresnelSchlick(cosine, reflectanceAtNormal);
        float3 refractionWeight = 0.0f;
        float3 refractionDirection = 0.0f;

        if (transmission > 0.0f)
        {
            const float eta = hit.frontFace ? (1.0f / indexOfRefraction) : indexOfRefraction;
            refractionDirection = refract(ray.direction, hit.normal, eta);
            const bool totalInternalReflection = dot(refractionDirection, refractionDirection) < 1.0e-8f;

            if (totalInternalReflection)
            {
                reflectionWeight = 1.0f;
            }
            else
            {
                const float fresnel = SchlickReflectance(cosine, indexOfRefraction);
                refractionWeight = transmission * (1.0f - fresnel);
            }
        }

        const float maximumRefraction = max(
            refractionWeight.x, max(refractionWeight.y, refractionWeight.z));
        if (maximumRefraction > kThroughputCutoff && pendingCount < kMaximumPendingRays)
        {
            RayState refracted;
            refracted.direction = normalize(refractionDirection);
            refracted.origin = OffsetRayOrigin(hit.position, hit.normal, refracted.direction);
            refracted.throughput = pathThroughput * refractionWeight;
            refracted.depth = state.depth + 1u;
            pending[pendingCount++] = refracted;
        }

        const float maximumReflection = max(
            reflectionWeight.x, max(reflectionWeight.y, reflectionWeight.z));
        if (maximumReflection > kThroughputCutoff && pendingCount < kMaximumPendingRays)
        {
            RayState reflected;
            reflected.direction = normalize(reflect(ray.direction, hit.normal));
            reflected.origin = OffsetRayOrigin(hit.position, hit.normal, reflected.direction);
            reflected.throughput = pathThroughput * reflectionWeight;
            reflected.depth = state.depth + 1u;
            pending[pendingCount++] = reflected;
        }
    }

    return max(result, 0.0f);
}

float RadicalInverse(uint index, uint base)
{
    const float inverseBase = 1.0f / float(base);
    float inversePower = inverseBase;
    float result = 0.0f;
    [loop]
    while (index > 0u)
    {
        const uint digit = index % base;
        result += float(digit) * inversePower;
        index /= base;
        inversePower *= inverseBase;
    }
    return result;
}

float2 SubpixelSample(uint sampleIndex)
{
    float2 sample;
    if (sampleIndex == 0u)
    {
        sample = 0.5f;
    }
    else
    {
        const uint sequenceIndex = sampleIndex + 1u;
        sample = float2(
            RadicalInverse(sequenceIndex, 2u),
            RadicalInverse(sequenceIndex, 3u));
    }

    if ((gFrame.samplingAndDebug.z | gFrame.samplingAndDebug.w) != 0u)
    {
        const float2 seedRotation = float2(
            float(HashUint(gFrame.samplingAndDebug.z) & 0x00ffffffu),
            float(HashUint(gFrame.samplingAndDebug.w + 0x9e3779b9u) & 0x00ffffffu))
            * (1.0f / 16777216.0f);
        sample = frac(sample + seedRotation);
    }
    return sample;
}

float3 EvaluateDebugView(Ray primaryRay, uint debugView)
{
    Hit hit;
    if (!TraceClosest(primaryRay, kRayEpsilon, 1.0e30f, hit))
    {
        return SkyColor(primaryRay.direction);
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
        const float3 emission = max(material.emissiveRoughness.xyz, 0.0f);
        return emission / (1.0f + emission);
    }
    return 0.0f;
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
    if (debugView == kDebugViewFinal && sampleIndex >= kMaximumAccumulationSamples)
    {
        return;
    }

    const float2 subpixel = debugView == kDebugViewFinal ? SubpixelSample(sampleIndex) : 0.5f;
    float2 screen = (float2(pixel) + subpixel) / float2(imageSize);
    screen = screen * 2.0f - 1.0f;
    screen.y = -screen.y;

    const float tanHalfFov = gFrame.cameraPositionTanHalfFov.w;
    const float aspect = gFrame.cameraForwardAspect.w;

    Ray primaryRay;
    primaryRay.origin = gFrame.cameraPositionTanHalfFov.xyz;
    primaryRay.direction = normalize(
        gFrame.cameraForwardAspect.xyz
        + gFrame.cameraRightTime.xyz * (screen.x * aspect * tanHalfFov)
        + gFrame.cameraUpExposure.xyz * (screen.y * tanHalfFov));

    if (debugView != kDebugViewFinal)
    {
        gOutput[pixel] = float4(EvaluateDebugView(primaryRay, debugView), 1.0f);
        return;
    }

    const float3 sampleColor = TraceWhitted(primaryRay);
    const float3 accumulatedColor = sampleIndex == 0u
        ? sampleColor
        : gOutput[pixel].rgb + (sampleColor - gOutput[pixel].rgb) / float(sampleIndex + 1u);
    gOutput[pixel] = float4(accumulatedColor, 1.0f);
}
