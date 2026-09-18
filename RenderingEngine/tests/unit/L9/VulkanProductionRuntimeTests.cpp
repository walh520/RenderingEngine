#include "restir/VulkanProductionRuntime.hpp"

#include "contracts/DescriptorRegistryV3.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace
{
    using namespace RenderingEngine;

    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "L9 production-runtime test failed: "
                    << message << '\n';
                ++failures_;
            }
        }
        [[nodiscard]] bool Passed() const noexcept { return failures_ == 0; }

    private:
        std::uint32_t failures_ = 0u;
    };

    template <typename Handle>
    [[nodiscard]] Handle FakeHandle(const std::uintptr_t value) noexcept
    {
        if constexpr (std::is_pointer_v<Handle>)
        {
            return reinterpret_cast<Handle>(value);
        }
        else
        {
            return static_cast<Handle>(value);
        }
    }

    class FakeTraversal final : public Rt::Gpu::IGpuTraversalBackend
    {
    public:
        [[nodiscard]] Rt::Gpu::GpuTraversalBackendDescriptor Descriptor()
            const noexcept override
        {
            return { "l9-production-test", "L9 production test", true, true,
                true };
        }
        [[nodiscard]] Rt::Gpu::GpuTraversalStatus BuildOrUpdateScene(
            const Rt::Gpu::GpuSceneBuildRequest&) override
        {
            return {};
        }
        [[nodiscard]] Rt::Gpu::GpuTraversalStatus RecordTraceClosestBatch(
            const Rt::Gpu::GpuTraceBatch&) override
        {
            return {};
        }
        [[nodiscard]] Rt::Gpu::GpuTraversalStatus RecordTraceAnyBatch(
            const Rt::Gpu::GpuTraceBatch&) override
        {
            return {};
        }
    };

    class FakeReconstruction final
        : public Renderers::IVulkanReSTIRReconstructionRecorder
    {
    public:
        [[nodiscard]] Renderers::ReSTIRRuntimeStatus RecordReconstruction(
            const Renderers::ReSTIRFramePlan&,
            const VkCommandBuffer,
            const std::array<VkDescriptorSet,
                Renderers::kVulkanReSTIRSetCount>&) override
        {
            return {};
        }
    };

    [[nodiscard]] Renderers::ReSTIRFramePlan MakePlan(
        const std::uint32_t lightCount)
    {
        using namespace Contracts::AbiV3;
        constexpr std::uint64_t pixelCount = 32u;
        constexpr std::uint32_t candidateCount = 4u;
        constexpr std::uint32_t neighborCount = 4u;

        Renderers::ReSTIRFramePlan plan{};
        plan.identity = { 0u, 11u, 12u, 13u, 14u, 8u, 4u };
        plan.primaryDirectOwner = Renderers::PrimaryDirectLightingOwner::ReSTIRDI;
        plan.historyDecision = Renderers::ReSTIRHistoryDecision::Reset;
        plan.historyReadPhysicalIndex =
            Renderers::kVulkanReSTIRInvalidHistoryIndex;
        plan.historyWritePhysicalIndex = 0u;
        plan.maximumWinnerVisibilityRays = pixelCount;
        plan.currentLightCount = lightCount;
        plan.initialCandidateCount = candidateCount;
        plan.spatialNeighborCount = neighborCount;
        plan.framesInFlight = 2u;
        plan.writeDebug = true;
        plan.footprint.pixelCount = pixelCount;

        auto& ranges = plan.minimumSet5BufferRanges;
        ranges[RestirBinding::Parameters] = 128u;
        ranges[RestirBinding::Candidates] =
            pixelCount * candidateCount * 160u;
        ranges[RestirBinding::DebugRecord] = pixelCount * 64u;
        ranges[RestirBinding::Statistics] = 64u;
        ranges[RestirBinding::VisibilityResults] = pixelCount * 96u;
        ranges[RestirBinding::ValidationReasons] = pixelCount * 4u;
        ranges[RestirBinding::CurrentToPreviousLightIndex] =
            static_cast<std::uint64_t>(lightCount) * 8u;
        ranges[RestirBinding::NeighborIndices] =
            pixelCount * neighborCount * 4u;
        ranges[RestirBinding::ShadowRayQueue] = pixelCount * 64u;
        ranges[RestirBinding::DirectDiffuse] = pixelCount * 16u;
        ranges[RestirBinding::DirectSpecular] = pixelCount * 16u;
        ranges[RestirBinding::InitialReservoir] = pixelCount * 256u;
        ranges[RestirBinding::TemporalReservoir] = pixelCount * 256u;
        ranges[RestirBinding::SpatialReservoir] = pixelCount * 256u;
        ranges[RestirBinding::PublishedReservoir] = pixelCount * 256u;

        constexpr std::array passes = {
            Renderers::ReSTIRPass::ClearStatistics,
            Renderers::ReSTIRPass::GenerateCandidates,
            Renderers::ReSTIRPass::InitialReservoir,
            Renderers::ReSTIRPass::TemporalReuse,
            Renderers::ReSTIRPass::SpatialReuse,
            Renderers::ReSTIRPass::PrepareWinnerVisibility,
            Renderers::ReSTIRPass::ResolveWinnerVisibility,
            Renderers::ReSTIRPass::PublishSplitDirectSignal,
            Renderers::ReSTIRPass::Reconstruction,
            Renderers::ReSTIRPass::PublishHistory,
            Renderers::ReSTIRPass::WriteDebug};
        for (const Renderers::ReSTIRPass pass : passes)
        {
            plan.passes.push_back({ pass, 1u, 1u, 1u,
                false, false,
                pass == Renderers::ReSTIRPass::Reconstruction });
        }
        return plan;
    }

    struct AttachmentFixture final
    {
        Renderers::VulkanReSTIRFrameContext frame{};
        Restir::VulkanReSTIRProductionEvidence evidence{};
        FakeReconstruction reconstruction{};
    };

    [[nodiscard]] AttachmentFixture MakeAttachment(
        const Renderers::ReSTIRFramePlan& plan)
    {
        using namespace Contracts::AbiV3;
        AttachmentFixture fixture{};
        const Restir::VulkanReSTIRResourcePlan resources =
            Restir::BuildVulkanReSTIRResourcePlan(plan);

        fixture.frame.commandBuffer = FakeHandle<VkCommandBuffer>(1u);
        for (std::size_t set = 0u;
            set < fixture.frame.descriptorSets.size(); ++set)
        {
            if (set != static_cast<std::size_t>(DescriptorSet::Restir))
            {
                fixture.frame.descriptorSets[set] =
                    FakeHandle<VkDescriptorSet>(100u + set);
            }
        }
        fixture.frame.referenceTraversalSet =
            FakeHandle<VkDescriptorSet>(201u);
        fixture.frame.winnerTraversalSet =
            FakeHandle<VkDescriptorSet>(202u);
        fixture.frame.sceneFingerprint = 0x1234u;
        fixture.frame.sceneGeneration = static_cast<std::uint32_t>(
            plan.identity.sceneGeneration);
        fixture.frame.frameSlot = 0u;
        fixture.frame.historyReadPhysicalIndex = plan.historyReadPhysicalIndex;
        fixture.frame.historyWritePhysicalIndex = plan.historyWritePhysicalIndex;
        fixture.frame.reconstructionRecorder = &fixture.reconstruction;

        for (std::uint32_t binding = 0u;
            binding < Renderers::kVulkanReSTIRSet5BindingCount; ++binding)
        {
            fixture.frame.resources.valid[binding] = true;
            if (binding != RestirBinding::DebugImage)
            {
                fixture.frame.resources.buffers[binding] = {
                    FakeHandle<VkBuffer>(1000u + binding), 0u,
                    resources.descriptorBufferBytes[binding] };
            }
        }
        fixture.frame.resources.debugImage = {
            VK_NULL_HANDLE, FakeHandle<VkImageView>(300u),
            VK_IMAGE_LAYOUT_GENERAL };
        fixture.frame.resources.debugImageExtent = {
            plan.identity.width, plan.identity.height };
        fixture.frame.resources.debugImageFormat =
            VK_FORMAT_R32G32B32A32_SFLOAT;

        fixture.frame.referenceTraversalRays =
            fixture.frame.resources.buffers[RestirBinding::ShadowRayQueue];
        fixture.frame.winnerTraversalRays =
            fixture.frame.resources.buffers[RestirBinding::ShadowRayQueue];
        fixture.frame.referenceTraversalHits =
            fixture.frame.resources.buffers[RestirBinding::ReferenceVisibility];
        fixture.frame.winnerTraversalHits =
            fixture.frame.resources.buffers[RestirBinding::VisibilityResults];

        fixture.evidence.primarySurfaceV2 = {
            FakeHandle<VkBuffer>(400u), 0u,
            plan.footprint.pixelCount
                * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2) };
        fixture.evidence.motionVectorsV2 = {
            FakeHandle<VkBuffer>(401u), 0u,
            plan.footprint.pixelCount
                * sizeof(Contracts::AbiV2::GpuGBufferRecordV2) };
        fixture.evidence.pbrLightTableL6 = {
            FakeHandle<VkBuffer>(402u), 0u,
            static_cast<VkDeviceSize>(plan.currentLightCount) * 96u };
        fixture.evidence.pbrLightCount = plan.currentLightCount;
        fixture.evidence.reconstructionOutput = {
            VK_NULL_HANDLE, FakeHandle<VkImageView>(403u),
            VK_IMAGE_LAYOUT_GENERAL };
        fixture.evidence.reconstructionOutputExtent = {
            plan.identity.width, plan.identity.height };
        fixture.evidence.sceneFingerprint = fixture.frame.sceneFingerprint;
        fixture.evidence.sceneGeneration = static_cast<std::uint32_t>(
            plan.identity.sceneGeneration);
        fixture.evidence.resourceGeneration = static_cast<std::uint32_t>(
            plan.identity.resourceGeneration);
        fixture.evidence.lightGeneration = static_cast<std::uint32_t>(
            plan.identity.lightGeneration);
        fixture.evidence.pipelineLayout = FakeHandle<VkPipelineLayout>(500u);
        for (std::size_t pass = 0u;
            pass < fixture.evidence.pipelines.size(); ++pass)
        {
            if (pass != static_cast<std::size_t>(
                    Renderers::ReSTIRPass::Reconstruction))
            {
                fixture.evidence.pipelines[pass] =
                    FakeHandle<VkPipeline>(600u + pass);
            }
        }
        return fixture;
    }
}

