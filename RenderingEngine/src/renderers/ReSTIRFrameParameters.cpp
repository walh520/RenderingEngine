#include "renderers/ReSTIRFrameParameters.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace RenderingEngine::Renderers
{
    namespace
    {
        using Contracts::AbiV3::AbiFloat4;
        using Contracts::AbiV3::AbiUInt4;

        [[nodiscard]] bool SameIdentity(
            const ReSTIRFrameIdentity& left,
            const ReSTIRFrameIdentity& right) noexcept
        {
            return left.frameIndex == right.frameIndex
                && left.configGeneration == right.configGeneration
                && left.sceneGeneration == right.sceneGeneration
                && left.resourceGeneration == right.resourceGeneration
                && left.lightGeneration == right.lightGeneration
                && left.width == right.width
                && left.height == right.height
                && left.shadowMethod == right.shadowMethod;
        }

        [[nodiscard]] bool Finite(const AbiFloat4& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] std::uint32_t CandidateSource(
            const LightSelectionStrategy selection) noexcept
        {
            using namespace Contracts::AbiV3;
            switch (selection)
            {
            case LightSelectionStrategy::Uniform:
                return RestirCandidateUniformLight;
            case LightSelectionStrategy::PowerWeighted:
                return RestirCandidatePowerWeightedLight;
            default:
                return RestirCandidateInvalid;
            }
        }

        [[nodiscard]] std::uint32_t DebugMode(const DebugView debugView) noexcept
        {
            switch (debugView)
            {
            case DebugView::ReservoirM: return 0u;
            case DebugView::ReservoirWeight: return 1u;
            case DebugView::ReservoirLightId: return 2u;
            case DebugView::ReservoirSource: return 3u;
            case DebugView::ReservoirReuse: return 4u;
            case DebugView::ReservoirRejection: return 5u;
            case DebugView::WinnerVisibility: return 6u;
            default: return 0u;
            }
        }

        [[nodiscard]] ReSTIRFrameParameterResult Fail(std::string reason)
        {
            ReSTIRFrameParameterResult result{};
            result.status = {
                ReSTIRRuntimeStatusCode::InvalidRequest, std::move(reason)};
            return result;
        }
    }

    ReSTIRFrameParameterResult BuildReSTIRFrameParameters(
        const ReSTIRFrameRequest& request,
        const ReSTIRFramePlan& plan,
        const ReSTIRFrameParameterSources& sources)
    {
        if (!ValidateReSTIRFramePlan(plan)
            || !SameIdentity(request.identity, plan.identity))
        {
            return Fail("abi-v3 parameters require the canonical validated frame plan");
        }
        if (plan.footprint.pixelCount
            > std::numeric_limits<std::uint32_t>::max())
        {
            return Fail("abi-v3 pixel count exceeds its uint32 shader field");
        }
        constexpr std::uint64_t maximumShaderGeneration =
            std::numeric_limits<std::uint32_t>::max();
        if (request.identity.configGeneration > maximumShaderGeneration
            || request.identity.sceneGeneration > maximumShaderGeneration
            || request.identity.resourceGeneration > maximumShaderGeneration
            || request.identity.lightGeneration > maximumShaderGeneration)
        {
            return Fail("abi-v3 generation identity exceeds its uint32 shader field");
        }
        if (sources.currentLightCount != request.settings.lightCount
            || sources.currentLightCount != plan.currentLightCount
            || sources.currentLightCount == 0u
            || (plan.historyDecision == ReSTIRHistoryDecision::Reuse
                && (sources.previousLightCount == 0u
                    || sources.previousLightCount != plan.previousLightCount))
            || sources.historyGeneration == 0u)
        {
            return Fail("light counts or history generation do not match the frame plan");
        }
        if (!Finite(sources.validation) || !Finite(sources.cameraPosition)
            || sources.validation.x < -1.0f || sources.validation.x > 1.0f
            || sources.validation.y < 0.0f || sources.validation.z < 0.0f
            || sources.validation.w < 0.0f)
        {
            return Fail("ReSTIR validation thresholds and camera position must be finite and bounded");
        }
        const std::uint32_t candidateSource = CandidateSource(
            request.config.lightSelection);
        if (candidateSource == Contracts::AbiV3::RestirCandidateInvalid)
        {
            return Fail("the selected light-selection strategy has no abi-v3 candidate source");
        }

        ReSTIRFrameParameterResult result{};
        auto& parameters = result.parameters;
        parameters.extentAndCandidates = AbiUInt4{
            request.identity.width,
            request.identity.height,
            static_cast<std::uint32_t>(plan.footprint.pixelCount),
            request.settings.initialCandidateCount};
        parameters.reuseLimits = AbiUInt4{
            request.settings.spatialReuse
                ? request.settings.spatialNeighborCount : 0u,
            request.settings.maximumReservoirM,
            request.settings.maximumHistoryAge,
            request.settings.framesInFlight};
        parameters.generations = AbiUInt4{
            static_cast<std::uint32_t>(request.identity.sceneGeneration),
            static_cast<std::uint32_t>(request.identity.lightGeneration),
            sources.historyGeneration,
            static_cast<std::uint32_t>(request.identity.frameIndex)};
        parameters.historyGenerations = AbiUInt4{
            static_cast<std::uint32_t>(request.identity.configGeneration),
            static_cast<std::uint32_t>(request.identity.resourceGeneration),
            DebugMode(request.config.debugView),
            static_cast<std::uint32_t>(request.identity.shadowMethod)};
        parameters.modeAndFlags = AbiUInt4{
            static_cast<std::uint32_t>(request.settings.estimatorMode),
            candidateSource,
            plan.historyDecision == ReSTIRHistoryDecision::Reuse
                    && request.settings.temporalReuse
                ? 1u : 0u,
            static_cast<std::uint32_t>(request.identity.frameIndex >> 32u)};
        parameters.lightTableCounts = AbiUInt4{
            sources.currentLightCount, sources.previousLightCount, 0u, 0u};
        parameters.validation = sources.validation;
        parameters.cameraPosition = sources.cameraPosition;
        return result;
    }
}
