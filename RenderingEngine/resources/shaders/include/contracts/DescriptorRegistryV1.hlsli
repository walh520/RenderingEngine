#ifndef RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V1_HLSLI
#define RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V1_HLSLI

static const uint kDescriptorSetFrameV1 = 0u;
static const uint kDescriptorSetSceneV1 = 1u;
static const uint kDescriptorSetTraversalBackendV1 = 2u;
static const uint kDescriptorSetWavefrontQueuesV1 = 3u;
static const uint kDescriptorSetReconstructionReservedV1 = 4u;
static const uint kDescriptorSetRestirReservedV1 = 5u;
static const uint kDescriptorSetDebugProfilerV1 = 6u;

static const uint kTraversalBindingBackendSceneV1 = 0u;
static const uint kTraversalBindingRaysV1 = 1u;
static const uint kTraversalBindingHitsV1 = 2u;
static const uint kTraversalBindingAlphaAtlasV1 = 8u;
static const uint kTraversalBindingAlphaSamplerV1 = 9u;

static const uint kWavefrontBindingConstantsV1 = 0u;
static const uint kWavefrontBindingPathStatesV1 = 1u;
static const uint kWavefrontBindingRayQueueV1 = 2u;
static const uint kWavefrontBindingHitQueueV1 = 3u;
static const uint kWavefrontBindingShadowQueueV1 = 4u;

#endif
