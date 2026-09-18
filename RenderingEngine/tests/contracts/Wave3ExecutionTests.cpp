#include "renderers/Wave3Execution.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "Wave 3 execution test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept { return failures_ == 0; }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] RenderingEngine::Wave3::FrameRequest MakeRequest()
    {
        using namespace RenderingEngine;
        Wave3::FrameRequest request{};
        request.config.scene = ScenePreset::CornellBox;
        request.config.backend = TraversalBackend::GpuLbvh;
        request.config.executionArchitecture = ExecutionArchitecture::Wavefront;
        request.config.directLightingEstimator =
            DirectLightingEstimator::MultipleImportanceSampling;
        request.config.lightSelection =
            LightSelectionStrategy::PowerWeighted;
        request.config.reconstruction = ReconstructionMode::Svgf;
        request.config.render.width = 64u;
        request.config.render.height = 64u;
        request.config.render.samplesPerFrame = 1u;
        request.config.render.maximumBounce = 3u;
        request.providers = {true, true, true, true, true, true, true, true};
        request.frameIndex = 9u;
        request.configGeneration = 2u;
        request.sceneGeneration = 3u;
        request.resourceGeneration = 4u;
        request.framesInFlight = 2u;
        request.atrousIterations = 3u;
        request.includeProfiler = true;
        return request;
    }

    [[nodiscard]] RenderingEngine::Wave3::ProviderPassResult Pass(
        const RenderingEngine::Wave3::ScheduledPass& pass)
    {
        using namespace RenderingEngine::Wave3;
        ProviderPassResult result{};
        result.succeeded = true;
        result.provider = std::string(pass.lane);
        if (HasTimestampQueries(pass))
        {
            result.timing = {
                TimingProvenance::VulkanGpuTimestampQuery,
                pass.beginTimestampQuery,
                pass.endTimestampQuery,
                0.25};
        }
        return result;
    }
}

bool RunWave3ExecutionTests()
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Wave3;

    TestContext tests;
    FrameRequest request = MakeRequest();
    FramePlan plan = BuildFramePlan(request);
    std::uint32_t commitCalls = 0u;
    ExecutionCallbacks callbacks{};
    callbacks.executePass = Pass;
    callbacks.commitHistory = [&commitCalls, &request](
        const HistoryIdentity& identity)
    {
        ++commitCalls;
        return HistoryCommitResult{
            identity.valid
                && identity.publishedFrame == request.frameIndex
                && identity.sceneGeneration == request.sceneGeneration,
            {}};
    };

    FrameExecutionResult result =
        ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.Succeeded()
            && result.completedPassCount == plan.passes.size()
            && result.passes.size() == plan.passes.size()
            && result.historyCommitted && commitCalls == 1u,
        "a complete provider transaction must commit history once after all passes");

    commitCalls = 0u;
    callbacks.executePass = [](const ScheduledPass& pass)
    {
        ProviderPassResult result = Pass(pass);
        if (pass.kind == PassKind::WavefrontShade)
        {
            result.succeeded = false;
            result.reason = "injected shade failure";
        }
        return result;
    };
    result = ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.status == ExecutionStatus::ProviderFailure
            && !result.historyCommitted && commitCalls == 0u,
        "provider failure must abort before deferred history publication");

    callbacks.executePass = [](const ScheduledPass& pass)
    {
        ProviderPassResult result = Pass(pass);
        if (HasTimestampQueries(pass))
        {
            result.timing.provenance = TimingProvenance::CpuWallClock;
        }
        return result;
    };
    result = ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.status == ExecutionStatus::InvalidTimingEvidence
            && commitCalls == 0u,
        "CPU wall time must not satisfy a Vulkan GPU timestamp slot");

    callbacks.executePass = [](const ScheduledPass& pass)
    {
        ProviderPassResult result = Pass(pass);
        if (pass.kind == PassKind::WavefrontIntersect)
        {
            result.faults.queueOverflows = 1u;
        }
        return result;
    };
    result = ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.status == ExecutionStatus::RuntimeFault
            && result.faults.queueOverflows == 1u && commitCalls == 0u,
        "queue overflow must fail the frame transaction");

    FramePlan drifted = plan;
    drifted.passes[1].bounce = 99u;
    callbacks.executePass = Pass;
    result = ExecuteFrameTransaction(request, drifted, callbacks);
    tests.Expect(result.status == ExecutionStatus::InvalidPlan
            && result.passes.empty() && commitCalls == 0u,
        "a plan that drifted from the immutable request must not execute");

    callbacks.executePass = [](const ScheduledPass& pass)
    {
        if (pass.kind == PassKind::LbvhMorton)
        {
            throw std::runtime_error("injected provider exception");
        }
        return Pass(pass);
    };
    result = ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.status == ExecutionStatus::ProviderFailure
            && !result.historyCommitted && commitCalls == 0u,
        "provider exceptions must be converted into a failed audit record");

    callbacks.executePass = Pass;
    callbacks.commitHistory = [](const HistoryIdentity&)
    {
        return HistoryCommitResult{false, "injected commit refusal"};
    };
    result = ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.status == ExecutionStatus::HistoryCommitFailure
            && result.completedPassCount == plan.passes.size()
            && !result.historyCommitted,
        "history publication must remain explicit and fail closed");

    request.config.reconstruction = ReconstructionMode::ProgressiveMean;
    plan = BuildFramePlan(request);
    commitCalls = 0u;
    callbacks.executePass = Pass;
    callbacks.commitHistory = [&commitCalls](const HistoryIdentity&)
    {
        ++commitCalls;
        return HistoryCommitResult{true, {}};
    };
    result = ExecuteFrameTransaction(request, plan, callbacks);
    tests.Expect(result.Succeeded() && !result.historyCommitted
            && commitCalls == 0u,
        "Raw mode must not publish temporal history");

    if (tests.Passed())
    {
        std::cout << "Wave 3 provider execution and deferred-history checks passed.\n";
    }
    return tests.Passed();
}
