#include "renderers/Wave4FrameGraph.hpp"

#include <iostream>
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
                std::cerr << "Wave 4 frame graph test failed: "
                    << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failures_ == 0;
        }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] RenderingEngine::Wave4::FrameRequest MakeRequest()
    {
        using namespace RenderingEngine;
        using namespace RenderingEngine::Wave4;
        FrameRequest request{};
        request.config.scene = ScenePreset::ManyLightsRestirArena;
        request.config.backend = TraversalBackend::GpuFlattenedSahBvh;
        request.config.executionArchitecture = ExecutionArchitecture::Megakernel;
        request.config.directLightingEstimator =
            DirectLightingEstimator::RestirDirectIllumination;
        request.config.lightSelection =
            LightSelectionStrategy::Uniform;
        request.config.reconstruction = ReconstructionMode::TemporalAccumulation;
        request.config.restir.manyLightsTier = ManyLightsTier::Lights100;
        request.config.restir.reuseStage = RestirReuseStage::TemporalSpatial;
        request.config.restir.comparisonCandidateBudgetPerPixel = 64u;
        request.config.restir.comparisonVisibilityBudgetPerPixel = 8u;
        request.providers = {
            true, true, true,
            true, true, true, true, true, false,
            true, true, true, true, true, true};
        request.primaryOwnsDirectSignal = true;
        request.candidateBudget = 64u;
        request.visibilityBudget = 8u;

        // The predecessor is intentionally provider-shaped.  This test does
        // not claim that Wave 3 has been attached to the live renderer.
        request.wave3Plan.status = Wave3::PlanStatus::Ready;
        const auto MakePredecessorPass = [](
            const Wave3::PassKind kind,
            const Wave3::PassDomain domain,
            const std::string_view lane,
            const std::uint32_t ordinal)
        {
            Wave3::ScheduledPass pass{};
            pass.kind = kind;
            pass.domain = domain;
            pass.lane = lane;
            if (domain != Wave3::PassDomain::Present)
            {
                pass.beginTimestampQuery = ordinal * 2u;
                pass.endTimestampQuery = ordinal * 2u + 1u;
            }
            pass.consumesPreviousPass = ordinal != 0u;
            return pass;
        };
        request.wave3Plan.passes = {
            MakePredecessorPass(Wave3::PassKind::FrameReset,
                Wave3::PassDomain::Transfer, "L0", 0u),
            MakePredecessorPass(Wave3::PassKind::MegakernelIntegrate,
                Wave3::PassDomain::Compute, "L6", 1u),
            MakePredecessorPass(Wave3::PassKind::PrimarySurfaceExport,
                Wave3::PassDomain::Compute, "L0/L8", 2u),
            MakePredecessorPass(Wave3::PassKind::ComposeTemporal,
                Wave3::PassDomain::Compute, "L8", 3u),
            MakePredecessorPass(Wave3::PassKind::Present,
                Wave3::PassDomain::Present, "L0", 4u)};
        request.wave3Plan.timestampQueryCount =
            static_cast<std::uint32_t>((request.wave3Plan.passes.size() - 1u) * 2u);
        return request;
    }
}

bool RunWave4FrameGraphTests()
{
    using namespace RenderingEngine::Wave4;
    TestContext tests;

    FrameRequest request = MakeRequest();
    FramePlan plan = BuildFramePlan(request);
    tests.Expect(plan.IsReady(),
        "complete Wave 3 predecessor and Wave 4 providers must plan");
    tests.Expect(ValidateFramePlan(plan),
        "generated Wave 4 plan must pass order and direct-ownership audit");
    tests.Expect(plan.passes[0].kind == PassKind::PrimarySurfaceExport
            && plan.passes[1].kind == PassKind::LightMapping
            && plan.passes[2].kind == PassKind::CandidateGeneration
            && plan.passes[3].kind == PassKind::InitialReservoir
            && plan.passes[4].kind == PassKind::TemporalReuse
            && plan.passes[5].kind == PassKind::SpatialReuse
            && plan.passes[6].kind == PassKind::WinnerVisibility
            && plan.passes[7].kind == PassKind::SplitDirectSignal
            && plan.passes[8].kind == PassKind::Reconstruction
            && plan.passes[9].kind == PassKind::HistoryPublish
            && plan.passes[10].kind == PassKind::DebugOutput,
        "default graph must keep the required production order");
    tests.Expect(plan.winnerVisibilityPassCount == 1u,
        "exactly one winner visibility pass must be declared");
    tests.Expect(plan.historyDecision == HistoryDecision::Reset,
        "first frame must reset history");
    tests.Expect(plan.passes[4].noOp,
        "temporal reuse must be an explicit no-op when history resets");

    request = MakeRequest();
    request.framesInFlight = 1u;
    tests.Expect(BuildFramePlan(request).status == PlanStatus::InvalidRequest,
        "Wave 4 must reject fewer than two frame slots like the Vulkan recorder");
    request.framesInFlight = 4u;
    tests.Expect(BuildFramePlan(request).IsReady(),
        "Wave 4 and the Vulkan recorder must share the four-slot upper boundary");
    request.framesInFlight = 5u;
    tests.Expect(BuildFramePlan(request).status == PlanStatus::InvalidRequest,
        "Wave 4 must reject more than four frame slots before recorder attachment");

    request = MakeRequest();

    request.includeReferenceCorrectionVisibility = true;
    plan = BuildFramePlan(request);
    tests.Expect(plan.status == PlanStatus::ProviderUnavailable,
        "optional unbiased reference visibility must fail closed when unavailable");
    request.providers.unbiasedReferenceVisibility = true;
    plan = BuildFramePlan(request);
    tests.Expect(plan.IsReady()
            && plan.passes[5].kind == PassKind::SpatialReuse
            && plan.passes[6].kind == PassKind::ReferenceCorrectionVisibility
            && plan.passes[7].kind == PassKind::WinnerVisibility,
        "available unbiased reference visibility must remain optional and ordered");

    request.includeReferenceCorrectionVisibility = false;
    request.history = {
        true,
        request.frameIndex,
        request.configGeneration,
        request.sceneGeneration,
        request.resourceGeneration,
        request.lightGeneration,
        request.config.restir.manyLightsTier,
        request.config.render.width,
        request.config.render.height,
        request.config.backend,
        request.config.transportModel,
        request.config.executionArchitecture,
        request.config.reconstruction};
    request.frameIndex = 1u;
    plan = BuildFramePlan(request);
    tests.Expect(plan.historyDecision == HistoryDecision::Reuse,
        "exact previous frame and light generation must reuse history");
    tests.Expect(!plan.passes[4].noOp,
        "temporal reuse must execute when history identity is exact");
    tests.Expect(plan.historyReadPhysicalIndex != plan.historyWritePhysicalIndex,
        "history read and write slots must not alias");

    ++request.lightGeneration;
    plan = BuildFramePlan(request);
    tests.Expect(plan.historyDecision == HistoryDecision::Reset,
        "light generation changes must invalidate history");

    request = MakeRequest();
    request.primaryOwnsDirectSignal = false;
    plan = BuildFramePlan(request);
    tests.Expect(plan.status == PlanStatus::InvalidRequest,
        "direct ownership must be explicit");

    request = MakeRequest();
    request.providers.abiV3Published = false;
    plan = BuildFramePlan(request);
    tests.Expect(plan.status == PlanStatus::ProviderUnavailable,
        "Wave 4 GPU ReSTIR must remain blocked before abi-v3 publication");

    if (tests.Passed())
    {
        std::cout << "Wave 4 frame graph order, provider, ownership, and history checks passed.\n";
    }
    return tests.Passed();
}
