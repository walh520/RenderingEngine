#include "renderers/Wave3FrameGraph.hpp"

#include <algorithm>
#include <iostream>
#include <set>
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
                std::cerr << "Wave 3 frame-graph test failed: " << message << '\n';
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
        request.config.render.width = 256u;
        request.config.render.height = 256u;
        request.config.render.samplesPerFrame = 1u;
        request.config.render.maximumBounce = 4u;
        request.providers = {true, true, true, true, true, true, true, true};
        request.frameIndex = 8u;
        request.configGeneration = 3u;
        request.sceneGeneration = 5u;
        request.resourceGeneration = 7u;
        request.framesInFlight = 3u;
        request.atrousIterations = 5u;
        return request;
    }

    [[nodiscard]] bool HasPass(
        const RenderingEngine::Wave3::FramePlan& plan,
        const RenderingEngine::Wave3::PassKind kind)
    {
        return std::ranges::any_of(plan.passes,
            [kind](const RenderingEngine::Wave3::ScheduledPass& pass)
            {
                return pass.kind == kind;
            });
    }
}

bool RunWave3FrameGraphTests()
{
    using namespace RenderingEngine;
    using namespace RenderingEngine::Wave3;

    TestContext tests;
    FrameRequest request = MakeRequest();
    FramePlan plan = BuildFramePlan(request);
    tests.Expect(plan.IsReady(), "complete LBVH/Wavefront/SVGF tuple must plan");
    tests.Expect(ValidateFramePlan(plan), "generated plan must pass dependency audit");
    tests.Expect(plan.historyDecision == HistoryDecision::Reset,
        "missing exact previous history must reset");
    tests.Expect(HasPass(plan, PassKind::LbvhMorton)
            && HasPass(plan, PassKind::LbvhRadixSort)
            && HasPass(plan, PassKind::LbvhHierarchy)
            && HasPass(plan, PassKind::LbvhBounds),
        "GPU LBVH must retain separate build stages");
    tests.Expect(HasPass(plan, PassKind::WavefrontIntersect)
            && HasPass(plan, PassKind::WavefrontTraceShadow)
            && HasPass(plan, PassKind::WavefrontResolve),
        "Wavefront stage graph must be present");
    tests.Expect(HasPass(plan, PassKind::TemporalAccumulation)
            && HasPass(plan, PassKind::VarianceBootstrap)
            && HasPass(plan, PassKind::ComposeSvgf),
        "SVGF stages must be present");
    tests.Expect(plan.timestampQueryCount == (plan.passes.size() - 1u) * 2u
            && !HasTimestampQueries(plan.passes.back()),
        "every GPU command pass must own timestamps while present stays unmeasured");

    request = MakeRequest();
    request.config.reconstruction = ReconstructionMode::SpatialFixedAtrous;
    request.providers.temporal = false;
    plan = BuildFramePlan(request);
    tests.Expect(plan.IsReady() && ValidateFramePlan(plan)
            && plan.historyDecision == HistoryDecision::Unused,
        "spatial-only A-Trous must not require or publish temporal history");
    tests.Expect(HasPass(plan, PassKind::PrepareSignal)
            && HasPass(plan, PassKind::VarianceBootstrap)
            && HasPass(plan, PassKind::AtrousIteration)
            && HasPass(plan, PassKind::ComposeAtrous)
            && !HasPass(plan, PassKind::MotionVectors)
            && !HasPass(plan, PassKind::TemporalAccumulation)
            && !HasPass(plan, PassKind::PublishHistory),
        "spatial-only A-Trous must consume the current frame without temporal passes");

    request = MakeRequest();
    request.history = {
        true,
        request.frameIndex - 1u,
        request.configGeneration,
        request.sceneGeneration,
        request.resourceGeneration,
        request.config.render.width,
        request.config.render.height,
        request.config.backend,
        request.config.transportModel,
        request.config.executionArchitecture,
        request.config.reconstruction};
    plan = BuildFramePlan(request);
    tests.Expect(plan.historyDecision == HistoryDecision::Reuse,
        "exact prior frame and generations must reuse history");
    tests.Expect(plan.historyReadPhysicalIndex != plan.historyWritePhysicalIndex,
        "2N history mapping must not alias read and write generations");

    ++request.history.sceneGeneration;
    plan = BuildFramePlan(request);
    tests.Expect(plan.historyDecision == HistoryDecision::Reset,
        "scene generation change must invalidate history");

    for (const std::uint32_t framesInFlight : {2u, 4u})
    {
        request = MakeRequest();
        request.framesInFlight = framesInFlight;
        std::set<std::uint32_t> physicalIndices;
        std::uint32_t previousWrite = 0xffffffffu;
        for (std::uint64_t frame = 0u; frame < 2ull * framesInFlight; ++frame)
        {
            request.frameIndex = frame;
            request.history = {};
            if (frame != 0u)
            {
                request.history = {
                    true,
                    frame - 1u,
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
            plan = BuildFramePlan(request);
            physicalIndices.insert(plan.historyWritePhysicalIndex);
            tests.Expect(plan.IsReady(),
                "even-N history rotation must remain plannable");
            if (frame != 0u)
            {
                tests.Expect(plan.historyDecision == HistoryDecision::Reuse
                        && plan.historyReadPhysicalIndex == previousWrite
                        && plan.historyReadPhysicalIndex
                            != plan.historyWritePhysicalIndex,
                    "exact prior frame must read the previous distinct slot");
            }
            previousWrite = plan.historyWritePhysicalIndex;
        }
        tests.Expect(physicalIndices.size() == 2u * framesInFlight,
            "even frames-in-flight must traverse every 2N history resource");
    }

    request = MakeRequest();
    request.providers.gpuLbvh = false;
    plan = BuildFramePlan(request);
    tests.Expect(plan.status == PlanStatus::ProviderUnavailable && plan.passes.empty(),
        "missing selected backend must fail closed");

    request = MakeRequest();
    request.includeComparisonBaseline = true;
    plan = BuildFramePlan(request);
    tests.Expect(plan.IsReady()
            && HasPass(plan, PassKind::MegakernelIntegrate)
            && HasPass(plan, PassKind::WavefrontResolve),
        "comparison gate must preserve both integrators");

    request = MakeRequest();
    request.config.backend = TraversalBackend::VulkanRayTracingPipeline;
    request.config.executionArchitecture = ExecutionArchitecture::Megakernel;
    request.config.reconstruction = ReconstructionMode::ProgressiveMean;
    plan = BuildFramePlan(request);
    tests.Expect(plan.IsReady()
            && HasPass(plan, PassKind::RtPipelineSbtPrepare)
            && HasPass(plan, PassKind::ComposeRaw)
            && plan.historyDecision == HistoryDecision::Unused,
        "RT Pipeline/Megakernel/Raw comparison tuple must plan without history");

    request.config.render.samplesPerFrame = 2u;
    plan = BuildFramePlan(request);
    tests.Expect(plan.status == PlanStatus::InvalidRequest,
        "Wave 3 frame graph must preserve the 1-SPP scheduling gate");

    if (tests.Passed())
    {
        std::cout << "Wave 3 frame graph, history identity, and profiler-slot checks passed.\n";
    }
    return tests.Passed();
}
