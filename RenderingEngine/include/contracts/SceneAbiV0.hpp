#pragma once

#include "contracts/AbiTypesV0.hpp"
#include "contracts/AbiVersion.hpp"

namespace RenderingEngine::Contracts::AbiV0
{
    enum SceneFlags : std::uint32_t
    {
        SceneFlagNone = 0u,
        SceneFlagHasEnvironment = 1u << 0u,
        SceneFlagHasAlphaMask = 1u << 1u,
        SceneFlagHasAnimatedRigidInstances = 1u << 2u
    };

    enum GeometryFlags : std::uint32_t
    {
        GeometryFlagNone = 0u,
        GeometryFlagOpaque = 1u << 0u,
        GeometryFlagAlphaMask = 1u << 1u,
        GeometryFlagDoubleSided = 1u << 2u
    };

    enum InstanceFlags : std::uint32_t
    {
        InstanceFlagNone = 0u,
        InstanceFlagVisible = 1u << 0u,
        InstanceFlagCastsShadow = 1u << 1u,
        InstanceFlagAnimatedRigid = 1u << 2u
    };

    enum MaterialModel : std::uint32_t
    {
        MaterialModelMetallicRoughness = 0u,
        MaterialModelSmoothDielectric = 1u
    };

    enum MaterialFlags : std::uint32_t
    {
        MaterialFlagNone = 0u,
        MaterialFlagDoubleSided = 1u << 0u,
        MaterialFlagAlphaMask = 1u << 1u,
        MaterialFlagEmissive = 1u << 2u,
        MaterialFlagThinWalled = 1u << 3u
    };

    enum LightType : std::uint32_t
    {
        LightTypePoint = 0u,
        LightTypeDirectional = 1u,
        LightTypeSpot = 2u,
        LightTypeSphereArea = 3u,
        LightTypeEmissiveTriangle = 4u,
        LightTypeEnvironment = 5u
    };

    enum LightFlags : std::uint32_t
    {
        LightFlagNone = 0u,
        LightFlagEnabled = 1u << 0u,
        LightFlagTwoSided = 1u << 1u,
        LightFlagAnimated = 1u << 2u,
        LightFlagDelta = 1u << 3u
    };

    struct alignas(16) GpuSceneConstantsV0
    {
        AbiUInt4 counts0;      // vertices, indices, geometries, instances.
        AbiUInt4 counts1;      // materials, lights, textures, samplers.
        AbiFloat4 sceneBoundsMin;
        AbiFloat4 sceneBoundsMax;
        AbiUInt4 versionFlags; // ABI version, scene generation, SceneFlags, reserved.
        AbiUInt4 environment;  // texture index, width, height, reserved.
    };

    struct alignas(16) GpuVertexV0
    {
        AbiFloat4 position;  // xyz in object-space meters; w = 1.
        AbiFloat4 normal;    // xyz object-space unit normal; w = 0.
        AbiFloat4 tangent;   // xyz object-space tangent; w = handedness.
        AbiFloat4 texcoord0; // xy = UV0; zw = 0.
    };

    struct alignas(16) GpuGeometryV0
    {
        AbiUInt4 indexRange; // first index, count, non-negative vertex offset, first primitive ID.
        AbiUInt4 identity;   // geometry ID, mesh ID, material ID, GeometryFlags.
        AbiFloat4 localBoundsMin;
        AbiFloat4 localBoundsMax;
    };

    struct alignas(16) GpuInstanceV0
    {
        AbiMat4Rows objectToWorld;
        AbiMat4Rows worldToObject;
        AbiMat4Rows previousObjectToWorld;
        AbiUInt4 metadata; // first geometry, geometry count, stable instance ID, InstanceFlags.
        AbiUInt4 reserved0;
    };

    struct alignas(16) GpuMaterialV0
    {
        AbiFloat4 baseColorFactor;
        AbiFloat4 emissiveFactorStrength;   // rgb factor; w = emissive strength.
        AbiFloat4 surfaceParams;            // metallic, roughness, normal scale, alpha cutoff.
        AbiFloat4 transmissionParams;       // transmission, IOR, reserved, reserved.
        AbiFloat4 attenuationColorDistance; // rgb attenuation color; w = reference distance.
        AbiUInt4 textureImageIndices;       // base color, metal-rough, normal, emissive.
        AbiUInt4 textureSamplerIndices;     // Sampler index paired with each image above.
        AbiUInt4 metadata;                  // MaterialModel, MaterialFlags, stable ID, reserved.
    };

    struct alignas(16) GpuLightV0
    {
        AbiFloat4 positionRange;
        AbiFloat4 directionCosOuter;
        AbiFloat4 radianceScale;
        AbiFloat4 shapeParams;
        AbiUInt4 identity; // LightType, stable light ID, instance ID, primitive ID.
        AbiUInt4 extra;    // texture ID, LightFlags, distribution offset/count.
    };
}
