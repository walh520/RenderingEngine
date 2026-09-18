#include "contracts/AbiV0.hpp"
#include "contracts/AbiV2.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Abi = RenderingEngine::Contracts::AbiV0;

#define ABI_ASSERT_RECORD(Type, ExpectedSize) \
    static_assert(sizeof(Type) == ExpectedSize); \
    static_assert(alignof(Type) == 16u); \
    static_assert(std::is_standard_layout_v<Type>); \
    static_assert(std::is_trivially_copyable_v<Type>)

#define ABI_ASSERT_FIELD(Type, Field, ExpectedOffset, ExpectedSize) \
    static_assert(offsetof(Type, Field) == ExpectedOffset); \
    static_assert(sizeof(((Type*)nullptr)->Field) == ExpectedSize)

ABI_ASSERT_RECORD(Abi::AbiFloat4, 16u);
ABI_ASSERT_FIELD(Abi::AbiFloat4, x, 0u, 4u);
ABI_ASSERT_FIELD(Abi::AbiFloat4, y, 4u, 4u);
ABI_ASSERT_FIELD(Abi::AbiFloat4, z, 8u, 4u);
ABI_ASSERT_FIELD(Abi::AbiFloat4, w, 12u, 4u);

ABI_ASSERT_RECORD(Abi::AbiUInt4, 16u);
ABI_ASSERT_FIELD(Abi::AbiUInt4, x, 0u, 4u);
ABI_ASSERT_FIELD(Abi::AbiUInt4, y, 4u, 4u);
ABI_ASSERT_FIELD(Abi::AbiUInt4, z, 8u, 4u);
ABI_ASSERT_FIELD(Abi::AbiUInt4, w, 12u, 4u);

ABI_ASSERT_RECORD(Abi::AbiMat4Rows, 64u);
ABI_ASSERT_FIELD(Abi::AbiMat4Rows, row0, 0u, 16u);
ABI_ASSERT_FIELD(Abi::AbiMat4Rows, row1, 16u, 16u);
ABI_ASSERT_FIELD(Abi::AbiMat4Rows, row2, 32u, 16u);
ABI_ASSERT_FIELD(Abi::AbiMat4Rows, row3, 48u, 16u);

ABI_ASSERT_RECORD(Abi::GpuFrameConstantsV0, 256u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, clipFromWorld, 0u, 64u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, worldFromClip, 64u, 64u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, cameraPositionExposure, 128u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, renderExtentInvExtent, 144u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, timeAndEpsilon, 160u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, frameInfo, 176u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, renderInfo, 192u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, modeInfo, 208u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, sceneInfo, 224u, 16u);
ABI_ASSERT_FIELD(Abi::GpuFrameConstantsV0, reserved0, 240u, 16u);

ABI_ASSERT_RECORD(Abi::GpuSceneConstantsV0, 96u);
ABI_ASSERT_FIELD(Abi::GpuSceneConstantsV0, counts0, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuSceneConstantsV0, counts1, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuSceneConstantsV0, sceneBoundsMin, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuSceneConstantsV0, sceneBoundsMax, 48u, 16u);
ABI_ASSERT_FIELD(Abi::GpuSceneConstantsV0, versionFlags, 64u, 16u);
ABI_ASSERT_FIELD(Abi::GpuSceneConstantsV0, environment, 80u, 16u);

ABI_ASSERT_RECORD(Abi::GpuVertexV0, 64u);
ABI_ASSERT_FIELD(Abi::GpuVertexV0, position, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuVertexV0, normal, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuVertexV0, tangent, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuVertexV0, texcoord0, 48u, 16u);

ABI_ASSERT_RECORD(Abi::GpuGeometryV0, 64u);
ABI_ASSERT_FIELD(Abi::GpuGeometryV0, indexRange, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuGeometryV0, identity, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuGeometryV0, localBoundsMin, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuGeometryV0, localBoundsMax, 48u, 16u);

ABI_ASSERT_RECORD(Abi::GpuInstanceV0, 224u);
ABI_ASSERT_FIELD(Abi::GpuInstanceV0, objectToWorld, 0u, 64u);
ABI_ASSERT_FIELD(Abi::GpuInstanceV0, worldToObject, 64u, 64u);
ABI_ASSERT_FIELD(Abi::GpuInstanceV0, previousObjectToWorld, 128u, 64u);
ABI_ASSERT_FIELD(Abi::GpuInstanceV0, metadata, 192u, 16u);
ABI_ASSERT_FIELD(Abi::GpuInstanceV0, reserved0, 208u, 16u);

ABI_ASSERT_RECORD(Abi::GpuMaterialV0, 128u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, baseColorFactor, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, emissiveFactorStrength, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, surfaceParams, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, transmissionParams, 48u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, attenuationColorDistance, 64u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, textureImageIndices, 80u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, textureSamplerIndices, 96u, 16u);
ABI_ASSERT_FIELD(Abi::GpuMaterialV0, metadata, 112u, 16u);

ABI_ASSERT_RECORD(Abi::GpuLightV0, 96u);
ABI_ASSERT_FIELD(Abi::GpuLightV0, positionRange, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuLightV0, directionCosOuter, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuLightV0, radianceScale, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuLightV0, shapeParams, 48u, 16u);
ABI_ASSERT_FIELD(Abi::GpuLightV0, identity, 64u, 16u);
ABI_ASSERT_FIELD(Abi::GpuLightV0, extra, 80u, 16u);

ABI_ASSERT_RECORD(Abi::GpuRayV0, 64u);
ABI_ASSERT_FIELD(Abi::GpuRayV0, originTMin, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuRayV0, directionTMax, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuRayV0, query, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuRayV0, reserved0, 48u, 16u);

