#ifndef RENDERING_ENGINE_PBR_LIGHT_SAMPLING_HLSLI
#define RENDERING_ENGINE_PBR_LIGHT_SAMPLING_HLSLI

#include "pbr_fixture_traversal.hlsli"

[[vk::binding(2, 0)]] StructuredBuffer<PbrLightGpuL6> gPbrLightsL6;
[[vk::binding(3, 0)]] StructuredBuffer<PbrAliasEntryGpuL6> gPbrLightAliasL6;
[[vk::binding(4, 0)]] StructuredBuffer<PbrAliasEntryGpuL6> gPbrEnvironmentRowAliasL6;
[[vk::binding(5, 0)]] StructuredBuffer<PbrAliasEntryGpuL6> gPbrEnvironmentColumnAliasL6;
[[vk::binding(6, 0)]] StructuredBuffer<float> gPbrLightSelectionPmfL6;
[[vk::binding(10, 0)]] Texture2D<float4> gPbrEnvironmentTextureL6;
[[vk::binding(11, 0)]] SamplerState gPbrEnvironmentSamplerL6;

PbrLightSampleL6 PbrInvalidLightSampleL6()
{
    PbrLightSampleL6 sample;
    sample.wi = 0.0f;
    sample.distance = 0.0f;
    sample.Li = 0.0f;
    sample.selectionPmf = 0.0f;
    sample.conditionalPdf = 0.0f;
    sample.combinedPdfW = 0.0f;
    sample.lightIndex = PBR_L6_INVALID_INDEX;
    sample.primitiveId = PBR_L6_INVALID_INDEX;
    sample.position = 0.0f;
    sample.measure = PBR_L6_MEASURE_SOLID_ANGLE;
    sample.normal = 0.0f;
    sample.flags = 0u;
    return sample;
}

void PbrBuildBasisL6(float3 normal, out float3 tangent, out float3 bitangent)
{
    const float signValue = normal.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (signValue + normal.z);
    const float b = normal.x * normal.y * a;
    tangent = float3(1.0f + signValue * normal.x * normal.x * a, signValue * b, -signValue * normal.x);
    bitangent = float3(b, signValue + normal.y * normal.y * a, -normal.y);
}

float3 PbrLightRadianceScaleL6(PbrLightGpuL6 light)
{
    return light.radianceScale.xyz * light.radianceScale.w;
}

PbrAliasEntryGpuL6 PbrSampleAliasL6(
    StructuredBuffer<PbrAliasEntryGpuL6> table,
    uint offset,
    uint count,
    float uniformSample)
{
    PbrAliasEntryGpuL6 invalidEntry;
    invalidEntry.q = 0.0f;
    invalidEntry.pmf = 0.0f;
    invalidEntry.alias = 0u;
    invalidEntry.item = PBR_L6_INVALID_INDEX;
    if (count == 0u)
    {
        return invalidEntry;
    }
    const float scaled = uniformSample * float(count);
    const uint bin = min(uint(scaled), count - 1u);
    const float withinBin = scaled - float(bin);
    const PbrAliasEntryGpuL6 entry = table[offset + bin];
    const uint selectedBin = withinBin < entry.q ? bin : entry.alias;
    if (selectedBin >= count)
    {
        return invalidEntry;
    }
    return table[offset + selectedBin];
}

float PbrEnvironmentTexelSolidAngleL6(uint row)
{
    const uint width = gPbrFrameL6.environment.y;
    const uint height = gPbrFrameL6.environment.z;
    if (width == 0u || height == 0u || row >= height)
    {
        return 0.0f;
    }
    const float theta0 = PBR_L6_PI * float(row) / float(height);
    const float theta1 = PBR_L6_PI * float(row + 1u) / float(height);
    return (PBR_L6_TWO_PI / float(width)) * (cos(theta0) - cos(theta1));
}

