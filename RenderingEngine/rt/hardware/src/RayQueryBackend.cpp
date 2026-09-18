#include "RayQueryBackend.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
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

        [[nodiscard]] bool IsValidBufferInfo(const VkDescriptorBufferInfo& info) noexcept
        {
            return info.buffer != VK_NULL_HANDLE && info.range != 0u;
        }

        [[nodiscard]] bool IsValidImageInfo(const VkDescriptorImageInfo& info) noexcept
        {
            return info.imageView != VK_NULL_HANDLE && info.imageLayout != VK_IMAGE_LAYOUT_UNDEFINED;
        }

        [[nodiscard]] bool IsValidSamplerInfo(const VkDescriptorImageInfo& info) noexcept
        {
            return info.sampler != VK_NULL_HANDLE;
        }

        [[nodiscard]] RenderingEngine::Rt::Gpu::GpuTraversalStatus ToGpuStatus(
            const Status& status,
            RenderingEngine::Rt::Gpu::GpuTraversalStatusCode failureCode)
        {
            if (status.Succeeded())
            {
                return {};
            }
            return RenderingEngine::Rt::Gpu::GpuTraversalStatus{failureCode, status.message};
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
            VkDescriptorSetLayoutBinding{0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr},
            VkDescriptorSetLayoutBinding{6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, computeStage, nullptr}};
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
        // Ray Query v1 carries independent ray/hit record offsets.  Keep the
        // Wave 3 RT Pipeline's five-word RayBatchPushConstants untouched, but
        // expose the full six-word range used by this compute pipeline.
        pushRange.size = sizeof(RayQueryPushConstants);
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
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, batchCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9u * batchCount},
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

    Status RayQueryBackend::UpdateSceneDescriptors(
        VkDescriptorSet sceneSet,
        const HardwareSceneBufferBindings& bindings) const noexcept
    {
        if (device_ == VK_NULL_HANDLE || sceneSet == VK_NULL_HANDLE)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "Ray Query scene descriptor update requires a device and descriptor set");
        }
        const std::array infos{bindings.constants, bindings.vertices, bindings.indices, bindings.geometries,
            bindings.instances, bindings.materials, bindings.lights};
        if (!std::all_of(infos.begin(), infos.end(), IsValidBufferInfo))
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "Ray Query scene descriptor update contains an invalid canonical buffer");
        }
        std::array<VkWriteDescriptorSet, 7> writes{};
        for (std::uint32_t index = 0u; index < writes.size(); ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = sceneSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1u;
            writes[index].descriptorType = index == kCanonicalSceneConstantsBinding
                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &infos[index];
        }
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
        return Status::Success();
    }

    Status RayQueryBackend::UpdateTraversalDescriptors(
        VkDescriptorSet traversalSet,
        const RayQueryTraversalBindings& bindings) const noexcept
    {
        if (device_ == VK_NULL_HANDLE || traversalSet == VK_NULL_HANDLE ||
            bindings.topLevelAccelerationStructure == VK_NULL_HANDLE ||
            !IsValidBufferInfo(bindings.rays) || !IsValidBufferInfo(bindings.hits) ||
            !IsValidImageInfo(bindings.alphaAtlas) || !IsValidSamplerInfo(bindings.alphaSampler))
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "Ray Query traversal descriptor update contains an invalid binding");
        }
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
        return Status::Success();
    }

    Status RayQueryBackend::RecordTraceClosestBatch(
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
            RayQueryPushConstants{rayCount, RayBatchQuery::Closest, alphaAtlasLayerCount,
                alphaSamplerId, rayOffset, hitOffset});
    }

    Status RayQueryBackend::RecordTraceAnyBatch(
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
            RayQueryPushConstants{rayCount, RayBatchQuery::Any, alphaAtlasLayerCount,
                alphaSamplerId, rayOffset, hitOffset});
    }

    Status RayQueryBackend::RecordTraceClosestBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        return RecordTraceClosestBatch(commandBuffer, sceneSet, traversalSet,
            0u, 0u, rayCount, alphaAtlasLayerCount, alphaSamplerId);
    }

    Status RayQueryBackend::RecordTraceAnyBatch(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        std::uint32_t rayCount,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) const noexcept
    {
        return RecordTraceAnyBatch(commandBuffer, sceneSet, traversalSet,
            0u, 0u, rayCount, alphaAtlasLayerCount, alphaSamplerId);
    }

    VkDescriptorSetLayout RayQueryBackend::SceneAdapterLayout() const noexcept { return sceneAdapterLayout_; }
    VkDescriptorSetLayout RayQueryBackend::TraversalLayout() const noexcept { return traversalLayout_; }
    VkPipelineLayout RayQueryBackend::PipelineLayout() const noexcept { return pipelineLayout_; }
    VkPipeline RayQueryBackend::Pipeline() const noexcept { return pipeline_; }

    Status RayQueryBackend::Record(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet sceneSet,
        VkDescriptorSet traversalSet,
        const RayQueryPushConstants& constants) const noexcept
    {
        if (constants.rayCount == 0u)
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "Ray Query trace batch must contain at least one ray");
        }
        if (commandBuffer == VK_NULL_HANDLE || sceneSet == VK_NULL_HANDLE ||
            traversalSet == VK_NULL_HANDLE || pipeline_ == VK_NULL_HANDLE ||
            pipelineLayout_ == VK_NULL_HANDLE || maxComputeWorkGroupCountX_ == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "Ray Query trace batch has an uninitialized command or pipeline handle");
        }
        if (constants.query != RayBatchQuery::Closest && constants.query != RayBatchQuery::Any)
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "Ray Query trace batch has an unknown query mode");
        }
        const std::uint64_t rayEnd = static_cast<std::uint64_t>(constants.rayOffset) + constants.rayCount;
        const std::uint64_t hitEnd = static_cast<std::uint64_t>(constants.hitOffset) + constants.rayCount;
        if (rayEnd > std::numeric_limits<std::uint32_t>::max() ||
            hitEnd > std::numeric_limits<std::uint32_t>::max())
        {
            return Status::Failure(VK_ERROR_VALIDATION_FAILED_EXT,
                "Ray Query trace batch record range overflows uint32");
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
            // The RT Pipeline keeps its five-word push ABI.  Ray Query uses
            // the six-word type above so the independent hit offset reaches
            // the shader for every split dispatch.
            RayQueryPushConstants queryChunk = constants;
            queryChunk.rayCount = chunkRayCount;
            queryChunk.rayOffset = static_cast<std::uint32_t>(constants.rayOffset + offset);
            queryChunk.hitOffset = static_cast<std::uint32_t>(constants.hitOffset + offset);
            vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                0u, sizeof(queryChunk), &queryChunk);
            vkCmdDispatch(commandBuffer,
                1u + (chunkRayCount - 1u) / kRayQueryWorkGroupSizeX, 1u, 1u);
            offset += chunkRayCount;
            remaining -= chunkRayCount;
        }
        return Status::Success();
    }

    RayQueryTraversalAdapter::RayQueryTraversalAdapter(
        RayQueryBackend& backend,
        std::uint32_t alphaAtlasLayerCount,
        std::uint32_t alphaSamplerId) noexcept
        : backend_(&backend)
        , alphaAtlasLayerCount_(alphaAtlasLayerCount)
        , alphaSamplerId_(alphaSamplerId)
    {
    }

    RenderingEngine::Rt::Gpu::GpuTraversalBackendDescriptor
        RayQueryTraversalAdapter::Descriptor() const noexcept
    {
        return {"hardware-ray-query", "Vulkan Hardware Ray Query", true, true, true};
    }

    RenderingEngine::Rt::Gpu::GpuTraversalStatus RayQueryTraversalAdapter::BuildOrUpdateScene(
        const RenderingEngine::Rt::Gpu::GpuSceneBuildRequest& request)
    {
        using namespace RenderingEngine::Rt::Gpu;
        if (backend_ == nullptr || request.commandBuffer == VK_NULL_HANDLE ||
            request.canonicalSceneSet == VK_NULL_HANDLE || request.sceneFingerprint == 0u ||
            request.sceneGeneration == 0u)
        {
            return {GpuTraversalStatusCode::InvalidArgument,
                "Ray Query scene build requires command buffer, canonical scene set, fingerprint, and generation"};
        }
        // This adapter deliberately does not build acceleration structures.
        // The owner submits BLAS/TLAS work and calls this boundary only after
        // recording that scene state; we retain the identity for trace checks.
        canonicalSceneSet_ = request.canonicalSceneSet;
        sceneFingerprint_ = request.sceneFingerprint;
        sceneGeneration_ = request.sceneGeneration;
        sceneReady_ = true;
        return {};
    }

    RenderingEngine::Rt::Gpu::GpuTraversalStatus RayQueryTraversalAdapter::ValidateTrace(
        const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch) const
    {
        using namespace RenderingEngine::Rt::Gpu;
        if (backend_ == nullptr)
        {
            return {GpuTraversalStatusCode::Unsupported, "Ray Query backend adapter is not configured"};
        }
        if (!sceneReady_)
        {
            return {GpuTraversalStatusCode::MissingScene,
                "Ray Query trace was requested before a scene identity was accepted"};
        }
        if (batch.commandBuffer == VK_NULL_HANDLE || batch.canonicalSceneSet == VK_NULL_HANDLE ||
            batch.traversalSet == VK_NULL_HANDLE || batch.rayCount == 0u)
        {
            return {GpuTraversalStatusCode::InvalidArgument,
                "Ray Query trace requires command buffer, scene/traversal sets, and non-zero ray count"};
        }
        if (batch.sceneFingerprint == 0u || batch.sceneGeneration == 0u)
        {
            return {GpuTraversalStatusCode::InvalidArgument,
                "Ray Query trace requires a non-zero scene fingerprint and generation"};
        }
        if (batch.canonicalSceneSet != canonicalSceneSet_ ||
            batch.sceneFingerprint != sceneFingerprint_ || batch.sceneGeneration != sceneGeneration_)
        {
            return {GpuTraversalStatusCode::MissingScene,
                "Ray Query trace scene identity does not match the accepted canonical scene"};
        }
        const std::uint64_t rayEnd = static_cast<std::uint64_t>(batch.rayOffset) + batch.rayCount;
        const std::uint64_t hitEnd = static_cast<std::uint64_t>(batch.hitOffset) + batch.rayCount;
        if (rayEnd > std::numeric_limits<std::uint32_t>::max() ||
            hitEnd > std::numeric_limits<std::uint32_t>::max())
        {
            return {GpuTraversalStatusCode::InvalidArgument,
                "Ray Query trace record range overflows uint32"};
        }
        return {};
    }

    RenderingEngine::Rt::Gpu::GpuTraversalStatus RayQueryTraversalAdapter::ForwardTrace(
        const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch,
        RayBatchQuery query)
    {
        const auto validation = ValidateTrace(batch);
        if (!validation)
        {
            return validation;
        }
        const Status status = query == RayBatchQuery::Closest
            ? backend_->RecordTraceClosestBatch(batch.commandBuffer, batch.canonicalSceneSet,
                batch.traversalSet, batch.rayOffset, batch.hitOffset, batch.rayCount,
                alphaAtlasLayerCount_, alphaSamplerId_)
            : backend_->RecordTraceAnyBatch(batch.commandBuffer, batch.canonicalSceneSet,
                batch.traversalSet, batch.rayOffset, batch.hitOffset, batch.rayCount,
                alphaAtlasLayerCount_, alphaSamplerId_);
        return ToGpuStatus(status,
            status.result == VK_ERROR_VALIDATION_FAILED_EXT
                ? RenderingEngine::Rt::Gpu::GpuTraversalStatusCode::InvalidArgument
                : RenderingEngine::Rt::Gpu::GpuTraversalStatusCode::RecordingFailed);
    }

    RenderingEngine::Rt::Gpu::GpuTraversalStatus RayQueryTraversalAdapter::RecordTraceClosestBatch(
        const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch)
    {
        return ForwardTrace(batch, RayBatchQuery::Closest);
    }

    RenderingEngine::Rt::Gpu::GpuTraversalStatus RayQueryTraversalAdapter::RecordTraceAnyBatch(
        const RenderingEngine::Rt::Gpu::GpuTraceBatch& batch)
    {
        return ForwardTrace(batch, RayBatchQuery::Any);
    }
}
