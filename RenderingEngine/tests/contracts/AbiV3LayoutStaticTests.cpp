#include "contracts/AbiV3.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Abi0 = RenderingEngine::Contracts::AbiV0;
namespace Abi1 = RenderingEngine::Contracts::AbiV1;
namespace Abi2 = RenderingEngine::Contracts::AbiV2;
namespace Abi3 = RenderingEngine::Contracts::AbiV3;

#define ABI_V3_ASSERT_RECORD(Type, ExpectedSize) \
    static_assert(sizeof(Type) == ExpectedSize); \
    static_assert(alignof(Type) == 16u); \
    static_assert(std::is_standard_layout_v<Type>); \
    static_assert(std::is_trivially_copyable_v<Type>)

#define ABI_V3_ASSERT_FIELD(Type, Field, ExpectedOffset, ExpectedSize) \
    static_assert(offsetof(Type, Field) == ExpectedOffset); \
    static_assert(sizeof(((Type*)nullptr)->Field) == ExpectedSize)

// Numeric versions are append-only; this file does not alter any prior ABI.
static_assert(Abi0::kAbiVersion == 1u);
static_assert(Abi1::kAbiVersion == 2u);
static_assert(Abi2::kAbiVersion == 3u);
static_assert(Abi3::kAbiVersion == 4u);
static_assert(Abi3::kInvalidId == Abi2::kInvalidId);

ABI_V3_ASSERT_RECORD(Abi3::GpuPersistentLightSampleV3, 128u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, positionDistance, 0u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, directionCombinedPdf, 16u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, radianceDiscretePdf, 32u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, conditionalPdf, 48u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, identity, 64u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, generation, 80u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, metadata, 96u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuPersistentLightSampleV3, sourceIdentity, 112u, 16u);

ABI_V3_ASSERT_RECORD(Abi3::GpuRestirCandidateV3, 160u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirCandidateV3, sample, 0u, 128u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirCandidateV3, targetProposalSupportCorrection, 128u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirCandidateV3, provenance, 144u, 16u);

ABI_V3_ASSERT_RECORD(Abi3::GpuRestirHistoryIdentityV3, 64u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirHistoryIdentityV3, surfaceIdentity, 0u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirHistoryIdentityV3, sceneIdentity, 16u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirHistoryIdentityV3, frameIdentity, 32u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirHistoryIdentityV3, reprojection, 48u, 16u);

ABI_V3_ASSERT_RECORD(Abi3::GpuRestirReservoirV3, 256u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirReservoirV3, selected, 0u, 128u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirReservoirV3, weightState, 128u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirReservoirV3, selectedTerms, 144u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirReservoirV3, state, 160u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirReservoirV3, provenance, 176u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirReservoirV3, history, 192u, 64u);

ABI_V3_ASSERT_RECORD(Abi3::GpuRestirDebugV3, 64u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirDebugV3, identity, 0u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirDebugV3, generation, 16u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirDebugV3, scalar, 32u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirDebugV3, state, 48u, 16u);

ABI_V3_ASSERT_RECORD(Abi3::GpuRestirStatisticsV3, 64u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirStatisticsV3, candidateCounts, 0u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirStatisticsV3, reuseCounts, 16u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirStatisticsV3, visibilityCounts, 32u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirStatisticsV3, errorMetrics, 48u, 16u);

ABI_V3_ASSERT_RECORD(Abi3::GpuRestirParametersV3, 128u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, extentAndCandidates, 0u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, reuseLimits, 16u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, generations, 32u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, historyGenerations, 48u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, modeAndFlags, 64u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, lightTableCounts, 80u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, validation, 96u, 16u);
ABI_V3_ASSERT_FIELD(Abi3::GpuRestirParametersV3, cameraPosition, 112u, 16u);

static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::Frame) == 0u);
static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::Scene) == 1u);
static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::TraversalBackend) == 2u);
static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::WavefrontQueues) == 3u);
static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::Reconstruction) == 4u);
static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::Restir) == 5u);
static_assert(static_cast<std::uint32_t>(Abi3::DescriptorSet::DebugProfiler) == 6u);

