#include "contracts/AbiV3.hpp"
#include "renderers/VulkanReSTIRRecorder.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace
{
    using RenderingEngine::Contracts::AbiV3::DescriptorSet;
    using RenderingEngine::Renderers::IReSTIRGpuRecorder;
    using RenderingEngine::Renderers::VulkanReSTIRRecorder;

    static_assert(std::is_base_of_v<IReSTIRGpuRecorder, VulkanReSTIRRecorder>);
    static_assert(RenderingEngine::Renderers::kVulkanReSTIRSetCount == 7u);
    static_assert(RenderingEngine::Renderers::kVulkanReSTIRSet5BindingCount == 31u);

    static_assert(static_cast<std::uint32_t>(DescriptorSet::Restir) == 5u);
    static_assert(VulkanReSTIRRecorder::DescriptorTypeForBinding(
        RenderingEngine::Contracts::AbiV3::RestirBinding::Parameters)
        == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    static_assert(VulkanReSTIRRecorder::DescriptorTypeForBinding(
        RenderingEngine::Contracts::AbiV3::RestirBinding::DebugImage)
        == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    static_assert(VulkanReSTIRRecorder::DescriptorTypeForBinding(
        RenderingEngine::Contracts::AbiV3::RestirBinding::PublishedReservoir)
        == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    static_assert(VulkanReSTIRRecorder::DescriptorTypeForBinding(
        RenderingEngine::Contracts::AbiV3::RestirBinding::PreviousPublishedReservoir)
        == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::CurrentToPreviousLightIndex == 20u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::PreviousToCurrentLightIndex == 21u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::NeighborIndices == 22u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::ShadowRayQueue == 23u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::DirectDiffuse == 24u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::DirectSpecular == 25u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::InitialReservoir == 26u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::TemporalReservoir == 27u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::SpatialReservoir == 28u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::PublishedReservoir == 29u);
    static_assert(RenderingEngine::Contracts::AbiV3::RestirBinding::PreviousPublishedReservoir == 30u);

    // The recorder intentionally has no test-only fallback: production setup
    // must provide Vulkan handles, all canonical descriptors, and both
    // traversal descriptor sets before BeginFrame can succeed.
    static_assert(RenderingEngine::Renderers::kVulkanReSTIRInvalidHistoryIndex
        == 0xffffffffu);

    static_assert(VulkanReSTIRRecorder::ExternalSetLayoutsRequired(
        VK_NULL_HANDLE));
    static_assert(VulkanReSTIRRecorder::RequiresFollowingComputeBarrier(
        RenderingEngine::Renderers::ReSTIRPass::PublishHistory, true));
    static_assert(!VulkanReSTIRRecorder::RequiresFollowingComputeBarrier(
        RenderingEngine::Renderers::ReSTIRPass::PublishHistory, false));
    static_assert(!VulkanReSTIRRecorder::RequiresFollowingComputeBarrier(
        RenderingEngine::Renderers::ReSTIRPass::WriteDebug, false));
}

bool RunVulkanReSTIRRecorderContractTests()
{
    int failures = 0;
    const auto Expect = [&failures](const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "Vulkan ReSTIR recorder contract test failed: "
                << message << '\n';
            ++failures;
        }
    };
    const VkBuffer firstBuffer = reinterpret_cast<VkBuffer>(
        static_cast<std::uintptr_t>(1u));
    const VkBuffer secondBuffer = reinterpret_cast<VkBuffer>(
        static_cast<std::uintptr_t>(2u));
    const VkDescriptorBufferInfo first{firstBuffer, 0u, 256u};
    const VkDescriptorBufferInfo overlapping{firstBuffer, 128u, 256u};
    const VkDescriptorBufferInfo adjacent{firstBuffer, 256u, 256u};
    const VkDescriptorBufferInfo separate{secondBuffer, 0u, 256u};
    const VkDescriptorBufferInfo unknownEnd{firstBuffer, 0u, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo referenceHits{firstBuffer, 256u, 256u};
    const VkDescriptorBufferInfo winnerHits{firstBuffer, 512u, 256u};
    const VkDescriptorBufferInfo mismatchedRays{firstBuffer, 1u, 255u};
    Expect(VulkanReSTIRRecorder::BufferViewsOverlap(first, overlapping),
        "partially overlapping subranges must be rejected");
    Expect(!VulkanReSTIRRecorder::BufferViewsOverlap(first, adjacent),
        "adjacent finite subranges must remain legal");
    Expect(!VulkanReSTIRRecorder::BufferViewsOverlap(first, separate),
        "different Vulkan buffers cannot overlap");
    Expect(VulkanReSTIRRecorder::BufferViewsOverlap(first, unknownEnd),
        "VK_WHOLE_SIZE must fail closed when allocation size is unavailable");
    Expect(VulkanReSTIRRecorder::SameBufferView(first, first)
            && !VulkanReSTIRRecorder::SameBufferView(first, mismatchedRays),
        "set-2 traversal rays must exactly match the set-5 shadow-ray view");
    Expect(VulkanReSTIRRecorder::TraceAnyViewsNonOverlapping(
            first, referenceHits, winnerHits),
        "shared rays and separate reference/winner hit outputs may use adjacent ranges");
    Expect(!VulkanReSTIRRecorder::TraceAnyViewsNonOverlapping(
            first, overlapping, winnerHits),
        "TraceAny ray input must not overlap either hit output");
    Expect(VulkanReSTIRRecorder::DebugImageMetadataValid(
            VK_FORMAT_R32G32B32A32_SFLOAT, VkExtent2D{320u, 180u})
            && !VulkanReSTIRRecorder::DebugImageMetadataValid(
                VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent2D{320u, 180u}),
        "debug image metadata must name the writable ABI-v3 float4 format");
    Expect(VulkanReSTIRRecorder::DebugImageCoversPlan(
            VkExtent2D{320u, 180u}, 320u, 180u)
            && !VulkanReSTIRRecorder::DebugImageCoversPlan(
                VkExtent2D{319u, 180u}, 320u, 180u),
        "debug image extent must cover the complete frame plan");
    Expect(!VulkanReSTIRRecorder::ExternalSetLayoutsRequired(
            reinterpret_cast<VkPipelineLayout>(static_cast<std::uintptr_t>(1u))),
        "an externally supplied pipeline layout must not require six redundant layouts");
    return failures == 0;
}
