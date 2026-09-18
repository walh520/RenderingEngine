#pragma once

#include "contracts/AbiV1.hpp"
#include "rt/cpu/Geometry.hpp"
#include "rt/software_gpu/SoftwareGpu.hpp"
#include "scene/CanonicalScene.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RenderingEngine::Rt::Gpu
{
    struct alignas(16) CanonicalTraversalTriangle
    {
        std::array<Contracts::AbiV1::AbiFloat4, 3> positions{};
        std::array<Contracts::AbiV1::AbiFloat4, 3> normals{};
        std::array<Contracts::AbiV1::AbiFloat4, 3> texcoords{};
        Contracts::AbiV1::AbiUInt4 identity{}; // instance, primitive, geometry, material.
        Contracts::AbiV1::AbiUInt4 metadata{}; // geometry flags, material flags, traversal ID, instance flags.
    };

    static_assert(sizeof(CanonicalTraversalTriangle) == 176u);
    static_assert(alignof(CanonicalTraversalTriangle) == 16u);

    struct CanonicalTraversalScene
    {
        std::string stableId;
        std::uint64_t fingerprint = 0u;
        std::uint32_t generation = 0u;
        std::vector<Cpu::Triangle<float>> cpuTriangles;
        std::vector<SoftwareGpu::SoftwarePrimitiveRecord> softwareBuildPrimitives;
        std::vector<CanonicalTraversalTriangle> triangles;
    };

    struct CanonicalTraversalSceneBuild
    {
        CanonicalTraversalScene scene;
        std::string error;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error.empty();
        }
    };

    // Expands canonical object-space geometry into one deterministic world-space
    // triangle stream. The L3 primitive ID is a traversal-local stable index;
    // the canonical instance/primitive/geometry/material tuple remains intact
    // in CanonicalTraversalTriangle::identity.
    [[nodiscard]] CanonicalTraversalSceneBuild BuildCanonicalTraversalScene(
        const Scene::CanonicalSceneView& source);
}
