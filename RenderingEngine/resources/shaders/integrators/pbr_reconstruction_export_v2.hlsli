#ifndef RENDERING_ENGINE_PBR_RECONSTRUCTION_EXPORT_V2_HLSLI
#define RENDERING_ENGINE_PBR_RECONSTRUCTION_EXPORT_V2_HLSLI

#include "include/contracts/AbiV2.hlsli"

// ABI-v2 reconstruction inputs are shared by every production integrator.
// Keeping them in set 4 makes reconstruction an orthogonal post stage instead
// of a private side effect of the Wavefront scheduler.
struct PbrMotionInputV2
{
    float3 objectPosition;
    uint currentTransformIndex;
    uint previousTransformIndex;
    uint3 reserved;
};

[[vk::binding(kReconstructionBindingGBufferV2,
    kDescriptorSetReconstructionV2)]] RWStructuredBuffer<GpuGBufferRecordV2>
    gPbrGBufferV2;
[[vk::binding(kReconstructionBindingMotionInputsV2,
    kDescriptorSetReconstructionV2)]] RWStructuredBuffer<PbrMotionInputV2>
    gPbrMotionInputV2;
[[vk::binding(kReconstructionBindingRawSignalV2,
    kDescriptorSetReconstructionV2)]] RWStructuredBuffer<GpuReconstructionSignalV2>
    gPbrReconstructionSignalV2;
[[vk::binding(kReconstructionBindingPrimarySurfaceExportV2,
    kDescriptorSetReconstructionV2)]] RWStructuredBuffer<GpuPrimarySurfaceV2>
    gPbrPrimarySurfaceV2;

// Private terminal signal: camera-visible emission/background must not enter
// diffuse demodulation or be replaced by ReSTIR's primary-direct ownership.
[[vk::binding(23, 4)]] RWStructuredBuffer<float4> gPbrTerminalSignal;

void PbrClearReconstructionInputsV2(uint pixelIndex)
{
    gPbrGBufferV2[pixelIndex] = (GpuGBufferRecordV2)0;
    gPbrMotionInputV2[pixelIndex] = (PbrMotionInputV2)0;
    gPbrReconstructionSignalV2[pixelIndex] =
        (GpuReconstructionSignalV2)0;
    gPbrPrimarySurfaceV2[pixelIndex] = (GpuPrimarySurfaceV2)0;
    gPbrTerminalSignal[pixelIndex] = 0.0f;
}

void PbrPublishPrimarySurfaceV2(
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
    GpuGBufferRecordV2 record = (GpuGBufferRecordV2)0;
    record.primary.worldPositionLinearDepth =
        float4(worldPosition, max(linearDepth, 0.0f));
    record.primary.geometricNormalRoughness =
        float4(normalize(geometricNormal), saturate(roughness));
    record.primary.shadingNormalMetallic =
        float4(normalize(shadingNormal), saturate(metallic));
    const float3 diffuseAlbedo = max(baseColor, 0.0f.xxx)
        * (1.0f - saturate(metallic))
        * (1.0f - saturate(transmission));
    const float3 specularAlbedo = max(
        lerp(f0, baseColor, saturate(metallic)), 0.0f.xxx);
    record.primary.diffuseAlbedo = float4(diffuseAlbedo, 0.0f);
    record.primary.specularAlbedo = float4(specularAlbedo, 0.0f);
    uint flags = kPrimarySurfaceFlagValidV2;
    if (any(diffuseAlbedo > 0.0f.xxx))
        flags |= kPrimarySurfaceFlagHasDiffuseV2;
    if (any(specularAlbedo > 0.0f.xxx))
        flags |= kPrimarySurfaceFlagHasSpecularV2;
    if (frontFace != 0u)
        flags |= kPrimarySurfaceFlagFrontFaceV2;
    record.primary.identity = uint4(
        materialId, instanceId, primitiveId, flags);
    gPbrGBufferV2[pixelIndex] = record;
    GpuPrimarySurfaceV2 restirSurface = record.primary;
    if (!restirPrimarySupported)
        restirSurface.identity.w &= ~kPrimarySurfaceFlagValidV2;
    gPbrPrimarySurfaceV2[pixelIndex] = restirSurface;

    PbrMotionInputV2 motion = (PbrMotionInputV2)0;
#if defined(PBR_L6_RECONSTRUCTION_HAS_SCENE_INSTANCES)
    uint transformIndex = 0xffffffffu;
    const uint instanceCount = gPbrSceneConstantsL6.counts0.w;
    for (uint candidate = 0u; candidate < instanceCount; ++candidate)
    {
        if (gPbrInstancesL6[candidate].metadata.z == instanceId)
        {
            transformIndex = candidate;
            break;
        }
    }
    if (transformIndex != 0xffffffffu)
    {
        const AbiMat4Rows worldToObject =
            gPbrInstancesL6[transformIndex].worldToObject;
        const float4 world = float4(worldPosition, 1.0f);
        motion.objectPosition = float3(
            dot(worldToObject.row0, world),
            dot(worldToObject.row1, world),
            dot(worldToObject.row2, world));
        motion.currentTransformIndex = transformIndex;
        motion.previousTransformIndex = transformIndex;
    }
    else
    {
        // An unresolved stable instance ID must fail closed in MotionVectorsCS.
        motion.objectPosition = 0.0f.xxx;
        motion.currentTransformIndex = instanceCount;
        motion.previousTransformIndex = instanceCount;
    }
#else
    // Seed/resolve-only entry points include this shared ABI writer but never
    // own traversal scene bindings. Keep their unused function definition
    // self-contained; production surface exporters define the scene macro.
    motion.objectPosition = worldPosition;
    motion.currentTransformIndex = 0u;
    motion.previousTransformIndex = 0u;
#endif
    gPbrMotionInputV2[pixelIndex] = motion;
}

void PbrPublishReconstructionSignalV2(
    uint pixelIndex,
    float3 cameraEmission,
    float3 directDiffuse,
    float3 directSpecular,
    float3 indirectDiffuse,
    float3 indirectSpecular)
{
    GpuReconstructionSignalV2 signal =
        (GpuReconstructionSignalV2)0;
    gPbrTerminalSignal[pixelIndex] = float4(cameraEmission, 0.0f);
    signal.directDiffuse = float4(directDiffuse, 0.0f);
    signal.directSpecular = float4(directSpecular, 0.0f);
    signal.indirectDiffuse = float4(indirectDiffuse, 0.0f);
    signal.indirectSpecular = float4(indirectSpecular, 0.0f);
    gPbrReconstructionSignalV2[pixelIndex] = signal;
    gPbrGBufferV2[pixelIndex].signal = signal;
}

#endif