float2 PbrEnvironmentUvFromLocalDirectionL6(float3 localDirection)
{
    const float3 direction = normalize(localDirection);
    float phi = atan2(direction.z, direction.x);
    if (phi < 0.0f)
    {
        phi += PBR_L6_TWO_PI;
    }
    const float theta = acos(clamp(direction.y, -1.0f, 1.0f));
    return float2(phi / PBR_L6_TWO_PI, theta / PBR_L6_PI);
}

float3 PbrEnvironmentRadianceL6(float3 worldDirection)
{
    const uint lightIndex = gPbrFrameL6.environment.x;
    if (lightIndex == PBR_L6_INVALID_INDEX || lightIndex >= gPbrFrameL6.trace.w)
    {
        return 0.0f;
    }
    const PbrLightGpuL6 light = gPbrLightsL6[lightIndex];
    if (light.identity.x != PBR_L6_LIGHT_ENVIRONMENT
        || (light.identity.y & PBR_L6_LIGHT_ENABLED) == 0u)
    {
        return 0.0f;
    }
    const float3 localDirection = normalize(PbrTransformDirectionRowsL6(
        worldDirection,
        gPbrFrameL6.worldToEnvironment0,
        gPbrFrameL6.worldToEnvironment1,
        gPbrFrameL6.worldToEnvironment2));
    const float2 uv = PbrEnvironmentUvFromLocalDirectionL6(localDirection);
    return gPbrEnvironmentTextureL6.SampleLevel(gPbrEnvironmentSamplerL6, uv, 0.0f).rgb
        * PbrLightRadianceScaleL6(light);
}

float PbrEnvironmentPdfL6(float3 worldDirection)
{
    const uint width = gPbrFrameL6.environment.y;
    const uint height = gPbrFrameL6.environment.z;
    if (width == 0u || height == 0u
        || gPbrFrameL6.distribution.y != height
        || gPbrFrameL6.distribution.z != width * height)
    {
        return 0.0f;
    }
    const float3 localDirection = normalize(PbrTransformDirectionRowsL6(
        worldDirection,
        gPbrFrameL6.worldToEnvironment0,
        gPbrFrameL6.worldToEnvironment1,
        gPbrFrameL6.worldToEnvironment2));
    const float2 uv = PbrEnvironmentUvFromLocalDirectionL6(localDirection);
    const uint row = min(uint(uv.y * float(height)), height - 1u);
    const uint column = min(uint(uv.x * float(width)), width - 1u);
    const float texelPmf = gPbrEnvironmentRowAliasL6[row].pmf
        * gPbrEnvironmentColumnAliasL6[row * width + column].pmf;
    const float solidAngle = PbrEnvironmentTexelSolidAngleL6(row);
    return solidAngle > 0.0f ? texelPmf / solidAngle : 0.0f;
}

PbrLightSampleL6 PbrSamplePointOrSpotLightL6(
    PbrLightContextL6 context,
    PbrLightGpuL6 light,
    uint lightIndex,
    float selectionPmf)
{
    PbrLightSampleL6 sample = PbrInvalidLightSampleL6();
    const float3 toLight = light.positionRange.xyz - context.position;
    const float distanceSquared = dot(toLight, toLight);
    if (!(distanceSquared > 0.0f) || !(selectionPmf > 0.0f))
    {
        return sample;
    }
    const float distance = sqrt(distanceSquared);
    if (light.positionRange.w > 0.0f && distance > light.positionRange.w)
    {
        return sample;
    }
    const float3 wi = toLight / distance;
    float falloff = 1.0f;
    if (light.identity.x == PBR_L6_LIGHT_SPOT)
    {
        const float directionLengthSquared = dot(
            light.directionCosOuter.xyz, light.directionCosOuter.xyz);
        if (!(directionLengthSquared > 0.0f) ||
            !PbrIsFiniteFloatL6(directionLengthSquared))
        {
            return sample;
        }
        const float3 forward = light.directionCosOuter.xyz /
            sqrt(directionLengthSquared);
        const float cosAngle = dot(forward, -wi);
        const float cosOuter = light.directionCosOuter.w;
        const float cosInner = light.shapeParams.y;
        falloff = cosInner == cosOuter
            ? (cosAngle >= cosOuter ? 1.0f : 0.0f)
            : smoothstep(cosOuter, cosInner, cosAngle);
        if (!(falloff > 0.0f))
        {
            return sample;
        }
    }
    sample.wi = wi;
    sample.distance = distance;
    sample.Li = PbrLightRadianceScaleL6(light) * (falloff / distanceSquared);
    sample.selectionPmf = selectionPmf;
    sample.conditionalPdf = 1.0f;
    sample.combinedPdfW = 0.0f;
    sample.lightIndex = lightIndex;
    sample.primitiveId = PBR_L6_INVALID_INDEX;
    sample.position = light.positionRange.xyz;
    sample.measure = PBR_L6_MEASURE_DISCRETE;
    sample.flags = PBR_L6_SAMPLE_VALID | PBR_L6_SAMPLE_DELTA;
    return sample;
}

