#include "renderers/ReSTIRDIRuntime.hpp"
#include "contracts/AbiV3.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "ReSTIR production runtime test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept { return failures_ == 0; }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] RenderingEngine::Renderers::ReSTIRFrameRequest MakeRequest()
    {
        using namespace RenderingEngine;
        Renderers::ReSTIRFrameRequest request{};
        request.config.scene = ScenePreset::ManyLightsRestirArena;
        request.config.backend = TraversalBackend::VulkanRayQuery;
        request.config.executionArchitecture = ExecutionArchitecture::Wavefront;
        request.config.directLightingEstimator =
            DirectLightingEstimator::RestirDirectIllumination;
        request.config.lightSelection =
            LightSelectionStrategy::PowerWeighted;
        request.config.reconstruction = ReconstructionMode::Svgf;
        request.config.render.width = 320u;
        request.config.render.height = 180u;
        request.config.restir.manyLightsTier = ManyLightsTier::Lights1000;
        request.config.restir.biasMode = RestirBiasMode::ReferenceCorrection;
        request.providers = {
            true, true, true, true, true, true, true, true, true, true};
        request.identity = { 8u, 3u, 5u, 7u, 11u, 320u, 180u };
        request.settings.lightCount = 1000u;
        request.settings.initialCandidateCount = 8u;
        request.settings.spatialNeighborCount = 5u;
        request.settings.maximumReservoirM = 32u;
        request.settings.maximumHistoryAge = 20u;
        request.settings.framesInFlight = 3u;
        request.settings.estimatorMode =
            Renderers::ReSTIREstimatorMode::ReferenceCorrection;
        return request;
    }

    [[nodiscard]] std::size_t CountPass(
        const RenderingEngine::Renderers::ReSTIRFramePlan& plan,
        const RenderingEngine::Renderers::ReSTIRPass pass)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            plan.passes,
            [pass](const RenderingEngine::Renderers::ReSTIRScheduledPass& scheduled)
            {
                return scheduled.pass == pass;
            }));
    }

    [[nodiscard]] std::string ReadRestirShader(const std::string_view fileName)
    {
        const std::array roots{
            std::filesystem::path{"RenderingEngine/resources/shaders/restir"},
            std::filesystem::path{"../../resources/shaders/restir"},
            std::filesystem::path{"../../../RenderingEngine/resources/shaders/restir"}
        };
        for (const std::filesystem::path& root : roots)
        {
            const std::filesystem::path path = root / fileName;
            std::ifstream stream(path, std::ios::binary);
            if (stream)
            {
                return {
                    std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>()
                };
            }
        }
        return {};
    }

    class RecordingRecorder final
        : public RenderingEngine::Renderers::IReSTIRGpuRecorder
    {
    public:
        std::vector<std::string> events;
        bool failNextBarrier = false;
        bool returnShortTraceBatch = false;
        std::uint32_t abortCount = 0u;

        RenderingEngine::Renderers::ReSTIRRuntimeStatus BeginFrame(
            const RenderingEngine::Renderers::ReSTIRFramePlan&) override
        {
            events.emplace_back("begin");
            return {};
        }

        RenderingEngine::Renderers::ReSTIRRuntimeStatus RecordCompute(
            const RenderingEngine::Renderers::ReSTIRPass pass,
            std::uint32_t,
            std::uint32_t,
            std::uint32_t) override
        {
            events.emplace_back("compute:" + std::string(
                RenderingEngine::Renderers::ToString(pass)));
            return {};
        }

        RenderingEngine::Renderers::ReSTIRRuntimeStatus RecordBarrier(
            const RenderingEngine::Renderers::ReSTIRBarrier barrier) override
        {
            if (failNextBarrier)
            {
                failNextBarrier = false;
                return {RenderingEngine::Renderers::ReSTIRRuntimeStatusCode::RecordingFailed,
                    "injected barrier failure"};
            }
            using RenderingEngine::Renderers::ReSTIRBarrier;
            switch (barrier)
            {
            case ReSTIRBarrier::ComputeToReconstruction:
                events.emplace_back("barrier:compute-reconstruction");
                break;
            case ReSTIRBarrier::ReconstructionToCompute:
                events.emplace_back("barrier:reconstruction-compute");
                break;
            default:
                events.emplace_back("barrier");
                break;
            }
            return {};
        }

        RenderingEngine::Renderers::ReSTIRRuntimeStatus RecordReconstruction(
            const RenderingEngine::Renderers::ReSTIRFramePlan&) override
        {
            events.emplace_back("reconstruction");
            return {};
        }

        RenderingEngine::Rt::Gpu::GpuTraceBatch BuildTraceAnyBatch(
            RenderingEngine::Renderers::ReSTIRPass,
            const std::uint32_t maximumRayCount) override
        {
            RenderingEngine::Rt::Gpu::GpuTraceBatch batch{};
            batch.rayCount = returnShortTraceBatch && maximumRayCount != 0u
                ? maximumRayCount - 1u
                : maximumRayCount;
            return batch;
        }

        RenderingEngine::Renderers::ReSTIRRuntimeStatus EndFrame(
            const RenderingEngine::Renderers::ReSTIRFramePlan&) override
        {
            events.emplace_back("end");
            return {};
        }

        void AbortFrame() noexcept override
        {
            ++abortCount;
            events.emplace_back("abort");
        }
    };

    class RecordingTraversal final
        : public RenderingEngine::Rt::Gpu::IGpuTraversalBackend
    {
    public:
        std::uint32_t traceAnyCount = 0u;

        RenderingEngine::Rt::Gpu::GpuTraversalBackendDescriptor Descriptor()
            const noexcept override
        {
            return {"test-trace-any", "Test Trace Any", false, true, false};
        }

        RenderingEngine::Rt::Gpu::GpuTraversalStatus BuildOrUpdateScene(
            const RenderingEngine::Rt::Gpu::GpuSceneBuildRequest&) override
        {
            return {};
        }

        RenderingEngine::Rt::Gpu::GpuTraversalStatus RecordTraceClosestBatch(
            const RenderingEngine::Rt::Gpu::GpuTraceBatch&) override
        {
            return {};
        }

        RenderingEngine::Rt::Gpu::GpuTraversalStatus RecordTraceAnyBatch(
            const RenderingEngine::Rt::Gpu::GpuTraceBatch&) override
        {
            ++traceAnyCount;
            return {};
        }
    };
}

