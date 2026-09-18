#include "restir/LightHistory.hpp"
#include "scene/ManyLightsArena.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
    class ManyLightsTestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "Many Lights Arena test failed: " << message << '\n';
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

    [[nodiscard]] std::vector<RenderingEngine::Restir::ProductionLightIdentity>
        ToProductionIdentities(
            const std::vector<RenderingEngine::Scene::ManyLightsArenaLightIdentity>& input)
    {
        std::vector<RenderingEngine::Restir::ProductionLightIdentity> result;
        result.reserve(input.size());
        for (const auto& identity : input)
        {
            result.push_back({
                identity.stableLightId, identity.primitiveId,
                identity.generation, 0u
            });
        }
        return result;
    }
}

int RunManyLightsArenaTests()
{
    using namespace RenderingEngine::Scene;

    ManyLightsTestContext tests;
    constexpr std::array tiers{
        ManyLightsArenaTier::Lights100,
        ManyLightsArenaTier::Lights1000,
        ManyLightsArenaTier::Lights10000
    };
    for (const ManyLightsArenaTier tier : tiers)
    {
        ManyLightsArenaOptions options;
        options.tier = tier;
        const ManyLightsArenaFrame first = BuildManyLightsArena(options);
        const ManyLightsArenaFrame second = BuildManyLightsArena(options);
        const std::uint32_t expected = ResolveManyLightsArenaCount(tier);
        tests.Expect(static_cast<bool>(ValidateCanonicalScene(first.canonical)),
            "each tier must satisfy the frozen canonical scene validator");
        tests.Expect(first.canonical.lights.size() == expected
                && first.lightIdentities.size() == expected,
            "100/1k/10k tiers must publish exactly the requested lights");
        tests.Expect(first.canonical.instances.size() == expected + 4u,
            "each emitter triangle must have an instance in addition to room/occluders");
        tests.Expect(CanonicalSceneFingerprint(first.canonical)
                == CanonicalSceneFingerprint(second.canonical),
            "same tier/frame/topology must be deterministic");
        tests.Expect(first.canonical.lights.front().identity.y == 0u
                && first.canonical.lights.back().identity.y == expected - 1u,
            "legacy table identity remains contiguous while abi-v3 identity is external");
    }

    ManyLightsArenaOptions animatedOptions;
    animatedOptions.tier = ManyLightsArenaTier::Lights100;
    const ManyLightsArenaFrame frame0 = BuildManyLightsArena(animatedOptions);
    animatedOptions.frameIndex = 1u;
    const ManyLightsArenaFrame frame1 = BuildManyLightsArena(animatedOptions);
    const std::uint32_t firstEmitter = frame1.firstLightInstanceIndex;
    tests.Expect(frame1.lightIdentities == frame0.lightIdentities,
        "continuous animation must preserve stable light identity/generation");
    tests.Expect(frame1.canonical.lights[0].positionRange.x
            != frame0.canonical.lights[0].positionRange.x,
        "light position animation must change a current-frame source");
    tests.Expect(frame1.canonical.instances[firstEmitter].previousObjectToWorld.row0.w
            == frame0.canonical.instances[firstEmitter].objectToWorld.row0.w,
        "current light transforms must publish exact previous-frame transforms");
    tests.Expect(frame1.canonical.instances[1].previousObjectToWorld.row2.w
            == frame0.canonical.instances[1].objectToWorld.row2.w,
        "moving rigid occluders must publish exact previous-frame transforms");

    float minimumArea = std::numeric_limits<float>::max();
    float maximumArea = 0.0f;
    float minimumRadiance = std::numeric_limits<float>::max();
    float maximumRadiance = 0.0f;
    for (const auto& light : frame0.canonical.lights)
    {
        minimumArea = (std::min)(minimumArea, light.shapeParams.z);
        maximumArea = (std::max)(maximumArea, light.shapeParams.z);
        const float radiance = light.radianceScale.x
            + light.radianceScale.y + light.radianceScale.z;
        minimumRadiance = (std::min)(minimumRadiance, radiance);
        maximumRadiance = (std::max)(maximumRadiance, radiance);
    }
    tests.Expect(maximumArea > minimumArea * 20.0f,
        "the arena must mix small and large emissive triangles");
    tests.Expect(maximumRadiance > minimumRadiance * 8.0f,
        "the arena must mix weak and strong emissive triangles");

    ManyLightsArenaOptions replacedOptions;
    replacedOptions.tier = ManyLightsArenaTier::Lights100;
    replacedOptions.topologyEpoch = 1u;
    const ManyLightsArenaFrame replaced = BuildManyLightsArena(replacedOptions);
    const auto previousIdentities = ToProductionIdentities(frame0.lightIdentities);
    const auto currentIdentities = ToProductionIdentities(replaced.lightIdentities);
    const auto mapping = RenderingEngine::Restir::BuildLightHistoryMapping(
        previousIdentities, currentIdentities,
        frame0.lightSetGeneration, replaced.lightSetGeneration);
    tests.Expect(mapping.valid && mapping.retained == 99u
            && mapping.added == 1u && mapping.deleted == 1u,
        "topology epoch must model one dynamic delete/add without invalidating retained lights");
    tests.Expect(replaced.replacedLightTableIndex == 50u
            && mapping.currentToPrevious[replaced.replacedLightTableIndex]
                == RenderingEngine::Restir::kInvalidLightIndex,
        "the added light must not reuse the deleted light's reservoir sample");

    if (tests.ExitCode() == 0)
    {
        std::cout << "Many Lights 100/1k/10k scene, animation, and topology checks passed.\n";
    }
    return tests.ExitCode();
}
