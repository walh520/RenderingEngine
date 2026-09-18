#include "RtPipelineBackend.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace RenderingEngine::Rt::Hardware
{
    std::uint32_t ComputeRtDispatchChunkCount(
        const std::uint32_t rayCount,
        const std::uint32_t maximumInvocationCount) noexcept
    {
        if (rayCount == 0u || maximumInvocationCount == 0u)
        {
            return 0u;
        }
        return 1u + (rayCount - 1u) / maximumInvocationCount;
    }

    namespace
    {
        [[nodiscard]] Status CreateDescriptorLayout(
            VkDevice device,
            std::span<const VkDescriptorSetLayoutBinding> bindings,
            VkDescriptorSetLayout& output) noexcept
        {
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.bindingCount = static_cast<std::uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            const VkResult result = vkCreateDescriptorSetLayout(device, &info, nullptr, &output);
            return result == VK_SUCCESS
                ? Status::Success()
                : Status::Failure(result, "vkCreateDescriptorSetLayout failed for RT Pipeline");
        }

        void UpdateScene(
            VkDevice device,
            VkDescriptorSet sceneSet,
            const HardwareSceneBufferBindings& bindings) noexcept
        {
            const std::array infos{bindings.constants, bindings.vertices,
                bindings.indices, bindings.geometries, bindings.instances,
                bindings.materials, bindings.lights};
            std::array<VkWriteDescriptorSet, 7> writes{};
            for (std::size_t index = 0u; index < writes.size(); ++index)
            {
                writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[index].dstSet = sceneSet;
                writes[index].dstBinding = static_cast<std::uint32_t>(index);
                writes[index].descriptorCount = 1u;
                writes[index].descriptorType = index == 0u
                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[index].pBufferInfo = &infos[index];
            }
            vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
        }

        void UpdateTraversal(
            VkDevice device,
            VkDescriptorSet traversalSet,
            const RayQueryTraversalBindings& bindings) noexcept
        {
            VkWriteDescriptorSetAccelerationStructureKHR accelerationStructure{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
            accelerationStructure.accelerationStructureCount = 1u;
            accelerationStructure.pAccelerationStructures = &bindings.topLevelAccelerationStructure;
            std::array<VkWriteDescriptorSet, 5> writes{};
            writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[0].pNext = &accelerationStructure;
            writes[0].dstSet = traversalSet;
            writes[0].dstBinding = 0u;
            writes[0].descriptorCount = 1u;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[1].dstSet = traversalSet;
            writes[1].dstBinding = 1u;
            writes[1].descriptorCount = 1u;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &bindings.rays;
            writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[2].dstSet = traversalSet;
            writes[2].dstBinding = 2u;
            writes[2].descriptorCount = 1u;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &bindings.hits;
            writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[3].dstSet = traversalSet;
            writes[3].dstBinding = 8u;
            writes[3].descriptorCount = 1u;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[3].pImageInfo = &bindings.alphaAtlas;
            writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[4].dstSet = traversalSet;
            writes[4].dstBinding = 9u;
            writes[4].descriptorCount = 1u;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[4].pImageInfo = &bindings.alphaSampler;
            vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
        }
    }

    RtPipelineBackend::~RtPipelineBackend()
    {
        Reset();
    }

    RtPipelineBackend::RtPipelineBackend(RtPipelineBackend&& other) noexcept
    {
        *this = std::move(other);
    }

    RtPipelineBackend& RtPipelineBackend::operator=(RtPipelineBackend&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            dispatch_ = std::exchange(other.dispatch_, nullptr);
            emptyFrameLayout_ = std::exchange(other.emptyFrameLayout_, VK_NULL_HANDLE);
            sceneAdapterLayout_ = std::exchange(other.sceneAdapterLayout_, VK_NULL_HANDLE);
            traversalLayout_ = std::exchange(other.traversalLayout_, VK_NULL_HANDLE);
            pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
            pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
            sbt_ = std::move(other.sbt_);
            maxRayDispatchInvocationCount_ = std::exchange(other.maxRayDispatchInvocationCount_, 0u);
        }
        return *this;
    }

    Status RtPipelineBackend::Create(
        const DeviceBufferAllocator& allocator,
        const DeviceDispatch& dispatch,
        const HardwareRtLimits& limits,
        std::span<const std::uint32_t> shaderLibrarySpirv) noexcept
    {
        if (allocator.Device() == VK_NULL_HANDLE || !dispatch.HasRtPipelineFunctions() ||
            shaderLibrarySpirv.empty() || limits.maxRayRecursionDepth < kRtPipelineRecursionDepth ||
            limits.maxRayHitAttributeSize < kRtPipelineTriangleAttributeSize ||
            limits.maxRayDispatchInvocationCount == 0u)
        {
            return Status::Failure(VK_ERROR_FEATURE_NOT_PRESENT, "RT Pipeline requirements are not satisfied");
        }
        Reset();
        device_ = allocator.Device();
        dispatch_ = &dispatch;
        maxRayDispatchInvocationCount_ = limits.maxRayDispatchInvocationCount;
        Status status = CreateDescriptorLayout(device_, {}, emptyFrameLayout_);
        if (!status)
        {
            Reset();
            return status;
        }

        constexpr VkShaderStageFlags rayStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
            VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
            VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        const std::array sceneBindings{
            VkDescriptorSetLayoutBinding{0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr}};
        status = CreateDescriptorLayout(device_, sceneBindings, sceneAdapterLayout_);
        if (!status)
        {
            Reset();
            return status;
        }
        const std::array traversalBindings{
            VkDescriptorSetLayoutBinding{0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, rayStages, nullptr},
            VkDescriptorSetLayoutBinding{9u, VK_DESCRIPTOR_TYPE_SAMPLER, 1u, rayStages, nullptr}};
        status = CreateDescriptorLayout(device_, traversalBindings, traversalLayout_);
        if (!status)
        {
            Reset();
            return status;
        }

        const std::array layouts{emptyFrameLayout_, sceneAdapterLayout_, traversalLayout_};
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = rayStages;
        pushRange.size = sizeof(RayBatchPushConstants);
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1u;
        layoutInfo.pPushConstantRanges = &pushRange;
        VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_);
        if (result != VK_SUCCESS)
        {
            Reset();
            return Status::Failure(result, "vkCreatePipelineLayout failed for RT Pipeline");
        }

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = shaderLibrarySpirv.size_bytes();
        moduleInfo.pCode = shaderLibrarySpirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        result = vkCreateShaderModule(device_, &moduleInfo, nullptr, &module);
        if (result != VK_SUCCESS)
        {
            Reset();
            return Status::Failure(result, "vkCreateShaderModule failed for RT Pipeline");
        }

        const std::array stageNames{std::string_view{"RayGenerationMain"}, std::string_view{"MissMain"},
            std::string_view{"ClosestHitMain"}, std::string_view{"AnyHitMain"}};
        const std::array<VkShaderStageFlagBits, 4> stageBits{
            VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_SHADER_STAGE_ANY_HIT_BIT_KHR};
        std::array<VkPipelineShaderStageCreateInfo, 4> stages{};
        for (std::size_t index = 0u; index < stages.size(); ++index)
        {
            stages[index].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[index].stage = stageBits[index];
            stages[index].module = module;
            stages[index].pName = stageNames[index].data();
        }
        std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> groups{};
        for (VkRayTracingShaderGroupCreateInfoKHR& group : groups)
        {
            group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            group.generalShader = VK_SHADER_UNUSED_KHR;
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0u;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1u;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].closestHitShader = 2u;
        groups[2].anyHitShader = 3u;

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{
            VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = static_cast<std::uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = kRtPipelineRecursionDepth;
        pipelineInfo.layout = pipelineLayout_;
        result = dispatch.createRayTracingPipelines(
            device_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipeline_);
        vkDestroyShaderModule(device_, module, nullptr);
        if (result != VK_SUCCESS)
        {
            Reset();
            return Status::Failure(result, "vkCreateRayTracingPipelinesKHR failed");
        }

        const std::array<SbtRecordData, 3> records{};
        SbtBuilder sbtBuilder(allocator, dispatch, limits);
        status = sbtBuilder.Build(pipeline_, SbtGroupCounts{}, records, sbt_);
        if (!status)
        {
            Reset();
            return status;
        }
        return Status::Success();
    }

    void RtPipelineBackend::Reset() noexcept
    {
        sbt_ = ShaderBindingTable{};
        if (device_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE && traversalLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, traversalLayout_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE && sceneAdapterLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, sceneAdapterLayout_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE && emptyFrameLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, emptyFrameLayout_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        dispatch_ = nullptr;
        emptyFrameLayout_ = VK_NULL_HANDLE;
        sceneAdapterLayout_ = VK_NULL_HANDLE;
        traversalLayout_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        maxRayDispatchInvocationCount_ = 0u;
    }

    Status RtPipelineBackend::CreateDescriptorPool(
        std::uint32_t batchCount,
        VkDescriptorPool& output) const noexcept
    {
        if (device_ == VK_NULL_HANDLE || batchCount == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid RT Pipeline descriptor-pool request");
        }
        const std::array poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8u * batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, batchCount}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = 2u * batchCount;
        info.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        info.pPoolSizes = poolSizes.data();
        const VkResult result = vkCreateDescriptorPool(device_, &info, nullptr, &output);
        return result == VK_SUCCESS
            ? Status::Success()
            : Status::Failure(result, "vkCreateDescriptorPool failed for RT Pipeline");
    }

    Status RtPipelineBackend::AllocateDescriptorSets(
        VkDescriptorPool pool,
        VkDescriptorSet& sceneSet,
        VkDescriptorSet& traversalSet) const noexcept
    {
        const std::array layouts{sceneAdapterLayout_, traversalLayout_};
        std::array<VkDescriptorSet, 2> sets{};
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool;
        info.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();
        const VkResult result = vkAllocateDescriptorSets(device_, &info, sets.data());
        if (result != VK_SUCCESS)
        {
            return Status::Failure(result, "vkAllocateDescriptorSets failed for RT Pipeline");
        }
        sceneSet = sets[0];
        traversalSet = sets[1];
        return Status::Success();
    }

    void RtPipelineBackend::UpdateSceneDescriptors(
        VkDescriptorSet sceneSet,
        const HardwareSceneBufferBindings& bindings) const noexcept
    {
        UpdateScene(device_, sceneSet, bindings);
    }

    void RtPipelineBackend::UpdateTraversalDescriptors(
        VkDescriptorSet traversalSet,
        const RayQueryTraversalBindings& bindings) const noexcept
    {
        UpdateTraversal(device_, traversalSet, bindings);
    }

    Status RtPipelineBackend::RecordTraceClosestBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayOffset,
        std::uint32_t hitOffset,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        return Record(commandBuffer, sceneSet, traversalSet,
            RayBatchPushConstants{rayCount, RayBatchQuery::Closest, alphaAtlasLayerCount,
                alphaSamplerId, rayOffset, hitOffset});
    }

    Status RtPipelineBackend::RecordTraceAnyBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayOffset,
        std::uint32_t hitOffset,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        return Record(commandBuffer, sceneSet, traversalSet,
            RayBatchPushConstants{rayCount, RayBatchQuery::Any, alphaAtlasLayerCount,
                alphaSamplerId, rayOffset, hitOffset});
    }

    void RtPipelineBackend::RecordTraceClosestBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        static_cast<void>(Record(commandBuffer, sceneSet, traversalSet,
            RayBatchPushConstants{rayCount, RayBatchQuery::Closest, alphaAtlasLayerCount,
                alphaSamplerId, 0u, 0u}));
    }

    void RtPipelineBackend::RecordTraceAnyBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        static_cast<void>(Record(commandBuffer, sceneSet, traversalSet,
            RayBatchPushConstants{rayCount, RayBatchQuery::Any, alphaAtlasLayerCount,
                alphaSamplerId, 0u, 0u}));
    }

    VkDescriptorSetLayout RtPipelineBackend::SceneAdapterLayout() const noexcept { return sceneAdapterLayout_; }
    VkDescriptorSetLayout RtPipelineBackend::TraversalLayout() const noexcept { return traversalLayout_; }
    VkPipelineLayout RtPipelineBackend::PipelineLayout() const noexcept { return pipelineLayout_; }
    VkPipeline RtPipelineBackend::Pipeline() const noexcept { return pipeline_; }
    const ShaderBindingTable& RtPipelineBackend::Sbt() const noexcept { return sbt_; }

    Status RtPipelineBackend::Record(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        const RayBatchPushConstants& constants) const noexcept
    {
        if (constants.rayCount == 0u)
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "RT Pipeline trace batch must contain at least one ray");
        }
        if (commandBuffer == VK_NULL_HANDLE || sceneSet == VK_NULL_HANDLE ||
            traversalSet == VK_NULL_HANDLE ||
            pipeline_ == VK_NULL_HANDLE || pipelineLayout_ == VK_NULL_HANDLE ||
            dispatch_ == nullptr || !sbt_.IsValid() || maxRayDispatchInvocationCount_ == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "RT Pipeline trace batch has an uninitialized command or pipeline handle");
        }
        if (constants.query != RayBatchQuery::Closest && constants.query != RayBatchQuery::Any)
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "RT Pipeline trace batch has an unknown query mode");
        }
        const std::uint64_t rayEnd =
            static_cast<std::uint64_t>(constants.rayOffset) + constants.rayCount;
        const std::uint64_t hitEnd =
            static_cast<std::uint64_t>(constants.hitOffset) + constants.rayCount;
        if (rayEnd > (std::numeric_limits<std::uint32_t>::max)() ||
            hitEnd > (std::numeric_limits<std::uint32_t>::max)())
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "RT Pipeline trace batch record range overflows uint32");
        }
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
        const std::array sets{sceneSet, traversalSet};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
            pipelineLayout_, 1u, static_cast<std::uint32_t>(sets.size()), sets.data(), 0u, nullptr);
        RayBatchPushConstants chunk = constants;
        std::uint32_t remaining = constants.rayCount;
        std::uint32_t offset = 0u;
        while (remaining != 0u)
        {
            chunk.rayCount = std::min(remaining, maxRayDispatchInvocationCount_);
            chunk.rayOffset = constants.rayOffset + offset;
            chunk.hitOffset = constants.hitOffset + offset;
            vkCmdPushConstants(commandBuffer, pipelineLayout_,
                VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
                0u, sizeof(chunk), &chunk);
            dispatch_->cmdTraceRays(commandBuffer,
                &sbt_.RayGenerationRegion(), &sbt_.MissRegion(), &sbt_.HitRegion(), &sbt_.CallableRegion(),
                chunk.rayCount, 1u, 1u);
            remaining -= chunk.rayCount;
            offset += chunk.rayCount;
        }
        return Status::Success();
    }
}
