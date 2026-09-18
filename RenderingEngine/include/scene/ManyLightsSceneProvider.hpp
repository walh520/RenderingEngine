#pragma once

#include "app/RuntimeConfig.hpp"
#include "restir/LightHistory.hpp"
#include "scene/ManyLightsArena.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace RenderingEngine::Scene
{
    // Runtime frame identity is supplied by the frame scheduler.  It is kept
    // separate from abi-v0 scene data so the scene provider cannot silently
    // invent a generation when a caller forgot to provide one.
    struct ManyLightsSceneFrameRequest final
    {
        RuntimeConfig config;
        std::optional<std::uint32_t> frameIndex;
        std::optional<std::uint64_t> frameGeneration;
        std::optional<std::uint32_t> sceneGeneration;
        std::optional<std::uint32_t> topologyEpoch;
    };

    struct ManyLightsSceneProviderPreviousFrame final
    {
        std::uint64_t frameGeneration = 0u;
        std::uint32_t sceneGeneration = 0u;
        std::uint64_t lightSetGeneration = 0u;
        std::span<const Restir::ProductionLightIdentity> productionLightIdentities;
    };

    struct ManyLightsSceneProviderFrame final
    {
        ManyLightsArenaFrame arena;
        std::vector<Restir::ProductionLightIdentity> productionLightIdentities;
        std::uint32_t frameIndex = 0u;
        std::uint64_t frameGeneration = 0u;
        std::uint32_t sceneGeneration = 0u;
        std::uint32_t topologyEpoch = 0u;
        std::uint64_t lightSetGeneration = 0u;
        std::optional<Restir::LightHistoryMapping> history;
    };

    enum class ManyLightsSceneProviderCode : std::uint8_t
    {
        Accepted,
        InvalidConfiguration,
        MissingGeneration,
        ArenaBuildFailed,
        HistoryGenerationMismatch,
        InvalidHistory
    };

    struct ManyLightsSceneProviderStatus
    {
        ManyLightsSceneProviderCode code = ManyLightsSceneProviderCode::Accepted;
        std::string reason;

        [[nodiscard]] bool Accepted() const noexcept
        {
            return code == ManyLightsSceneProviderCode::Accepted;
        }
    };

    struct ManyLightsSceneProviderResult final : ManyLightsSceneProviderStatus
    {
        std::optional<ManyLightsSceneProviderFrame> frame;
    };

    // Stateless composition boundary: L0 supplies RuntimeConfig and the
    // scheduler's explicit generation tuple; L10 receives only canonical scene
    // data plus Restir-owned identity/history records.  No GPU resource,
    // renderer capability, or synthetic telemetry is created here.
    [[nodiscard]] ManyLightsSceneProviderResult BuildManyLightsSceneFrame(
        const ManyLightsSceneFrameRequest& request,
        const ManyLightsSceneProviderPreviousFrame* previous = nullptr);
}