PbrLightSampleL6 PbrSampleDirectionalLightL6(
    PbrLightGpuL6 light,
    uint lightIndex,
    float selectionPmf)
{
    PbrLightSampleL6 sample = PbrInvalidLightSampleL6();
    const float directionLengthSquared = dot(light.directionCosOuter.xyz, light.directionCosOuter.xyz);
    if (!(directionLengthSquared > 0.0f) || !(selectionPmf > 0.0f))
    {
        return sample;
    }
    sample.wi = -light.directionCosOuter.xyz * rsqrt(directionLengthSquared);
    sample.distance = 1.0e30f;
    sample.Li = PbrLightRadianceScaleL6(light);
    sample.selectionPmf = selectionPmf;
    sample.conditionalPdf = 1.0f;
    sample.combinedPdfW = 0.0f;
    sample.lightIndex = lightIndex;
    sample.primitiveId = PBR_L6_INVALID_INDEX;
    sample.measure = PBR_L6_MEASURE_DISCRETE;
    sample.flags = PBR_L6_SAMPLE_VALID | PBR_L6_SAMPLE_DELTA | PBR_L6_SAMPLE_INFINITE;
    return sample;
}

PbrLightSampleL6 PbrSampleTriangleLightL6(
    PbrLightContextL6 context,
    PbrLightGpuL6 light,
    uint lightIndex,
    float selectionPmf,
    float2 randomSample)
{
    PbrLightSampleL6 sample = PbrInvalidLightSampleL6();
    if (light.payload.x >= gPbrFrameL6.trace.x || !(selectionPmf > 0.0f))
    {
        return sample;
    }
    const PbrFixtureTriangleGpuL6 fixtureTriangle = gPbrFixtureTrianglesL6[light.payload.x];
    const float3 edge1 = fixtureTriangle.p1.xyz - fixtureTriangle.p0.xyz;
    const float3 edge2 = fixtureTriangle.p2.xyz - fixtureTriangle.p0.xyz;
    const float3 normalUnnormalized = cross(edge1, edge2);
    const float twiceArea = length(normalUnnormalized);
    if (!(twiceArea > 0.0f))
    {
        return sample;
    }
    const float root = sqrt(randomSample.x);
    const float barycentric0 = 1.0f - root;
    const float barycentric1 = root * (1.0f - randomSample.y);
    const float barycentric2 = root * randomSample.y;
    const float3 lightPosition = barycentric0 * fixtureTriangle.p0.xyz
        + barycentric1 * fixtureTriangle.p1.xyz
        + barycentric2 * fixtureTriangle.p2.xyz;
    const float3 toLight = lightPosition - context.position;
    const float distanceSquared = dot(toLight, toLight);
    if (!(distanceSquared > 0.0f))
    {
        return sample;
    }
    const float distance = sqrt(distanceSquared);
    const float3 wi = toLight / distance;
    const float3 lightNormal = normalUnnormalized / twiceArea;
    const float lightCosine = dot(lightNormal, -wi);
    const bool twoSided = (light.identity.y & PBR_L6_LIGHT_TWO_SIDED) != 0u;
    const float absoluteCosine = twoSided ? abs(lightCosine) : lightCosine;
    if (!(absoluteCosine > 0.0f))
    {
        return sample;
    }
    const float area = 0.5f * twiceArea;
    const float pdfArea = rcp(area);
    const float pdfSolidAngle = pdfArea * distanceSquared / absoluteCosine;
    sample.wi = wi;
    sample.distance = distance;
    sample.Li = PbrLightRadianceScaleL6(light);
    sample.selectionPmf = selectionPmf;
    sample.conditionalPdf = pdfArea;
    sample.combinedPdfW = selectionPmf * pdfSolidAngle;
    sample.lightIndex = lightIndex;
    sample.primitiveId = fixtureTriangle.metadata.y;
    sample.position = lightPosition;
    sample.measure = PBR_L6_MEASURE_AREA;
    sample.normal = lightNormal;
    sample.flags = PBR_L6_SAMPLE_VALID | (twoSided ? PBR_L6_SAMPLE_TWO_SIDED : 0u);
    return sample;
}

