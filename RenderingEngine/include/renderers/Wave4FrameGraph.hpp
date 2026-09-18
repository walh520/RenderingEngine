#pragma once

#include "contracts/AbiV2.hpp"
#include "renderers/Wave3FrameGraph.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Wave4
{
    // Wave 4 is deliberately a declaration-only composition boundary.  A
    // provider must explicitly publish abi-v3 and a validated Wave 3 graph;
    // the defaults therefore remain fail-closed.
    enum class PlanStatus : std::uint32_t
    {
        InvalidRequest = 0u,
        ProviderUnavailable,
        Ready
    };

    enum class HistoryDecision : std::uint32_t
    {
        Reset = 0u,
        Reuse
    };

    [[nodiscard]] constexpr std::uint32_t LightCount(
        const ManyLightsTier tier) noexcept
    {
        return ResolveManyLightsCount(tier);
    }

    enum class PassKind : std::uint32_t
    {
        PrimarySurfaceExport = 0u,
        LightMapping,
        CandidateGeneration,
        InitialReservoir,
        TemporalReuse,
        SpatialReuse,
        // Optional correction/validation of the selected sample against its
        // reuse-source surface, before the single final winner visibility.
        ReferenceCorrectionVisibility,
        WinnerVisibility,
        SplitDirectSignal,
        Reconstruction,
        HistoryPublish,
        DebugOutput
    };

    struct ProviderAvailability final
    {
        // abi-v2 is the Wave 3 prerequisite.  abi-v3 is intentionally a
        // provider fact until L0 publishes the shared Reservoir contract.
        bool abiV2 = true;
        bool abiV3Published = false;
        bool wave3FrameGraph = false;

        bool primarySurface = false;
        bool lightMapping = false;
        bool candidateGenerator = false;
        bool initialReservoir = false;
        bool temporalReuse = false;
        bool unbiasedReferenceVisibility = false;
        bool spatialReuse = false;
        bool winnerVisibility = false;
        bool splitDirectSignal = false;
        bool reconstruction = false;
        bool history = false;
        bool debug = false;
    };

    struct HistoryIdentity final
    {
        bool valid = false;
        std::uint64_t publishedFrame = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        std::uint64_t lightGeneration = 0u;
        ManyLightsTier tier = ManyLightsTier::Lights100;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        TraversalBackend backend = TraversalBackend::GpuFlattenedSahBvh;
        TransportModel transportModel = TransportModel::Pbr;
        ExecutionArchitecture executionArchitecture =
            ExecutionArchitecture::Megakernel;
        ReconstructionMode reconstruction = ReconstructionMode::ProgressiveMean;
    };

    struct FrameRequest final
    {
        RuntimeConfig config{};
        // This is the completed Wave 3 predecessor graph.  Wave 4 does not
        // silently rebuild or reinterpret it.
        Wave3::FramePlan wave3Plan{};
        ProviderAvailability providers{};
        HistoryIdentity history{};
        std::uint64_t frameIndex = 0u;
        std::uint64_t configGeneration = 1u;
        std::uint64_t sceneGeneration = 1u;
        std::uint64_t resourceGeneration = 1u;
        std::uint64_t lightGeneration = 1u;
        std::uint64_t candidateBudget = 0u;
        std::uint64_t visibilityBudget = 0u;
        std::uint32_t framesInFlight = 2u;
        bool includeReferenceCorrectionVisibility = false;
        bool primaryOwnsDirectSignal = false;
        bool includeDebug = true;
    };

    struct ScheduledPass final
    {
        PassKind kind = PassKind::PrimarySurfaceExport;
        std::string_view lane{};
        std::uint32_t ordinal = 0u;
        bool optional = false;
        bool noOp = false;
        bool readsPreviousPass = false;
        bool writesDirectSignal = false;
        std::string_view directSignalOwner{};
    };

    struct FramePlan final
    {
        PlanStatus status = PlanStatus::InvalidRequest;
        HistoryDecision historyDecision = HistoryDecision::Reset;
        std::uint32_t historyReadPhysicalIndex = 0xffffffffu;
        std::uint32_t historyWritePhysicalIndex = 0xffffffffu;
        std::uint32_t winnerVisibilityPassCount = 0u;
        std::vector<ScheduledPass> passes{};
        std::string reason{};

        [[nodiscard]] bool IsReady() const noexcept
        {
            return status == PlanStatus::Ready;
        }
    };

    [[nodiscard]] FramePlan BuildFramePlan(const FrameRequest& request);
    [[nodiscard]] bool ValidateFramePlan(const FramePlan& plan) noexcept;
    [[nodiscard]] bool HistoryMatches(
        const FrameRequest& request) noexcept;
    [[nodiscard]] std::string_view ToString(PassKind kind) noexcept;
    [[nodiscard]] std::string_view ToString(HistoryDecision decision) noexcept;
}
