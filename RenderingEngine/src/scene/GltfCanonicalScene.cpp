#define _CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "scene/GltfCanonicalScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RenderingEngine::Scene
{
    namespace
    {
        using Contracts::AbiV0::AbiFloat4;
        using Contracts::AbiV0::AbiMat4Rows;
        using Contracts::AbiV0::AbiUInt4;

        struct Vec3
        {
            float x{};
            float y{};
            float z{};
        };

        [[nodiscard]] Vec3 operator+(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
        }

        [[nodiscard]] Vec3 operator-(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        [[nodiscard]] Vec3 operator*(const Vec3 value, const float scalar) noexcept
        {
            return {value.x * scalar, value.y * scalar, value.z * scalar};
        }

        [[nodiscard]] float Dot(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        [[nodiscard]] Vec3 Cross(const Vec3 lhs, const Vec3 rhs) noexcept
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x};
        }

        [[nodiscard]] float LengthSquared(const Vec3 value) noexcept
        {
            return Dot(value, value);
        }

        [[nodiscard]] bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFinite(const Vec3 value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const cgltf_float* values, const std::size_t count) noexcept
        {
            for (std::size_t index = 0u; index < count; ++index)
            {
                if (!IsFinite(values[index]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Vec3 Normalize(const Vec3 value, const float epsilon = 1.0e-20f) noexcept
        {
            const float lengthSquared = LengthSquared(value);
            if (!IsFinite(lengthSquared) || lengthSquared <= epsilon)
            {
                return {};
            }
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            return value * inverseLength;
        }

        [[nodiscard]] AbiFloat4 ToAbi(const Vec3 value, const float w) noexcept
        {
            return {value.x, value.y, value.z, w};
        }

        [[nodiscard]] Vec3 FromAbi(const AbiFloat4 value) noexcept
        {
            return {value.x, value.y, value.z};
        }

        struct Bounds
        {
            Vec3 minimum{
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)()};
            Vec3 maximum{
                (std::numeric_limits<float>::lowest)(),
                (std::numeric_limits<float>::lowest)(),
                (std::numeric_limits<float>::lowest)()};
            bool empty = true;
        };

        void Expand(Bounds& bounds, const Vec3 point) noexcept
        {
            bounds.minimum.x = (std::min)(bounds.minimum.x, point.x);
            bounds.minimum.y = (std::min)(bounds.minimum.y, point.y);
            bounds.minimum.z = (std::min)(bounds.minimum.z, point.z);
            bounds.maximum.x = (std::max)(bounds.maximum.x, point.x);
            bounds.maximum.y = (std::max)(bounds.maximum.y, point.y);
            bounds.maximum.z = (std::max)(bounds.maximum.z, point.z);
            bounds.empty = false;
        }

        [[nodiscard]] bool IsValid(const Bounds& bounds) noexcept
        {
            return !bounds.empty && IsFinite(bounds.minimum) && IsFinite(bounds.maximum)
                && bounds.minimum.x <= bounds.maximum.x
                && bounds.minimum.y <= bounds.maximum.y
                && bounds.minimum.z <= bounds.maximum.z;
        }

        [[nodiscard]] AbiMat4Rows IdentityMatrix() noexcept
        {
            return {
                {1.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}};
        }

        [[nodiscard]] Vec3 TransformPoint(const AbiMat4Rows& matrix, const Vec3 point) noexcept
        {
            return {
                matrix.row0.x * point.x + matrix.row0.y * point.y
                    + matrix.row0.z * point.z + matrix.row0.w,
                matrix.row1.x * point.x + matrix.row1.y * point.y
                    + matrix.row1.z * point.z + matrix.row1.w,
                matrix.row2.x * point.x + matrix.row2.y * point.y
                    + matrix.row2.z * point.z + matrix.row2.w};
        }

        [[nodiscard]] Vec3 TransformVector(const AbiMat4Rows& matrix, const Vec3 vector) noexcept
        {
            return {
                matrix.row0.x * vector.x + matrix.row0.y * vector.y + matrix.row0.z * vector.z,
                matrix.row1.x * vector.x + matrix.row1.y * vector.y + matrix.row1.z * vector.z,
                matrix.row2.x * vector.x + matrix.row2.y * vector.y + matrix.row2.z * vector.z};
        }

        [[nodiscard]] float Determinant3x3(const AbiMat4Rows& matrix) noexcept
        {
            return matrix.row0.x * (matrix.row1.y * matrix.row2.z - matrix.row1.z * matrix.row2.y)
                - matrix.row0.y * (matrix.row1.x * matrix.row2.z - matrix.row1.z * matrix.row2.x)
                + matrix.row0.z * (matrix.row1.x * matrix.row2.y - matrix.row1.y * matrix.row2.x);
        }

        [[nodiscard]] bool IsRigidTransform(const AbiMat4Rows& matrix) noexcept
        {
            constexpr float kTolerance = 1.0e-4f;
            const Vec3 xAxis{matrix.row0.x, matrix.row1.x, matrix.row2.x};
            const Vec3 yAxis{matrix.row0.y, matrix.row1.y, matrix.row2.y};
            const Vec3 zAxis{matrix.row0.z, matrix.row1.z, matrix.row2.z};
            return IsFinite(matrix.row0.x) && IsFinite(matrix.row0.y) && IsFinite(matrix.row0.z)
                && IsFinite(matrix.row0.w) && IsFinite(matrix.row1.x) && IsFinite(matrix.row1.y)
                && IsFinite(matrix.row1.z) && IsFinite(matrix.row1.w) && IsFinite(matrix.row2.x)
                && IsFinite(matrix.row2.y) && IsFinite(matrix.row2.z) && IsFinite(matrix.row2.w)
                && IsFinite(matrix.row3.x) && IsFinite(matrix.row3.y) && IsFinite(matrix.row3.z)
                && IsFinite(matrix.row3.w)
                && std::abs(matrix.row3.x) <= kTolerance
                && std::abs(matrix.row3.y) <= kTolerance
                && std::abs(matrix.row3.z) <= kTolerance
                && std::abs(matrix.row3.w - 1.0f) <= kTolerance
                && std::abs(LengthSquared(xAxis) - 1.0f) <= kTolerance
                && std::abs(LengthSquared(yAxis) - 1.0f) <= kTolerance
                && std::abs(LengthSquared(zAxis) - 1.0f) <= kTolerance
                && std::abs(Dot(xAxis, yAxis)) <= kTolerance
                && std::abs(Dot(xAxis, zAxis)) <= kTolerance
                && std::abs(Dot(yAxis, zAxis)) <= kTolerance
                && std::abs(Determinant3x3(matrix) - 1.0f) <= 3.0e-4f;
        }

        [[nodiscard]] AbiMat4Rows ToAbiMatrix(const cgltf_float* matrix) noexcept
        {
            return {
                {matrix[0], matrix[4], matrix[8], matrix[12]},
                {matrix[1], matrix[5], matrix[9], matrix[13]},
                {matrix[2], matrix[6], matrix[10], matrix[14]},
                {matrix[3], matrix[7], matrix[11], matrix[15]}};
        }

        [[nodiscard]] AbiMat4Rows InverseRigid(const AbiMat4Rows& matrix) noexcept
        {
            return {
                {matrix.row0.x, matrix.row1.x, matrix.row2.x,
                 -(matrix.row0.x * matrix.row0.w + matrix.row1.x * matrix.row1.w
                     + matrix.row2.x * matrix.row2.w)},
                {matrix.row0.y, matrix.row1.y, matrix.row2.y,
                 -(matrix.row0.y * matrix.row0.w + matrix.row1.y * matrix.row1.w
                     + matrix.row2.y * matrix.row2.w)},
                {matrix.row0.z, matrix.row1.z, matrix.row2.z,
                 -(matrix.row0.z * matrix.row0.w + matrix.row1.z * matrix.row1.w
                     + matrix.row2.z * matrix.row2.w)},
                {0.0f, 0.0f, 0.0f, 1.0f}};
        }

        [[nodiscard]] Vec3 PickOrthogonalTangent(const Vec3 normal) noexcept
        {
            const Vec3 reference = std::abs(normal.x) <= std::abs(normal.y)
                && std::abs(normal.x) <= std::abs(normal.z)
                ? Vec3{1.0f, 0.0f, 0.0f}
                : (std::abs(normal.y) <= std::abs(normal.z)
                        ? Vec3{0.0f, 1.0f, 0.0f}
                        : Vec3{0.0f, 0.0f, 1.0f});
            return Normalize(Cross(reference, normal));
        }

        [[nodiscard]] GltfCanonicalSceneLoadResult Failure(
            const GltfCanonicalSceneError error,
            std::string reason)
        {
            return {{}, error, std::move(reason)};
        }

        template <typename T>
        [[nodiscard]] const T* FindPointer(
            const T* values,
            const cgltf_size count,
            const T* value) noexcept
        {
            if (value == nullptr)
            {
                return nullptr;
            }
            for (cgltf_size index = 0u; index < count; ++index)
            {
                if (values + index == value)
                {
                    return value;
                }
            }
            return nullptr;
        }

        template <typename T>
        [[nodiscard]] std::size_t PointerIndex(
            const T* values,
            const cgltf_size count,
            const T* value) noexcept
        {
            if (const T* found = FindPointer(values, count, value); found != nullptr)
            {
                return static_cast<std::size_t>(found - values);
            }
            return (std::numeric_limits<std::size_t>::max)();
        }

        [[nodiscard]] bool IsStableToken(const std::string_view value) noexcept
        {
            return !value.empty()
                && std::all_of(value.begin(), value.end(), [](const char character)
                {
                    const auto byte = static_cast<unsigned char>(character);
                    return byte >= 0x20u && byte != 0x7fu;
                });
        }

        [[nodiscard]] bool ReadFloats(
            const cgltf_accessor* accessor,
            const cgltf_type expectedType,
            const std::size_t expectedComponents,
            std::vector<float>& output) noexcept
        {
            if (accessor == nullptr || accessor->type != expectedType || accessor->count == 0u
                || cgltf_num_components(accessor->type) != expectedComponents
                || accessor->count > (std::numeric_limits<std::size_t>::max)() / expectedComponents)
            {
                return false;
            }
            output.assign(
                static_cast<std::size_t>(accessor->count) * expectedComponents,
                0.0f);
            const cgltf_size readCount = cgltf_accessor_unpack_floats(
                accessor,
                output.data(),
                static_cast<cgltf_size>(output.size()));
            if (readCount != output.size())
            {
                return false;
            }
            return IsFinite(output.data(), output.size());
        }

        [[nodiscard]] bool ReadIndices(
            const cgltf_accessor* accessor,
            std::vector<std::uint32_t>& output) noexcept
        {
            if (accessor == nullptr || accessor->type != cgltf_type_scalar
                || accessor->count == 0u || accessor->count % 3u != 0u
                || accessor->is_sparse
                || (accessor->component_type != cgltf_component_type_r_8u
                    && accessor->component_type != cgltf_component_type_r_16u
                    && accessor->component_type != cgltf_component_type_r_32u)
                || accessor->count > (std::numeric_limits<std::size_t>::max)())
            {
                return false;
            }
            output.resize(static_cast<std::size_t>(accessor->count));
            for (cgltf_size index = 0u; index < accessor->count; ++index)
            {
                const cgltf_size value = cgltf_accessor_read_index(accessor, index);
                if (value > (std::numeric_limits<std::uint32_t>::max)())
                {
                    return false;
                }
                output[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(value);
            }
            return true;
        }

        void ExpandTransformedBounds(
            Bounds& output,
            const Bounds& local,
            const AbiMat4Rows& transform) noexcept
        {
            const std::array<Vec3, 8> corners{
                Vec3{local.minimum.x, local.minimum.y, local.minimum.z},
                Vec3{local.minimum.x, local.minimum.y, local.maximum.z},
                Vec3{local.minimum.x, local.maximum.y, local.minimum.z},
                Vec3{local.minimum.x, local.maximum.y, local.maximum.z},
                Vec3{local.maximum.x, local.minimum.y, local.minimum.z},
                Vec3{local.maximum.x, local.minimum.y, local.maximum.z},
                Vec3{local.maximum.x, local.maximum.y, local.minimum.z},
                Vec3{local.maximum.x, local.maximum.y, local.maximum.z}};
            for (const Vec3 corner : corners)
            {
                Expand(output, TransformPoint(transform, corner));
            }
        }

        [[nodiscard]] std::string DefaultStableId(const std::filesystem::path& path)
        {
            const std::string stem = path.stem().string();
            return stem.empty() ? std::string{"gltf-scene"} : stem;
        }
    }

    GltfCanonicalSceneLoadResult LoadGltfCanonicalScene(
        const std::filesystem::path& path,
        GltfCanonicalSceneLoadOptions options)
    {
        if (path.empty())
        {
            return Failure(GltfCanonicalSceneError::FileNotFound, "glTF path is empty.");
        }
        if (options.generation == 0u)
        {
            return Failure(GltfCanonicalSceneError::InvalidIdentity,
                "Canonical glTF generation must be non-zero.");
        }

        std::string stableId = options.stableId.empty() ? DefaultStableId(path) : std::move(options.stableId);
        if (!IsStableToken(stableId))
        {
            return Failure(GltfCanonicalSceneError::InvalidIdentity,
                "Canonical glTF stable ID must be a non-empty printable token.");
        }

        cgltf_options parserOptions{};
        cgltf_data* rawData = nullptr;
        const std::string nativePath = path.string();
        const cgltf_result parseResult = cgltf_parse_file(
            &parserOptions,
            nativePath.c_str(),
            &rawData);
        if (parseResult != cgltf_result_success || rawData == nullptr)
        {
            return Failure(
                parseResult == cgltf_result_file_not_found
                    ? GltfCanonicalSceneError::FileNotFound
                    : GltfCanonicalSceneError::ParseFailed,
                "cgltf could not parse the glTF file.");
        }
        struct DataGuard
        {
            cgltf_data* data{};
            ~DataGuard()
            {
                if (data != nullptr)
                {
                    cgltf_free(data);
                }
            }
        } dataGuard{rawData};

        if (cgltf_load_buffers(&parserOptions, rawData, nativePath.c_str()) != cgltf_result_success)
        {
            return Failure(GltfCanonicalSceneError::BufferLoadFailed,
                "cgltf could not load one or more glTF buffers.");
        }
        if (cgltf_validate(rawData) != cgltf_result_success)
        {
            return Failure(GltfCanonicalSceneError::InvalidStructure,
                "cgltf validation rejected the glTF structure.");
        }
        if (rawData->meshes_count > (std::numeric_limits<std::uint32_t>::max)()
            || rawData->materials_count > (std::numeric_limits<std::uint32_t>::max)()
            || rawData->nodes_count > (std::numeric_limits<std::uint32_t>::max)())
        {
            return Failure(GltfCanonicalSceneError::InvalidCount,
                "glTF object counts exceed the canonical uint32 ABI capacity.");
        }
        if (rawData->scene == nullptr || rawData->scene->nodes_count == 0u)
        {
            return Failure(GltfCanonicalSceneError::InvalidStructure,
                "The glTF must select a non-empty default scene.");
        }
        if (rawData->animations_count != 0u || rawData->skins_count != 0u)
        {
            return Failure(GltfCanonicalSceneError::UnsupportedFeature,
                "Animated and skinned glTF content is not accepted by the static loader.");
        }

        for (cgltf_size meshIndex = 0u; meshIndex < rawData->meshes_count; ++meshIndex)
        {
            const cgltf_mesh& mesh = rawData->meshes[meshIndex];
            if (mesh.weights_count != 0u || mesh.target_names_count != 0u)
            {
                return Failure(GltfCanonicalSceneError::UnsupportedFeature,
                    "Mesh morph weights and targets are not accepted by the static loader.");
            }
            if (mesh.primitives_count == 0u)
            {
                return Failure(GltfCanonicalSceneError::InvalidStructure,
                    "Every glTF mesh must contain at least one primitive.");
            }
            if (mesh.primitives_count > (std::numeric_limits<std::uint32_t>::max)())
            {
                return Failure(GltfCanonicalSceneError::InvalidCount,
                    "glTF geometry count exceeds the canonical uint32 ABI capacity.");
            }
            for (cgltf_size primitiveIndex = 0u;
                 primitiveIndex < mesh.primitives_count;
                 ++primitiveIndex)
            {
                const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
                if (primitive.type != cgltf_primitive_type_triangles)
                {
                    return Failure(GltfCanonicalSceneError::UnsupportedFeature,
                        "Only glTF TRIANGLES primitives are accepted.");
                }
                if (primitive.targets_count != 0u || primitive.has_draco_mesh_compression
                    || primitive.mappings_count != 0u)
                {
                    return Failure(GltfCanonicalSceneError::UnsupportedFeature,
                        "Morph, Draco, and material-variant primitives are not accepted.");
                }
            }
        }
        for (cgltf_size materialIndex = 0u; materialIndex < rawData->materials_count; ++materialIndex)
        {
            const cgltf_material& material = rawData->materials[materialIndex];
            if (material.alpha_mode == cgltf_alpha_mode_blend)
            {
                return Failure(GltfCanonicalSceneError::UnsupportedFeature,
                    "glTF alpha BLEND materials are not accepted by the canonical scene ABI.");
            }
            if (material.has_pbr_specular_glossiness || material.unlit)
            {
                return Failure(GltfCanonicalSceneError::UnsupportedFeature,
                    "Only metallic-roughness glTF materials are accepted.");
            }
        }

        CanonicalScene scene{};
        scene.stableId = stableId;
        scene.generation = options.generation;

        std::vector<std::uint32_t> meshFirstGeometry(rawData->meshes_count, kInvalidId);
        std::vector<std::uint32_t> meshGeometryCount(rawData->meshes_count, 0u);
        std::uint32_t defaultMaterialId = kInvalidId;
        const auto EnsureDefaultMaterial = [&]() -> std::uint32_t
        {
            if (defaultMaterialId != kInvalidId)
            {
                return defaultMaterialId;
            }
            defaultMaterialId = static_cast<std::uint32_t>(scene.materials.size());
            scene.materials.push_back({
                {1.0f, 1.0f, 1.0f, 1.0f},
                {0.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 1.0f, 0.5f},
                {0.0f, 1.5f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f, 0.0f},
                {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                {MaterialModelMetallicRoughness, MaterialFlagNone, defaultMaterialId, 0u}});
            return defaultMaterialId;
        };

        for (cgltf_size materialIndex = 0u; materialIndex < rawData->materials_count; ++materialIndex)
        {
            const cgltf_material& source = rawData->materials[materialIndex];
            const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
            if (!IsFinite(pbr.base_color_factor, 4u)
                || !IsFinite(source.emissive_factor, 3u)
                || !IsFinite(pbr.metallic_factor) || !IsFinite(pbr.roughness_factor)
                || !IsFinite(source.alpha_cutoff)
                || (source.has_emissive_strength && !IsFinite(source.emissive_strength.emissive_strength)))
            {
                return Failure(GltfCanonicalSceneError::InvalidNumericValue,
                    "glTF material factors must be finite.");
            }
            const std::uint32_t materialId = static_cast<std::uint32_t>(scene.materials.size());
            std::uint32_t flags = MaterialFlagNone;
            if (source.double_sided)
            {
                flags |= MaterialFlagDoubleSided;
            }
            if (source.alpha_mode == cgltf_alpha_mode_mask)
            {
                flags |= MaterialFlagAlphaMask;
                scene.constants.versionFlags.z |= SceneFlagHasAlphaMask;
            }
            const float emissiveStrength = source.has_emissive_strength
                ? source.emissive_strength.emissive_strength
                : 1.0f;
            if (source.emissive_factor[0] != 0.0f || source.emissive_factor[1] != 0.0f
                || source.emissive_factor[2] != 0.0f)
            {
                flags |= MaterialFlagEmissive;
            }
            scene.materials.push_back({
                {pbr.base_color_factor[0], pbr.base_color_factor[1],
                 pbr.base_color_factor[2], pbr.base_color_factor[3]},
                {source.emissive_factor[0], source.emissive_factor[1],
                 source.emissive_factor[2], emissiveStrength},
                {pbr.metallic_factor, pbr.roughness_factor, 1.0f, source.alpha_cutoff},
                {0.0f, 1.5f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f, 0.0f},
                {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                {kInvalidId, kInvalidId, kInvalidId, kInvalidId},
                {MaterialModelMetallicRoughness, flags, materialId, 0u}});
        }

        std::vector<std::uint32_t> geometryMaterialIds;
        geometryMaterialIds.reserve(rawData->meshes_count);
        std::vector<Bounds> localGeometryBounds;
        for (cgltf_size meshIndex = 0u; meshIndex < rawData->meshes_count; ++meshIndex)
        {
            const cgltf_mesh& mesh = rawData->meshes[meshIndex];
            if (scene.geometries.size() > (std::numeric_limits<std::uint32_t>::max)()
                - static_cast<std::size_t>(mesh.primitives_count))
            {
                return Failure(GltfCanonicalSceneError::InvalidCount,
                    "glTF geometry count exceeds the canonical uint32 ABI capacity.");
            }
            meshFirstGeometry[meshIndex] = static_cast<std::uint32_t>(scene.geometries.size());
            meshGeometryCount[meshIndex] = static_cast<std::uint32_t>(mesh.primitives_count);
            for (cgltf_size primitiveIndex = 0u;
                 primitiveIndex < mesh.primitives_count;
                 ++primitiveIndex)
            {
                const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
                const cgltf_accessor* positionAccessor = cgltf_find_accessor(
                    &primitive, cgltf_attribute_type_position, 0);
                if (positionAccessor == nullptr)
                {
                    return Failure(GltfCanonicalSceneError::InvalidStructure,
                        "Every glTF triangle primitive must provide POSITION.");
                }
                std::vector<float> positions;
                if (!ReadFloats(positionAccessor, cgltf_type_vec3, 3u, positions)
                    || positionAccessor->count > (std::numeric_limits<std::uint32_t>::max)())
                {
                    return Failure(GltfCanonicalSceneError::InvalidStructure,
                        "glTF POSITION accessor is missing, malformed, or too large.");
                }
                const std::size_t vertexCount = static_cast<std::size_t>(positionAccessor->count);
                std::vector<float> normals;
                const cgltf_accessor* normalAccessor = cgltf_find_accessor(
                    &primitive, cgltf_attribute_type_normal, 0);
                if (normalAccessor != nullptr
                    && (!ReadFloats(normalAccessor, cgltf_type_vec3, 3u, normals)
                        || normalAccessor->count != positionAccessor->count))
                {
                    return Failure(GltfCanonicalSceneError::InvalidStructure,
                        "glTF NORMAL accessor must be a finite VEC3 per POSITION.");
                }
                std::vector<float> tangents;
                const cgltf_accessor* tangentAccessor = cgltf_find_accessor(
                    &primitive, cgltf_attribute_type_tangent, 0);
                if (tangentAccessor != nullptr
                    && (!ReadFloats(tangentAccessor, cgltf_type_vec4, 4u, tangents)
                        || tangentAccessor->count != positionAccessor->count))
                {
                    return Failure(GltfCanonicalSceneError::InvalidStructure,
                        "glTF TANGENT accessor must be a finite VEC4 per POSITION.");
                }
                std::vector<float> texcoords;
                const cgltf_accessor* texcoordAccessor = cgltf_find_accessor(
                    &primitive, cgltf_attribute_type_texcoord, 0);
                if (texcoordAccessor != nullptr
                    && (!ReadFloats(texcoordAccessor, cgltf_type_vec2, 2u, texcoords)
                        || texcoordAccessor->count != positionAccessor->count))
                {
                    return Failure(GltfCanonicalSceneError::InvalidStructure,
                        "glTF TEXCOORD_0 accessor must be a finite VEC2 per POSITION.");
                }

                std::vector<std::uint32_t> primitiveIndices;
                if (primitive.indices != nullptr)
                {
                    if (!ReadIndices(primitive.indices, primitiveIndices))
                    {
                        return Failure(GltfCanonicalSceneError::InvalidIndex,
                            "glTF indices must be scalar unsigned 8/16/32-bit TRIANGLES.");
                    }
                }
                else
                {
                    if (vertexCount == 0u || vertexCount % 3u != 0u
                        || vertexCount > (std::numeric_limits<std::uint32_t>::max)())
                    {
                        return Failure(GltfCanonicalSceneError::InvalidIndex,
                            "Non-indexed glTF TRIANGLES require a positive multiple of three vertices.");
                    }
                    primitiveIndices.resize(vertexCount);
                    for (std::size_t index = 0u; index < vertexCount; ++index)
                    {
                        primitiveIndices[index] = static_cast<std::uint32_t>(index);
                    }
                }
                if (primitiveIndices.empty() || primitiveIndices.size() % 3u != 0u)
                {
                    return Failure(GltfCanonicalSceneError::InvalidIndex,
                        "glTF TRIANGLES index count must be a positive multiple of three.");
                }

                Bounds localBounds{};
                const std::uint32_t firstIndex = static_cast<std::uint32_t>(scene.indices.size());
                const std::uint32_t firstPrimitiveId = static_cast<std::uint32_t>(
                    scene.indices.size() / 3u);
                for (std::size_t index = 0u; index < primitiveIndices.size(); ++index)
                {
                    const std::uint32_t sourceIndex = primitiveIndices[index];
                    if (sourceIndex >= vertexCount)
                    {
                        return Failure(GltfCanonicalSceneError::InvalidIndex,
                            "A glTF primitive index is outside POSITION.");
                    }
                    const Vec3 position{
                        positions[sourceIndex * 3u],
                        positions[sourceIndex * 3u + 1u],
                        positions[sourceIndex * 3u + 2u]};
                    Expand(localBounds, position);
                }
                for (std::size_t triangle = 0u; triangle < primitiveIndices.size(); triangle += 3u)
                {
                    const auto ReadPosition = [&](const std::size_t index)
                    {
                        return Vec3{
                            positions[primitiveIndices[index] * 3u],
                            positions[primitiveIndices[index] * 3u + 1u],
                            positions[primitiveIndices[index] * 3u + 2u]};
                    };
                    const Vec3 a = ReadPosition(triangle);
                    const Vec3 b = ReadPosition(triangle + 1u);
                    const Vec3 c = ReadPosition(triangle + 2u);
                    const Vec3 faceNormal = Cross(b - a, c - a);
                    if (!IsFinite(faceNormal) || LengthSquared(faceNormal) <= 1.0e-20f)
                    {
                        return Failure(GltfCanonicalSceneError::DegenerateTriangle,
                            "glTF TRIANGLES must not contain degenerate faces.");
                    }
                }
                if (!IsValid(localBounds)
                    || scene.indices.size() > (std::numeric_limits<std::uint32_t>::max)() - primitiveIndices.size()
                    || scene.vertices.size() > (std::numeric_limits<std::uint32_t>::max)() - vertexCount)
                {
                    return Failure(GltfCanonicalSceneError::InvalidCount,
                        "glTF canonical arrays exceed uint32 ABI capacity.");
                }

                const std::uint32_t materialId = primitive.material == nullptr
                    ? EnsureDefaultMaterial()
                    : static_cast<std::uint32_t>(PointerIndex(
                        rawData->materials, rawData->materials_count, primitive.material));
                if (materialId == kInvalidId || materialId >= scene.materials.size())
                {
                    return Failure(GltfCanonicalSceneError::InvalidStructure,
                        "A glTF primitive refers to an invalid material.");
                }
                const GpuMaterialV0& material = scene.materials[materialId];
                std::uint32_t geometryFlags =
                    (material.metadata.y & MaterialFlagAlphaMask) != 0u
                        ? GeometryFlagAlphaMask
                        : GeometryFlagOpaque;
                if ((material.metadata.y & MaterialFlagDoubleSided) != 0u)
                {
                    geometryFlags |= GeometryFlagDoubleSided;
                }
                const std::uint32_t geometryId = static_cast<std::uint32_t>(scene.geometries.size());
                scene.geometries.push_back({
                    {firstIndex, static_cast<std::uint32_t>(primitiveIndices.size()), 0u, firstPrimitiveId},
                    {geometryId, static_cast<std::uint32_t>(meshIndex), materialId, geometryFlags},
                    {localBounds.minimum.x, localBounds.minimum.y, localBounds.minimum.z, 0.0f},
                    {localBounds.maximum.x, localBounds.maximum.y, localBounds.maximum.z, 0.0f}});
                geometryMaterialIds.push_back(materialId);
                localGeometryBounds.push_back(localBounds);
                const std::uint32_t firstVertex = static_cast<std::uint32_t>(scene.vertices.size());
                std::vector<Vec3> generatedNormals(vertexCount);
                if (normals.empty())
                {
                    for (std::size_t triangle = 0u; triangle < primitiveIndices.size(); triangle += 3u)
                    {
                        const auto ReadPosition = [&](const std::size_t index)
                        {
                            return Vec3{
                                positions[primitiveIndices[index] * 3u],
                                positions[primitiveIndices[index] * 3u + 1u],
                                positions[primitiveIndices[index] * 3u + 2u]};
                        };
                        const Vec3 face = Cross(
                            ReadPosition(triangle + 1u) - ReadPosition(triangle),
                            ReadPosition(triangle + 2u) - ReadPosition(triangle));
                        generatedNormals[primitiveIndices[triangle]] =
                            generatedNormals[primitiveIndices[triangle]] + face;
                        generatedNormals[primitiveIndices[triangle + 1u]] =
                            generatedNormals[primitiveIndices[triangle + 1u]] + face;
                        generatedNormals[primitiveIndices[triangle + 2u]] =
                            generatedNormals[primitiveIndices[triangle + 2u]] + face;
                    }
                }

                for (std::size_t vertex = 0u; vertex < vertexCount; ++vertex)
                {
                    const Vec3 position{
                        positions[vertex * 3u], positions[vertex * 3u + 1u], positions[vertex * 3u + 2u]};
                    Vec3 normal{};
                    if (!normals.empty())
                    {
                        normal = Normalize({normals[vertex * 3u], normals[vertex * 3u + 1u], normals[vertex * 3u + 2u]});
                        if (LengthSquared(normal) <= 0.5f)
                        {
                            return Failure(GltfCanonicalSceneError::InvalidNumericValue,
                                "glTF NORMAL vectors must be finite and non-zero.");
                        }
                    }
                    else
                    {
                        normal = Normalize(generatedNormals[vertex]);
                        if (LengthSquared(normal) <= 0.5f)
                        {
                            normal = {0.0f, 0.0f, 1.0f};
                        }
                    }
                    Vec3 tangent{};
                    float tangentW = 1.0f;
                    if (!tangents.empty())
                    {
                        tangent = Normalize({
                            tangents[vertex * 4u], tangents[vertex * 4u + 1u], tangents[vertex * 4u + 2u]});
                        tangent = Normalize(tangent - normal * Dot(normal, tangent));
                        tangentW = tangents[vertex * 4u + 3u] < 0.0f ? -1.0f : 1.0f;
                        if (LengthSquared(tangent) <= 0.5f || !IsFinite(tangents[vertex * 4u + 3u]))
                        {
                            return Failure(GltfCanonicalSceneError::InvalidNumericValue,
                                "glTF TANGENT vectors must be finite and non-zero.");
                        }
                    }
                    else
                    {
                        tangent = PickOrthogonalTangent(normal);
                    }
                    if (LengthSquared(tangent) <= 0.5f)
                    {
                        return Failure(GltfCanonicalSceneError::InvalidNumericValue,
                            "glTF tangent construction produced a zero vector.");
                    }
                    const float u = texcoords.empty() ? 0.0f : texcoords[vertex * 2u];
                    const float v = texcoords.empty() ? 0.0f : texcoords[vertex * 2u + 1u];
                    scene.vertices.push_back({
                        ToAbi(position, 1.0f), ToAbi(normal, 0.0f), ToAbi(tangent, tangentW),
                        {u, v, 0.0f, 0.0f}});
                }
                for (const std::uint32_t sourceIndex : primitiveIndices)
                {
                    scene.indices.push_back(firstVertex + sourceIndex);
                }
            }
        }

        std::vector<std::uint8_t> visitedNodes(rawData->nodes_count, 0u);
        Bounds sceneBounds{};
        std::uint32_t nextInstanceId = 0u;
        std::uint32_t nextLightId = 0u;
        const auto NodeIndex = [&](const cgltf_node* node) -> std::size_t
        {
            return PointerIndex(rawData->nodes, rawData->nodes_count, node);
        };
        const auto AddCamera = [&](const cgltf_node& node, const AbiMat4Rows& world) -> bool
        {
            if (node.camera == nullptr)
            {
                return true;
            }
            const std::size_t nodeIndex = NodeIndex(&node);
            const Vec3 eye{world.row0.w, world.row1.w, world.row2.w};
            const Vec3 forward = TransformVector(world, {0.0f, 0.0f, -1.0f});
            const Vec3 up = TransformVector(world, {0.0f, 1.0f, 0.0f});
            const float fovDegrees = node.camera->type == cgltf_camera_type_perspective
                ? node.camera->data.perspective.yfov * 180.0f / 3.14159265358979323846f
                : 52.0f;
            if (!IsFinite(eye) || !IsFinite(forward) || !IsFinite(up)
                || !IsFinite(fovDegrees) || fovDegrees <= 0.0f || fovDegrees >= 180.0f
                || (node.camera->type == cgltf_camera_type_perspective
                    && (!IsFinite(node.camera->data.perspective.znear)
                        || node.camera->data.perspective.znear <= 0.0f))
                || (node.camera->type == cgltf_camera_type_orthographic
                    && (!IsFinite(node.camera->data.orthographic.xmag)
                        || !IsFinite(node.camera->data.orthographic.ymag)
                        || !IsFinite(node.camera->data.orthographic.znear)
                        || !IsFinite(node.camera->data.orthographic.zfar)
                        || node.camera->data.orthographic.xmag <= 0.0f
                        || node.camera->data.orthographic.ymag <= 0.0f))
                || node.camera->type == cgltf_camera_type_invalid)
            {
                return false;
            }
            scene.cameras.push_back({
                "camera:" + scene.stableId + ":" + std::to_string(nodeIndex),
                ToAbi(eye, 1.0f),
                ToAbi(eye + Normalize(forward), 1.0f),
                ToAbi(Normalize(up), 0.0f),
                fovDegrees});
            return true;
        };
        const auto AddPunctualLight = [&](const cgltf_node& node,
                                          const AbiMat4Rows& world,
                                          const std::uint32_t instanceId) -> bool
        {
            if (node.light == nullptr)
            {
                return true;
            }
            const cgltf_light& source = *node.light;
            if (!IsFinite(source.color, 3u) || !IsFinite(source.intensity)
                || !IsFinite(source.range) || !IsFinite(source.spot_outer_cone_angle)
                || source.intensity < 0.0f || source.range < 0.0f
                || (source.type != cgltf_light_type_directional
                    && source.type != cgltf_light_type_point
                    && source.type != cgltf_light_type_spot)
                || (source.type == cgltf_light_type_spot
                    && (!IsFinite(source.spot_inner_cone_angle)
                        || source.spot_inner_cone_angle < 0.0f
                        || source.spot_outer_cone_angle < source.spot_inner_cone_angle
                        || source.spot_outer_cone_angle > 1.5707964f)))
            {
                return false;
            }
            const Vec3 position{world.row0.w, world.row1.w, world.row2.w};
            const Vec3 direction = Normalize(TransformVector(world, {0.0f, 0.0f, -1.0f}));
            const float range = source.range > 0.0f && IsFinite(source.range) ? source.range : 0.0f;
            const float outerCos = source.type == cgltf_light_type_spot
                ? std::cos(source.spot_outer_cone_angle)
                : -1.0f;
            const std::uint32_t lightType = source.type == cgltf_light_type_directional
                ? LightTypeDirectional
                : (source.type == cgltf_light_type_spot ? LightTypeSpot : LightTypePoint);
            scene.lights.push_back({
                {position.x, position.y, position.z, range},
                {direction.x, direction.y, direction.z, outerCos},
                {source.color[0] * source.intensity, source.color[1] * source.intensity,
                 source.color[2] * source.intensity, 1.0f},
                {0.0f, 0.0f, 0.0f, 0.0f},
                {lightType, nextLightId++, instanceId, kInvalidId},
                {kInvalidId, LightFlagEnabled | LightFlagDelta, 0u, 0u}});
            return true;
        };
        const auto AddEmissiveLights = [&](const cgltf_node& node,
                                           const AbiMat4Rows& world,
                                           const std::uint32_t instanceId)
        {
            if (node.mesh == nullptr)
            {
                return;
            }
            const std::size_t meshIndex = PointerIndex(rawData->meshes, rawData->meshes_count, node.mesh);
            if (meshIndex == (std::numeric_limits<std::size_t>::max)())
            {
                return;
            }
            const std::uint32_t firstGeometry = meshFirstGeometry[meshIndex];
            const cgltf_mesh& mesh = *node.mesh;
            for (cgltf_size primitiveIndex = 0u; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
            {
                const std::uint32_t geometryId = firstGeometry + static_cast<std::uint32_t>(primitiveIndex);
                const std::uint32_t materialId = geometryMaterialIds[geometryId];
                const GpuMaterialV0& material = scene.materials[materialId];
                if ((material.metadata.y & MaterialFlagEmissive) == 0u)
                {
                    continue;
                }
                const GpuGeometryV0& geometry = scene.geometries[geometryId];
                const std::uint32_t firstIndex = geometry.indexRange.x;
                const std::uint32_t indexCount = geometry.indexRange.y;
                for (std::uint32_t offset = 0u; offset < indexCount; offset += 3u)
                {
                    const Vec3 p0 = TransformPoint(world, FromAbi(scene.vertices[scene.indices[firstIndex + offset]].position));
                    const Vec3 p1 = TransformPoint(world, FromAbi(scene.vertices[scene.indices[firstIndex + offset + 1u]].position));
                    const Vec3 p2 = TransformPoint(world, FromAbi(scene.vertices[scene.indices[firstIndex + offset + 2u]].position));
                    const Vec3 cross = Cross(p1 - p0, p2 - p0);
                    const float area = 0.5f * std::sqrt(LengthSquared(cross));
                    if (!(area > 0.0f) || !IsFinite(area))
                    {
                        continue;
                    }
                    const Vec3 normal = Normalize(cross);
                    const Vec3 centroid = (p0 + p1 + p2) * (1.0f / 3.0f);
                    const std::uint32_t primitiveId = geometry.indexRange.w + offset / 3u;
                    const std::uint32_t lightFlags = LightFlagEnabled
                        | (((material.metadata.y & MaterialFlagDoubleSided) != 0u)
                            ? LightFlagTwoSided
                            : 0u);
                    scene.lights.push_back({
                        {centroid.x, centroid.y, centroid.z, area},
                        {normal.x, normal.y, normal.z, 0.0f},
                        {material.emissiveFactorStrength.x, material.emissiveFactorStrength.y,
                         material.emissiveFactorStrength.z, 1.0f},
                        {p0.x, p0.y, p0.z, 0.0f},
                        {LightTypeEmissiveTriangle, nextLightId++, instanceId, primitiveId},
                        {kInvalidId, lightFlags, 0u, 0u}});
                }
            }
        };
        const auto VisitNode = [&](const auto& self, const cgltf_node& node) -> bool
        {
            const std::size_t nodeIndex = NodeIndex(&node);
            if (nodeIndex == (std::numeric_limits<std::size_t>::max)()
                || nodeIndex >= visitedNodes.size() || visitedNodes[nodeIndex] != 0u)
            {
                return false;
            }
            visitedNodes[nodeIndex] = 1u;
            if (node.skin != nullptr || node.weights_count != 0u || node.has_mesh_gpu_instancing)
            {
                return false;
            }
            cgltf_float localValues[16]{};
            cgltf_float worldValues[16]{};
            cgltf_node_transform_local(&node, localValues);
            cgltf_node_transform_world(&node, worldValues);
            const AbiMat4Rows local = ToAbiMatrix(localValues);
            const AbiMat4Rows world = ToAbiMatrix(worldValues);
            if (!IsRigidTransform(local) || !IsRigidTransform(world))
            {
                return false;
            }
            if (!AddCamera(node, world))
            {
                return false;
            }
            std::uint32_t instanceId = kInvalidId;
            if (node.mesh != nullptr)
            {
                const std::size_t meshIndex = PointerIndex(rawData->meshes, rawData->meshes_count, node.mesh);
                if (meshIndex == (std::numeric_limits<std::size_t>::max)()
                    || meshFirstGeometry[meshIndex] == kInvalidId)
                {
                    return false;
                }
                if (nextInstanceId == kInvalidId)
                {
                    return false;
                }
                instanceId = nextInstanceId++;
                const std::uint32_t firstGeometry = meshFirstGeometry[meshIndex];
                const std::uint32_t geometryCount = meshGeometryCount[meshIndex];
                scene.instances.push_back({
                    world,
                    InverseRigid(world),
                    world,
                    {firstGeometry, geometryCount, instanceId,
                     InstanceFlagVisible | InstanceFlagCastsShadow},
                    {0u, 0u, 0u, 0u}});
                for (std::uint32_t geometryOffset = 0u;
                     geometryOffset < geometryCount;
                     ++geometryOffset)
                {
                    ExpandTransformedBounds(
                        sceneBounds,
                        localGeometryBounds[firstGeometry + geometryOffset],
                        world);
                }
                AddEmissiveLights(node, world, instanceId);
            }
            if (!AddPunctualLight(node, world, instanceId))
            {
                return false;
            }
            for (cgltf_size childIndex = 0u; childIndex < node.children_count; ++childIndex)
            {
                if (node.children[childIndex] == nullptr
                    || node.children[childIndex]->parent != &node
                    || !self(self, *node.children[childIndex]))
                {
                    return false;
                }
            }
            return true;
        };

        for (cgltf_size rootIndex = 0u; rootIndex < rawData->scene->nodes_count; ++rootIndex)
        {
            if (rawData->scene->nodes[rootIndex] == nullptr
                || !VisitNode(VisitNode, *rawData->scene->nodes[rootIndex]))
            {
                return Failure(GltfCanonicalSceneError::InvalidTransform,
                    "The selected glTF scene contains a non-rigid, singular, or cyclic node graph.");
            }
        }
        if (scene.instances.empty() || !IsValid(sceneBounds))
        {
            return Failure(GltfCanonicalSceneError::InvalidStructure,
                "The selected glTF scene must contain at least one mesh instance.");
        }
        if (scene.materials.empty())
        {
            EnsureDefaultMaterial();
        }
        if (scene.cameras.empty())
        {
            const Vec3 center = (sceneBounds.minimum + sceneBounds.maximum) * 0.5f;
            const Vec3 extent = sceneBounds.maximum - sceneBounds.minimum;
            const float radius = (std::max)(1.0f, std::sqrt(LengthSquared(extent)) * 0.75f);
            scene.cameras.push_back({
                "camera:" + scene.stableId,
                ToAbi(center + Vec3{0.0f, 0.0f, radius * 2.0f}, 1.0f),
                ToAbi(center, 1.0f),
                {0.0f, 1.0f, 0.0f, 0.0f},
                52.0f});
        }
        scene.constants.counts0 = {
            static_cast<std::uint32_t>(scene.vertices.size()),
            static_cast<std::uint32_t>(scene.indices.size()),
            static_cast<std::uint32_t>(scene.geometries.size()),
            static_cast<std::uint32_t>(scene.instances.size())};
        scene.constants.counts1 = {
            static_cast<std::uint32_t>(scene.materials.size()),
            static_cast<std::uint32_t>(scene.lights.size()),
            0u,
            0u};
        scene.constants.sceneBoundsMin = ToAbi(sceneBounds.minimum, 0.0f);
        scene.constants.sceneBoundsMax = ToAbi(sceneBounds.maximum, 0.0f);
        scene.constants.versionFlags = {
            kAbiVersion,
            scene.generation,
            scene.constants.versionFlags.z,
            0u};
        scene.constants.environment = {kInvalidId, 0u, 0u, 0u};

        const CanonicalSceneValidation validation = ValidateCanonicalScene(scene);
        if (!validation)
        {
            return Failure(GltfCanonicalSceneError::InvalidStructure, validation.reason);
        }
        return {std::move(scene), GltfCanonicalSceneError::None, {}};
    }
}