ABI_ASSERT_RECORD(Abi::GpuHitV0, 96u);
ABI_ASSERT_FIELD(Abi::GpuHitV0, positionT, 0u, 16u);
ABI_ASSERT_FIELD(Abi::GpuHitV0, geometricNormalBaryU, 16u, 16u);
ABI_ASSERT_FIELD(Abi::GpuHitV0, shadingNormalBaryV, 32u, 16u);
ABI_ASSERT_FIELD(Abi::GpuHitV0, ids, 48u, 16u);
ABI_ASSERT_FIELD(Abi::GpuHitV0, metadata, 64u, 16u);
ABI_ASSERT_FIELD(Abi::GpuHitV0, reserved0, 80u, 16u);

static_assert(Abi::kAbiVersion == 1u);
static_assert(Abi::kInvalidId == 0xffffffffu);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::Frame) == 0u);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::Scene) == 1u);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::TraversalBackend) == 2u);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::WavefrontReserved) == 3u);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::ReconstructionReserved) == 4u);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::RestirReserved) == 5u);
static_assert(static_cast<std::uint32_t>(Abi::DescriptorSet::DebugProfiler) == 6u);
static_assert(Abi::FrameBinding::Constants == 0u);
static_assert(Abi::SceneBinding::Constants == 0u);
static_assert(Abi::SceneBinding::Vertices == 1u);
static_assert(Abi::SceneBinding::Indices == 2u);
static_assert(Abi::SceneBinding::Geometries == 3u);
static_assert(Abi::SceneBinding::Instances == 4u);
static_assert(Abi::SceneBinding::Materials == 5u);
static_assert(Abi::SceneBinding::Lights == 6u);
static_assert(Abi::SceneBinding::Textures == 16u);
static_assert(Abi::SceneBinding::Samplers == 17u);
static_assert(Abi::DebugProfilerBinding::Constants == 0u);

namespace Abi2 = RenderingEngine::Contracts::AbiV2;

ABI_ASSERT_RECORD(Abi2::GpuPrimarySurfaceV2, 96u);
ABI_ASSERT_FIELD(Abi2::GpuPrimarySurfaceV2, worldPositionLinearDepth, 0u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuPrimarySurfaceV2, geometricNormalRoughness, 16u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuPrimarySurfaceV2, shadingNormalMetallic, 32u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuPrimarySurfaceV2, diffuseAlbedo, 48u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuPrimarySurfaceV2, specularAlbedo, 64u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuPrimarySurfaceV2, identity, 80u, 16u);

ABI_ASSERT_RECORD(Abi2::GpuMotionVectorV2, 48u);
ABI_ASSERT_FIELD(Abi2::GpuMotionVectorV2, motionExpectedDepth, 0u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuMotionVectorV2, currentPreviousUv, 16u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuMotionVectorV2, identity, 32u, 16u);

ABI_ASSERT_RECORD(Abi2::GpuReconstructionSignalV2, 64u);
ABI_ASSERT_FIELD(Abi2::GpuReconstructionSignalV2, directDiffuse, 0u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuReconstructionSignalV2, directSpecular, 16u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuReconstructionSignalV2, indirectDiffuse, 32u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuReconstructionSignalV2, indirectSpecular, 48u, 16u);

ABI_ASSERT_RECORD(Abi2::GpuHistoryMetadataV2, 48u);
ABI_ASSERT_FIELD(Abi2::GpuHistoryMetadataV2, momentsVarianceHistory, 0u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuHistoryMetadataV2, surfaceIdentity, 16u, 16u);
ABI_ASSERT_FIELD(Abi2::GpuHistoryMetadataV2, frameIdentity, 32u, 16u);

ABI_ASSERT_RECORD(Abi2::GpuGBufferRecordV2, 208u);
ABI_ASSERT_FIELD(Abi2::GpuGBufferRecordV2, primary, 0u, 96u);
ABI_ASSERT_FIELD(Abi2::GpuGBufferRecordV2, motion, 96u, 48u);
ABI_ASSERT_FIELD(Abi2::GpuGBufferRecordV2, signal, 144u, 64u);

static_assert(Abi2::kAbiVersion == 3u);
static_assert(static_cast<std::uint32_t>(Abi2::DescriptorSet::WavefrontQueues) == 3u);
static_assert(static_cast<std::uint32_t>(Abi2::DescriptorSet::Reconstruction) == 4u);
static_assert(Abi2::WavefrontBinding::Constants == 0u);
static_assert(Abi2::WavefrontBinding::ShadowQueue == 4u);
static_assert(Abi2::WavefrontBinding::CameraEmission == 22u);
static_assert(Abi2::WavefrontBinding::ShadowAov == 23u);
static_assert(Abi2::WavefrontBinding::PrimarySurfaceV2Export == 24u);
static_assert(Abi2::WavefrontBinding::PrimarySurfaceV2Export
    != Abi2::WavefrontBinding::BounceCounters);
static_assert(Abi2::WavefrontBinding::PrivatePathStates == 6u);
static_assert(Abi2::WavefrontBinding::MaterialWork ==
    Abi2::WavefrontBinding::HitQueue);
static_assert(Abi2::ReconstructionBinding::AtrousVarianceOutput == 14u);
static_assert(Abi2::ReconstructionBinding::PrimarySurfaceExport == 15u);
static_assert(Abi2::ReconstructionBinding::PrimarySurface == 0u);
static_assert(Abi2::ReconstructionBinding::ComposedOutput == 13u);
static_assert(Abi2::ReconstructionBinding::ComposeConstants == 21u);

#undef ABI_ASSERT_FIELD
#undef ABI_ASSERT_RECORD
