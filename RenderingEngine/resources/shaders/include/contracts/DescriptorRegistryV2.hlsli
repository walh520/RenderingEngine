#ifndef RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V2_HLSLI
#define RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V2_HLSLI

static const uint kDescriptorSetFrameV2 = 0u;
static const uint kDescriptorSetSceneV2 = 1u;
static const uint kDescriptorSetTraversalV2 = 2u;
static const uint kDescriptorSetWavefrontV2 = 3u;
static const uint kDescriptorSetReconstructionV2 = 4u;
static const uint kDescriptorSetRestirReservedV2 = 5u;
static const uint kDescriptorSetDebugProfilerV2 = 6u;

static const uint kWavefrontBindingConstantsV2 = 0u;
static const uint kWavefrontBindingPathStatesV2 = 1u;
static const uint kWavefrontBindingRayQueueAV2 = 2u;
static const uint kWavefrontBindingHitQueueV2 = 3u;
static const uint kWavefrontBindingShadowQueueV2 = 4u;
static const uint kWavefrontBindingRayQueueBV2 = 5u;
static const uint kWavefrontBindingPrivatePathStatesV2 = 6u;
static const uint kWavefrontBindingDenseNextV2 = 7u;
static const uint kWavefrontBindingDenseShadowV2 = 8u;
static const uint kWavefrontBindingNextQueueV2 = 9u;
static const uint kWavefrontBindingCompactionFlagsV2 = 10u;
static const uint kWavefrontBindingPrefixV2 = 11u;
static const uint kWavefrontBindingScanScratchV2 = 12u;
static const uint kWavefrontBindingQueueHeadersV2 = 13u;
static const uint kWavefrontBindingIndirectArgumentsV2 = 14u;
static const uint kWavefrontBindingBounceCountersV2 = 15u;
static const uint kWavefrontBindingOutputV2 = 16u;
static const uint kWavefrontBindingDirectDiffuseV2 = 17u;
static const uint kWavefrontBindingDirectSpecularV2 = 18u;
static const uint kWavefrontBindingIndirectDiffuseV2 = 19u;
static const uint kWavefrontBindingIndirectSpecularV2 = 20u;
static const uint kWavefrontBindingDebugOutputV2 = 21u;
static const uint kWavefrontBindingCameraEmissionV2 = 22u;
static const uint kWavefrontBindingShadowAovV2 = 23u;
static const uint kWavefrontBindingPrimarySurfaceV2ExportV2 = 24u;
static const uint kWavefrontBindingMaterialWorkV2 = kWavefrontBindingHitQueueV2;

static const uint kReconstructionBindingPrimarySurfaceV2 = 0u;
static const uint kReconstructionBindingGBufferV2 = kReconstructionBindingPrimarySurfaceV2;
static const uint kReconstructionBindingMotionInputsV2 = 1u;
static const uint kReconstructionBindingCurrentTransformsV2 = 2u;
static const uint kReconstructionBindingPreviousTransformsV2 = 3u;
static const uint kReconstructionBindingRawSignalV2 = 4u;
static const uint kReconstructionBindingDemodulatedSignalV2 = 5u;
static const uint kReconstructionBindingHistoryReadV2 = 6u;
static const uint kReconstructionBindingHistoryWriteV2 = 7u;
static const uint kReconstructionBindingTemporalSignalV2 = 8u;
static const uint kReconstructionBindingTemporalDebugV2 = 9u;
static const uint kReconstructionBindingVarianceV2 = 10u;
static const uint kReconstructionBindingAtrousInputV2 = 11u;
static const uint kReconstructionBindingAtrousOutputV2 = 12u;
static const uint kReconstructionBindingComposedOutputV2 = 13u;
static const uint kReconstructionBindingAtrousVarianceOutputV2 = 14u;
static const uint kReconstructionBindingPrimarySurfaceExportV2 = 15u;
static const uint kReconstructionBindingMotionConstantsV2 = 16u;
static const uint kReconstructionBindingPrepareConstantsV2 = 17u;
static const uint kReconstructionBindingTemporalConstantsV2 = 18u;
static const uint kReconstructionBindingVarianceConstantsV2 = 19u;
static const uint kReconstructionBindingAtrousConstantsV2 = 20u;
static const uint kReconstructionBindingComposeConstantsV2 = 21u;

#endif