bool RunReSTIRDIRuntimeTests()
{
    using namespace RenderingEngine::Renderers;

    TestContext tests;
    ReSTIRFrameRequest request = MakeRequest();
    ReSTIRFramePlan plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.IsReady(), "complete production provider tuple must plan");
    ReSTIRFrameRequest spatialOnly = request;
    spatialOnly.config.restir.reuseStage = RenderingEngine::RestirReuseStage::Spatial;
    spatialOnly.settings.temporalReuse = false;
    spatialOnly.settings.spatialReuse = true;
    const ReSTIRFramePlan spatialPlan = BuildReSTIRFramePlan(spatialOnly);
    tests.Expect(spatialPlan.IsReady() && ValidateReSTIRFramePlan(spatialPlan)
            && spatialPlan.historyDecision != ReSTIRHistoryDecision::Reuse
            && spatialPlan.historyReadPhysicalIndex == 0xffffffffu
            && spatialPlan.spatialNeighborCount == 5u,
        "spatial-only must plan without a temporal history read or implicit temporal dependency");
    tests.Expect(ValidateReSTIRFramePlan(plan), "generated frame plan must validate");
    tests.Expect(plan.primaryDirectOwner == PrimaryDirectLightingOwner::ReSTIRDI,
        "ReSTIR must exclusively own primary-hit direct lighting");
    tests.Expect(CountPass(plan, ReSTIRPass::PrepareWinnerVisibility) == 1u
            && CountPass(plan, ReSTIRPass::ResolveWinnerVisibility) == 1u,
        "winner visibility must be prepared and resolved exactly once");
    tests.Expect(CountPass(plan, ReSTIRPass::PrepareReferenceVisibility) == 1u
            && CountPass(plan, ReSTIRPass::ResolveReferenceVisibility) == 1u,
        "reference-correction mode must retain separately counted visibility");
    tests.Expect(plan.maximumWinnerVisibilityRays == plan.footprint.pixelCount,
        "winner visibility budget must be one ray per primary pixel");
    tests.Expect(plan.maximumReferenceVisibilityRays
            == plan.footprint.pixelCount * 6u,
        "reference visibility budget must name center plus five neighbors");
    tests.Expect(plan.shadowMethod == RenderingEngine::ShadowMethod::Physical
            && plan.shadowRaysPerVisibility
                == RenderingEngine::Contracts::AbiV3::
                    kRestirPhysicalVisibilityRayCount,
        "physical ReSTIR visibility must retain its exact one-ray contract");
    const std::uint64_t pixelCount = 320ull * 180ull;
    tests.Expect(plan.footprint.reservoirBytes
            == pixelCount
                * sizeof(RenderingEngine::Contracts::AbiV3::GpuRestirReservoirV3)
                * 3u
            && plan.footprint.historyBytes
                == pixelCount
                    * sizeof(RenderingEngine::Contracts::AbiV3::GpuRestirReservoirV3)
                    * 6u,
        "three staging reservoirs and a 2N history ring must be counted exactly once");
    tests.Expect(plan.footprint.lightMappingBytes == 1000ull * 8ull
            && plan.minimumSet5BufferRanges[
                RenderingEngine::Contracts::AbiV3::RestirBinding::CurrentToPreviousLightIndex]
                == 1000ull * 8ull
            && plan.minimumSet5BufferRanges[
                RenderingEngine::Contracts::AbiV3::RestirBinding::PreviousToCurrentLightIndex]
                == 0u,
        "reset frames must size the uint2 current-light map without inventing a previous map");
    tests.Expect(plan.minimumSet5BufferRanges[
                RenderingEngine::Contracts::AbiV3::RestirBinding::VisibilityResults]
                == pixelCount
                    * sizeof(RenderingEngine::Contracts::AbiV1::GpuHitQueueRecordV1)
            && plan.minimumSet5BufferRanges[
                RenderingEngine::Contracts::AbiV3::RestirBinding::ReferenceVisibility]
                == pixelCount * 6u
                    * sizeof(RenderingEngine::Contracts::AbiV1::GpuHitQueueRecordV1)
            && plan.minimumSet5BufferRanges[
                RenderingEngine::Contracts::AbiV3::RestirBinding::ShadowRayQueue]
                == pixelCount * 6u
                    * sizeof(RenderingEngine::Contracts::AbiV1::GpuRayQueueRecordV1),
        "TraceAny hit outputs and shared shadow-ray input must retain distinct ABI strides");

    request = MakeRequest();
    request.identity.width = 960u;
    request.identity.height = 540u;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.IsReady()
            && plan.identity.width == 960u
            && plan.identity.height == 540u
            && plan.footprint.pixelCount == 960ull * 540ull,
        "a real swapchain resize must use the frame identity extent without rejecting stale requested dimensions");

    const std::string productionShader = ReadRestirShader(
        "RestirProductionV3.hlsli");
    tests.Expect(!productionShader.empty()
            && productionShader.find(
                "const float3 diffuseAlbedo = max(surface.diffuseAlbedo.xyz, 0.0f);")
                != std::string::npos
            && productionShader.find(
                "const float3 f0 = max(surface.specularAlbedo.xyz, 0.0f);")
                != std::string::npos
            && productionShader.find("diffuse = radiance * diffuseAlbedo")
                != std::string::npos
            && productionShader.find("(1.0f - metallic) * baseColor")
                == std::string::npos
            && productionShader.find("lerp(max(surface.specularAlbedo")
                == std::string::npos,
        "ReSTIR production shading must consume ABI-v2 albedos without a second metallic decomposition");

    request = MakeRequest();
    request.config.shadowMethod = RenderingEngine::ShadowMethod::Pcf;
    request.identity.shadowMethod = RenderingEngine::ShadowMethod::Pcf;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.IsReady()
            && plan.shadowRaysPerVisibility
                == RenderingEngine::Contracts::AbiV3::kRestirPcfFilterRayCount
            && plan.maximumWinnerVisibilityRays
                == pixelCount
                    * RenderingEngine::Contracts::AbiV3::kRestirPcfFilterRayCount
            && plan.maximumReferenceVisibilityRays
                == pixelCount * 6u
                    * RenderingEngine::Contracts::AbiV3::kRestirPcfFilterRayCount,
        "PCF must budget every winner and reference-correction filter ray");

    request = MakeRequest();
    request.config.shadowMethod = RenderingEngine::ShadowMethod::Pcss;
    request.identity.shadowMethod = RenderingEngine::ShadowMethod::Pcss;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.IsReady()
            && plan.shadowRaysPerVisibility
                == RenderingEngine::Contracts::AbiV3::kRestirPcssVisibilityRayCount
            && plan.maximumWinnerVisibilityRays
                == pixelCount
                    * RenderingEngine::Contracts::AbiV3::kRestirPcssVisibilityRayCount
            && plan.minimumSet5BufferRanges[
                RenderingEngine::Contracts::AbiV3::RestirBinding::ShadowRayQueue]
                == pixelCount * 6u
                    * RenderingEngine::Contracts::AbiV3::kRestirPcssVisibilityRayCount
                    * sizeof(RenderingEngine::Contracts::AbiV1::GpuRayQueueRecordV1),
        "PCSS must expose its four blocker plus twelve filter rays in the queue ABI");

    const std::string winnerResolveShader = ReadRestirShader(
        "RestirWinnerResolveV3.comp.hlsl");
    const std::string referenceResolveShader = ReadRestirShader(
        "RestirReferenceResolveV3.comp.hlsl");
    const std::string publishShader = ReadRestirShader(
        "RestirPublishDirectV3.comp.hlsl");
    tests.Expect(productionShader.find("RestirShadowKernelAngularRadiusV3")
                != std::string::npos
            && winnerResolveShader.find("kRestirPcssBlockerRayCountV3")
                != std::string::npos
            && referenceResolveShader.find("target * visibility")
                != std::string::npos
            && publishShader.find("reservoir.selectedTerms.w")
                != std::string::npos,
        "PCF/PCSS visibility must reach winner, reference, and final direct contribution paths");

    request = MakeRequest();
    request.history.valid = true;
    request.history.published = request.identity;
    --request.history.published.frameIndex;
    request.history.physicalIndex = 4u;
    request.settings.previousLightCount = 1000u;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.historyDecision == ReSTIRHistoryDecision::Reuse,
        "exact previous frame and generation tuple must reuse history");
    tests.Expect(plan.historyReadPhysicalIndex != plan.historyWritePhysicalIndex,
        "history ping-pong must not alias read and write physical slots");
    tests.Expect(plan.historyReadPhysicalIndex == 4u
            && plan.historyWritePhysicalIndex == 2u
            && plan.footprint.lightMappingBytes == 2000ull * 8ull,
        "frame eight of a three-flight 2N ring must read slot four and write slot two");

    request.config.shadowMethod = RenderingEngine::ShadowMethod::Pcf;
    request.identity.shadowMethod = RenderingEngine::ShadowMethod::Pcf;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.IsReady()
            && plan.historyDecision == ReSTIRHistoryDecision::Reset,
        "changing shadow method must invalidate otherwise exact ReSTIR history");

    request = MakeRequest();
    request.config.shadowMethod = RenderingEngine::ShadowMethod::Pcf;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.status.code == ReSTIRRuntimeStatusCode::InvalidRequest,
        "config and frame-identity shadow methods must not drift");

    request = MakeRequest();
    request.history.valid = true;
    request.history.published = request.identity;
    --request.history.published.frameIndex;
    request.history.physicalIndex = 4u;
    request.settings.previousLightCount = 1000u;
    ++request.history.published.lightGeneration;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.historyDecision == ReSTIRHistoryDecision::Reset,
        "light generation changes must invalidate old reservoirs");
    tests.Expect(CountPass(plan, ReSTIRPass::TemporalReuse) == 1u
            && CountPass(plan, ReSTIRPass::SpatialReuse) == 1u,
        "reset frames must retain explicit non-aliasing temporal/spatial copy passes");

    request = MakeRequest();
    request.settings.estimatorMode = ReSTIREstimatorMode::ExplicitlyBiased;
    request.config.restir.biasMode =
        RenderingEngine::RestirBiasMode::ExplicitlyBiased;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(CountPass(plan, ReSTIRPass::PrepareReferenceVisibility) == 0u
            && plan.maximumReferenceVisibilityRays == 0u,
        "biased mode must not pretend to run reference visibility");

    request = MakeRequest();
    request.providers.currentPreviousLightMapping = false;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.status.code == ReSTIRRuntimeStatusCode::ProviderUnavailable
            && plan.passes.empty(),
        "missing stable light mapping must fail before recording");

    request = MakeRequest();
    request.config.directLightingEstimator =
        RenderingEngine::DirectLightingEstimator::MultipleImportanceSampling;
    plan = BuildReSTIRFramePlan(request);
    tests.Expect(plan.status.code == ReSTIRRuntimeStatusCode::InvalidRequest,
        "conventional direct-light owner must not enter ReSTIR runtime");

    request = MakeRequest();
    plan = BuildReSTIRFramePlan(request);
    ReSTIRFramePlan mutatedPlan = plan;
    mutatedPlan.passes[1].recordsWinnerTraceAny = true;
    tests.Expect(!ValidateReSTIRFramePlan(mutatedPlan),
        "trace flags on a non-prepare pass must invalidate the canonical plan");
    mutatedPlan = plan;
    ++mutatedPlan.passes[2].dispatchGroupCountX;
    tests.Expect(!ValidateReSTIRFramePlan(mutatedPlan),
        "mutated dispatch groups must invalidate the canonical plan");
    mutatedPlan = plan;
    ++mutatedPlan.shadowRaysPerVisibility;
    tests.Expect(!ValidateReSTIRFramePlan(mutatedPlan),
        "visibility ray multiplier is an immutable shadow-method contract");

    RecordingRecorder recorder;
    RecordingTraversal traversal;
    const ReSTIRRuntimeStatus recordStatus =
        ReSTIRDIRuntime{}.RecordFrame(plan, recorder, traversal);
    const auto EventIndex = [&recorder](const std::string_view event)
    {
        return std::ranges::find(recorder.events, event) - recorder.events.begin();
    };
    tests.Expect(static_cast<bool>(recordStatus)
            && EventIndex("compute:restir-publish-split-direct")
                < EventIndex("barrier:compute-reconstruction")
            && EventIndex("barrier:compute-reconstruction")
                < EventIndex("reconstruction")
            && EventIndex("reconstruction")
                < EventIndex("barrier:reconstruction-compute")
            && EventIndex("barrier:reconstruction-compute")
                < EventIndex("compute:restir-publish-history"),
        "split direct, Wave 3 reconstruction, and history must execute in immutable order");

    RecordingRecorder recoveringRecorder;
    recoveringRecorder.failNextBarrier = true;
    const ReSTIRRuntimeStatus injectedFailure =
        ReSTIRDIRuntime{}.RecordFrame(plan, recoveringRecorder, traversal);
    tests.Expect(!static_cast<bool>(injectedFailure)
            && recoveringRecorder.abortCount == 1u,
        "a partial recording failure must abort recorder-side frame state");
    const ReSTIRRuntimeStatus recovered =
        ReSTIRDIRuntime{}.RecordFrame(plan, recoveringRecorder, traversal);
    tests.Expect(static_cast<bool>(recovered)
            && recoveringRecorder.abortCount == 1u,
        "the recorder contract must permit a clean frame after an aborted attempt");

    RecordingRecorder shortBatchRecorder;
    shortBatchRecorder.returnShortTraceBatch = true;
    RecordingTraversal shortBatchTraversal;
    const ReSTIRRuntimeStatus shortBatchFailure =
        ReSTIRDIRuntime{}.RecordFrame(plan, shortBatchRecorder, shortBatchTraversal);
    tests.Expect(!static_cast<bool>(shortBatchFailure)
            && shortBatchRecorder.abortCount == 1u
            && shortBatchTraversal.traceAnyCount == 0u,
        "a short TraceAny batch must fail and abort before unresolved tail hits can be consumed");

    if (tests.Passed())
    {
        std::cout << "ReSTIR frame ordering, history identity, visibility, and ownership checks passed.\n";
    }
    return tests.Passed();
}
