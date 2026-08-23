#ifndef RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V0_HLSLI
#define RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V0_HLSLI

static const uint kDescriptorSetFrameV0 = 0u;
static const uint kDescriptorSetSceneV0 = 1u;
static const uint kDescriptorSetTraversalBackendV0 = 2u;
static const uint kDescriptorSetWavefrontReservedV0 = 3u;
static const uint kDescriptorSetReconstructionReservedV0 = 4u;
static const uint kDescriptorSetRestirReservedV0 = 5u;
static const uint kDescriptorSetDebugProfilerV0 = 6u;

static const uint kFrameBindingConstantsV0 = 0u;

static const uint kSceneBindingConstantsV0 = 0u;
static const uint kSceneBindingVerticesV0 = 1u;
static const uint kSceneBindingIndicesV0 = 2u;
static const uint kSceneBindingGeometriesV0 = 3u;
static const uint kSceneBindingInstancesV0 = 4u;
static const uint kSceneBindingMaterialsV0 = 5u;
static const uint kSceneBindingLightsV0 = 6u;
static const uint kSceneBindingTexturesV0 = 16u;
static const uint kSceneBindingSamplersV0 = 17u;

static const uint kDebugProfilerBindingConstantsV0 = 0u;

#endif
