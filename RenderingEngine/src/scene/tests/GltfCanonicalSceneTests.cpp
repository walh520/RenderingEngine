#include "scene/GltfCanonicalScene.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "L2 glTF canonical scene test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] int ExitCode() const noexcept
        {
            return failures_ == 0 ? 0 : 1;
        }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] std::string FixtureJson(const std::string_view mode,
                                           const std::string_view alphaMode,
                                           const std::string_view scale)
    {
        return std::string{
            "{\"asset\":{\"version\":\"2.0\"},"
            "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
            "\"nodes\":[{\"mesh\":0,\"translation\":[1,2,3],\"scale\":"}
            + std::string{scale}
            + "}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
              "\"indices\":1,\"material\":0,\"mode\":"
            + std::string{mode}
            + "}]}],\"materials\":[{\"pbrMetallicRoughness\":{"
              "\"baseColorFactor\":[0.7,0.4,0.2,1],\"metallicFactor\":0.25,"
              "\"roughnessFactor\":0.6},\"emissiveFactor\":[1,0.5,0.25],"
              "\"alphaMode\":\""
            + std::string{alphaMode}
            + "\",\"alphaCutoff\":0.35,\"doubleSided\":true}],"
              "\"buffers\":[{\"uri\":\"fixture.bin\",\"byteLength\":60}],"
              "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},"
              "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12}],"
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,"
              "\"count\":4,\"type\":\"VEC3\"},{\"bufferView\":1,"
              "\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}";
    }

    [[nodiscard]] bool WriteFixture(
        const std::filesystem::path& directory,
        const std::string_view mode = "4",
        const std::string_view alphaMode = "MASK",
        const std::string_view scale = "[1,1,1]")
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            return false;
        }
        const std::filesystem::path binPath = directory / "fixture.bin";
        std::ofstream binary(binPath, std::ios::binary | std::ios::trunc);
        if (!binary)
        {
            return false;
        }
        constexpr std::array<float, 12> positions{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f};
        const std::array<std::uint16_t, 6> indices{0u, 1u, 2u, 0u, 2u, 3u};
        binary.write(reinterpret_cast<const char*>(positions.data()),
            static_cast<std::streamsize>(sizeof(positions)));
        binary.write(reinterpret_cast<const char*>(indices.data()),
            static_cast<std::streamsize>(sizeof(indices)));
        binary.close();

        std::ofstream json(directory / "fixture.gltf", std::ios::trunc);
        if (!json)
        {
            return false;
        }
        json << FixtureJson(mode, alphaMode, scale);
        return static_cast<bool>(json);
    }
}

int RunGltfCanonicalSceneTests()
{
    using namespace RenderingEngine::Scene;

    TestContext tests;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "RenderingEngine.GltfCanonicalSceneTests";
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    tests.Expect(WriteFixture(directory), "the minimal glTF+bin fixture must be writable");

    const std::filesystem::path fixture = directory / "fixture.gltf";
    const GltfCanonicalSceneLoadOptions options{"fixture:static", 7u};
    const GltfCanonicalSceneLoadResult first = LoadGltfCanonicalScene(fixture, options);
    const GltfCanonicalSceneLoadResult second = LoadGltfCanonicalScene(fixture, options);
    tests.Expect(static_cast<bool>(first), "the valid glTF fixture must load");
    tests.Expect(static_cast<bool>(second), "reloading the valid glTF fixture must load");
    if (first && second)
    {
        tests.Expect(
            CanonicalSceneFingerprint(first.scene) == CanonicalSceneFingerprint(second.scene),
            "reloading the same glTF fixture must preserve its fingerprint");
        tests.Expect(first.scene.vertices.size() == 4u && first.scene.indices.size() == 6u,
            "the fixture must produce four vertices and six indices");
        tests.Expect(first.scene.geometries.size() == 1u && first.scene.instances.size() == 1u,
            "the fixture must produce one geometry and one rigid instance");
        tests.Expect(first.scene.lights.size() == 2u,
            "each emissive triangle must produce an independent light");
        tests.Expect(first.scene.instances[0].metadata.z == 0u
                && first.scene.geometries[0].identity.x == 0u
                && first.scene.geometries[0].indexRange.w == 0u
                && first.scene.materials[0].metadata.z == 0u,
            "instance, geometry, primitive, and material IDs must be stable from zero");
        tests.Expect(first.scene.instances[0].objectToWorld.row0.w == 1.0f
                && first.scene.instances[0].objectToWorld.row1.w == 2.0f
                && first.scene.instances[0].objectToWorld.row2.w == 3.0f,
            "node translation must become the canonical rigid transform");
        tests.Expect((first.scene.materials[0].metadata.y & MaterialFlagAlphaMask) != 0u
                && (first.scene.materials[0].metadata.y & MaterialFlagDoubleSided) != 0u
                && (first.scene.geometries[0].identity.w & GeometryFlagAlphaMask) != 0u
                && (first.scene.geometries[0].identity.w & GeometryFlagDoubleSided) != 0u,
            "MASK and doubleSided material semantics must reach geometry and material flags");
        tests.Expect(first.scene.lights[0].identity.x == LightTypeEmissiveTriangle
                && first.scene.lights[0].identity.y == 0u
                && first.scene.lights[0].identity.z == 0u
                && first.scene.lights[0].identity.w == 0u
                && first.scene.lights[1].identity.y == 1u
                && first.scene.lights[1].identity.w == 1u,
            "emissive triangle light identities must be independent and deterministic");
        for (const GpuVertexV0& vertex : first.scene.vertices)
        {
            const float normalLength = std::sqrt(
                vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y
                + vertex.normal.z * vertex.normal.z);
            const float tangentDotNormal = vertex.tangent.x * vertex.normal.x
                + vertex.tangent.y * vertex.normal.y + vertex.tangent.z * vertex.normal.z;
            tests.Expect(std::abs(normalLength - 1.0f) <= 1.0e-5f
                    && std::abs(tangentDotNormal) <= 1.0e-5f,
                "missing normals and tangents must be deterministic orthonormal attributes");
        }
    }

    tests.Expect(WriteFixture(directory, "1"), "the malformed non-triangles fixture must be writable");
    tests.Expect(
        LoadGltfCanonicalScene(fixture, options).error == GltfCanonicalSceneError::UnsupportedFeature,
        "non-TRIANGLES primitives must fail closed");
    tests.Expect(WriteFixture(directory, "4", "BLEND"), "the alpha BLEND fixture must be writable");
    tests.Expect(
        LoadGltfCanonicalScene(fixture, options).error == GltfCanonicalSceneError::UnsupportedFeature,
        "alpha BLEND materials must fail closed");
    tests.Expect(WriteFixture(directory, "4", "MASK", "[2,1,1]"),
        "the non-rigid transform fixture must be writable");
    tests.Expect(
        LoadGltfCanonicalScene(fixture, options).error == GltfCanonicalSceneError::InvalidTransform,
        "non-rigid node transforms must fail closed");

    std::error_code finalCleanupError;
    std::filesystem::remove_all(directory, finalCleanupError);
    if (tests.ExitCode() == 0)
    {
        std::cout << "Static glTF canonical loader checks passed.\n";
    }
    return tests.ExitCode();
}
