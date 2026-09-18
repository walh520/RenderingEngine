#include "scene/ManyLightsSceneProvider.hpp"

#include <exception>
#include <string>
#include <utility>

namespace RenderingEngine::Scene
{
    namespace
    {
        [[nodiscard]] std::optional<ManyLightsArenaTier> ToArenaTier(
            const ManyLightsTier tier) noexcept
        {
            switch (tier)
            {
            case ManyLightsTier::Lights100:
                return ManyLightsArenaTier::Lights100;
            case ManyLightsTier::Lights1000:
                return ManyLightsArenaTier::Lights1000;
            case ManyLightsTier::Lights10000:
                return ManyLightsArenaTier::Lights10000;
            }
            return std::nullopt;
        }

        [[nodiscard]] ManyLightsSceneProviderResult Fail(
            const ManyLightsSceneProviderCode code,
            std::string reason)
        {
            ManyLightsSceneProviderResult result;
            result.code = code;
            result.reason = std::move(reason);
            return result;
        }

        [[nodiscard]] ManyLightsSceneProviderStatus ValidateRequest(
            const ManyLightsSceneFrameRequest& request,
            const std::optional<ManyLightsArenaTier>& arenaTier)
        {
            if (request.config.version != kRuntimeConfigVersion
                || request.config.scene != ScenePreset::ManyLightsRestirArena)
            {
                return {
                    ManyLightsSceneProviderCode::InvalidConfiguration,
                    "Many Lights scene provider requires the ManyLightsRestirArena RuntimeConfig scene"
                };
            }
            if (!arenaTier.has_value()
                || ResolveManyLightsCount(request.config.restir.manyLightsTier) == 0u)
            {
                return {
                    ManyLightsSceneProviderCode::InvalidConfiguration,
                    "RuntimeConfig contains an unknown Many Lights tier"
                };
            }
            if (request.config.restir.comparisonCandidateBudgetPerPixel == 0u
                || request.config.restir.comparisonVisibilityBudgetPerPixel == 0u
                || request.config.restir.maximumReservoirM == 0u)
            {
                return {
                    ManyLightsSceneProviderCode::InvalidConfiguration,
                    "Many Lights candidate, visibility, and reservoir budgets must be non-zero"
                };
            }
            if (!request.frameIndex.has_value()
                || !request.frameGeneration.has_value()
                || !request.sceneGeneration.has_value()
                || !request.topologyEpoch.has_value())
            {
                return {
                    ManyLightsSceneProviderCode::MissingGeneration,
                    "frame index, frame generation, scene generation, and topology epoch are required"
                };
            }
            if (*request.sceneGeneration == 0u
                || *request.frameGeneration == 0u)
            {
                return {
                    ManyLightsSceneProviderCode::MissingGeneration,
                    "frame and scene generations must be non-zero"
                };
            }
            return {};
        }

        [[nodiscard]] ManyLightsSceneProviderStatus ValidatePreviousFrame(
            const ManyLightsSceneProviderPreviousFrame& previous,
            const ManyLightsSceneProviderFrame& current)
        {
            if (previous.frameGeneration == 0u
                || previous.sceneGeneration == 0u
                || previous.lightSetGeneration == 0u
                || previous.productionLightIdentities.empty())
            {
                return {
                    ManyLightsSceneProviderCode::InvalidHistory,
                    "previous Many Lights frame is missing generation or production light identities"
                };
            }
            if (previous.frameGeneration >= current.frameGeneration
                || previous.sceneGeneration != current.sceneGeneration)
            {
                return {
                    ManyLightsSceneProviderCode::HistoryGenerationMismatch,
                    "previous frame must be older and belong to the same scene generation"
                };
            }
            return {};
        }
    }

    ManyLightsSceneProviderResult BuildManyLightsSceneFrame(
        const ManyLightsSceneFrameRequest& request,
        const ManyLightsSceneProviderPreviousFrame* const previous)
    {
        const std::optional<ManyLightsArenaTier> arenaTier = ToArenaTier(
            request.config.restir.manyLightsTier);
        const ManyLightsSceneProviderStatus requestStatus =
            ValidateRequest(request, arenaTier);
        if (!requestStatus.Accepted())
        {
            return Fail(requestStatus.code, requestStatus.reason);
        }

        ManyLightsArenaOptions arenaOptions;
        arenaOptions.tier = *arenaTier;
        arenaOptions.frameIndex = *request.frameIndex;
        arenaOptions.sceneGeneration = *request.sceneGeneration;
        arenaOptions.topologyEpoch = *request.topologyEpoch;
        arenaOptions.animateLights = request.config.restir.animateLights;
        // RuntimeConfig currently has no independent camera-animation bit.  A
        // disabled global light animation therefore also freezes the fixed
        // camera path, keeping an A/B comparison deterministic.
        arenaOptions.animateCamera = request.config.restir.animateLights;
        arenaOptions.animateRigidOccluders = request.config.restir.animateRigidOccluders;

        ManyLightsArenaFrame arena;
        try
        {
            arena = BuildManyLightsArena(arenaOptions);
        }
        catch (const std::exception& error)
        {
            return Fail(
                ManyLightsSceneProviderCode::ArenaBuildFailed,
                std::string("Many Lights arena construction failed: ") + error.what());
        }
        catch (...)
        {
            return Fail(
                ManyLightsSceneProviderCode::ArenaBuildFailed,
                "Many Lights arena construction failed with an unknown exception");
        }

        ManyLightsSceneProviderResult result;
        result.code = ManyLightsSceneProviderCode::Accepted;
        result.frame.emplace();
        ManyLightsSceneProviderFrame& frame = *result.frame;
        frame.arena = std::move(arena);
        frame.frameIndex = *request.frameIndex;
        frame.frameGeneration = *request.frameGeneration;
        frame.sceneGeneration = *request.sceneGeneration;
        frame.topologyEpoch = *request.topologyEpoch;
        frame.lightSetGeneration = frame.arena.lightSetGeneration;
        frame.productionLightIdentities.reserve(frame.arena.lightIdentities.size());
        for (const ManyLightsArenaLightIdentity& identity : frame.arena.lightIdentities)
        {
            frame.productionLightIdentities.push_back({
                identity.stableLightId,
                identity.primitiveId,
                identity.generation,
                0u
            });
        }

        if (previous != nullptr)
        {
            const ManyLightsSceneProviderStatus previousStatus =
                ValidatePreviousFrame(*previous, frame);
            if (!previousStatus.Accepted())
            {
                return Fail(previousStatus.code, previousStatus.reason);
            }
            Restir::LightHistoryMapping mapping = Restir::BuildLightHistoryMapping(
                previous->productionLightIdentities,
                frame.productionLightIdentities,
                previous->lightSetGeneration,
                frame.lightSetGeneration);
            if (!mapping.valid)
            {
                return Fail(
                    ManyLightsSceneProviderCode::InvalidHistory,
                    mapping.reason.empty()
                        ? "Many Lights light-history mapping was invalid"
                        : mapping.reason);
            }
            frame.history.emplace(std::move(mapping));
        }
        return result;
    }
}
