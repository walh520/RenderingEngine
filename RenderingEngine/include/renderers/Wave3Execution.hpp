#pragma once

#include "renderers/Wave3Acceptance.hpp"
#include "renderers/Wave3FrameGraph.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Wave3
{
    enum class TimingProvenance : std::uint32_t
    {
        Unavailable = 0u,
        VulkanGpuTimestampQuery,
        CpuWallClock,
        Synthetic
    };

    struct ProviderTiming final
    {
        TimingProvenance provenance = TimingProvenance::Unavailable;
        std::uint32_t beginQuery = 0xffffffffu;
        std::uint32_t endQuery = 0xffffffffu;
        double gpuMilliseconds = 0.0;
    };

    struct ProviderPassResult final
    {
        bool succeeded = false;
        std::string provider;
        ProviderTiming timing{};
        RuntimeFaultCounters faults{};
        std::string reason;
    };

    struct HistoryCommitResult final
    {
        bool committed = false;
        std::string reason;
    };

    using PassExecutor =
        std::function<ProviderPassResult(const ScheduledPass& pass)>;
    using HistoryCommitter =
        std::function<HistoryCommitResult(const HistoryIdentity& identity)>;

    struct ExecutionCallbacks final
    {
        PassExecutor executePass{};
        HistoryCommitter commitHistory{};
    };

    enum class ExecutionStatus : std::uint32_t
    {
        NotRun = 0u,
        InvalidPlan,
        ProviderFailure,
        InvalidTimingEvidence,
        RuntimeFault,
        HistoryCommitFailure,
        Succeeded
    };

    struct PassAuditRecord final
    {
        ScheduledPass pass{};
        std::string provider;
        ProviderTiming timing{};
        RuntimeFaultCounters faults{};
        bool succeeded = false;
        std::string reason;
    };

    struct FrameExecutionResult final
    {
        ExecutionStatus status = ExecutionStatus::NotRun;
        std::uint32_t completedPassCount = 0u;
        std::uint32_t failedPassIndex =
            std::numeric_limits<std::uint32_t>::max();
        bool historyCommitted = false;
        HistoryIdentity publishedHistory{};
        RuntimeFaultCounters faults{};
        std::vector<PassAuditRecord> passes{};
        std::string reason;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == ExecutionStatus::Succeeded;
        }
    };

    // Executes the canonical plan in-order and publishes history only after
    // every scheduled provider pass, including Present, succeeds. Timestamped
    // passes accept only provider-reported Vulkan query results. CPU wall time
    // and synthetic timings are retained as explicit rejection categories.
    [[nodiscard]] FrameExecutionResult ExecuteFrameTransaction(
        const FrameRequest& request,
        const FramePlan& plan,
        const ExecutionCallbacks& callbacks);

    [[nodiscard]] std::string_view ToString(ExecutionStatus status) noexcept;
    [[nodiscard]] std::string_view ToString(TimingProvenance provenance) noexcept;
}
