#include "RayQueryBackend.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace RenderingEngine::Rt::Hardware
{
    std::uint32_t ComputeRayQueryDispatchChunkCount(
        std::uint32_t rayCount,
        std::uint32_t maxComputeWorkGroupCountX) noexcept
    {
        if (rayCount == 0u)
        {
            return 0u;
        }
        if (maxComputeWorkGroupCountX == 0u)
        {
            return 0u;
        }
        const std::uint64_t maxRaysPerDispatch = std::min<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max(),
            static_cast<std::uint64_t>(maxComputeWorkGroupCountX) * kRayQueryWorkGroupSizeX);
        return static_cast<std::uint32_t>(
            1u + (static_cast<std::uint64_t>(rayCount) - 1u) / maxRaysPerDispatch);
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
                : Status::Failure(result, "vkCreateDescriptorSetLayout failed");
        }
    }

    RayQueryBackend::~RayQueryBackend()
    {
        Reset();
    }

    RayQueryBackend::RayQueryBackend(RayQueryBackend&& other) noexcept
    {
        *this = std::move(other);
    }

    RayQueryBackend& RayQueryBackend::operator=(RayQueryBackend&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            emptyFrameLayout_ = std::exchange(other.emptyFrameLayout_, VK_NULL_HANDLE);
            sceneAdapterLayout_ = std::exchange(other.sceneAdapterLayout_, VK_NULL_HANDLE);
            traversalLayout_ = std::exchange(other.traversalLayout_, VK_NULL_HANDLE);
            pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
            pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
            maxComputeWorkGroupCountX_ = std::exchange(other.maxComputeWorkGroupCountX_, 0u);
        }
        return *this;
    }

    Status RayQueryBackend::Create(
        VkDevice device,
        std::span<const std::uint32_t> computeShaderSpirv,
        std::uint32_t maxComputeWorkGroupCountX) noexcept
    {
        if (device == VK_NULL_HANDLE || computeShaderSpirv.empty() || maxComputeWorkGroupCountX == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "Ray Query backend requires device, SPIR-V, and a compute dispatch limit");
        }
        Reset();
        device_ = device;
        maxComputeWorkGroupCountX_ = maxComputeWorkGroupCountX;

        Status status = CreateDescriptorLayout(device_, {}, emptyFrameLayout_);
        if (!status)
        {
            Reset();
            return status;
        }

        constexpr VkShaderStageFlags computeStage = VK_SHADER_STAGE_COMPUTE_BIT;
        const std::array sceneBindings{
            VkDescriptorSetLayoutBinding{1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr}};
        status = CreateDescriptorLayout(device_, sceneBindings, sceneAdapterLayout_);
        if (!status)
        {
            Reset();
            return status;
        }

        const std::array traversalBindings{
            VkDescriptorSetLayoutBinding{0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{9u, VK_DESCRIPTOR_TYPE_SAMPLER, 1u, computeStage, nullptr}};
        status = CreateDescriptorLayout(device_, traversalBindings, traversalLayout_);
        if (!status)
        {
            Reset();
            return status;
        }

        const std::array layouts{emptyFrameLayout_, sceneAdapterLayout_, traversalLayout_};
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = computeStage;
        pushRange.offset = 0u;
        pushRange.size = sizeof(RayBatchPushConstants);
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
        pipelineLayoutInfo.pSetLayouts = layouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1u;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        VkResult result = vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_);
        if (result != VK_SUCCESS)
        {
            Reset();
            return Status::Failure(result, "vkCreatePipelineLayout failed for Ray Query");
        }

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = computeShaderSpirv.size_bytes();
        moduleInfo.pCode = computeShaderSpirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        result = vkCreateShaderModule(device_, &moduleInfo, nullptr, &shaderModule);
        if (result != VK_SUCCESS)
        {
            Reset();
            return Status::Failure(result, "vkCreateShaderModule failed for Ray Query");
        }

        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shaderModule;
        stage.pName = "RayQueryMain";
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stage;
        pipelineInfo.layout = pipelineLayout_;
        result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipeline_);
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        if (result != VK_SUCCESS)
        {
            Reset();
            return Status::Failure(result, "vkCreateComputePipelines failed for Ray Query");
        }
        return Status::Success();
    }

    void RayQueryBackend::Reset() noexcept
    {
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
        emptyFrameLayout_ = VK_NULL_HANDLE;
        sceneAdapterLayout_ = VK_NULL_HANDLE;
        traversalLayout_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        maxComputeWorkGroupCountX_ = 0u;
    }

    Status RayQueryBackend::CreateDescriptorPool(
        std::uint32_t batchCount,
        VkDescriptorPool& output) const noexcept
    {
        if (device_ == VK_NULL_HANDLE || batchCount == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid Ray Query descriptor-pool request");
        }
        const std::array poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7u * batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, batchCount}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = 2u * batchCount;
        info.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        info.pPoolSizes = poolSizes.data();
        const VkResult result = vkCreateDescriptorPool(device_, &info, nullptr, &output);
        return result == VK_SUCCESS
            ? Status::Success()
            : Status::Failure(result, "vkCreateDescriptorPool failed for Ray Query");
    }

    Status RayQueryBackend::AllocateDescriptorSets(
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
            return Status::Failure(result, "vkAllocateDescriptorSets failed for Ray Query");
        }
        sceneSet = sets[0];
        traversalSet = sets[1];
        return Status::Success();
    }

    void RayQueryBackend::UpdateSceneDescriptors(
        VkDescriptorSet sceneSet,
        const HardwareSceneBufferBindings& bindings) const noexcept
    {
        const std::array infos{bindings.vertices, bindings.indices, bindings.geometries,
            bindings.instances, bindings.materials};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (std::uint32_t index = 0u; index < writes.size(); ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = sceneSet;
            writes[index].dstBinding = index + 1u;
            writes[index].descriptorCount = 1u;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &infos[index];
        }
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
    }

    void RayQueryBackend::UpdateTraversalDescriptors(
        VkDescriptorSet traversalSet,
        const RayQueryTraversalBindings& bindings) const noexcept
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
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
    }

    void RayQueryBackend::RecordTraceClosestBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        Record(commandBuffer, sceneSet, traversalSet,
            RayBatchPushConstants{rayCount, RayBatchQuery::Closest, alphaAtlasLayerCount,
                alphaSamplerId, 0u});
    }

    void RayQueryBackend::RecordTraceAnyBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        Record(commandBuffer, sceneSet, traversalSet,
            RayBatchPushConstants{rayCount, RayBatchQuery::Any, alphaAtlasLayerCount,
                alphaSamplerId, 0u});
    }

    VkDescriptorSetLayout RayQueryBackend::SceneAdapterLayout() const noexcept { return sceneAdapterLayout_; }
    VkDescriptorSetLayout RayQueryBackend::TraversalLayout() const noexcept { return traversalLayout_; }
    VkPipelineLayout RayQueryBackend::PipelineLayout() const noexcept { return pipelineLayout_; }
    VkPipeline RayQueryBackend::Pipeline() const noexcept { return pipeline_; }

    void RayQueryBackend::Record(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        const RayBatchPushConstants& constants) const noexcept
    {
        if (constants.rayCount == 0u || commandBuffer == VK_NULL_HANDLE ||
            sceneSet == VK_NULL_HANDLE || traversalSet == VK_NULL_HANDLE ||
            pipeline_ == VK_NULL_HANDLE || pipelineLayout_ == VK_NULL_HANDLE ||
            maxComputeWorkGroupCountX_ == 0u)
        {
            return;
        }
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        const std::array sets{sceneSet, traversalSet};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
            1u, static_cast<std::uint32_t>(sets.size()), sets.data(), 0u, nullptr);
        const std::uint64_t maxRaysPerDispatch = std::min<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max(),
            static_cast<std::uint64_t>(maxComputeWorkGroupCountX_) * kRayQueryWorkGroupSizeX);
        std::uint64_t remaining = constants.rayCount;
        std::uint64_t offset = 0u;
        while (remaining != 0u)
        {
            const std::uint32_t chunkRayCount = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(remaining, maxRaysPerDispatch));
            RayBatchPushConstants chunk = constants;
            chunk.rayCount = chunkRayCount;
            chunk.rayOffset = static_cast<std::uint32_t>(offset);
            vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                0u, sizeof(chunk), &chunk);
            vkCmdDispatch(commandBuffer,
                1u + (chunkRayCount - 1u) / kRayQueryWorkGroupSizeX, 1u, 1u);
            offset += chunkRayCount;
            remaining -= chunkRayCount;
        }
    }
}
