#include "../../resources/shaders/include/contracts/AbiV0.hlsli"

[[vk::binding(0, 0)]] ConstantBuffer<GpuFrameConstantsV0> gFrame;
[[vk::binding(0, 1)]] ConstantBuffer<GpuSceneConstantsV0> gScene;
[[vk::binding(1, 1)]] StructuredBuffer<GpuVertexV0> gVertices;
[[vk::binding(2, 1)]] StructuredBuffer<uint> gIndices;
[[vk::binding(3, 1)]] StructuredBuffer<GpuGeometryV0> gGeometries;
[[vk::binding(4, 1)]] StructuredBuffer<GpuInstanceV0> gInstances;
[[vk::binding(5, 1)]] StructuredBuffer<GpuMaterialV0> gMaterials;
[[vk::binding(6, 1)]] StructuredBuffer<GpuLightV0> gLights;
// Set-2 bindings in this probe are test-only. They keep Ray/Hit member
// decorations alive without assigning production traversal bindings in abi-v0.
[[vk::binding(0, 2)]] StructuredBuffer<GpuRayV0> gRays;
[[vk::binding(1, 2)]] StructuredBuffer<GpuHitV0> gHits;
[[vk::binding(0, 6)]] RWByteAddressBuffer gProbeOutput;

void StoreFloat4(inout uint outputOffset, float4 value)
{
    gProbeOutput.Store4(outputOffset, asuint(value));
    outputOffset += 16u;
}

void StoreUInt4(inout uint outputOffset, uint4 value)
{
    gProbeOutput.Store4(outputOffset, value);
    outputOffset += 16u;
}

void StoreMatrix(inout uint outputOffset, AbiMat4Rows value)
{
    StoreFloat4(outputOffset, value.row0);
    StoreFloat4(outputOffset, value.row1);
    StoreFloat4(outputOffset, value.row2);
    StoreFloat4(outputOffset, value.row3);
}

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId != 0u))
    {
        return;
    }

    uint outputOffset = 0u;

    StoreMatrix(outputOffset, gFrame.clipFromWorld);
    StoreMatrix(outputOffset, gFrame.worldFromClip);
    StoreFloat4(outputOffset, gFrame.cameraPositionExposure);
    StoreFloat4(outputOffset, gFrame.renderExtentInvExtent);
    StoreFloat4(outputOffset, gFrame.timeAndEpsilon);
    StoreUInt4(outputOffset, gFrame.frameInfo);
    StoreUInt4(outputOffset, gFrame.renderInfo);
    StoreUInt4(outputOffset, gFrame.modeInfo);
    StoreUInt4(outputOffset, gFrame.sceneInfo);
    StoreUInt4(outputOffset, gFrame.reserved0);

    StoreUInt4(outputOffset, gScene.counts0);
    StoreUInt4(outputOffset, gScene.counts1);
    StoreFloat4(outputOffset, gScene.sceneBoundsMin);
    StoreFloat4(outputOffset, gScene.sceneBoundsMax);
    StoreUInt4(outputOffset, gScene.versionFlags);
    StoreUInt4(outputOffset, gScene.environment);

    const GpuVertexV0 vertex = gVertices[0];
    StoreFloat4(outputOffset, vertex.position);
    StoreFloat4(outputOffset, vertex.normal);
    StoreFloat4(outputOffset, vertex.tangent);
    StoreFloat4(outputOffset, vertex.texcoord0);
    StoreUInt4(outputOffset, uint4(gIndices[0], 0u, 0u, 0u));

    const GpuGeometryV0 geometry = gGeometries[0];
    StoreUInt4(outputOffset, geometry.indexRange);
    StoreUInt4(outputOffset, geometry.identity);
    StoreFloat4(outputOffset, geometry.localBoundsMin);
    StoreFloat4(outputOffset, geometry.localBoundsMax);

    const GpuInstanceV0 instanceValue = gInstances[0];
    StoreMatrix(outputOffset, instanceValue.objectToWorld);
    StoreMatrix(outputOffset, instanceValue.worldToObject);
    StoreMatrix(outputOffset, instanceValue.previousObjectToWorld);
    StoreUInt4(outputOffset, instanceValue.metadata);
    StoreUInt4(outputOffset, instanceValue.reserved0);

    const GpuMaterialV0 material = gMaterials[0];
    StoreFloat4(outputOffset, material.baseColorFactor);
    StoreFloat4(outputOffset, material.emissiveFactorStrength);
    StoreFloat4(outputOffset, material.surfaceParams);
    StoreFloat4(outputOffset, material.transmissionParams);
    StoreFloat4(outputOffset, material.attenuationColorDistance);
    StoreUInt4(outputOffset, material.textureImageIndices);
    StoreUInt4(outputOffset, material.textureSamplerIndices);
    StoreUInt4(outputOffset, material.metadata);

    const GpuLightV0 light = gLights[0];
    StoreFloat4(outputOffset, light.positionRange);
    StoreFloat4(outputOffset, light.directionCosOuter);
    StoreFloat4(outputOffset, light.radianceScale);
    StoreFloat4(outputOffset, light.shapeParams);
    StoreUInt4(outputOffset, light.identity);
    StoreUInt4(outputOffset, light.extra);

    const GpuRayV0 rayValue = gRays[0];
    StoreFloat4(outputOffset, rayValue.originTMin);
    StoreFloat4(outputOffset, rayValue.directionTMax);
    StoreUInt4(outputOffset, rayValue.query);
    StoreUInt4(outputOffset, rayValue.reserved0);

    const GpuHitV0 hit = gHits[0];
    StoreFloat4(outputOffset, hit.positionT);
    StoreFloat4(outputOffset, hit.geometricNormalBaryU);
    StoreFloat4(outputOffset, hit.shadingNormalBaryV);
    StoreUInt4(outputOffset, hit.ids);
    StoreUInt4(outputOffset, hit.metadata);
    StoreUInt4(outputOffset, hit.reserved0);
}