bool RunVulkanProductionRuntimeTests()
{
    using namespace RenderingEngine;
    TestContext tests;
    FakeTraversal traversal;

    VkDeviceSize previousTotal = 0u;
    for (const std::uint32_t tier : { 100u, 1000u, 10000u })
    {
        const Renderers::ReSTIRFramePlan plan = MakePlan(tier);
        const Restir::VulkanReSTIRResourcePlan resources =
            Restir::BuildVulkanReSTIRResourcePlan(plan);
        tests.Expect(resources.IsReady(),
            "100/1k/10k tier must produce a finite resource plan");
        tests.Expect(resources.historyRingSlotCount == 4u,
            "two frames in flight must allocate a 2N four-slot history ring");
        tests.Expect(resources.historyRingBytes
                == plan.footprint.pixelCount * 256u * 4u,
            "history ring bytes must be exact P*reservoir-stride*2N");
        tests.Expect(resources.minimumOwnerPayloadBytes > previousTotal,
            "larger real light tables must increase owner allocation size");
        previousTotal = resources.minimumOwnerPayloadBytes;

        AttachmentFixture fixture = MakeAttachment(plan);
        const Restir::VulkanReSTIRProductionReport report =
            Restir::ValidateVulkanReSTIRProductionAttachment(
                plan, fixture.frame, fixture.evidence, traversal);
        tests.Expect(report.IsReady(),
            "complete production handles and ranges must pass attachment");
    }

    const Renderers::ReSTIRFramePlan plan = MakePlan(100u);
    AttachmentFixture fixture = MakeAttachment(plan);
    fixture.evidence.primarySurfaceV2 = {};
    Restir::VulkanReSTIRProductionReport report =
        Restir::ValidateVulkanReSTIRProductionAttachment(
            plan, fixture.frame, fixture.evidence, traversal);
    tests.Expect(report.HasIssue(
            Restir::VulkanProductionIssue::MissingPrimarySurfaceV2),
        "missing primary-surface producer must fail closed");

    fixture = MakeAttachment(plan);
    fixture.evidence.pbrLightCount = 99u;
    report = Restir::ValidateVulkanReSTIRProductionAttachment(
        plan, fixture.frame, fixture.evidence, traversal);
    tests.Expect(report.HasIssue(
            Restir::VulkanProductionIssue::LightCountMismatch),
        "light count evidence must equal the selected tier");

    fixture = MakeAttachment(plan);
    fixture.evidence.reconstructionOutput = {};
    report = Restir::ValidateVulkanReSTIRProductionAttachment(
        plan, fixture.frame, fixture.evidence, traversal);
    tests.Expect(report.HasIssue(
            Restir::VulkanProductionIssue::MissingReconstructionOutput),
        "missing final reconstruction image must fail closed");

    fixture = MakeAttachment(plan);
    fixture.evidence.pipelines[static_cast<std::size_t>(
        Renderers::ReSTIRPass::GenerateCandidates)] = VK_NULL_HANDLE;
    report = Restir::ValidateVulkanReSTIRProductionAttachment(
        plan, fixture.frame, fixture.evidence, traversal);
    tests.Expect(report.HasIssue(
            Restir::VulkanProductionIssue::MissingComputePipeline),
        "missing candidate-export pipeline must fail closed");

    fixture = MakeAttachment(plan);
    fixture.frame.winnerTraversalHits = fixture.frame.referenceTraversalHits;
    report = Restir::ValidateVulkanReSTIRProductionAttachment(
        plan, fixture.frame, fixture.evidence, traversal);
    tests.Expect(report.HasIssue(
            Restir::VulkanProductionIssue::InvalidTraversalAttachment),
        "aliased reference/winner TraceAny hits must fail closed");

    Renderers::ReSTIRFramePlan arbitraryLightCount = MakePlan(101u);
    const Restir::VulkanReSTIRResourcePlan arbitraryResources =
        Restir::BuildVulkanReSTIRResourcePlan(arbitraryLightCount);
    tests.Expect(arbitraryResources.IsReady(),
        "mixed-scene ReSTIR must accept any non-zero canonical light count");
    Renderers::ReSTIRFramePlan emptyLightTable = MakePlan(0u);
    const Restir::VulkanReSTIRResourcePlan emptyResources =
        Restir::BuildVulkanReSTIRResourcePlan(emptyLightTable);
    tests.Expect(!emptyResources.IsReady(),
        "ReSTIR must reject an empty canonical light table");

    for (const Renderers::ReSTIRScheduledPass& scheduled : plan.passes)
    {
        const std::string_view file =
            Restir::VulkanReSTIRShaderFileName(scheduled.pass);
        tests.Expect(
            scheduled.pass == Renderers::ReSTIRPass::Reconstruction
                ? file.empty() : !file.empty(),
            "every compute pass must map to a concrete ABI-v3 SPIR-V artifact");
    }

    if (tests.Passed())
    {
        std::cout << "L9 production Vulkan attachment, 2N resource planning, "
            "and ABI-v3 pipeline manifest checks passed.\n";
    }
    return tests.Passed();
}
