#pragma once

#include "contracts/AbiV2.hpp"
#include "reconstruction/Types.hpp"

#include <cstdint>

namespace rendering::reconstruction {

struct AbiV2SurfaceInputs final {
    Float3 worldPosition{};
    Float3 geometricNormal{0.0F, 0.0F, 1.0F};
    float roughness{1.0F};
    float metallic{};
    std::uint32_t primitiveId{kInvalidStableId};
    bool frontFace{true};
};

struct SplitSignalPixel final {
    Float3 directDiffuse{};
    Float3 directSpecular{};
    Float3 indirectDiffuse{};
    Float3 indirectSpecular{};
};

struct AbiV2UnpackedGBuffer final {
    GBufferPixel gbuffer{};
    AbiV2SurfaceInputs surface{};
    SplitSignalPixel signal{};
};

[[nodiscard]] RenderingEngine::Contracts::AbiV2::GpuPrimarySurfaceV2
PackPrimarySurfaceV2(
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface) noexcept;

[[nodiscard]] RenderingEngine::Contracts::AbiV2::GpuMotionVectorV2
PackMotionVectorV2(
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface,
    Float2 currentUv) noexcept;

[[nodiscard]] RenderingEngine::Contracts::AbiV2::GpuReconstructionSignalV2
PackReconstructionSignalV2(const SplitSignalPixel& signal) noexcept;

[[nodiscard]] RenderingEngine::Contracts::AbiV2::GpuGBufferRecordV2
PackGBufferV2(
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface,
    const SplitSignalPixel& signal,
    Float2 currentUv) noexcept;

[[nodiscard]] RenderingEngine::Contracts::AbiV2::GpuHistoryMetadataV2
PackHistoryMetadataV2(
    Moments moments,
    float variance,
    std::uint32_t historyLength,
    const GBufferPixel& gbuffer,
    const AbiV2SurfaceInputs& surface,
    std::uint64_t publishedFrame,
    std::uint32_t generation,
    std::uint32_t historyFlags) noexcept;

[[nodiscard]] AbiV2UnpackedGBuffer UnpackGBufferV2(
    const RenderingEngine::Contracts::AbiV2::GpuGBufferRecordV2& value) noexcept;

} // namespace rendering::reconstruction