constexpr bool AreRestirBindingsUnique() noexcept
{
    constexpr std::uint32_t bindings[] = {
        Abi3::RestirBinding::Parameters,
        Abi3::RestirBinding::Candidates,
        Abi3::RestirBinding::ReservoirRead,
        Abi3::RestirBinding::HistoryReservoirRead,
        Abi3::RestirBinding::SurfaceCurrent,
        Abi3::RestirBinding::SurfaceHistory,
        Abi3::RestirBinding::HistoryIdentity,
        Abi3::RestirBinding::ReservoirWrite,
        Abi3::RestirBinding::DebugRecord,
        Abi3::RestirBinding::Statistics,
        Abi3::RestirBinding::VisibilityResults,
        Abi3::RestirBinding::CandidateAtCenter,
        Abi3::RestirBinding::LightTable,
        Abi3::RestirBinding::PairwiseTargetSupport,
        Abi3::RestirBinding::DirectLighting,
        Abi3::RestirBinding::ValidationReasons,
        Abi3::RestirBinding::DebugImage,
        Abi3::RestirBinding::HistoryAtCurrent,
        Abi3::RestirBinding::ReferenceTarget,
        Abi3::RestirBinding::ReferenceVisibility,
        Abi3::RestirBinding::CurrentToPreviousLightIndex,
        Abi3::RestirBinding::PreviousToCurrentLightIndex,
        Abi3::RestirBinding::NeighborIndices,
        Abi3::RestirBinding::ShadowRayQueue,
        Abi3::RestirBinding::DirectDiffuse,
        Abi3::RestirBinding::DirectSpecular,
        Abi3::RestirBinding::InitialReservoir,
        Abi3::RestirBinding::TemporalReservoir,
        Abi3::RestirBinding::SpatialReservoir,
        Abi3::RestirBinding::PublishedReservoir,
        Abi3::RestirBinding::PreviousPublishedReservoir};

    for (std::size_t i = 0u; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
    {
        for (std::size_t j = i + 1u; j < sizeof(bindings) / sizeof(bindings[0]); ++j)
        {
            if (bindings[i] == bindings[j])
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(AreRestirBindingsUnique());
static_assert(Abi3::RestirBinding::Parameters == 0u);
static_assert(Abi3::RestirBinding::ReferenceVisibility == 19u);
static_assert(Abi3::RestirBinding::CurrentToPreviousLightIndex == 20u);
static_assert(Abi3::RestirBinding::PreviousToCurrentLightIndex == 21u);
static_assert(Abi3::RestirBinding::NeighborIndices == 22u);
static_assert(Abi3::RestirBinding::ShadowRayQueue == 23u);
static_assert(Abi3::RestirBinding::DirectDiffuse == 24u);
static_assert(Abi3::RestirBinding::DirectSpecular == 25u);
static_assert(Abi3::RestirBinding::InitialReservoir == 26u);
static_assert(Abi3::RestirBinding::TemporalReservoir == 27u);
static_assert(Abi3::RestirBinding::SpatialReservoir == 28u);
static_assert(Abi3::RestirBinding::PublishedReservoir == 29u);
static_assert(Abi3::RestirBinding::PreviousPublishedReservoir == 30u);

// The v3 registry preserves the v2 set and non-ReSTIR binding numbers while
// replacing only the previously reserved set-5 ownership.
static_assert(Abi3::FrameBinding::Constants == 0u);
static_assert(Abi3::SceneBinding::Lights == 6u);
static_assert(Abi3::TraversalBinding::Rays == 1u);
static_assert(Abi3::WavefrontBinding::ShadowQueue == 4u);
static_assert(Abi3::WavefrontBinding::BounceCounters == 15u);
static_assert(Abi3::WavefrontBinding::PrimarySurfaceV2Export == 24u);
static_assert(Abi3::ReconstructionBinding::HistoryRead == 6u);
static_assert(Abi3::ReconstructionBinding::PrimarySurfaceExport == 15u);
static_assert(Abi3::DebugProfilerBinding::Constants == 0u);

#undef ABI_V3_ASSERT_FIELD
#undef ABI_V3_ASSERT_RECORD
