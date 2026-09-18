#include "renderers/Wave4FrameGraph.hpp"

#include <iterator>
#include <limits>
#include <utility>

namespace RenderingEngine::Wave4
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex = 0xffffffffu;

        [[nodiscard]] bool IsWave4Backend(
            const TraversalBackend backend) noexcept
        {
            return backend == TraversalBackend::CanonicalLinearGpu
                || backend == TraversalBackend::GpuFlattenedSahBvh
                || backend == TraversalBackend::GpuLbvh
                || backend == TraversalBackend::VulkanRayQuery
                || backend == TraversalBackend::VulkanRayTracingPipeline;
        }

        [[nodiscard]] bool IsWave4Execution(
            const ExecutionArchitecture architecture) noexcept
        {
            return architecture == ExecutionArchitecture::Staged
                || architecture == ExecutionArchitecture::Megakernel
                || architecture == ExecutionArchitecture::Wavefront;
        }

        [[nodiscard]] bool UsesHistory(
            const FrameRequest& request) noexcept
        {
            return request.providers.temporalReuse
                || request.config.reconstruction == ReconstructionMode::TemporalAccumulation
                || request.config.reconstruction == ReconstructionMode::Svgf;
        }

        [[nodiscard]] bool HistoryMatchesInternal(
            const FrameRequest& request) noexcept
        {
            const HistoryIdentity& history = request.history;
            return request.frameIndex != 0u
                && history.valid
                && history.publishedFrame + 1u == request.frameIndex
                && history.configGeneration == request.configGeneration
                && history.sceneGeneration == request.sceneGeneration
                && history.resourceGeneration == request.resourceGeneration
                && history.lightGeneration == request.lightGeneration
                && history.tier == request.config.restir.manyLightsTier
                && history.width == request.config.render.width
                && history.height == request.config.render.height
                && history.backend == request.config.backend
                && history.transportModel == request.config.transportModel
                && history.executionArchitecture
                    == request.config.executionArchitecture
                && history.reconstruction == request.config.reconstruction;
        }

        void Fail(
            FramePlan& plan,
            const PlanStatus status,
            std::string reason)
        {
            plan.status = status;
            plan.reason = std::move(reason);
            plan.passes.clear();
            plan.historyReadPhysicalIndex = kInvalidIndex;
            plan.historyWritePhysicalIndex = kInvalidIndex;
            plan.winnerVisibilityPassCount = 0u;
        }

        class PassBuilder final
        {
        public:
            explicit PassBuilder(FramePlan& plan) noexcept
                : plan_(plan)
            {
            }

            void Add(
                const PassKind kind,
                const std::string_view lane,
                const bool optional = false,
                const bool writesDirectSignal = false,
                const std::string_view directSignalOwner = {},
                const bool noOp = false)
            {
                ScheduledPass pass{};
                pass.kind = kind;
                pass.lane = lane;
                pass.ordinal = static_cast<std::uint32_t>(plan_.passes.size());
                pass.optional = optional;
                pass.noOp = noOp;
                pass.readsPreviousPass = !plan_.passes.empty();
                pass.writesDirectSignal = writesDirectSignal;
                pass.directSignalOwner = directSignalOwner;
                plan_.passes.push_back(pass);
                if (kind == PassKind::WinnerVisibility)
                {
                    ++plan_.winnerVisibilityPassCount;
                }
            }

        private:
            FramePlan& plan_;
        };

        [[nodiscard]] bool ProviderForBackend(
            const FrameRequest& request) noexcept
        {
            switch (request.config.backend)
            {
            case TraversalBackend::CanonicalLinearGpu:
            case TraversalBackend::GpuFlattenedSahBvh:
                return request.wave3Plan.IsReady();
            case TraversalBackend::GpuLbvh:
                return request.providers.wave3FrameGraph;
            case TraversalBackend::VulkanRayQuery:
                return request.wave3Plan.IsReady();
            case TraversalBackend::VulkanRayTracingPipeline:
                return request.providers.wave3FrameGraph;
            default:
                return false;
            }
        }

        [[nodiscard]] bool ProviderForExecution(
            const FrameRequest& request) noexcept
        {
            return IsWave4Execution(request.config.executionArchitecture)
                && request.wave3Plan.IsReady();
        }

        [[nodiscard]] bool ProviderSetIsReady(
            const FrameRequest& request) noexcept
        {
            const ProviderAvailability& providers = request.providers;
            return providers.abiV2
                && Contracts::AbiV2::kAbiVersion == 3u
                && providers.abiV3Published
                && providers.wave3FrameGraph
                && providers.primarySurface
                && providers.lightMapping
                && providers.candidateGenerator
                && providers.initialReservoir
                && providers.temporalReuse
                && providers.spatialReuse
                && providers.winnerVisibility
                && providers.splitDirectSignal
                && providers.reconstruction
                && providers.history
                && providers.debug
                && ProviderForBackend(request)
                && ProviderForExecution(request)
                && (!request.includeReferenceCorrectionVisibility
                    || providers.unbiasedReferenceVisibility);
        }
    }

    bool HistoryMatches(const FrameRequest& request) noexcept
    {
        return HistoryMatchesInternal(request);
    }

    FramePlan BuildFramePlan(const FrameRequest& request)
    {
        FramePlan plan{};
        const RuntimeConfig& config = request.config;
        if (config.version != kRuntimeConfigVersion
            || !IsWave4Backend(config.backend)
            || config.transportModel != TransportModel::Pbr
            || !IsWave4Execution(config.executionArchitecture)
            || config.directLightingEstimator
                != DirectLightingEstimator::RestirDirectIllumination
            || (config.lightSelection != LightSelectionStrategy::Uniform
                && config.lightSelection != LightSelectionStrategy::PowerWeighted)
            || (config.environmentSampler
                    != EnvironmentDirectionSampler::UniformSphere
                && config.environmentSampler
                    != EnvironmentDirectionSampler::ImportanceMap)
            || config.render.width == 0u || config.render.height == 0u
            || config.render.samplesPerFrame != 1u
            || config.render.maximumBounce == 0u
            || request.configGeneration == 0u
            || request.sceneGeneration == 0u
            || request.resourceGeneration == 0u
            || request.lightGeneration == 0u
            || request.candidateBudget == 0u
            || request.visibilityBudget == 0u
            || request.candidateBudget
                != config.restir.comparisonCandidateBudgetPerPixel
            || request.visibilityBudget
                != config.restir.comparisonVisibilityBudgetPerPixel
            || request.framesInFlight < 2u
            || request.framesInFlight > 4u
            || !request.primaryOwnsDirectSignal
            || !request.includeDebug)
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "ReSTIR requires a valid mixed-runtime frame request, 1-SPP budgets, generations, direct ownership, and debug output.");
            return plan;
        }

        if (LightCount(config.restir.manyLightsTier) == 0u)
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "Wave 4 accepts only the 100, 1,000, or 10,000-light tiers.");
            return plan;
        }

        if (!request.wave3Plan.IsReady()
            || !Wave3::ValidateFramePlan(request.wave3Plan))
        {
            Fail(plan, PlanStatus::ProviderUnavailable,
                "Wave 4 requires a validated Wave 3 predecessor frame graph.");
            return plan;
        }

        if (!ProviderSetIsReady(request))
        {
            Fail(plan, PlanStatus::ProviderUnavailable,
                "A Wave 4 provider, abi-v3 publication, or Wave 3 production frame-graph attachment is unavailable.");
            return plan;
        }

        const std::uint64_t physicalCount =
            2ull * static_cast<std::uint64_t>(request.framesInFlight);
        const std::uint64_t writePhysical =
            (request.frameIndex & 1ull)
                * static_cast<std::uint64_t>(request.framesInFlight)
            + request.frameIndex % request.framesInFlight;
        if (writePhysical >= physicalCount
            || writePhysical > std::numeric_limits<std::uint32_t>::max())
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "Wave 4 history write index overflowed.");
            return plan;
        }

        plan.historyDecision = HistoryMatchesInternal(request)
            ? HistoryDecision::Reuse
            : HistoryDecision::Reset;
        plan.historyWritePhysicalIndex =
            static_cast<std::uint32_t>(writePhysical);
        if (plan.historyDecision == HistoryDecision::Reuse)
        {
            const std::uint64_t previousFrame = request.frameIndex - 1u;
            const std::uint64_t readPhysical =
                (previousFrame & 1ull)
                    * static_cast<std::uint64_t>(request.framesInFlight)
                + previousFrame % request.framesInFlight;
            if (readPhysical >= physicalCount
                || readPhysical > std::numeric_limits<std::uint32_t>::max()
                || readPhysical == writePhysical)
            {
                Fail(plan, PlanStatus::InvalidRequest,
                    "Wave 4 history read/write mapping is invalid.");
                return plan;
            }
            plan.historyReadPhysicalIndex =
                static_cast<std::uint32_t>(readPhysical);
        }

        PassBuilder builder(plan);
        builder.Add(PassKind::PrimarySurfaceExport, "L0/L6/L8");
        builder.Add(PassKind::LightMapping, "L2");
        builder.Add(PassKind::CandidateGeneration, "L9");
        builder.Add(PassKind::InitialReservoir, "L9");
        builder.Add(PassKind::TemporalReuse, "L9", false, false, {},
            plan.historyDecision == HistoryDecision::Reset);
        builder.Add(PassKind::SpatialReuse, "L9");
        if (request.includeReferenceCorrectionVisibility)
        {
            builder.Add(PassKind::ReferenceCorrectionVisibility,
                "L9", true);
        }
        builder.Add(PassKind::WinnerVisibility, "L9/L5");
        builder.Add(PassKind::SplitDirectSignal,
            "L9/L6", false, true, "L9");
        builder.Add(PassKind::Reconstruction, "L8");
        builder.Add(PassKind::HistoryPublish, "L8/L9");
        builder.Add(PassKind::DebugOutput, "L10");

        plan.status = PlanStatus::Ready;
        plan.reason.clear();
        if (!ValidateFramePlan(plan))
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "The generated Wave 4 frame graph failed its internal order or ownership audit.");
        }
        return plan;
    }

    bool ValidateFramePlan(const FramePlan& plan) noexcept
    {
        if (!plan.IsReady() || plan.passes.size() < 11u
            || plan.passes.front().kind != PassKind::PrimarySurfaceExport
            || plan.passes.back().kind != PassKind::DebugOutput
            || plan.winnerVisibilityPassCount != 1u)
        {
            return false;
        }

        constexpr PassKind mandatory[] = {
            PassKind::PrimarySurfaceExport,
            PassKind::LightMapping,
            PassKind::CandidateGeneration,
            PassKind::InitialReservoir,
            PassKind::TemporalReuse,
            PassKind::SpatialReuse,
            PassKind::WinnerVisibility,
            PassKind::SplitDirectSignal,
            PassKind::Reconstruction,
            PassKind::HistoryPublish,
            PassKind::DebugOutput};
        std::size_t mandatoryIndex = 0u;
        bool sawReference = false;
        for (std::size_t index = 0u; index < plan.passes.size(); ++index)
        {
            const ScheduledPass& pass = plan.passes[index];
            if (pass.ordinal != index || pass.lane.empty()
                || (index == 0u ? pass.readsPreviousPass
                                 : !pass.readsPreviousPass))
            {
                return false;
            }
            if (pass.kind == PassKind::ReferenceCorrectionVisibility)
            {
                if (!pass.optional || sawReference || mandatoryIndex != 6u)
                {
                    return false;
                }
                sawReference = true;
                continue;
            }
            if (pass.optional || mandatoryIndex >= std::size(mandatory)
                || pass.kind != mandatory[mandatoryIndex])
            {
                return false;
            }
            if (pass.kind == PassKind::TemporalReuse
                && pass.noOp != (plan.historyDecision == HistoryDecision::Reset))
            {
                return false;
            }
            if (pass.kind == PassKind::SplitDirectSignal
                && (!pass.writesDirectSignal
                    || pass.directSignalOwner != "L9"))
            {
                return false;
            }
            ++mandatoryIndex;
        }
        return mandatoryIndex == std::size(mandatory)
            && plan.winnerVisibilityPassCount == 1u;
    }

    std::string_view ToString(const PassKind kind) noexcept
    {
        switch (kind)
        {
        case PassKind::PrimarySurfaceExport: return "primary-surface";
        case PassKind::LightMapping: return "light-mapping";
        case PassKind::CandidateGeneration: return "candidate-generation";
        case PassKind::InitialReservoir: return "initial-reservoir";
        case PassKind::TemporalReuse: return "temporal-reuse";
        case PassKind::ReferenceCorrectionVisibility:
            return "reference-correction-visibility";
        case PassKind::SpatialReuse: return "spatial-reuse";
        case PassKind::WinnerVisibility: return "winner-visibility";
        case PassKind::SplitDirectSignal: return "split-direct-signal";
        case PassKind::Reconstruction: return "reconstruction";
        case PassKind::HistoryPublish: return "history-publish";
        case PassKind::DebugOutput: return "debug-output";
        default: return "invalid";
        }
    }

    std::string_view ToString(const HistoryDecision decision) noexcept
    {
        switch (decision)
        {
        case HistoryDecision::Reset: return "reset";
        case HistoryDecision::Reuse: return "reuse";
        default: return "invalid";
        }
    }
}
