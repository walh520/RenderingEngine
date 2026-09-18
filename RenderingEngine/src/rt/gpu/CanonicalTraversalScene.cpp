#include "rt/gpu/CanonicalTraversalScene.hpp"

#include <cmath>
#include <limits>

namespace RenderingEngine::Rt::Gpu
{
    namespace
    {
        using AbiFloat4 = Contracts::AbiV1::AbiFloat4;
        using CpuVec3 = Cpu::Vec3<float>;

        [[nodiscard]] CpuVec3 TransformPosition(
            const Contracts::AbiV0::AbiMat4Rows& transform,
            const AbiFloat4 position) noexcept
        {
            return {
                transform.row0.x * position.x + transform.row0.y * position.y
                    + transform.row0.z * position.z + transform.row0.w,
                transform.row1.x * position.x + transform.row1.y * position.y
                    + transform.row1.z * position.z + transform.row1.w,
                transform.row2.x * position.x + transform.row2.y * position.y
                    + transform.row2.z * position.z + transform.row2.w
            };
        }

        [[nodiscard]] CpuVec3 TransformNormal(
            const Contracts::AbiV0::AbiMat4Rows& worldToObject,
            const AbiFloat4 normal) noexcept
        {
            CpuVec3 result{
                worldToObject.row0.x * normal.x + worldToObject.row1.x * normal.y
                    + worldToObject.row2.x * normal.z,
                worldToObject.row0.y * normal.x + worldToObject.row1.y * normal.y
                    + worldToObject.row2.y * normal.z,
                worldToObject.row0.z * normal.x + worldToObject.row1.z * normal.y
                    + worldToObject.row2.z * normal.z
            };
            const double lengthSquared = static_cast<double>(result.x) * result.x
                + static_cast<double>(result.y) * result.y
                + static_cast<double>(result.z) * result.z;
            if (!(lengthSquared > 0.0) || !std::isfinite(lengthSquared))
            {
                return {};
            }
            const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
            result *= inverseLength;
            return result;
        }

        [[nodiscard]] AbiFloat4 ToAbi(const CpuVec3 value, const float w) noexcept
        {
            return {value.x, value.y, value.z, w};
        }

        [[nodiscard]] SoftwareGpu::Float4 ToSoftware(
            const CpuVec3 value,
            const float w) noexcept
        {
            return {value.x, value.y, value.z, w};
        }

