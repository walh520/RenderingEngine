#ifndef RENDERING_ENGINE_WAVEFRONT_RECONSTRUCTION_EXPORT_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_RECONSTRUCTION_EXPORT_HLSLI

#include "include/contracts/SceneAbiV0.hlsli"

// Shade otherwise consumes the canonical scene only through the traversal
// queue. Reconstruction additionally needs the instance transform pair to
// export object-space motion input.
[[vk::binding(0, 1)]] ConstantBuffer<GpuSceneConstantsV0> gPbrSceneConstantsL6;
[[vk::binding(4, 1)]] StructuredBuffer<GpuInstanceV0> gPbrInstancesL6;

#define PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES
#include "pbr_reconstruction_export_v2.hlsli"
#undef PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES

void WfClearPrimarySurfaceV2(uint pixelIndex)
{
    PbrClearReconstructionInputsV2(pixelIndex);
}

void WfPublishPrimarySurfaceV2(
    uint pixelIndex,
    float3 worldPosition,
    float linearDepth,
    float3 geometricNormal,
    float3 shadingNormal,
    uint materialId,
    uint instanceId,
    uint primitiveId,
    uint frontFace,
    float3 baseColor,
    float metallic,
    float roughness,
    float transmission,
    float3 f0,
    bool restirPrimarySupported)
{
    PbrPublishPrimarySurfaceV2(
        pixelIndex,
        worldPosition,
        linearDepth,
        geometricNormal,
        shadingNormal,
        materialId,
        instanceId,
        primitiveId,
        frontFace,
        baseColor,
        metallic,
        roughness,
        transmission,
        f0,
        restirPrimarySupported);
}

void WfPublishReconstructionSignalV2(
    uint pixelIndex,
    float3 cameraEmission,
    float3 directDiffuse,
    float3 directSpecular,
    float3 indirectDiffuse,
    float3 indirectSpecular)
{
    PbrPublishReconstructionSignalV2(
        pixelIndex,
        cameraEmission,
        directDiffuse,
        directSpecular,
        indirectDiffuse,
        indirectSpecular);
}

#endif
