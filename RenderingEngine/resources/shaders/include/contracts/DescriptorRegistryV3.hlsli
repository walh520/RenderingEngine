#ifndef RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V3_HLSLI
#define RENDERING_ENGINE_DESCRIPTOR_REGISTRY_V3_HLSLI

static const uint kDescriptorSetFrameV3 = 0u;
static const uint kDescriptorSetSceneV3 = 1u;
static const uint kDescriptorSetTraversalV3 = 2u;
static const uint kDescriptorSetWavefrontV3 = 3u;
static const uint kDescriptorSetReconstructionV3 = 4u;
static const uint kDescriptorSetRestirV3 = 5u;
static const uint kDescriptorSetDebugProfilerV3 = 6u;

static const uint kFrameBindingConstantsV3 = 0u;
static const uint kSceneBindingConstantsV3 = 0u;
static const uint kSceneBindingVerticesV3 = 1u;
static const uint kSceneBindingIndicesV3 = 2u;
static const uint kSceneBindingGeometriesV3 = 3u;
static const uint kSceneBindingInstancesV3 = 4u;
static const uint kSceneBindingMaterialsV3 = 5u;
static const uint kSceneBindingLightsV3 = 6u;
static const uint kSceneBindingTexturesV3 = 16u;
static const uint kSceneBindingSamplersV3 = 17u;

static const uint kTraversalBindingBackendSceneV3 = 0u;
static const uint kTraversalBindingRaysV3 = 1u;
static const uint kTraversalBindingHitsV3 = 2u;
static const uint kTraversalBindingAlphaAtlasV3 = 8u;
static const uint kTraversalBindingAlphaSamplerV3 = 9u;

static const uint kWavefrontBindingConstantsV3 = 0u;
static const uint kWavefrontBindingPathStatesV3 = 1u;
static const uint kWavefrontBindingRayQueueAV3 = 2u;
static const uint kWavefrontBindingHitQueueV3 = 3u;
static const uint kWavefrontBindingShadowQueueV3 = 4u;
static const uint kWavefrontBindingRayQueueBV3 = 5u;
static const uint kWavefrontBindingPrivatePathStatesV3 = 6u;
static const uint kWavefrontBindingDenseNextV3 = 7u;
static const uint kWavefrontBindingDenseShadowV3 = 8u;
static const uint kWavefrontBindingNextQueueV3 = 9u;
static const uint kWavefrontBindingCompactionFlagsV3 = 10u;
static const uint kWavefrontBindingPrefixV3 = 11u;
static const uint kWavefrontBindingScanScratchV3 = 12u;
static const uint kWavefrontBindingQueueHeadersV3 = 13u;
static const uint kWavefrontBindingIndirectArgumentsV3 = 14u;
static const uint kWavefrontBindingBounceCountersV3 = 15u;
static const uint kWavefrontBindingOutputV3 = 16u;
static const uint kWavefrontBindingDirectDiffuseV3 = 17u;
static const uint kWavefrontBindingDirectSpecularV3 = 18u;
static const uint kWavefrontBindingIndirectDiffuseV3 = 19u;
static const uint kWavefrontBindingIndirectSpecularV3 = 20u;
static const uint kWavefrontBindingDebugOutputV3 = 21u;
static const uint kWavefrontBindingCameraEmissionV3 = 22u;
static const uint kWavefrontBindingShadowAovV3 = 23u;
static const uint kWavefrontBindingPrimarySurfaceV2ExportV3 = 24u;

static const uint kReconstructionBindingPrimarySurfaceV3 = 0u;
static const uint kReconstructionBindingMotionInputsV3 = 1u;
static const uint kReconstructionBindingCurrentTransformsV3 = 2u;
static const uint kReconstructionBindingPreviousTransformsV3 = 3u;
static const uint kReconstructionBindingRawSignalV3 = 4u;
static const uint kReconstructionBindingDemodulatedSignalV3 = 5u;
static const uint kReconstructionBindingHistoryReadV3 = 6u;
static const uint kReconstructionBindingHistoryWriteV3 = 7u;
static const uint kReconstructionBindingTemporalSignalV3 = 8u;
static const uint kReconstructionBindingTemporalDebugV3 = 9u;
static const uint kReconstructionBindingVarianceV3 = 10u;
static const uint kReconstructionBindingAtrousInputV3 = 11u;
static const uint kReconstructionBindingAtrousOutputV3 = 12u;
static const uint kReconstructionBindingComposedOutputV3 = 13u;
static const uint kReconstructionBindingAtrousVarianceOutputV3 = 14u;
static const uint kReconstructionBindingPrimarySurfaceExportV3 = 15u;
static const uint kReconstructionBindingMotionConstantsV3 = 16u;
static const uint kReconstructionBindingPrepareConstantsV3 = 17u;
static const uint kReconstructionBindingTemporalConstantsV3 = 18u;
static const uint kReconstructionBindingVarianceConstantsV3 = 19u;
static const uint kReconstructionBindingAtrousConstantsV3 = 20u;
static const uint kReconstructionBindingComposeConstantsV3 = 21u;

static const uint kDebugProfilerBindingConstantsV3 = 0u;

// Set 5 has one canonical meaning per binding across all ReSTIR stages.
static const uint kRestirBindingParametersV3 = 0u;
static const uint kRestirBindingCandidatesV3 = 1u;
static const uint kRestirBindingReservoirReadV3 = 2u;
static const uint kRestirBindingHistoryReservoirReadV3 = 3u;
static const uint kRestirBindingSurfaceCurrentV3 = 4u;
static const uint kRestirBindingSurfaceHistoryV3 = 5u;
static const uint kRestirBindingHistoryIdentityV3 = 6u;
static const uint kRestirBindingReservoirWriteV3 = 7u;
static const uint kRestirBindingDebugRecordV3 = 8u;
static const uint kRestirBindingStatisticsV3 = 9u;
static const uint kRestirBindingVisibilityResultsV3 = 10u;
static const uint kRestirBindingCandidateAtCenterV3 = 11u;
static const uint kRestirBindingLightTableV3 = 12u;
static const uint kRestirBindingPairwiseTargetSupportV3 = 13u;
static const uint kRestirBindingDirectLightingV3 = 14u;
static const uint kRestirBindingValidationReasonsV3 = 15u;
static const uint kRestirBindingDebugImageV3 = 16u;
static const uint kRestirBindingHistoryAtCurrentV3 = 17u;
static const uint kRestirBindingReferenceTargetV3 = 18u;
static const uint kRestirBindingReferenceVisibilityV3 = 19u;

// Wave 4 production-frame graph resources. Bindings 0-19 are append-only.
static const uint kRestirBindingCurrentToPreviousLightIndexV3 = 20u;
static const uint kRestirBindingPreviousToCurrentLightIndexV3 = 21u;
static const uint kRestirBindingNeighborIndicesV3 = 22u;
static const uint kRestirBindingShadowRayQueueV3 = 23u;
static const uint kRestirBindingDirectDiffuseV3 = 24u;
static const uint kRestirBindingDirectSpecularV3 = 25u;
static const uint kRestirBindingInitialReservoirV3 = 26u;
static const uint kRestirBindingTemporalReservoirV3 = 27u;
static const uint kRestirBindingSpatialReservoirV3 = 28u;
static const uint kRestirBindingPublishedReservoirV3 = 29u;
static const uint kRestirBindingPreviousPublishedReservoirV3 = 30u;

#endif