        [[nodiscard]] bool IsFinite(const CpuVec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    CanonicalTraversalSceneBuild BuildCanonicalTraversalScene(
        const Scene::CanonicalSceneView& source)
    {
        CanonicalTraversalSceneBuild result;
        if (source.stableId.empty() || source.fingerprint == 0u || source.generation == 0u
            || source.constants == nullptr || source.vertices.empty() || source.indices.empty()
            || source.geometries.empty() || source.instances.empty() || source.materials.empty())
        {
            result.error = "Canonical traversal expansion requires a validated, identified scene view.";
            return result;
        }

        result.scene.stableId.assign(source.stableId);
        result.scene.fingerprint = source.fingerprint;
        result.scene.generation = source.generation;
        const std::uint64_t maximumTriangleCount = source.indices.size() / 3u
            * static_cast<std::uint64_t>(source.instances.size());
        if (maximumTriangleCount > std::numeric_limits<std::uint32_t>::max())
        {
            result.error = "Canonical traversal expansion exceeds uint32 triangle identity.";
            return result;
        }
        result.scene.cpuTriangles.reserve(static_cast<std::size_t>(maximumTriangleCount));
        result.scene.softwareBuildPrimitives.reserve(static_cast<std::size_t>(maximumTriangleCount));
        result.scene.triangles.reserve(static_cast<std::size_t>(maximumTriangleCount));

        for (const Contracts::AbiV0::GpuInstanceV0& instance : source.instances)
        {
            if ((instance.metadata.w & Contracts::AbiV0::InstanceFlagVisible) == 0u)
            {
                continue;
            }
            if (instance.metadata.x >= source.geometries.size()
                || instance.metadata.y > source.geometries.size() - instance.metadata.x)
            {
                result.error = "Canonical instance geometry range is invalid during traversal expansion.";
                return result;
            }
            for (std::uint32_t localGeometry = 0u; localGeometry < instance.metadata.y;
                 ++localGeometry)
            {
                const Contracts::AbiV0::GpuGeometryV0& geometry =
                    source.geometries[instance.metadata.x + localGeometry];
                if (geometry.identity.z >= source.materials.size())
                {
                    result.error = "Canonical traversal geometry references an invalid material.";
                    return result;
                }
                const Contracts::AbiV0::GpuMaterialV0& material =
                    source.materials[geometry.identity.z];
                for (std::uint32_t indexOffset = 0u; indexOffset < geometry.indexRange.y;
                     indexOffset += 3u)
                {
                    const std::uint32_t firstIndex = geometry.indexRange.x + indexOffset;
                    if (firstIndex > source.indices.size()
                        || 3u > source.indices.size() - firstIndex)
                    {
                        result.error = "Canonical traversal triangle index range is invalid.";
                        return result;
                    }
                    std::array<std::uint32_t, 3> vertexIndices{};
                    std::array<CpuVec3, 3> positions{};
                    std::array<CpuVec3, 3> normals{};
                    for (std::uint32_t corner = 0u; corner < 3u; ++corner)
                    {
                        vertexIndices[corner] = source.indices[firstIndex + corner]
                            + geometry.indexRange.z;
                        if (vertexIndices[corner] >= source.vertices.size())
                        {
                            result.error = "Canonical traversal triangle references an invalid vertex.";
                            return result;
                        }
                        const Contracts::AbiV0::GpuVertexV0& vertex =
                            source.vertices[vertexIndices[corner]];
                        positions[corner] = TransformPosition(instance.objectToWorld, vertex.position);
                        normals[corner] = TransformNormal(instance.worldToObject, vertex.normal);
                        if (!IsFinite(positions[corner]) || !IsFinite(normals[corner]))
                        {
                            result.error = "Canonical traversal transform produced non-finite geometry.";
                            return result;
                        }
                    }

                    const std::uint32_t traversalId =
                        static_cast<std::uint32_t>(result.scene.triangles.size());
                    const std::uint32_t primitiveId = geometry.indexRange.w + indexOffset / 3u;
                    result.scene.cpuTriangles.emplace_back(
                        positions[0], positions[1], positions[2], traversalId);

                    SoftwareGpu::SoftwarePrimitiveRecord software{};
                    software.v0 = ToSoftware(positions[0], 1.0f);
                    software.v1 = ToSoftware(positions[1], 1.0f);
                    software.v2 = ToSoftware(positions[2], 1.0f);
                    software.identity = {traversalId, primitiveId, geometry.identity.x,
                        instance.metadata.z};
                    result.scene.softwareBuildPrimitives.push_back(software);

                    CanonicalTraversalTriangle triangle{};
                    for (std::uint32_t corner = 0u; corner < 3u; ++corner)
                    {
                        triangle.positions[corner] = ToAbi(positions[corner], 1.0f);
                        triangle.normals[corner] = ToAbi(normals[corner], 0.0f);
                        triangle.texcoords[corner] = source.vertices[vertexIndices[corner]].texcoord0;
                    }
                    triangle.identity = {instance.metadata.z, primitiveId,
                        geometry.identity.x, material.metadata.z};
                    triangle.metadata = {geometry.identity.w, material.metadata.y,
                        traversalId, instance.metadata.w};
                    result.scene.triangles.push_back(triangle);
                }
            }
        }
        if (result.scene.triangles.empty())
        {
            result.error = "Canonical traversal expansion produced no visible triangles.";
        }
        return result;
    }
}