PbrLightSampleL6 PbrSampleSphereLightL6(
    PbrLightContextL6 context,
    PbrLightGpuL6 light,
    uint lightIndex,
    float selectionPmf,
    float2 randomSample)
{
    PbrLightSampleL6 sample = PbrInvalidLightSampleL6();
    const float radius = light.shapeParams.x;
    const float3 center = light.positionRange.xyz;
    if (!(radius > 0.0f) || !(selectionPmf > 0.0f))
    {
        return sample;
    }
    const float3 toCenter = center - context.position;
    const float distanceSquared = dot(toCenter, toCenter);
    const float radiusSquared = radius * radius;
    const bool twoSided = (light.identity.y & PBR_L6_LIGHT_TWO_SIDED) != 0u;

    float3 wi;
    float3 lightPosition;
    float3 lightNormal;
    float distance;
    float conditionalPdf;
    uint measure;
    if (distanceSquared > radiusSquared * 1.000001f)
    {
        const float centerDistance = sqrt(distanceSquared);
        const float3 centerDirection = toCenter / centerDistance;
        const float sinThetaMaximumSquared = radiusSquared / distanceSquared;
        const float cosThetaMaximum = sqrt(max(0.0f, 1.0f - sinThetaMaximumSquared));
        const float oneMinusCosThetaMaximum = sinThetaMaximumSquared < 0.00068523f
            ? 0.5f * sinThetaMaximumSquared
            : 1.0f - cosThetaMaximum;
        const float cosTheta = 1.0f - randomSample.x * oneMinusCosThetaMaximum;
        const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
        const float phi = PBR_L6_TWO_PI * randomSample.y;
        float3 tangent;
        float3 bitangent;
        PbrBuildBasisL6(centerDirection, tangent, bitangent);
        wi = normalize(centerDirection * cosTheta
            + tangent * (cos(phi) * sinTheta)
            + bitangent * (sin(phi) * sinTheta));

        const float3 originToCenter = context.position - center;
        const float halfB = dot(originToCenter, wi);
        const float discriminant = halfB * halfB - (dot(originToCenter, originToCenter) - radiusSquared);
        if (!(discriminant >= 0.0f))
        {
            return sample;
        }
        distance = -halfB - sqrt(discriminant);
        if (!(distance > 0.0f))
        {
            distance = -halfB + sqrt(discriminant);
        }
        if (!(distance > 0.0f) || !(oneMinusCosThetaMaximum > 0.0f))
        {
            return sample;
        }
        lightPosition = context.position + wi * distance;
        lightNormal = normalize(lightPosition - center);
        conditionalPdf = rcp(PBR_L6_TWO_PI * oneMinusCosThetaMaximum);
        measure = PBR_L6_MEASURE_SOLID_ANGLE;
    }
    else
    {
        const float z = 1.0f - 2.0f * randomSample.x;
        const float radiusOnDisk = sqrt(max(0.0f, 1.0f - z * z));
        const float phi = PBR_L6_TWO_PI * randomSample.y;
        lightNormal = float3(radiusOnDisk * cos(phi), z, radiusOnDisk * sin(phi));
        lightPosition = center + radius * lightNormal;
        const float3 toLight = lightPosition - context.position;
        const float sampledDistanceSquared = dot(toLight, toLight);
        if (!(sampledDistanceSquared > 0.0f))
        {
            return sample;
        }
        distance = sqrt(sampledDistanceSquared);
        wi = toLight / distance;
        const float lightCosine = dot(lightNormal, -wi);
        const float absoluteCosine = twoSided ? abs(lightCosine) : lightCosine;
        if (!(absoluteCosine > 0.0f))
        {
            return sample;
        }
        conditionalPdf = rcp(4.0f * PBR_L6_PI * radiusSquared);
        const float pdfSolidAngle = conditionalPdf * sampledDistanceSquared / absoluteCosine;
        sample.combinedPdfW = selectionPmf * pdfSolidAngle;
        measure = PBR_L6_MEASURE_AREA;
    }

    sample.wi = wi;
    sample.distance = distance;
    sample.Li = PbrLightRadianceScaleL6(light);
    sample.selectionPmf = selectionPmf;
    sample.conditionalPdf = conditionalPdf;
    if (measure == PBR_L6_MEASURE_SOLID_ANGLE)
    {
        sample.combinedPdfW = selectionPmf * conditionalPdf;
    }
    sample.lightIndex = lightIndex;
    sample.primitiveId = light.identity.w;
    sample.position = lightPosition;
    sample.measure = measure;
    sample.normal = lightNormal;
    sample.flags = PBR_L6_SAMPLE_VALID | (twoSided ? PBR_L6_SAMPLE_TWO_SIDED : 0u);
    return sample;
}

