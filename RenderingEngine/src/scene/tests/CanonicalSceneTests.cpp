#include "scene/CanonicalScene.hpp"

#include <iostream>
#include <limits>
#include <string_view>

int RunGltfCanonicalSceneTests();
int RunExperimentScenesTests();
int RunManyLightsArenaTests();
int RunManyLightsSceneProviderTests();

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "L2 canonical scene test failed: " << message << '\n';
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
}

int main()
{
    using namespace RenderingEngine::Scene;

    TestContext tests;
    const CanonicalScene first = BuildCanonicalTriangleScene();
    const CanonicalScene second = BuildCanonicalTriangleScene();
    tests.Expect(static_cast<bool>(ValidateCanonicalScene(first)),
        "the Wave 1 canonical triangle must satisfy the abi-v0 scene validator");
    tests.Expect(CanonicalSceneFingerprint(first) == CanonicalSceneFingerprint(second),
        "reloading the canonical triangle must preserve its deterministic fingerprint");
    tests.Expect(first.geometries.size() == 1u
            && first.geometries[0].identity.x == 0u
            && first.geometries[0].indexRange.w == 0u
            && first.instances.size() == 1u
            && first.instances[0].metadata.z == 0u
            && first.materials.size() == 1u
            && first.materials[0].metadata.z == 0u
            && first.lights.size() == 1u
            && first.lights[0].identity.y == 0u,
        "geometry, primitive, instance, material, and light IDs must start from stable zero IDs");
    tests.Expect(first.instances[0].objectToWorld.row0.x
                == first.instances[0].previousObjectToWorld.row0.x
            && first.instances[0].objectToWorld.row3.w
                == first.instances[0].previousObjectToWorld.row3.w,
        "Wave 1 instances must publish both current and previous rigid transforms");

    const std::span<const SceneRegistryEntry> registry = Wave1SceneRegistry();
    const SceneRegistryEntry* const entry = FindWave1Scene("canonical-triangle");
    tests.Expect(registry.size() == 1u
            && entry != nullptr
            && entry->fixedCameraStableId == "camera:canonical-triangle"
            && FindWave1Scene("unknown") == nullptr,
        "the Wave 1 registry must expose one stable canonical triangle and fixed camera");

    const CanonicalScene cornell = BuildCanonicalCornellScene();
    const CanonicalScene cornellReload = BuildCanonicalCornellScene();
    tests.Expect(static_cast<bool>(ValidateCanonicalScene(cornell)),
        "the Wave 2 canonical Cornell box must satisfy the frozen abi-v0 scene validator");
    tests.Expect(CanonicalSceneFingerprint(cornell) == CanonicalSceneFingerprint(cornellReload),
        "reloading canonical Cornell must preserve its deterministic fingerprint");
    tests.Expect(cornell.indices.size() == 108u
            && cornell.indices.size() / 3u == 36u
            && cornell.geometries.size() == 18u
            && cornell.materials.size() == 4u
            && cornell.lights.size() == 2u
            && cornell.lights[0].identity.w != cornell.lights[1].identity.w,
        "Cornell must publish 36 stable triangles, four materials, and one light per emitter primitive");
    tests.Expect(cornell.instances.size() == 1u
            && cornell.instances[0].metadata.x == 0u
            && cornell.instances[0].metadata.y == cornell.geometries.size()
            && cornell.instances[0].metadata.z == 0u,
        "Cornell geometry must be covered by one stable canonical instance");
    const std::span<const SceneRegistryEntry> canonicalRegistry = CanonicalSceneRegistry();
    const SceneRegistryEntry* const cornellEntry = FindCanonicalScene("canonical-cornell");
    tests.Expect(canonicalRegistry.size() == 2u
            && cornellEntry != nullptr
            && cornellEntry->fixedCameraStableId == "camera:canonical-cornell"
            && FindCanonicalScene("unknown") == nullptr,
        "the canonical registry must publish the Wave 1 triangle and Wave 2 Cornell scene");

    CanonicalScene badIndex = first;
    badIndex.indices[2] = 99u;
    tests.Expect(ValidateCanonicalScene(badIndex).error == CanonicalSceneError::InvalidIndex,
        "an out-of-range mesh index must fail explicitly");

    CanonicalScene degenerate = first;
    degenerate.vertices[2].position = degenerate.vertices[1].position;
    tests.Expect(ValidateCanonicalScene(degenerate).error
            == CanonicalSceneError::DegenerateTriangle,
        "a degenerate canonical triangle must fail explicitly");

    CanonicalScene invalidTransform = first;
    invalidTransform.instances[0].previousObjectToWorld.row0.x =
        std::numeric_limits<float>::quiet_NaN();
    tests.Expect(ValidateCanonicalScene(invalidTransform).error
            == CanonicalSceneError::InvalidStableIdentity,
        "non-finite current/previous transforms must fail explicitly");

    CanonicalScene duplicateIdentity = first;
    duplicateIdentity.materials.push_back(duplicateIdentity.materials.front());
    duplicateIdentity.constants.counts1.x = 2u;
    tests.Expect(ValidateCanonicalScene(duplicateIdentity).error
            == CanonicalSceneError::InvalidStableIdentity,
        "duplicate stable material IDs must fail explicitly");

    const int gltfExitCode = RunGltfCanonicalSceneTests();
    const int experimentExitCode = RunExperimentScenesTests();
    const int manyLightsExitCode = RunManyLightsArenaTests();
    const int providerExitCode = RunManyLightsSceneProviderTests();
    if (tests.ExitCode() == 0 && gltfExitCode == 0
        && experimentExitCode == 0 && manyLightsExitCode == 0
        && providerExitCode == 0)
    {
        std::cout << "Canonical triangle, Cornell, and static glTF scene checks passed.\n";
    }
    return tests.ExitCode() == 0 && gltfExitCode == 0
        && experimentExitCode == 0 && manyLightsExitCode == 0
        && providerExitCode == 0 ? 0 : 1;
}
