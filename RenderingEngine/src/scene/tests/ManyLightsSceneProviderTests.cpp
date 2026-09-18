#include "scene/ManyLightsSceneProvider.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Scene;

    class Checks final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "Many Lights scene provider test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] int ExitCode() const noexcept
        {
            return failures_ == 0u ? 0 : 1;
        }

    private:
        std::size_t failures_ = 0u;
    };

    [[nodiscard]] ManyLightsSceneFrameRequest MakeRequest(
        const ManyLightsTier tier,
        const std::uint32_t frameIndex,
        const std::uint64_t frameGeneration,
        const std::uint32_t sceneGeneration,
        const std::uint32_t topologyEpoch)
    {
        ManyLightsSceneFrameRequest request;
        request.config.scene = ScenePreset::ManyLightsRestirArena;
        request.config.restir.manyLightsTier = tier;
        request.config.restir.comparisonCandidateBudgetPerPixel = 64u;
        request.config.restir.comparisonVisibilityBudgetPerPixel = 32u;
        request.config.restir.maximumReservoirM = 32u;
        request.config.restir.animateLights = true;
        request.config.restir.animateRigidOccluders = true;
        request.frameIndex = frameIndex;
        request.frameGeneration = frameGeneration;
        request.sceneGeneration = sceneGeneration;
        request.topologyEpoch = topologyEpoch;
        return request;
    }

    [[nodiscard]] ManyLightsSceneProviderPreviousFrame MakePrevious(
        const ManyLightsSceneProviderFrame& frame)
    {
        return {
            frame.frameGeneration,
            frame.sceneGeneration,
            frame.lightSetGeneration,
            frame.productionLightIdentities
        };
    }

    void TestTierMapping(Checks& tests)
    {
        constexpr std::array tiers{
            ManyLightsTier::Lights100,
            ManyLightsTier::Lights1000,
            ManyLightsTier::Lights10000
        };
        constexpr std::array counts{100u, 1'000u, 10'000u};
        for (std::size_t index = 0u; index < tiers.size(); ++index)
        {
            const ManyLightsSceneProviderResult result = BuildManyLightsSceneFrame(
                MakeRequest(tiers[index], 17u, 4u, 9u, 0u));
            tests.Expect(result.Accepted() && result.frame.has_value(),
                "valid Many Lights RuntimeConfig must produce a provider frame");
            if (!result.frame.has_value())
            {
                continue;
            }
            tests.Expect(result.frame->arena.canonical.lights.size() == counts[index]
                    && result.frame->productionLightIdentities.size() == counts[index],
                "RuntimeConfig tier must map to the exact arena light count and production identities");
            tests.Expect(result.frame->frameIndex == 17u
                    && result.frame->frameGeneration == 4u
                    && result.frame->sceneGeneration == 9u
                    && result.frame->lightSetGeneration == 1u,
                "frame, scene, and initial light-set generations must be preserved explicitly");
            tests.Expect(!result.frame->history.has_value(),
                "the first frame must not invent a history mapping");
            tests.Expect(result.frame->productionLightIdentities.front().stableLightId
                    != Restir::kInvalidLightIndex,
                "arena identities must map to valid Restir production identities");
        }
    }

    void TestAnimationMapping(Checks& tests)
    {
        ManyLightsSceneFrameRequest frozen = MakeRequest(
            ManyLightsTier::Lights100, 0u, 1u, 1u, 0u);
        frozen.config.restir.animateLights = false;
        frozen.config.restir.animateRigidOccluders = false;
        const ManyLightsSceneProviderResult frozen0 = BuildManyLightsSceneFrame(frozen);
        frozen.frameIndex = 1u;
        frozen.frameGeneration = 2u;
        const ManyLightsSceneProviderResult frozen1 = BuildManyLightsSceneFrame(frozen);
        tests.Expect(frozen0.frame.has_value() && frozen1.frame.has_value(),
            "frozen animation requests must still build frames");
        if (frozen0.frame.has_value() && frozen1.frame.has_value())
        {
            tests.Expect(frozen0.frame->arena.canonical.lights[0].positionRange.x
                    == frozen1.frame->arena.canonical.lights[0].positionRange.x,
                "disabled RuntimeConfig animation must freeze light placement");
            tests.Expect(frozen0.frame->arena.canonical.cameras.front().eye.x
                    == frozen1.frame->arena.canonical.cameras.front().eye.x,
                "disabled global animation must freeze the provider camera path");
        }
    }

    void TestTopologyHistory(Checks& tests)
    {
        const ManyLightsSceneProviderResult first = BuildManyLightsSceneFrame(
            MakeRequest(ManyLightsTier::Lights100, 0u, 1u, 4u, 0u));
        tests.Expect(first.frame.has_value(), "topology baseline frame must build");
        if (!first.frame.has_value())
        {
            return;
        }
        const ManyLightsSceneProviderPreviousFrame previous = MakePrevious(*first.frame);
        const ManyLightsSceneProviderResult second = BuildManyLightsSceneFrame(
            MakeRequest(ManyLightsTier::Lights100, 1u, 2u, 4u, 1u), &previous);
        tests.Expect(second.Accepted() && second.frame.has_value()
                && second.frame->history.has_value(),
            "one topology epoch must publish a precise history mapping");
        if (!second.frame.has_value() || !second.frame->history.has_value())
        {
            return;
        }
        const Restir::LightHistoryMapping& mapping = *second.frame->history;
        tests.Expect(mapping.valid && mapping.retained == 99u
                && mapping.added == 1u && mapping.deleted == 1u
                && mapping.generationChanged == 0u,
            "topology epoch must retain 99 lights and model exactly one delete/add");
        tests.Expect(second.frame->arena.replacedLightTableIndex == 50u
                && mapping.currentToPrevious[50u] == Restir::kInvalidLightIndex,
            "replacement light must not reuse the deleted light's reservoir history");
        tests.Expect(second.frame->productionLightIdentities[0].stableLightId
                == first.frame->productionLightIdentities[0].stableLightId,
            "retained lights must preserve stable production identity");
        tests.Expect(second.frame->productionLightIdentities[50].stableLightId
                != first.frame->productionLightIdentities[50].stableLightId,
            "replacement slot must receive a new stable production identity");
    }

    void TestFailClosed(Checks& tests)
    {
        ManyLightsSceneFrameRequest nonManyLights = MakeRequest(
            ManyLightsTier::Lights100, 0u, 1u, 1u, 0u);
        nonManyLights.config.scene = ScenePreset::CornellBox;
        const ManyLightsSceneProviderResult wrongScene =
            BuildManyLightsSceneFrame(nonManyLights);
        tests.Expect(wrongScene.code == ManyLightsSceneProviderCode::InvalidConfiguration
                && !wrongScene.frame.has_value(),
            "non-Many-Lights scene configuration must fail closed");

        ManyLightsSceneFrameRequest missingGeneration = MakeRequest(
            ManyLightsTier::Lights100, 0u, 1u, 1u, 0u);
        missingGeneration.frameGeneration.reset();
        const ManyLightsSceneProviderResult missing =
            BuildManyLightsSceneFrame(missingGeneration);
        tests.Expect(missing.code == ManyLightsSceneProviderCode::MissingGeneration
                && !missing.frame.has_value(),
            "missing frame generation must not be inferred");

        ManyLightsSceneFrameRequest invalidTier = MakeRequest(
            ManyLightsTier::Lights100, 0u, 1u, 1u, 0u);
        invalidTier.config.restir.manyLightsTier = static_cast<ManyLightsTier>(255u);
        const ManyLightsSceneProviderResult invalid =
            BuildManyLightsSceneFrame(invalidTier);
        tests.Expect(invalid.code == ManyLightsSceneProviderCode::InvalidConfiguration
                && !invalid.frame.has_value(),
            "invalid tier must fail closed rather than defaulting to 100 lights");

        const ManyLightsSceneProviderResult first = BuildManyLightsSceneFrame(
            MakeRequest(ManyLightsTier::Lights100, 0u, 1u, 2u, 0u));
        if (first.frame.has_value())
        {
            const ManyLightsSceneProviderPreviousFrame previous = MakePrevious(*first.frame);
            const ManyLightsSceneProviderResult wrongSceneGeneration =
                BuildManyLightsSceneFrame(
                    MakeRequest(ManyLightsTier::Lights100, 1u, 2u, 3u, 1u),
                    &previous);
            tests.Expect(wrongSceneGeneration.code
                    == ManyLightsSceneProviderCode::HistoryGenerationMismatch
                    && !wrongSceneGeneration.frame.has_value(),
                "scene-generation changes must invalidate old light history");
        }
    }
}

int RunManyLightsSceneProviderTests()
{
    Checks tests;
    TestTierMapping(tests);
    TestAnimationMapping(tests);
    TestTopologyHistory(tests);
    TestFailClosed(tests);
    if (tests.ExitCode() == 0)
    {
        std::cout << "Many Lights scene provider mapping and history checks passed.\n";
    }
    return tests.ExitCode();
}