PbrLightSampleL6 PbrSampleEnvironmentLightL6(
    PbrLightGpuL6 light,
    uint lightIndex,
    float selectionPmf,
    float4 randomSample)
{
    PbrLightSampleL6 sample = PbrInvalidLightSampleL6();
    const uint width = gPbrFrameL6.environment.y;
    const uint height = gPbrFrameL6.environment.z;
    if (!(selectionPmf > 0.0f) || width == 0u || height == 0u
        || gPbrFrameL6.distribution.y != height
        || gPbrFrameL6.distribution.z != width * height)
    {
        return sample;
    }
    const PbrAliasEntryGpuL6 rowEntry = PbrSampleAliasL6(
        gPbrEnvironmentRowAliasL6,
        0u,
        height,
        randomSample.x);
    if (rowEntry.item >= height || !(rowEntry.pmf > 0.0f))
    {
        return sample;
    }
    const PbrAliasEntryGpuL6 columnEntry = PbrSampleAliasL6(
        gPbrEnvironmentColumnAliasL6,
        rowEntry.item * width,
        width,
        randomSample.y);
    if (columnEntry.item >= width || !(columnEntry.pmf > 0.0f))
    {
        return sample;
    }
    const float theta0 = PBR_L6_PI * float(rowEntry.item) / float(height);
    const float theta1 = PBR_L6_PI * float(rowEntry.item + 1u) / float(height);
    const float cosTheta = lerp(cos(theta0), cos(theta1), randomSample.w);
    const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = PBR_L6_TWO_PI
        * (float(columnEntry.item) + randomSample.z)
        / float(width);
    const float3 localDirection = float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
    const float3 worldDirection = normalize(PbrTransformDirectionRowsL6(
        localDirection,
        gPbrFrameL6.environmentToWorld0,
        gPbrFrameL6.environmentToWorld1,
        gPbrFrameL6.environmentToWorld2));
    const float solidAngle = PbrEnvironmentTexelSolidAngleL6(rowEntry.item);
    if (!(solidAngle > 0.0f))
    {
        return sample;
    }
    const float2 uv = float2(
        (float(columnEntry.item) + randomSample.z) / float(width),
        acos(clamp(cosTheta, -1.0f, 1.0f)) / PBR_L6_PI);
    const float pdfSolidAngle = rowEntry.pmf * columnEntry.pmf / solidAngle;
    sample.wi = worldDirection;
    sample.distance = 1.0e30f;
    sample.Li = gPbrEnvironmentTextureL6.SampleLevel(
        gPbrEnvironmentSamplerL6,
        uv,
        0.0f).rgb * PbrLightRadianceScaleL6(light);
    sample.selectionPmf = selectionPmf;
    sample.conditionalPdf = pdfSolidAngle;
    sample.combinedPdfW = selectionPmf * pdfSolidAngle;
    sample.lightIndex = lightIndex;
    sample.primitiveId = PBR_L6_INVALID_INDEX;
    sample.measure = PBR_L6_MEASURE_SOLID_ANGLE;
    sample.flags = PBR_L6_SAMPLE_VALID | PBR_L6_SAMPLE_INFINITE;
    return sample;
}

