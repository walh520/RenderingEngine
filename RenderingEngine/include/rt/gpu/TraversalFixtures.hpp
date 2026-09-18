#pragma once

#include "contracts/GpuRecordsAbiV1.hpp"
#include "scene/CanonicalScene.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace RenderingEngine::Rt::Gpu
{
    enum class AnyHitExpectationV1 : std::uint32_t
    {
        Unoccluded = 0u,
        Occluded = 1u,
        Invalid = 2u
    };

#if defined(_MSC_VER)
#pragma warning(push)
    // The fixture intentionally embeds 16-byte GPU ABI records next to
    // host-only identity fields; it is never a serialized wire record.
#pragma warning(disable: 4324)
#endif
    struct FixedHitFixtureV1
    {
        std::string_view sceneStableId;
        std::uint32_t sceneGeneration = 0u;
        std::array<Contracts::AbiV1::AbiFloat4, 3> trianglePositions{};
        std::array<std::uint32_t, 3> triangleIndices{};
        Contracts::AbiV1::AbiUInt4 triangleIdentity{}; // instance, primitive, geometry, material.
        std::array<Contracts::AbiV1::GpuRayQueueRecordV1, 3> rays{};
        std::array<Contracts::AbiV1::GpuHitQueueRecordV1, 3> hits{};
        std::array<AnyHitExpectationV1, 3> anyHit{};
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    // Stable three-query corpus over the exact Wave 1 canonical triangle: one
    // front-face hit, one miss, and one invalid ray. L4/L5/L6 may consume it
    // without importing each other or inventing another bootstrap geometry.
    [[nodiscard]] FixedHitFixtureV1 BuildFixedHitFixtureV1() noexcept;
    [[nodiscard]] bool ValidateFixedHitFixtureV1(
        const FixedHitFixtureV1& fixture,
        std::string& reason) noexcept;
}
