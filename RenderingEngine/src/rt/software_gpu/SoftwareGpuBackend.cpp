#include "rt/software_gpu/SoftwareGpuBackend.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace RenderingEngine::Rt::SoftwareGpu
{
    namespace
    {
        constexpr std::uint32_t kFlattenedSahTraversalMode = 0u;
        constexpr std::uint32_t kCanonicalLinearTraversalMode = 1u;

        [[nodiscard]] Gpu::GpuTraversalStatus Failure(std::string message)
        {
            return {Gpu::GpuTraversalStatusCode::RecordingFailed, std::move(message)};
        }

        [[nodiscard]] Gpu::GpuTraversalStatus Invalid(std::string message)
        {
            return {Gpu::GpuTraversalStatusCode::InvalidArgument, std::move(message)};
        }
    }

    SoftwareGpuTraversalBackend::~SoftwareGpuTraversalBackend()
    {
        Reset();
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::Create(
        const SoftwareGpuBackendCreateInfo& createInfo) noexcept
    {
        Reset();
        if (createInfo.device == VK_NULL_HANDLE
            || createInfo.frameLayout == VK_NULL_HANDLE
            || createInfo.canonicalSceneLayout == VK_NULL_HANDLE
            || createInfo.computeShaderSpirv.empty()
            || createInfo.maximumComputeGroupCountX == 0u)
        {
            return Invalid("Software GPU backend requires device, set layouts, SPIR-V, and dispatch limits.");
        }
        device_ = createInfo.device;
        maximumComputeGroupCountX_ = createInfo.maximumComputeGroupCountX;
        canonicalLinear_ = createInfo.canonicalLinear;
        constexpr VkShaderStageFlags computeStage = VK_SHADER_STAGE_COMPUTE_BIT;
        const std::array bindings{
            VkDescriptorSetLayoutBinding{0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{9u, VK_DESCRIPTOR_TYPE_SAMPLER, 1u, computeStage, nullptr}};
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        VkResult vkResult = vkCreateDescriptorSetLayout(
            device_, &layoutInfo, nullptr, &traversalLayout_);
        if (vkResult != VK_SUCCESS)
        {
            Reset();
            return Failure("vkCreateDescriptorSetLayout failed for Software GPU traversal.");
        }

        const std::array setLayouts{
            createInfo.frameLayout, createInfo.canonicalSceneLayout, traversalLayout_};
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = computeStage;
        pushRange.size = sizeof(SoftwareGpuTracePushConstants);
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1u;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        vkResult = vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_);
        if (vkResult != VK_SUCCESS)
        {
            Reset();
            return Failure("vkCreatePipelineLayout failed for Software GPU traversal.");
        }

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = createInfo.computeShaderSpirv.size_bytes();
        moduleInfo.pCode = createInfo.computeShaderSpirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vkResult = vkCreateShaderModule(device_, &moduleInfo, nullptr, &module);
        if (vkResult != VK_SUCCESS)
        {
            Reset();
            return Failure("vkCreateShaderModule failed for Software GPU traversal.");
        }
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "SoftwareTraceV1";
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stage;
        pipelineInfo.layout = pipelineLayout_;
        vkResult = vkCreateComputePipelines(
            device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipeline_);
        vkDestroyShaderModule(device_, module, nullptr);
        if (vkResult != VK_SUCCESS)
        {
            Reset();
            return Failure("vkCreateComputePipelines failed for Software GPU traversal.");
        }
        return {};
    }

    void SoftwareGpuTraversalBackend::Reset() noexcept
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
        device_ = VK_NULL_HANDLE;
        traversalLayout_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        counterBinding_ = {};
        maximumComputeGroupCountX_ = 0u;
        nodeCount_ = 0u;
        triangleCount_ = 0u;
        stackCapacity_ = 0u;
        alphaAtlasLayerCount_ = 0u;
        alphaSamplerId_ = 0xffffffffu;
        sceneFingerprint_ = 0u;
        sceneGeneration_ = 0u;
        canonicalLinear_ = false;
        descriptorsConfigured_ = false;
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::CreateDescriptorPool(
        const std::uint32_t setCount,
        VkDescriptorPool& output) const noexcept
    {
        output = VK_NULL_HANDLE;
        if (device_ == VK_NULL_HANDLE || setCount == 0u)
        {
            return Invalid("Software GPU descriptor pool requires a live backend and non-zero set count.");
        }
        const std::array sizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5u * setCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, setCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, setCount}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = setCount;
        info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        return vkCreateDescriptorPool(device_, &info, nullptr, &output) == VK_SUCCESS
            ? Gpu::GpuTraversalStatus{}
            : Failure("vkCreateDescriptorPool failed for Software GPU traversal.");
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::AllocateTraversalSet(
        const VkDescriptorPool pool,
        VkDescriptorSet& output) const noexcept
    {
        output = VK_NULL_HANDLE;
        if (device_ == VK_NULL_HANDLE || pool == VK_NULL_HANDLE
            || traversalLayout_ == VK_NULL_HANDLE)
        {
            return Invalid("Software GPU traversal-set allocation requires live Vulkan objects.");
        }
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool;
        info.descriptorSetCount = 1u;
        info.pSetLayouts = &traversalLayout_;
        return vkAllocateDescriptorSets(device_, &info, &output) == VK_SUCCESS
            ? Gpu::GpuTraversalStatus{}
            : Failure("vkAllocateDescriptorSets failed for Software GPU traversal.");
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::UpdateTraversalSet(
        const VkDescriptorSet set,
        const SoftwareGpuTraversalBindings& bindings) noexcept
    {
        const auto validBuffer = [](const VkDescriptorBufferInfo& value)
        {
            return value.buffer != VK_NULL_HANDLE && value.range != 0u;
        };
        const auto validImage = [](const VkDescriptorImageInfo& value)
        {
            return value.imageView != VK_NULL_HANDLE
                && value.imageLayout != VK_IMAGE_LAYOUT_UNDEFINED;
        };
        const auto validSampler = [](const VkDescriptorImageInfo& value)
        {
            return value.sampler != VK_NULL_HANDLE;
        };
        if (device_ == VK_NULL_HANDLE || set == VK_NULL_HANDLE
            || !validBuffer(bindings.nodes) || !validBuffer(bindings.rays)
            || !validBuffer(bindings.hits) || !validBuffer(bindings.triangles)
            || !validBuffer(bindings.counters) || !validImage(bindings.alphaAtlas)
            || !validSampler(bindings.alphaSampler) || bindings.triangleCount == 0u
            || bindings.stackCapacity > 64u
            || (!canonicalLinear_
                && (bindings.nodeCount == 0u || bindings.stackCapacity == 0u)))
        {
            return Invalid("Software GPU traversal descriptors or record counts are invalid.");
        }
        const std::array infos{
            bindings.nodes, bindings.rays, bindings.hits, bindings.triangles, bindings.counters};
        std::array<VkWriteDescriptorSet, infos.size()> writes{};
        for (std::size_t index = 0u; index < writes.size(); ++index)
        {
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[index].dstSet = set;
            writes[index].dstBinding = static_cast<std::uint32_t>(index);
            writes[index].descriptorCount = 1u;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &infos[index];
        }
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
            writes.data(), 0u, nullptr);
        std::array<VkWriteDescriptorSet, 2u> imageWrites{};
        imageWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        imageWrites[0].dstSet = set;
        imageWrites[0].dstBinding = 8u;
        imageWrites[0].descriptorCount = 1u;
        imageWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        imageWrites[0].pImageInfo = &bindings.alphaAtlas;
        imageWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        imageWrites[1].dstSet = set;
        imageWrites[1].dstBinding = 9u;
        imageWrites[1].descriptorCount = 1u;
        imageWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        imageWrites[1].pImageInfo = &bindings.alphaSampler;
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(imageWrites.size()),
            imageWrites.data(), 0u, nullptr);
        counterBinding_ = bindings.counters;
        nodeCount_ = bindings.nodeCount;
        triangleCount_ = bindings.triangleCount;
        stackCapacity_ = bindings.stackCapacity;
        alphaAtlasLayerCount_ = bindings.alphaAtlasLayerCount;
        alphaSamplerId_ = bindings.alphaSamplerId;
        descriptorsConfigured_ = true;
        return {};
    }

    Gpu::GpuTraversalBackendDescriptor SoftwareGpuTraversalBackend::Descriptor() const noexcept
    {
        return canonicalLinear_
            ? Gpu::GpuTraversalBackendDescriptor{
                "canonical-linear", "Canonical Linear Software GPU", true, true, false}
            : Gpu::GpuTraversalBackendDescriptor{
                "flattened-cpu-sah", "Flattened CPU SAH Software GPU", true, true, false};
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::BuildOrUpdateScene(
        const Gpu::GpuSceneBuildRequest& request)
    {
        if (!descriptorsConfigured_ || request.commandBuffer == VK_NULL_HANDLE
            || request.canonicalSceneSet == VK_NULL_HANDLE || request.sceneFingerprint == 0u
            || request.sceneGeneration == 0u)
        {
            return Invalid("Software GPU scene build requires configured descriptors and canonical identity.");
        }
        sceneFingerprint_ = request.sceneFingerprint;
        sceneGeneration_ = request.sceneGeneration;
        return {};
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::RecordTraceClosestBatch(
        const Gpu::GpuTraceBatch& batch)
    {
        return RecordTrace(batch, QueryMode::Closest);
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::RecordTraceAnyBatch(
        const Gpu::GpuTraceBatch& batch)
    {
        return RecordTrace(batch, QueryMode::Any);
    }

    VkDescriptorSetLayout SoftwareGpuTraversalBackend::TraversalLayout() const noexcept
    {
        return traversalLayout_;
    }
    VkPipelineLayout SoftwareGpuTraversalBackend::PipelineLayout() const noexcept
    {
        return pipelineLayout_;
    }
    VkPipeline SoftwareGpuTraversalBackend::Pipeline() const noexcept
    {
        return pipeline_;
    }

    Gpu::GpuTraversalStatus SoftwareGpuTraversalBackend::RecordTrace(
        const Gpu::GpuTraceBatch& batch,
        const QueryMode mode) noexcept
    {
        if (!descriptorsConfigured_ || pipeline_ == VK_NULL_HANDLE
            || batch.commandBuffer == VK_NULL_HANDLE || batch.canonicalSceneSet == VK_NULL_HANDLE
            || batch.traversalSet == VK_NULL_HANDLE || batch.rayCount == 0u
            || batch.sceneFingerprint != sceneFingerprint_
            || batch.sceneGeneration != sceneGeneration_)
        {
            return Invalid("Software GPU trace batch is incomplete or references a different scene.");
        }
        if (batch.rayOffset > std::numeric_limits<std::uint32_t>::max() - batch.rayCount
            || batch.hitOffset > std::numeric_limits<std::uint32_t>::max() - batch.rayCount)
        {
            return Invalid("Software GPU trace batch record range overflows uint32.");
        }

        vkCmdFillBuffer(batch.commandBuffer, counterBinding_.buffer,
            counterBinding_.offset, counterBinding_.range, 0u);
        VkBufferMemoryBarrier counterBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        counterBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        counterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        counterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        counterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        counterBarrier.buffer = counterBinding_.buffer;
        counterBarrier.offset = counterBinding_.offset;
        counterBarrier.size = counterBinding_.range;
        vkCmdPipelineBarrier(batch.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 1u, &counterBarrier,
            0u, nullptr);

        vkCmdBindPipeline(batch.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        const std::array sets{batch.canonicalSceneSet, batch.traversalSet};
        vkCmdBindDescriptorSets(batch.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout_, 1u, static_cast<std::uint32_t>(sets.size()), sets.data(),
            0u, nullptr);
        SoftwareGpuTracePushConstants constants{
            nodeCount_, triangleCount_, batch.rayCount,
            static_cast<std::uint32_t>(mode), batch.rayOffset, batch.hitOffset,
            stackCapacity_, alphaAtlasLayerCount_, alphaSamplerId_,
            canonicalLinear_ ? kCanonicalLinearTraversalMode : kFlattenedSahTraversalMode};
        const std::uint64_t maximumRaysPerDispatch =
            static_cast<std::uint64_t>(maximumComputeGroupCountX_) * kWave2TraceWorkgroupSize;
        std::uint64_t remaining = batch.rayCount;
        while (remaining != 0u)
        {
            const std::uint32_t chunk = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(remaining, maximumRaysPerDispatch));
            constants.rayCount = chunk;
            vkCmdPushConstants(batch.commandBuffer, pipelineLayout_,
                VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(constants), &constants);
            vkCmdDispatch(batch.commandBuffer,
                (chunk + kWave2TraceWorkgroupSize - 1u) / kWave2TraceWorkgroupSize,
                1u, 1u);
            constants.rayOffset += chunk;
            constants.hitOffset += chunk;
            remaining -= chunk;
        }
        return {};
    }
}