PbrLightSampleL6 PbrSampleOneLightL6(PbrLightContextL6 context, PbrLightRandomL6 randomSample)
{
    PbrLightSampleL6 invalidSample = PbrInvalidLightSampleL6();
    if (gPbrFrameL6.distribution.x == 0u)
    {
        return invalidSample;
    }
    const PbrAliasEntryGpuL6 selected = PbrSampleAliasL6(
        gPbrLightAliasL6,
        0u,
        gPbrFrameL6.distribution.x,
        randomSample.selection);
    if (selected.item >= gPbrFrameL6.trace.w || !(selected.pmf > 0.0f))
    {
        return invalidSample;
    }
    const PbrLightGpuL6 light = gPbrLightsL6[selected.item];
    if ((light.identity.y & PBR_L6_LIGHT_ENABLED) == 0u)
    {
        return invalidSample;
    }
    if (light.identity.x == PBR_L6_LIGHT_POINT || light.identity.x == PBR_L6_LIGHT_SPOT)
    {
        return PbrSamplePointOrSpotLightL6(context, light, selected.item, selected.pmf);
    }
    if (light.identity.x == PBR_L6_LIGHT_DIRECTIONAL)
    {
        return PbrSampleDirectionalLightL6(light, selected.item, selected.pmf);
    }
    if (light.identity.x == PBR_L6_LIGHT_SPHERE_AREA)
    {
        return PbrSampleSphereLightL6(
            context,
            light,
            selected.item,
            selected.pmf,
            float2(randomSample.shape0, randomSample.shape1));
    }
    if (light.identity.x == PBR_L6_LIGHT_EMISSIVE_TRIANGLE)
    {
        return PbrSampleTriangleLightL6(
            context,
            light,
            selected.item,
            selected.pmf,
            float2(randomSample.shape0, randomSample.shape1));
    }
    if (light.identity.x == PBR_L6_LIGHT_ENVIRONMENT)
    {
        return PbrSampleEnvironmentLightL6(
            light,
            selected.item,
            selected.pmf,
            float4(
                randomSample.shape0,
                randomSample.shape1,
                randomSample.shape2,
                randomSample.shape3));
    }
    return invalidSample;
}

