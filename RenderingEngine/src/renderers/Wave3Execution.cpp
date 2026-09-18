#include "renderers/Wave3Execution.hpp"

#include <cmath>
#include <exception>

namespace RenderingEngine::Wave3
{
    namespace
    {
        [[nodiscard]] bool SamePass(
            const ScheduledPass& left,
            const ScheduledPass& right) noexcept
        {
            return left.kind == right.kind
                && left.domain == right.domain
                && left.lane == right.lane
                && left.bounce == right.bounce
                && left.iteration == right.iteration
                && left.beginTimestampQuery == right.beginTimestampQuery
                && left.endTimestampQuery == right.endTimestampQuery
                && left.consumesPreviousPass == right.consumesPreviousPass
                && left.writesIndirectArguments == right.writesIndirectArguments
                && left.readsIndirectArguments == right.readsIndirectArguments;
        }

        [[nodiscard]] bool SamePlan(
            const FramePlan& left,
            const FramePlan& right) noexcept
        {
            if (left.status != right.status
                || left.historyDecision != right.historyDecision
                || left.historyReadPhysicalIndex != right.historyReadPhysicalIndex
                || left.historyWritePhysicalIndex != right.historyWritePhysicalIndex
                || left.timestampQueryCount != right.timestampQueryCount
                || left.passes.size() != right.passes.size())
            {
                return false;
            }
            for (std::size_t index = 0u; index < left.passes.size(); ++index)
            {
                if (!SamePass(left.passes[index], right.passes[index]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] HistoryIdentity MakePublishedHistory(
            const FrameRequest& request) noexcept
        {
            return {
                true,
                request.frameIndex,
                request.configGeneration,
                request.sceneGeneration,
                request.resourceGeneration,
                request.config.render.width,
                request.config.render.height,
                request.config.backend,
                request.config.transportModel,
                request.config.executionArchitecture,
                request.config.reconstruction};
        }

        void Fail(
            FrameExecutionResult& result,
            const ExecutionStatus status,
            const std::uint32_t passIndex,
            std::string reason)
        {
            result.status = status;
            result.failedPassIndex = passIndex;
            result.reason = std::move(reason);
        }

        [[nodiscard]] bool ValidTiming(
            const ScheduledPass& pass,
            const ProviderTiming& timing) noexcept
        {
            if (HasTimestampQueries(pass))
            {
                return timing.provenance
                        == TimingProvenance::VulkanGpuTimestampQuery
                    && timing.beginQuery == pass.beginTimestampQuery
                    && timing.endQuery == pass.endTimestampQuery
                    && std::isfinite(timing.gpuMilliseconds)
                    && timing.gpuMilliseconds >= 0.0;
            }
            return timing.provenance == TimingProvenance::Unavailable
                && timing.beginQuery == 0xffffffffu
                && timing.endQuery == 0xffffffffu;
        }
    }

    FrameExecutionResult ExecuteFrameTransaction(
        const FrameRequest& request,
        const FramePlan& plan,
        const ExecutionCallbacks& callbacks)
    {
        FrameExecutionResult result{};
        const FramePlan canonical = BuildFramePlan(request);
        if (!canonical.IsReady() || !ValidateFramePlan(plan)
            || !SamePlan(plan, canonical))
        {
            Fail(result, ExecutionStatus::InvalidPlan,
                std::numeric_limits<std::uint32_t>::max(),
                "The supplied Wave 3 plan is invalid or drifted from its request.");
            return result;
        }
        if (!callbacks.executePass)
        {
            Fail(result, ExecutionStatus::ProviderFailure,
                0u, "No Wave 3 pass executor was supplied.");
            return result;
        }

        result.passes.reserve(plan.passes.size());
        bool sawHistoryPrepare = false;
        for (std::size_t index = 0u; index < plan.passes.size(); ++index)
        {
            const ScheduledPass& pass = plan.passes[index];
            ProviderPassResult provider{};
            try
            {
                provider = callbacks.executePass(pass);
            }
            catch (const std::exception& error)
            {
                provider.reason = error.what();
            }
            catch (...)
            {
                provider.reason = "Wave 3 provider threw a non-standard exception.";
            }

            PassAuditRecord audit{
                pass,
                provider.provider,
                provider.timing,
                provider.faults,
                provider.succeeded,
                provider.reason};
            result.passes.push_back(std::move(audit));

            const std::uint32_t passIndex = static_cast<std::uint32_t>(index);
            if (!provider.succeeded || provider.provider.empty())
            {
                Fail(result, ExecutionStatus::ProviderFailure, passIndex,
                    provider.reason.empty()
                        ? "A Wave 3 provider did not complete its scheduled pass."
                        : std::move(provider.reason));
                return result;
            }
            if (!ValidTiming(pass, provider.timing))
            {
                Fail(result, ExecutionStatus::InvalidTimingEvidence, passIndex,
                    "A profiled pass did not provide the exact Vulkan GPU timestamp query pair, or an unprofiled pass claimed timing evidence.");
                return result;
            }
            if (!provider.faults.AllZero())
            {
                result.faults = provider.faults;
                Fail(result, ExecutionStatus::RuntimeFault, passIndex,
                    "A Wave 3 pass reported validation, overflow, drop, numeric, PDF, or hit faults.");
                return result;
            }
            sawHistoryPrepare = sawHistoryPrepare
                || pass.kind == PassKind::PublishHistory;
            ++result.completedPassCount;
        }

        if (plan.historyDecision != HistoryDecision::Unused)
        {
            if (!sawHistoryPrepare || !callbacks.commitHistory)
            {
                Fail(result, ExecutionStatus::HistoryCommitFailure,
                    static_cast<std::uint32_t>(plan.passes.size()),
                    "A history-using frame completed without a deferred history committer.");
                return result;
            }
            result.publishedHistory = MakePublishedHistory(request);
            HistoryCommitResult commit{};
            try
            {
                commit = callbacks.commitHistory(result.publishedHistory);
            }
            catch (const std::exception& error)
            {
                commit.reason = error.what();
            }
            catch (...)
            {
                commit.reason = "Wave 3 history committer threw a non-standard exception.";
            }
            if (!commit.committed)
            {
                Fail(result, ExecutionStatus::HistoryCommitFailure,
                    static_cast<std::uint32_t>(plan.passes.size()),
                    commit.reason.empty()
                        ? "Wave 3 history publication was not committed."
                        : std::move(commit.reason));
                return result;
            }
            result.historyCommitted = true;
        }

        result.status = ExecutionStatus::Succeeded;
        result.failedPassIndex = std::numeric_limits<std::uint32_t>::max();
        result.reason.clear();
        return result;
    }

    std::string_view ToString(const ExecutionStatus status) noexcept
    {
        switch (status)
        {
        case ExecutionStatus::NotRun: return "not-run";
        case ExecutionStatus::InvalidPlan: return "invalid-plan";
        case ExecutionStatus::ProviderFailure: return "provider-failure";
        case ExecutionStatus::InvalidTimingEvidence: return "invalid-timing-evidence";
        case ExecutionStatus::RuntimeFault: return "runtime-fault";
        case ExecutionStatus::HistoryCommitFailure: return "history-commit-failure";
        case ExecutionStatus::Succeeded: return "succeeded";
        default: return "invalid";
        }
    }

    std::string_view ToString(const TimingProvenance provenance) noexcept
    {
        switch (provenance)
        {
        case TimingProvenance::Unavailable: return "unavailable";
        case TimingProvenance::VulkanGpuTimestampQuery: return "vulkan-gpu-timestamp-query";
        case TimingProvenance::CpuWallClock: return "cpu-wall-clock";
        case TimingProvenance::Synthetic: return "synthetic";
        default: return "invalid";
        }
    }
}
