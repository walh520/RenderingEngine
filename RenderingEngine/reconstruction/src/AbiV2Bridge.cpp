#include "reconstruction/AbiV2Bridge.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rendering::reconstruction {
namespace {

namespace Abi = RenderingEngine::Contracts::AbiV2;

[[nodiscard]] Abi::AbiFloat4 ToAbiFloat4(
    const Float3 value,
    const float w = 0.0F) noexcept {
    return {value.x, value.y, value.z, w};
}

[[nodiscard]] Float3 Float3From(const Abi::AbiFloat4 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] float FiniteOrZero(const float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

[[nodiscard]] Float3 FiniteOrZero(const Float3 value) noexcept {
    return IsFinite(value) ? value : Float3{};
}

[[nodiscard]] float UnitOrZero(const float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

} // namespace

Abi::GpuPrimarySurfaceV2 PackPrimarySurfaceV2(
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface) noexcept {
    Abi::GpuPrimarySurfaceV2 result{};
    const bool valid = gbuffer.valid && std::isfinite(gbuffer.linearDepth)
        && gbuffer.linearDepth >= 0.0F && IsFinite(surface.worldPosition)
        && IsFinite(surface.geometricNormal) && IsFinite(gbuffer.worldNormal);
    std::uint32_t flags = valid ? Abi::PrimarySurfaceFlagValid : Abi::PrimarySurfaceFlagNone;
    if (surface.frontFace) flags |= Abi::PrimarySurfaceFlagFrontFace;
    if (IsFinite(gbuffer.diffuseAlbedo)) flags |= Abi::PrimarySurfaceFlagHasDiffuse;
    if (IsFinite(gbuffer.specularAlbedo)) flags |= Abi::PrimarySurfaceFlagHasSpecular;

    result.worldPositionLinearDepth = ToAbiFloat4(
        FiniteOrZero(surface.worldPosition), FiniteOrZero(gbuffer.linearDepth));
    result.geometricNormalRoughness = ToAbiFloat4(
        Normalize(FiniteOrZero(surface.geometricNormal)), UnitOrZero(surface.roughness));
    result.shadingNormalMetallic = ToAbiFloat4(
        Normalize(FiniteOrZero(gbuffer.worldNormal)), UnitOrZero(surface.metallic));
    result.diffuseAlbedo = ToAbiFloat4(FiniteOrZero(gbuffer.diffuseAlbedo));
    result.specularAlbedo = ToAbiFloat4(FiniteOrZero(gbuffer.specularAlbedo));
    result.identity = {gbuffer.materialId, gbuffer.objectId, surface.primitiveId, flags};
    return result;
}

Abi::GpuMotionVectorV2 PackMotionVectorV2(
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface,
    const Float2 currentUv) noexcept {
    Abi::GpuMotionVectorV2 result{};
    const bool valid = gbuffer.valid && gbuffer.motionValid && IsFinite(gbuffer.motion)
        && IsFinite(currentUv) && std::isfinite(gbuffer.expectedPreviousLinearDepth)
        && gbuffer.expectedPreviousLinearDepth >= 0.0F;
    const Float2 motion = valid ? gbuffer.motion : Float2{};
    const Float2 safeCurrent = IsFinite(currentUv) ? currentUv : Float2{};
    const Float2 previousUv = safeCurrent + motion;
    result.motionExpectedDepth = {
        motion.x, motion.y,
        valid ? gbuffer.expectedPreviousLinearDepth : 0.0F, 0.0F};
    result.currentPreviousUv = {
        safeCurrent.x, safeCurrent.y, previousUv.x, previousUv.y};
    result.identity = {
        gbuffer.materialId, gbuffer.objectId, surface.primitiveId,
        valid ? Abi::MotionFlagValid : Abi::MotionFlagNone};
    return result;
}

Abi::GpuReconstructionSignalV2 PackReconstructionSignalV2(
    const SplitSignalPixel& signal) noexcept {
    return {
        ToAbiFloat4(FiniteOrZero(signal.directDiffuse)),
        ToAbiFloat4(FiniteOrZero(signal.directSpecular)),
        ToAbiFloat4(FiniteOrZero(signal.indirectDiffuse)),
        ToAbiFloat4(FiniteOrZero(signal.indirectSpecular))};
}

Abi::GpuGBufferRecordV2 PackGBufferV2(
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface,
    const SplitSignalPixel& signal,
    const Float2 currentUv) noexcept {
    return {
        PackPrimarySurfaceV2(gbuffer, surface),
        PackMotionVectorV2(gbuffer, surface, currentUv),
        PackReconstructionSignalV2(signal)};
}

Abi::GpuHistoryMetadataV2 PackHistoryMetadataV2(
    const Moments moments,
    const float variance,
    const std::uint32_t historyLength,
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface,
    const std::uint64_t publishedFrame,
    const std::uint32_t generation,
    const std::uint32_t historyFlags) noexcept {
    Abi::GpuHistoryMetadataV2 result{};
    result.momentsVarianceHistory = {
        FiniteOrZero(moments.first), FiniteOrZero(moments.second),
        std::max(0.0F, FiniteOrZero(variance)), static_cast<float>(historyLength)};
    result.surfaceIdentity = {
        gbuffer.materialId, gbuffer.objectId, surface.primitiveId, 0u};
    result.frameIdentity = {
        static_cast<std::uint32_t>(publishedFrame),
        static_cast<std::uint32_t>(publishedFrame >> 32u), generation, historyFlags};
    return result;
}

AbiV2UnpackedGBuffer UnpackGBufferV2(const Abi::GpuGBufferRecordV2& value) noexcept {
    AbiV2UnpackedGBuffer result{};
    result.gbuffer.linearDepth = value.primary.worldPositionLinearDepth.w;
    result.gbuffer.worldNormal = Float3From(value.primary.shadingNormalMetallic);
    result.gbuffer.diffuseAlbedo = Float3From(value.primary.diffuseAlbedo);
    result.gbuffer.specularAlbedo = Float3From(value.primary.specularAlbedo);
    result.gbuffer.expectedPreviousLinearDepth = value.motion.motionExpectedDepth.z;
    result.gbuffer.motion = {
        value.motion.motionExpectedDepth.x, value.motion.motionExpectedDepth.y};
    result.gbuffer.materialId = value.primary.identity.x;
    result.gbuffer.objectId = value.primary.identity.y;
    result.gbuffer.valid =
        (value.primary.identity.w & Abi::PrimarySurfaceFlagValid) != 0u;
    result.gbuffer.motionValid =
        (value.motion.identity.w & Abi::MotionFlagValid) != 0u;
    result.surface.worldPosition = Float3From(value.primary.worldPositionLinearDepth);
    result.surface.geometricNormal = Float3From(value.primary.geometricNormalRoughness);
    result.surface.roughness = value.primary.geometricNormalRoughness.w;
    result.surface.metallic = value.primary.shadingNormalMetallic.w;
    result.surface.primitiveId = value.primary.identity.z;
    result.surface.frontFace =
        (value.primary.identity.w & Abi::PrimarySurfaceFlagFrontFace) != 0u;
    result.signal.directDiffuse = Float3From(value.signal.directDiffuse);
    result.signal.directSpecular = Float3From(value.signal.directSpecular);
    result.signal.indirectDiffuse = Float3From(value.signal.indirectDiffuse);
    result.signal.indirectSpecular = Float3From(value.signal.indirectSpecular);
    return result;
}

} // namespace rendering::reconstruction