float PbrLightPdfLiL6(
    uint lightIndex,
    PbrLightContextL6 context,
    float3 wi,
    float distanceToEmitter,
    float3 emitterNormal)
{
    if (lightIndex == PBR_L6_INVALID_INDEX || lightIndex >= gPbrFrameL6.trace.w)
    {
        return 0.0f;
    }
    const PbrLightGpuL6 light = gPbrLightsL6[lightIndex];
    if ((light.identity.y & PBR_L6_LIGHT_ENABLED) == 0u)
    {
        return 0.0f;
    }
    if (light.identity.x == PBR_L6_LIGHT_POINT
        || light.identity.x == PBR_L6_LIGHT_DIRECTIONAL
        || light.identity.x == PBR_L6_LIGHT_SPOT)
    {
        return 0.0f;
    }
    if (light.identity.x == PBR_L6_LIGHT_ENVIRONMENT)
    {
        return PbrEnvironmentPdfL6(wi);
    }
    if (light.identity.x == PBR_L6_LIGHT_EMISSIVE_TRIANGLE)
    {
        if (light.payload.x >= gPbrFrameL6.trace.x || !(distanceToEmitter > 0.0f))
        {
            return 0.0f;
        }
        const PbrFixtureTriangleGpuL6 fixtureTriangle = gPbrFixtureTrianglesL6[light.payload.x];
        const float area = 0.5f * length(cross(
            fixtureTriangle.p1.xyz - fixtureTriangle.p0.xyz,
            fixtureTriangle.p2.xyz - fixtureTriangle.p0.xyz));
        const float lightCosine = dot(emitterNormal, -wi);
        const float cosine = (light.identity.y & PBR_L6_LIGHT_TWO_SIDED) != 0u
            ? abs(lightCosine)
            : lightCosine;
        return area > 0.0f && cosine > 0.0f
            ? distanceToEmitter * distanceToEmitter / (area * cosine)
            : 0.0f;
    }
    if (light.identity.x == PBR_L6_LIGHT_SPHERE_AREA)
    {
        const float radius = light.shapeParams.x;
        const float3 toCenter = light.positionRange.xyz - context.position;
        const float distanceSquared = dot(toCenter, toCenter);
        const float radiusSquared = radius * radius;
        if (!(radius > 0.0f))
        {
            return 0.0f;
        }
        if (distanceSquared > radiusSquared * 1.000001f)
        {
            const float sinThetaMaximumSquared = radiusSquared / distanceSquared;
            const float cosThetaMaximum = sqrt(max(0.0f, 1.0f - sinThetaMaximumSquared));
            const float oneMinusCosThetaMaximum = sinThetaMaximumSquared < 0.00068523f
                ? 0.5f * sinThetaMaximumSquared
                : 1.0f - cosThetaMaximum;
            return oneMinusCosThetaMaximum > 0.0f
                ? rcp(PBR_L6_TWO_PI * oneMinusCosThetaMaximum)
                : 0.0f;
        }
        const float lightCosine = dot(emitterNormal, -wi);
        const float cosine = (light.identity.y & PBR_L6_LIGHT_TWO_SIDED) != 0u
            ? abs(lightCosine)
            : lightCosine;
        const float area = 4.0f * PBR_L6_PI * radiusSquared;
        return distanceToEmitter > 0.0f && cosine > 0.0f
            ? distanceToEmitter * distanceToEmitter / (area * cosine)
            : 0.0f;
    }
    return 0.0f;
}

float PbrSelectedLightPdfL6(
    uint lightIndex,
    PbrLightContextL6 context,
    float3 wi,
    float distanceToEmitter,
    float3 emitterNormal)
{
    if (lightIndex == PBR_L6_INVALID_INDEX || lightIndex >= gPbrFrameL6.trace.w)
    {
        return 0.0f;
    }
    return gPbrLightSelectionPmfL6[lightIndex]
        * PbrLightPdfLiL6(lightIndex, context, wi, distanceToEmitter, emitterNormal);
}

float3 PbrOffsetRayOriginL6(float3 position, float3 geometricNormal, float3 direction)
{
    const float side = dot(geometricNormal, direction) >= 0.0f ? 1.0f : -1.0f;
    return position + geometricNormal * (side * gPbrFrameL6.russianRoulette.w);
}

#endif
