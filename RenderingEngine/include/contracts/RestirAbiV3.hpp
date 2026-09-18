#pragma once

#include "contracts/AbiTypesV0.hpp"
#include "contracts/AbiVersionV3.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RenderingEngine::Contracts::AbiV3
{
    using AbiFloat4 = AbiV0::AbiFloat4;
    using AbiUInt4 = AbiV0::AbiUInt4;

    enum RestirCandidateSource : std::uint32_t
    {
        RestirCandidateInvalid = 0u,
        RestirCandidateUniformLight = 1u,
        RestirCandidatePowerWeightedLight = 2u,
        RestirCandidateEmissiveTriangle = 3u,
        RestirCandidateEnvironment = 4u,
        RestirCandidateTemporalReuse = 5u,
        RestirCandidateSpatialReuse = 6u
    };

    enum RestirReuseSource : std::uint32_t
    {
        RestirReuseNone = 0u,
        RestirReuseTemporal = 1u,
        RestirReuseSpatial = 2u
    };

    // Mirrors RuntimeConfig::ShadowMethod without importing application types
    // into the versioned GPU ABI. The visibility ray budgets are part of the
    // production contract rather than an implementation-private multiplier.
    enum RestirShadowMode : std::uint32_t
    {
        RestirShadowPcf = 0u,
        RestirShadowPcss = 1u,
        RestirShadowPhysical = 2u
    };
    inline constexpr std::uint32_t kRestirPcfFilterRayCount = 8u;
    inline constexpr std::uint32_t kRestirPcssBlockerRayCount = 4u;
    inline constexpr std::uint32_t kRestirPcssFilterRayCount = 12u;
    inline constexpr std::uint32_t kRestirPcssVisibilityRayCount =
        kRestirPcssBlockerRayCount + kRestirPcssFilterRayCount;
    inline constexpr std::uint32_t kRestirPhysicalVisibilityRayCount = 1u;

    enum RestirSampleFlags : std::uint32_t
    {
        RestirSampleFlagNone = 0u,
        RestirSampleFlagValid = 1u << 0u,
        RestirSampleFlagDelta = 1u << 1u,
        RestirSampleFlagHasAreaPdf = 1u << 2u,
        RestirSampleFlagHasSolidAnglePdf = 1u << 3u
    };

    enum RestirReservoirFlags : std::uint32_t
    {
        RestirReservoirFlagNone = 0u,
        RestirReservoirFlagValid = 1u << 0u,
        RestirReservoirFlagMClamped = 1u << 1u,
        RestirReservoirFlagTemporalAccepted = 1u << 2u,
        RestirReservoirFlagSpatialAccepted = 1u << 3u,
        RestirReservoirFlagReferenceMode = 1u << 4u,
        RestirReservoirFlagFinalVisibilityEvaluated = 1u << 5u,
        RestirReservoirFlagVisibilityValid = 1u << 6u
    };

    enum RestirHistoryFlags : std::uint32_t
    {
        RestirHistoryFlagNone = 0u,
        RestirHistoryFlagValid = 1u << 0u,
        RestirHistoryFlagReset = 1u << 1u,
        RestirHistoryFlagDisoccluded = 1u << 2u,
        RestirHistoryFlagCameraCut = 1u << 3u,
        RestirHistoryFlagSceneChanged = 1u << 4u,
        RestirHistoryFlagLightSetChanged = 1u << 5u,
        RestirHistoryFlagResolutionChanged = 1u << 6u
    };

    enum RestirRejectionReason : std::uint32_t
    {
        RestirRejectNone = 0u,
        RestirRejectInvalidCandidate = 1u,
        RestirRejectReprojectionOutside = 2u,
        RestirRejectMotionInvalid = 3u,
        RestirRejectCameraCut = 4u,
        RestirRejectResize = 5u,
        RestirRejectDepth = 6u,
        RestirRejectNormal = 7u,
        RestirRejectInstance = 8u,
        RestirRejectThinGeometry = 9u,
        RestirRejectSceneGeneration = 10u,
        RestirRejectLightGeneration = 11u,
        RestirRejectAge = 12u,
        RestirRejectMaterial = 13u
    };

    // A persistent light sample is the only light sample representation that
    // crosses the abi-v3 set-5 boundary. All PDFs are non-negative float32.
    // positionDistance.xyz is the sampled light position and .w is distance.
    // directionCombinedPdf.xyz points from the surface to the light and .w is
    // the combined solid-angle PDF. radianceDiscretePdf.xyz is emitted
    // radiance and .w is the discrete light-selection PDF. conditionalPdf.x
    // is the conditional area PDF and .y is the conditional solid-angle PDF;
    // unused components are zero. metadata.x is SampleMeasure from abi-v1;
    // metadata.w is the source light-table index for current/previous mapping.
    struct alignas(16) GpuPersistentLightSampleV3
    {
        AbiFloat4 positionDistance;
        AbiFloat4 directionCombinedPdf;
        AbiFloat4 radianceDiscretePdf;
        AbiFloat4 conditionalPdf;
        AbiUInt4 identity;       // stable light, primitive, sample low/high.
        AbiUInt4 generation;     // light, scene, sample generation, reserved.
        AbiUInt4 metadata;       // measure, sample flags, light flags, light-table index.
        AbiUInt4 sourceIdentity; // source surface, material, instance, primitive.
    };

    // A candidate owns the estimator terms used by RIS. target excludes final
    // visibility, support is a binary 0/1 term, correction is finite, and
    // proposalPdf is the density of this candidate in the declared measure.
    // provenance.x/y are RestirCandidateSource/RestirReuseSource and .z is the
    // source surface index. The sample identity remains in sample.identity.
    struct alignas(16) GpuRestirCandidateV3
    {
        GpuPersistentLightSampleV3 sample;
        AbiFloat4 targetProposalSupportCorrection;
        AbiUInt4 provenance;
    };

    // Temporal history identity is explicit so a reservoir cannot be reused
    // across a different surface, scene/light generation, camera epoch, or
    // resolution epoch. frameIdentity.x/y store frame low/high; .z is the
    // logical history generation and .w is RestirHistoryFlags.
    struct alignas(16) GpuRestirHistoryIdentityV3
    {
        AbiUInt4 surfaceIdentity; // material, instance, primitive, instance generation.
        AbiUInt4 sceneIdentity;   // scene, light, camera, resolution generation.
        AbiUInt4 frameIdentity;   // frame low/high, history generation, history flags.
        AbiUInt4 reprojection;    // source pixel, source width, source height, rejection.
    };

    // Reservoir finalization uses selectedTarget and selectedProposalPdf from
    // weightState plus selectedSupport/selectedCorrection from selectedTerms.
    // weightState = (weightSum, normalizationWeight, selectedTarget,
    // selectedProposalPdf). selectedTerms = (selectedSupport,
    // selectedCorrection, selectedCandidateWeight, finalContributionWeight).
    // state = (M, age, RestirReservoirFlags, RestirRejectionReason).
    // provenance = (source, reuse source, source surface, candidate flags).
    struct alignas(16) GpuRestirReservoirV3
    {
        GpuPersistentLightSampleV3 selected;
        AbiFloat4 weightState;
        AbiFloat4 selectedTerms;
        AbiUInt4 state;
        AbiUInt4 provenance;
        GpuRestirHistoryIdentityV3 history;
    };

    // Debug is a stable, read-only projection of the selected sample and
    // reservoir state; it is not a replacement for the reservoir record.
    struct alignas(16) GpuRestirDebugV3
    {
        AbiUInt4 identity;   // light, primitive, candidate source, reuse source.
        AbiUInt4 generation; // light generation, scene generation, sample generation, reserved.
        AbiFloat4 scalar;    // weight sum, normalization weight, target, final weight.
        AbiUInt4 state;      // M, age, reservoir flags, rejection reason.
    };

    struct alignas(16) GpuRestirStatisticsV3
    {
        AbiUInt4 candidateCounts;  // generated, valid, rejected, M-clamped.
        AbiUInt4 reuseCounts;      // temporal attempts, accepted, spatial attempts, accepted.
        AbiUInt4 visibilityCounts; // submitted, evaluated, visible, invalid.
        AbiFloat4 errorMetrics;    // initial MAE, final MAE, initial RMSE, final RMSE.
    };

    // Frame-owned set-5 constants. This record is mirrored byte-for-byte in
    // RestirAbiV3.hlsli so the host cannot populate an unversioned private
    // parameter shape.
    struct alignas(16) GpuRestirParametersV3
    {
        AbiUInt4 extentAndCandidates; // width, height, pixel count, candidates/pixel.
        AbiUInt4 reuseLimits;         // neighbors, max M, max age, frames in flight.
        AbiUInt4 generations;         // scene, light set, history, frame low.
        AbiUInt4 historyGenerations;  // config/camera, resource/resolution, debug mode, shadow mode.
        AbiUInt4 modeAndFlags;        // estimator, candidate source, history valid, frame high.
        AbiUInt4 lightTableCounts;    // current, previous, reserved, reserved.
        AbiFloat4 validation;         // normal cosine, relative depth, position, thin position.
        AbiFloat4 cameraPosition;     // xyz world position, reserved.
    };

    static_assert(sizeof(GpuPersistentLightSampleV3) == 128u);
    static_assert(sizeof(GpuRestirCandidateV3) == 160u);
    static_assert(sizeof(GpuRestirHistoryIdentityV3) == 64u);
    static_assert(sizeof(GpuRestirReservoirV3) == 256u);
    static_assert(sizeof(GpuRestirDebugV3) == 64u);
    static_assert(sizeof(GpuRestirStatisticsV3) == 64u);
    static_assert(sizeof(GpuRestirParametersV3) == 128u);

    static_assert(alignof(GpuPersistentLightSampleV3) == 16u);
    static_assert(alignof(GpuRestirCandidateV3) == 16u);
    static_assert(alignof(GpuRestirHistoryIdentityV3) == 16u);
    static_assert(alignof(GpuRestirReservoirV3) == 16u);
    static_assert(alignof(GpuRestirDebugV3) == 16u);
    static_assert(alignof(GpuRestirStatisticsV3) == 16u);
    static_assert(alignof(GpuRestirParametersV3) == 16u);

    static_assert(std::is_standard_layout_v<GpuPersistentLightSampleV3>);
    static_assert(std::is_trivially_copyable_v<GpuPersistentLightSampleV3>);
    static_assert(std::is_standard_layout_v<GpuRestirCandidateV3>);
    static_assert(std::is_trivially_copyable_v<GpuRestirCandidateV3>);
    static_assert(std::is_standard_layout_v<GpuRestirHistoryIdentityV3>);
    static_assert(std::is_trivially_copyable_v<GpuRestirHistoryIdentityV3>);
    static_assert(std::is_standard_layout_v<GpuRestirReservoirV3>);
    static_assert(std::is_trivially_copyable_v<GpuRestirReservoirV3>);
    static_assert(std::is_standard_layout_v<GpuRestirDebugV3>);
    static_assert(std::is_trivially_copyable_v<GpuRestirDebugV3>);
    static_assert(std::is_standard_layout_v<GpuRestirStatisticsV3>);
    static_assert(std::is_trivially_copyable_v<GpuRestirStatisticsV3>);
    static_assert(std::is_standard_layout_v<GpuRestirParametersV3>);
    static_assert(std::is_trivially_copyable_v<GpuRestirParametersV3>);

    static_assert(offsetof(GpuPersistentLightSampleV3, positionDistance) == 0u);
    static_assert(offsetof(GpuPersistentLightSampleV3, directionCombinedPdf) == 16u);
    static_assert(offsetof(GpuPersistentLightSampleV3, radianceDiscretePdf) == 32u);
    static_assert(offsetof(GpuPersistentLightSampleV3, conditionalPdf) == 48u);
    static_assert(offsetof(GpuPersistentLightSampleV3, identity) == 64u);
    static_assert(offsetof(GpuPersistentLightSampleV3, generation) == 80u);
    static_assert(offsetof(GpuPersistentLightSampleV3, metadata) == 96u);
    static_assert(offsetof(GpuPersistentLightSampleV3, sourceIdentity) == 112u);

    static_assert(offsetof(GpuRestirCandidateV3, sample) == 0u);
    static_assert(offsetof(GpuRestirCandidateV3, targetProposalSupportCorrection) == 128u);
    static_assert(offsetof(GpuRestirCandidateV3, provenance) == 144u);

    static_assert(offsetof(GpuRestirHistoryIdentityV3, surfaceIdentity) == 0u);
    static_assert(offsetof(GpuRestirHistoryIdentityV3, sceneIdentity) == 16u);
    static_assert(offsetof(GpuRestirHistoryIdentityV3, frameIdentity) == 32u);
    static_assert(offsetof(GpuRestirHistoryIdentityV3, reprojection) == 48u);

    static_assert(offsetof(GpuRestirReservoirV3, selected) == 0u);
    static_assert(offsetof(GpuRestirReservoirV3, weightState) == 128u);
    static_assert(offsetof(GpuRestirReservoirV3, selectedTerms) == 144u);
    static_assert(offsetof(GpuRestirReservoirV3, state) == 160u);
    static_assert(offsetof(GpuRestirReservoirV3, provenance) == 176u);
    static_assert(offsetof(GpuRestirReservoirV3, history) == 192u);

    static_assert(offsetof(GpuRestirDebugV3, identity) == 0u);
    static_assert(offsetof(GpuRestirDebugV3, generation) == 16u);
    static_assert(offsetof(GpuRestirDebugV3, scalar) == 32u);
    static_assert(offsetof(GpuRestirDebugV3, state) == 48u);

    static_assert(offsetof(GpuRestirStatisticsV3, candidateCounts) == 0u);
    static_assert(offsetof(GpuRestirStatisticsV3, reuseCounts) == 16u);
    static_assert(offsetof(GpuRestirStatisticsV3, visibilityCounts) == 32u);
    static_assert(offsetof(GpuRestirStatisticsV3, errorMetrics) == 48u);

    static_assert(offsetof(GpuRestirParametersV3, extentAndCandidates) == 0u);
    static_assert(offsetof(GpuRestirParametersV3, reuseLimits) == 16u);
    static_assert(offsetof(GpuRestirParametersV3, generations) == 32u);
    static_assert(offsetof(GpuRestirParametersV3, historyGenerations) == 48u);
    static_assert(offsetof(GpuRestirParametersV3, modeAndFlags) == 64u);
    static_assert(offsetof(GpuRestirParametersV3, lightTableCounts) == 80u);
    static_assert(offsetof(GpuRestirParametersV3, validation) == 96u);
    static_assert(offsetof(GpuRestirParametersV3, cameraPosition) == 112u);
}
