#include "rt/software_gpu/GpuLbvhBuildOwner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace RenderingEngine::Rt::SoftwareGpu
{
    namespace
    {
        constexpr std::uint32_t kBuildThreads = 64u;
        constexpr std::uint32_t kRadixThreads = 128u;
        constexpr std::uint32_t kRadixBuckets = 16u;

        [[nodiscard]] constexpr std::uint32_t DivideRoundUp(
            const std::uint32_t value,
            const std::uint32_t divisor) noexcept
        {
            return (value + divisor - 1u) / divisor;
        }

        class FaultError final : public std::runtime_error
        {
        public:
            FaultError(const GpuLbvhBuildFault fault, std::string message)
                : std::runtime_error(std::move(message)), fault_(fault)
            {
            }

            [[nodiscard]] GpuLbvhBuildFault Fault() const noexcept { return fault_; }

        private:
            GpuLbvhBuildFault fault_;
        };

        class VulkanError final : public std::runtime_error
        {
        public:
            VulkanError(const char* operation, const VkResult result)
                : std::runtime_error(std::string(operation) + " failed with VkResult " +
                    std::to_string(static_cast<std::int32_t>(result)))
            {
            }
        };

        void Check(const VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                throw VulkanError(operation, result);
            }
        }

        [[nodiscard]] GpuLbvhBuildOutput Failure(
            const GpuLbvhBuildFault fault,
            std::string message)
        {
            GpuLbvhBuildOutput output{};
            output.fault = fault;
            output.message = std::move(message);
            return output;
        }

        [[nodiscard]] std::vector<std::uint32_t> LoadSpirv(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw FaultError(GpuLbvhBuildFault::MissingShader,
                    "missing LBVH SPIR-V file: " + path.string());
            }
            const std::streampos end = stream.tellg();
            if (end <= 0 ||
                (static_cast<std::uint64_t>(end) % sizeof(std::uint32_t)) != 0u)
            {
                throw FaultError(GpuLbvhBuildFault::MissingShader,
                    "malformed LBVH SPIR-V byte count: " + path.string());
            }
            std::vector<std::uint32_t> words(
                static_cast<std::size_t>(end) / sizeof(std::uint32_t));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char*>(words.data()),
                static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
            if (!stream)
            {
                throw FaultError(GpuLbvhBuildFault::MissingShader,
                    "failed to read LBVH SPIR-V file: " + path.string());
            }
            return words;
        }

        struct alignas(16) MortonConfig
        {
            Float4 sceneBoundsMin{};
            Float4 sceneBoundsMax{};
            std::uint32_t primitiveCount{};
            std::array<std::uint32_t, 3> reserved{};
        };

        struct alignas(16) RadixConfig
        {
            std::uint32_t elementCount{};
            std::uint32_t groupCount{};
            std::uint32_t shift{};
            std::uint32_t field{};
        };

        struct alignas(16) HierarchyConfig
        {
            std::uint32_t primitiveCount{};
            std::uint32_t nodeCount{};
            std::array<std::uint32_t, 2> reserved{};
        };

        struct alignas(16) BoundsConfig
        {
            std::uint32_t primitiveCount{};
            std::uint32_t nodeCount{};
            std::uint32_t currentDepth{};
            std::uint32_t maximumDepthLimit{};
        };

        struct alignas(16) CanonicalReorderConfig
        {
            std::uint32_t primitiveCount{};
            std::array<std::uint32_t, 3> reserved{};
        };

        static_assert(sizeof(MortonConfig) == 48u);
        static_assert(sizeof(RadixConfig) == 16u);
        static_assert(sizeof(HierarchyConfig) == 16u);
        static_assert(sizeof(BoundsConfig) == 16u);
        static_assert(sizeof(CanonicalReorderConfig) == 16u);

        [[nodiscard]] Aabb SceneBounds(
            const std::span<const SoftwarePrimitiveRecord> primitives)
        {
            Aabb result = PrimitiveBounds(primitives.front());
            for (std::size_t index = 1u; index < primitives.size(); ++index)
            {
                const Aabb bounds = PrimitiveBounds(primitives[index]);
                result.minimum.x = (std::min)(result.minimum.x, bounds.minimum.x);
                result.minimum.y = (std::min)(result.minimum.y, bounds.minimum.y);
                result.minimum.z = (std::min)(result.minimum.z, bounds.minimum.z);
                result.maximum.x = (std::max)(result.maximum.x, bounds.maximum.x);
                result.maximum.y = (std::max)(result.maximum.y, bounds.maximum.y);
                result.maximum.z = (std::max)(result.maximum.z, bounds.maximum.z);
            }
            return result;
        }
    }

    class GpuLbvhBuildOwner::Impl final
    {
    private:
        class Buffer final
        {
        public:
            Buffer(
                Impl& owner,
                const VkDeviceSize size,
                const VkBufferUsageFlags usage,
                const VkMemoryPropertyFlags properties)
                : owner_(&owner), size_(size), properties_(properties)
            {
                if (size == 0u)
                {
                    throw FaultError(GpuLbvhBuildFault::InvalidArgument,
                        "zero-sized LBVH Vulkan buffer requested");
                }
                VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                info.size = size;
                info.usage = usage;
                info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                Check(vkCreateBuffer(owner.device_, &info, nullptr, &buffer_),
                    "vkCreateBuffer(LBVH)");
                try
                {
                    VkMemoryRequirements requirements{};
                    vkGetBufferMemoryRequirements(owner.device_, buffer_, &requirements);
                    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    allocation.allocationSize = requirements.size;
                    allocation.memoryTypeIndex = owner.FindMemoryType(
                        requirements.memoryTypeBits, properties);
                    Check(vkAllocateMemory(owner.device_, &allocation, nullptr, &memory_),
                        "vkAllocateMemory(LBVH)");
                    Check(vkBindBufferMemory(owner.device_, buffer_, memory_, 0u),
                        "vkBindBufferMemory(LBVH)");
                }
                catch (...)
                {
                    Reset();
                    throw;
                }
            }

            ~Buffer() { Reset(); }
            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;

            [[nodiscard]] VkBuffer Handle() const noexcept { return buffer_; }
            [[nodiscard]] VkDeviceSize Size() const noexcept { return size_; }

            void Write(const void* data, const std::size_t byteCount)
            {
                if (data == nullptr || byteCount > size_ ||
                    (properties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u)
                {
                    throw FaultError(GpuLbvhBuildFault::InvalidArgument,
                        "invalid host write to LBVH Vulkan buffer");
                }
                EnsureMapped();
                std::memcpy(mapped_, data, byteCount);
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = memory_;
                range.offset = 0u;
                range.size = VK_WHOLE_SIZE;
                Check(vkFlushMappedMemoryRanges(owner_->device_, 1u, &range),
                    "vkFlushMappedMemoryRanges(LBVH)");
            }

            void Read(void* data, const std::size_t byteCount)
            {
                if (data == nullptr || byteCount > size_ ||
                    (properties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u)
                {
                    throw FaultError(GpuLbvhBuildFault::InvalidArgument,
                        "invalid host read from LBVH Vulkan buffer");
                }
                EnsureMapped();
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = memory_;
                range.offset = 0u;
                range.size = VK_WHOLE_SIZE;
                Check(vkInvalidateMappedMemoryRanges(owner_->device_, 1u, &range),
                    "vkInvalidateMappedMemoryRanges(LBVH)");
                std::memcpy(data, mapped_, byteCount);
            }

        private:
            void EnsureMapped()
            {
                if (mapped_ == nullptr)
                {
                    Check(vkMapMemory(owner_->device_, memory_, 0u, VK_WHOLE_SIZE,
                        0u, &mapped_), "vkMapMemory(LBVH)");
                }
            }

            void Reset() noexcept
            {
                if (owner_ == nullptr || owner_->device_ == VK_NULL_HANDLE)
                {
                    return;
                }
                if (mapped_ != nullptr)
                {
                    vkUnmapMemory(owner_->device_, memory_);
                    mapped_ = nullptr;
                }
                if (buffer_ != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(owner_->device_, buffer_, nullptr);
                    buffer_ = VK_NULL_HANDLE;
                }
                if (memory_ != VK_NULL_HANDLE)
                {
                    vkFreeMemory(owner_->device_, memory_, nullptr);
                    memory_ = VK_NULL_HANDLE;
                }
            }

            Impl* owner_{};
            VkDeviceSize size_{};
            VkMemoryPropertyFlags properties_{};
            VkBuffer buffer_{VK_NULL_HANDLE};
            VkDeviceMemory memory_{VK_NULL_HANDLE};
            void* mapped_{};
        };

        struct DescriptorResource
        {
            Buffer* buffer{};
            VkDescriptorType type{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        };

        class Kernel final
        {
        public:
            Kernel(
                Impl& owner,
                const std::filesystem::path& shaderPath,
                const char* entryPoint,
                const std::span<const VkDescriptorType> descriptorTypes)
                : owner_(&owner)
            {
                try
                {
                    std::vector<VkDescriptorSetLayoutBinding> bindings;
                    bindings.reserve(descriptorTypes.size());
                    std::uint32_t storageCount = 0u;
                    std::uint32_t uniformCount = 0u;
                    for (std::uint32_t index = 0u; index < descriptorTypes.size(); ++index)
                    {
                        bindings.push_back(VkDescriptorSetLayoutBinding{
                            index, descriptorTypes[index], 1u,
                            VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
                        if (descriptorTypes[index] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                        {
                            ++uniformCount;
                        }
                        else
                        {
                            ++storageCount;
                        }
                    }
                    VkDescriptorSetLayoutCreateInfo layoutInfo{
                        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
                    layoutInfo.pBindings = bindings.data();
                    Check(vkCreateDescriptorSetLayout(owner.device_, &layoutInfo, nullptr,
                        &setLayout_), "vkCreateDescriptorSetLayout(LBVH)");

                    const std::array layouts{
                        owner.emptySetLayout_, owner.emptySetLayout_, setLayout_};
                    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
                        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                    pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
                    pipelineLayoutInfo.pSetLayouts = layouts.data();
                    Check(vkCreatePipelineLayout(owner.device_, &pipelineLayoutInfo, nullptr,
                        &pipelineLayout_), "vkCreatePipelineLayout(LBVH)");

                    const std::vector<std::uint32_t> words = LoadSpirv(shaderPath);
                    VkShaderModuleCreateInfo moduleInfo{
                        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                    moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
                    moduleInfo.pCode = words.data();
                    VkShaderModule module = VK_NULL_HANDLE;
                    Check(vkCreateShaderModule(owner.device_, &moduleInfo, nullptr, &module),
                        "vkCreateShaderModule(LBVH)");
                    VkPipelineShaderStageCreateInfo stage{
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                    stage.module = module;
                    stage.pName = entryPoint;
                    VkComputePipelineCreateInfo pipelineInfo{
                        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                    pipelineInfo.stage = stage;
                    pipelineInfo.layout = pipelineLayout_;
                    const VkResult pipelineResult = vkCreateComputePipelines(owner.device_,
                        VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipeline_);
                    vkDestroyShaderModule(owner.device_, module, nullptr);
                    Check(pipelineResult, "vkCreateComputePipelines(LBVH)");

                    std::array<VkDescriptorPoolSize, 2> sizes{};
                    std::uint32_t sizeCount = 0u;
                    if (storageCount != 0u)
                    {
                        sizes[sizeCount++] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageCount};
                    }
                    if (uniformCount != 0u)
                    {
                        sizes[sizeCount++] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformCount};
                    }
                    VkDescriptorPoolCreateInfo poolInfo{
                        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                    poolInfo.maxSets = 1u;
                    poolInfo.poolSizeCount = sizeCount;
                    poolInfo.pPoolSizes = sizes.data();
                    Check(vkCreateDescriptorPool(owner.device_, &poolInfo, nullptr,
                        &pool_), "vkCreateDescriptorPool(LBVH)");
                    VkDescriptorSetAllocateInfo allocateInfo{
                        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                    allocateInfo.descriptorPool = pool_;
                    allocateInfo.descriptorSetCount = 1u;
                    allocateInfo.pSetLayouts = &setLayout_;
                    Check(vkAllocateDescriptorSets(owner.device_, &allocateInfo, &set_),
                        "vkAllocateDescriptorSets(LBVH)");
                }
                catch (...)
                {
                    Reset();
                    throw;
                }
            }

            ~Kernel() { Reset(); }
            Kernel(const Kernel&) = delete;
            Kernel& operator=(const Kernel&) = delete;

            void Bind(const std::span<const DescriptorResource> resources)
            {
                std::vector<VkDescriptorBufferInfo> infos(resources.size());
                std::vector<VkWriteDescriptorSet> writes(resources.size());
                for (std::uint32_t index = 0u; index < resources.size(); ++index)
                {
                    if (resources[index].buffer == nullptr)
                    {
                        throw FaultError(GpuLbvhBuildFault::InvalidArgument,
                            "null buffer in LBVH descriptor update");
                    }
                    infos[index] = {resources[index].buffer->Handle(), 0u,
                        resources[index].buffer->Size()};
                    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[index].dstSet = set_;
                    writes[index].dstBinding = index;
                    writes[index].descriptorCount = 1u;
                    writes[index].descriptorType = resources[index].type;
                    writes[index].pBufferInfo = &infos[index];
                }
                vkUpdateDescriptorSets(owner_->device_,
                    static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
            }

            [[nodiscard]] double Dispatch(const std::uint32_t groupCountX)
            {
                if (groupCountX == 0u || groupCountX > owner_->maximumGroupCountX_)
                {
                    throw FaultError(GpuLbvhBuildFault::UnsupportedDevice,
                        "LBVH dispatch group count is zero or exceeds the device limit");
                }
                return owner_->Submit(true, [&](const VkCommandBuffer commandBuffer)
                {
                    owner_->ComputeBoundary(commandBuffer);
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline_);
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipelineLayout_, 2u, 1u, &set_, 0u, nullptr);
                    vkCmdDispatch(commandBuffer, groupCountX, 1u, 1u);
                });
            }

        private:
            void Reset() noexcept
            {
                if (owner_ == nullptr || owner_->device_ == VK_NULL_HANDLE)
                {
                    return;
                }
                if (pool_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(owner_->device_, pool_, nullptr);
                    pool_ = VK_NULL_HANDLE;
                }
                if (pipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(owner_->device_, pipeline_, nullptr);
                    pipeline_ = VK_NULL_HANDLE;
                }
                if (pipelineLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(owner_->device_, pipelineLayout_, nullptr);
                    pipelineLayout_ = VK_NULL_HANDLE;
                }
                if (setLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(owner_->device_, setLayout_, nullptr);
                    setLayout_ = VK_NULL_HANDLE;
                }
            }

            Impl* owner_{};
            VkDescriptorSetLayout setLayout_{VK_NULL_HANDLE};
            VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
            VkPipeline pipeline_{VK_NULL_HANDLE};
            VkDescriptorPool pool_{VK_NULL_HANDLE};
            VkDescriptorSet set_{VK_NULL_HANDLE};
        };

        enum class KernelId : std::size_t
        {
            MortonReset,
            Morton,
            RadixHistogram,
            RadixPrefix,
            RadixScatter,
            StableReset,
            StableValidate,
            HierarchyReset,
            Hierarchy,
            BoundsReset,
            EmitLeaves,
            ComputeDepths,
            InternalBounds,
            ReorderCanonicalTriangles,
            Count,
        };

        struct BuildResources
        {
            std::unique_ptr<Buffer> sourcePrimitives;
            std::unique_ptr<Buffer> sourceCanonicalTriangles;
            std::unique_ptr<Buffer> pairA;
            std::unique_ptr<Buffer> pairB;
            std::unique_ptr<Buffer> histogram;
            std::unique_ptr<Buffer> offsets;
            std::unique_ptr<Buffer> mortonInvalid;
            std::unique_ptr<Buffer> stableIdInvalid;
            std::unique_ptr<Buffer> mortonConfig;
            std::unique_ptr<Buffer> radixConfig;
            std::unique_ptr<Buffer> nodes;
            std::unique_ptr<Buffer> parents;
            std::unique_ptr<Buffer> hierarchyInvalid;
            std::unique_ptr<Buffer> hierarchyConfig;
            std::unique_ptr<Buffer> sortedPrimitives;
            std::unique_ptr<Buffer> depths;
            std::unique_ptr<Buffer> maximumDepth;
            std::unique_ptr<Buffer> boundsConfig;
            std::unique_ptr<Buffer> sortedCanonicalTriangles;
            std::unique_ptr<Buffer> canonicalReorderConfig;
        };

    public:
        ~Impl() { Reset(); }

        [[nodiscard]] GpuLbvhBuildOutput Create(
            const GpuLbvhBuildOwnerCreateInfo& info) noexcept
        {
            Reset();
            if (info.physicalDevice == VK_NULL_HANDLE || info.device == VK_NULL_HANDLE ||
                info.queue == VK_NULL_HANDLE || info.commandPool == VK_NULL_HANDLE ||
                info.shaderDirectory.empty() || info.maximumDepth == 0u)
            {
                output_ = Failure(GpuLbvhBuildFault::InvalidArgument,
                    "GPU LBVH creation requires physical/device/queue/command-pool handles, shader directory, and maximum depth");
                return output_;
            }
            try
            {
                physicalDevice_ = info.physicalDevice;
                device_ = info.device;
                queue_ = info.queue;
                commandPool_ = info.commandPool;
                queueFamilyIndex_ = info.queueFamilyIndex;
                shaderDirectory_ = info.shaderDirectory;
                maximumDepthLimit_ = info.maximumDepth;

                vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
                maximumGroupCountX_ = properties.limits.maxComputeWorkGroupCount[0];
                timestampPeriod_ = properties.limits.timestampPeriod;
                if (properties.limits.maxComputeWorkGroupInvocations < kRadixThreads ||
                    properties.limits.maxComputeWorkGroupSize[0] < kRadixThreads ||
                    maximumGroupCountX_ == 0u)
                {
                    throw FaultError(GpuLbvhBuildFault::UnsupportedDevice,
                        "GPU LBVH requires 128 compute invocations in X and a non-zero group-count limit");
                }

                std::uint32_t familyCount = 0u;
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
                std::vector<VkQueueFamilyProperties> families(familyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(
                    physicalDevice_, &familyCount, families.data());
                if (queueFamilyIndex_ >= families.size() ||
                    (families[queueFamilyIndex_].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0u)
                {
                    throw FaultError(GpuLbvhBuildFault::InvalidArgument,
                        "GPU LBVH queue family is out of range or lacks compute support");
                }
                timestampValidBits_ = families[queueFamilyIndex_].timestampValidBits;

                VkDescriptorSetLayoutCreateInfo emptyInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                Check(vkCreateDescriptorSetLayout(device_, &emptyInfo, nullptr,
                    &emptySetLayout_), "vkCreateDescriptorSetLayout(LBVH empty)");
                if (timestampValidBits_ != 0u)
                {
                    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
                    queryInfo.queryCount = 2u;
                    Check(vkCreateQueryPool(device_, &queryInfo, nullptr, &timestampPool_),
                        "vkCreateQueryPool(LBVH timestamp)");
                }
                CreateKernels();
                created_ = true;
                output_ = {};
                output_.fault = GpuLbvhBuildFault::None;
                output_.message = timestampPool_ == VK_NULL_HANDLE
                    ? "GPU LBVH owner created; queue timestamps are unavailable"
                    : "GPU LBVH owner created";
                return output_;
            }
            catch (const FaultError& error)
            {
                const GpuLbvhBuildOutput failure = Failure(error.Fault(), error.what());
                ResetResources();
                output_ = failure;
                return output_;
            }
            catch (const std::exception& error)
            {
                const GpuLbvhBuildOutput failure = Failure(
                    GpuLbvhBuildFault::VulkanFailure, error.what());
                ResetResources();
                output_ = failure;
                return output_;
            }
        }

        [[nodiscard]] GpuLbvhBuildOutput Build(
            const std::span<const SoftwarePrimitiveRecord> primitives,
            const std::span<const Gpu::CanonicalTraversalTriangle> canonicalTriangles,
            const std::uint64_t sceneFingerprint,
            const std::uint32_t sceneGeneration) noexcept
        {
            if (!created_)
            {
                return Failure(GpuLbvhBuildFault::InvalidArgument,
                    "GPU LBVH Build was called before Create succeeded");
            }
            if (primitives.empty() || canonicalTriangles.size() != primitives.size() ||
                sceneFingerprint == 0u || sceneGeneration == 0u ||
                primitives.size() >
                    (static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()) + 1u) / 2u)
            {
                return Failure(GpuLbvhBuildFault::InvalidArgument,
                    "GPU LBVH Build requires equally-sized non-empty build/ABI triangle streams, non-zero scene identity, and a representable node count");
            }
            for (std::size_t index = 0u; index < primitives.size(); ++index)
            {
                const SoftwarePrimitiveRecord& primitive = primitives[index];
                const Gpu::CanonicalTraversalTriangle& triangle =
                    canonicalTriangles[index];
                const bool positionsMatch =
                    triangle.positions[0].x == primitive.v0.x &&
                    triangle.positions[0].y == primitive.v0.y &&
                    triangle.positions[0].z == primitive.v0.z &&
                    triangle.positions[1].x == primitive.v1.x &&
                    triangle.positions[1].y == primitive.v1.y &&
                    triangle.positions[1].z == primitive.v1.z &&
                    triangle.positions[2].x == primitive.v2.x &&
                    triangle.positions[2].y == primitive.v2.y &&
                    triangle.positions[2].z == primitive.v2.z;
                if (!positionsMatch || triangle.metadata.z != primitive.identity.x)
                {
                    return Failure(GpuLbvhBuildFault::InvalidArgument,
                        "GPU LBVH build and ABI-v1 triangle streams disagree at source index " +
                            std::to_string(index));
                }
            }
            try
            {
                const std::uint32_t primitiveCount =
                    static_cast<std::uint32_t>(primitives.size());
                const std::uint32_t nodeCount = primitiveCount * 2u - 1u;
                const std::uint32_t radixGroupCount =
                    DivideRoundUp(primitiveCount, kRadixThreads);
                BuildResources next;
                next.sourcePrimitives = DeviceBuffer(primitives.size_bytes());
                next.sourceCanonicalTriangles = DeviceBuffer(canonicalTriangles.size_bytes());
                next.pairA = DeviceBuffer(
                    static_cast<VkDeviceSize>(primitiveCount) * sizeof(MortonPair));
                next.pairB = DeviceBuffer(
                    static_cast<VkDeviceSize>(primitiveCount) * sizeof(MortonPair));
                next.histogram = DeviceBuffer(static_cast<VkDeviceSize>(radixGroupCount) *
                    kRadixBuckets * sizeof(std::uint32_t));
                next.offsets = DeviceBuffer(static_cast<VkDeviceSize>(radixGroupCount) *
                    kRadixBuckets * sizeof(std::uint32_t));
                next.mortonInvalid = DeviceBuffer(sizeof(std::uint32_t));
                next.stableIdInvalid = DeviceBuffer(sizeof(std::uint32_t));
                next.mortonConfig = UniformBuffer(sizeof(MortonConfig));
                next.radixConfig = UniformBuffer(sizeof(RadixConfig));
                Upload(*next.sourcePrimitives, primitives.data(), primitives.size_bytes());
                Upload(*next.sourceCanonicalTriangles, canonicalTriangles.data(),
                    canonicalTriangles.size_bytes());

                const Aabb bounds = SceneBounds(primitives);
                const MortonConfig mortonSettings{
                    {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f},
                    {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f},
                    primitiveCount, {0u, 0u, 0u}};
                next.mortonConfig->Write(&mortonSettings, sizeof(mortonSettings));
                constexpr std::array mortonTypes{
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
                const std::array mortonResources{
                    DescriptorResource{next.sourcePrimitives.get(), mortonTypes[0]},
                    DescriptorResource{next.pairA.get(), mortonTypes[1]},
                    DescriptorResource{next.mortonConfig.get(), mortonTypes[2]},
                    DescriptorResource{next.mortonInvalid.get(), mortonTypes[3]}};
                KernelAt(KernelId::MortonReset).Bind(mortonResources);
                KernelAt(KernelId::Morton).Bind(mortonResources);
                double elapsed = KernelAt(KernelId::MortonReset).Dispatch(1u);
                elapsed += KernelAt(KernelId::Morton).Dispatch(
                    DivideRoundUp(primitiveCount, kBuildThreads));
                RequireZero(*next.mortonInvalid, GpuLbvhBuildFault::InvalidPrimitive,
                    "Morton primitive validation");

                Buffer* radixInput = next.pairA.get();
                Buffer* radixOutput = next.pairB.get();
                const auto bindRadix = [&](Kernel& kernel)
                {
                    const std::array resources{
                        DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                        DescriptorResource{radixOutput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                        DescriptorResource{next.histogram.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                        DescriptorResource{next.offsets.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                        DescriptorResource{next.radixConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                        DescriptorResource{next.stableIdInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
                    kernel.Bind(resources);
                };
                const auto radixPass = [&](const std::uint32_t shift, const std::uint32_t field)
                {
                    const RadixConfig settings{
                        primitiveCount, radixGroupCount, shift, field};
                    next.radixConfig->Write(&settings, sizeof(settings));
                    bindRadix(KernelAt(KernelId::RadixHistogram));
                    bindRadix(KernelAt(KernelId::RadixPrefix));
                    bindRadix(KernelAt(KernelId::RadixScatter));
                    elapsed += KernelAt(KernelId::RadixHistogram).Dispatch(radixGroupCount);
                    elapsed += KernelAt(KernelId::RadixPrefix).Dispatch(1u);
                    elapsed += KernelAt(KernelId::RadixScatter).Dispatch(radixGroupCount);
                    std::swap(radixInput, radixOutput);
                };
                for (std::uint32_t shift = 0u; shift < 32u; shift += 4u)
                {
                    radixPass(shift, 0u);
                }
                const RadixConfig validationSettings{
                    primitiveCount, radixGroupCount, 0u, 0u};
                next.radixConfig->Write(&validationSettings, sizeof(validationSettings));
                bindRadix(KernelAt(KernelId::StableReset));
                bindRadix(KernelAt(KernelId::StableValidate));
                elapsed += KernelAt(KernelId::StableReset).Dispatch(1u);
                elapsed += KernelAt(KernelId::StableValidate).Dispatch(radixGroupCount);
                RequireZero(*next.stableIdInvalid,
                    GpuLbvhBuildFault::DuplicatePrimitiveId,
                    "stable primitive-ID validation");
                for (std::uint32_t shift = 0u; shift < 32u; shift += 4u)
                {
                    radixPass(shift, 1u);
                }

                next.nodes = DeviceBuffer(
                    static_cast<VkDeviceSize>(nodeCount) * sizeof(SoftwareNodeRecord));
                next.parents = DeviceBuffer(
                    static_cast<VkDeviceSize>(nodeCount) * sizeof(std::uint32_t));
                next.hierarchyInvalid = DeviceBuffer(sizeof(std::uint32_t));
                next.hierarchyConfig = UniformBuffer(sizeof(HierarchyConfig));
                const HierarchyConfig hierarchySettings{
                    primitiveCount, nodeCount, {0u, 0u}};
                next.hierarchyConfig->Write(&hierarchySettings, sizeof(hierarchySettings));
                const std::array hierarchyResources{
                    DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.nodes.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.parents.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.hierarchyConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                    DescriptorResource{next.hierarchyInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
                KernelAt(KernelId::HierarchyReset).Bind(hierarchyResources);
                KernelAt(KernelId::Hierarchy).Bind(hierarchyResources);
                elapsed += KernelAt(KernelId::HierarchyReset).Dispatch(
                    DivideRoundUp(nodeCount, kBuildThreads));
                if (primitiveCount > 1u)
                {
                    elapsed += KernelAt(KernelId::Hierarchy).Dispatch(
                        DivideRoundUp(primitiveCount - 1u, kBuildThreads));
                }
                RequireZero(*next.hierarchyInvalid,
                    GpuLbvhBuildFault::MalformedHierarchy,
                    "Karras hierarchy validation");

                next.sortedPrimitives = DeviceBuffer(
                    static_cast<VkDeviceSize>(primitiveCount) * sizeof(SoftwarePrimitiveRecord));
                next.depths = DeviceBuffer(
                    static_cast<VkDeviceSize>(nodeCount) * sizeof(std::uint32_t));
                next.maximumDepth = DeviceBuffer(sizeof(std::uint32_t));
                next.boundsConfig = UniformBuffer(sizeof(BoundsConfig));
                BoundsConfig boundsSettings{
                    primitiveCount, nodeCount, 0u, maximumDepthLimit_};
                next.boundsConfig->Write(&boundsSettings, sizeof(boundsSettings));
                const std::array boundsResources{
                    DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.sourcePrimitives.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.sortedPrimitives.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.nodes.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.parents.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.depths.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.maximumDepth.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.hierarchyInvalid.get(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.boundsConfig.get(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}};
                KernelAt(KernelId::BoundsReset).Bind(boundsResources);
                KernelAt(KernelId::EmitLeaves).Bind(boundsResources);
                KernelAt(KernelId::ComputeDepths).Bind(boundsResources);
                KernelAt(KernelId::InternalBounds).Bind(boundsResources);
                elapsed += KernelAt(KernelId::BoundsReset).Dispatch(1u);
                elapsed += KernelAt(KernelId::EmitLeaves).Dispatch(
                    DivideRoundUp(primitiveCount, kBuildThreads));
                elapsed += KernelAt(KernelId::ComputeDepths).Dispatch(
                    DivideRoundUp(nodeCount, kBuildThreads));
                RequireZero(*next.hierarchyInvalid,
                    GpuLbvhBuildFault::MalformedHierarchy,
                    "LBVH depth validation");
                const std::uint32_t maximumDepth = DownloadValue<std::uint32_t>(
                    *next.maximumDepth);
                if (maximumDepth > maximumDepthLimit_)
                {
                    throw FaultError(GpuLbvhBuildFault::DepthOverflow,
                        "GPU LBVH depth exceeds the configured limit");
                }
                if (primitiveCount > 1u)
                {
                    for (std::uint32_t depth = maximumDepth;; --depth)
                    {
                        boundsSettings.currentDepth = depth;
                        next.boundsConfig->Write(&boundsSettings, sizeof(boundsSettings));
                        elapsed += KernelAt(KernelId::InternalBounds).Dispatch(
                            DivideRoundUp(primitiveCount - 1u, kBuildThreads));
                        if (depth == 0u)
                        {
                            break;
                        }
                    }
                }
                RequireZero(*next.hierarchyInvalid,
                    GpuLbvhBuildFault::MalformedHierarchy,
                    "LBVH bottom-up bounds validation");

                next.sortedCanonicalTriangles = DeviceBuffer(
                    static_cast<VkDeviceSize>(primitiveCount) *
                        sizeof(Gpu::CanonicalTraversalTriangle));
                next.canonicalReorderConfig = UniformBuffer(
                    sizeof(CanonicalReorderConfig));
                const CanonicalReorderConfig reorderSettings{
                    primitiveCount, {0u, 0u, 0u}};
                next.canonicalReorderConfig->Write(
                    &reorderSettings, sizeof(reorderSettings));
                const std::array reorderResources{
                    DescriptorResource{radixInput, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.sourceCanonicalTriangles.get(),
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.sortedCanonicalTriangles.get(),
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.hierarchyInvalid.get(),
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                    DescriptorResource{next.canonicalReorderConfig.get(),
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}};
                KernelAt(KernelId::ReorderCanonicalTriangles).Bind(reorderResources);
                elapsed += KernelAt(KernelId::ReorderCanonicalTriangles).Dispatch(
                    DivideRoundUp(primitiveCount, kBuildThreads));
                RequireZero(*next.hierarchyInvalid,
                    GpuLbvhBuildFault::MalformedHierarchy,
                    "ABI-v1 canonical triangle reorder validation");
                static_cast<void>(Submit(false,
                    [&](const VkCommandBuffer commandBuffer)
                    {
                        BufferBarrier(commandBuffer, *next.nodes,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                        BufferBarrier(commandBuffer, *next.sortedCanonicalTriangles,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    }));

                resources_ = std::move(next);
                output_ = {};
                output_.fault = GpuLbvhBuildFault::None;
                output_.message =
                    "device-side Morton/radix/Karras/bounds/ABI-v1 reorder build completed";
                output_.nodes = {resources_.nodes->Handle(), 0u, resources_.nodes->Size()};
                output_.triangles = {resources_.sortedCanonicalTriangles->Handle(), 0u,
                    resources_.sortedCanonicalTriangles->Size()};
                output_.nodeCount = nodeCount;
                output_.triangleCount = primitiveCount;
                output_.maximumDepth = maximumDepth;
                output_.sceneFingerprint = sceneFingerprint;
                output_.sceneGeneration = sceneGeneration;
                output_.buildGpuMilliseconds = elapsed;
                output_.buildGpuTimestampMeasured = timestampPool_ != VK_NULL_HANDLE;
                return output_;
            }
            catch (const FaultError& error)
            {
                return Failure(error.Fault(), error.what());
            }
            catch (const std::exception& error)
            {
                return Failure(GpuLbvhBuildFault::VulkanFailure, error.what());
            }
        }

        void Reset() noexcept
        {
            ResetResources();
            output_ = Failure(GpuLbvhBuildFault::InvalidArgument,
                "GPU LBVH owner is not created");
        }

        [[nodiscard]] bool IsCreated() const noexcept { return created_; }
        [[nodiscard]] const GpuLbvhBuildOutput& Output() const noexcept { return output_; }

    private:
        void ResetResources() noexcept
        {
            if (device_ != VK_NULL_HANDLE)
            {
                static_cast<void>(vkQueueWaitIdle(queue_));
            }
            resources_ = {};
            for (auto& kernel : kernels_)
            {
                kernel.reset();
            }
            if (device_ != VK_NULL_HANDLE && timestampPool_ != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device_, timestampPool_, nullptr);
            }
            if (device_ != VK_NULL_HANDLE && emptySetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, emptySetLayout_, nullptr);
            }
            physicalDevice_ = VK_NULL_HANDLE;
            device_ = VK_NULL_HANDLE;
            queue_ = VK_NULL_HANDLE;
            commandPool_ = VK_NULL_HANDLE;
            queueFamilyIndex_ = 0u;
            memoryProperties_ = {};
            emptySetLayout_ = VK_NULL_HANDLE;
            timestampPool_ = VK_NULL_HANDLE;
            timestampValidBits_ = 0u;
            timestampPeriod_ = 0.0f;
            maximumGroupCountX_ = 0u;
            maximumDepthLimit_ = 0u;
            shaderDirectory_.clear();
            created_ = false;
        }

        void CreateKernels()
        {
            constexpr std::array mortonTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
            constexpr std::array radixTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
            constexpr std::array hierarchyTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
            constexpr std::array boundsTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
            constexpr std::array reorderTypes{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
            const auto create = [&](const KernelId id, const char* file,
                const char* entry, const std::span<const VkDescriptorType> types)
            {
                kernels_[static_cast<std::size_t>(id)] = std::make_unique<Kernel>(
                    *this, shaderDirectory_ / file, entry, types);
            };
            create(KernelId::MortonReset, "software_lbvh_morton_reset.spv",
                "ResetMortonValidationCS", mortonTypes);
            create(KernelId::Morton, "software_lbvh_morton.spv", "CSMain", mortonTypes);
            create(KernelId::RadixHistogram, "software_lbvh_radix_histogram.spv",
                "HistogramCS", radixTypes);
            create(KernelId::RadixPrefix, "software_lbvh_radix_prefix.spv",
                "PrefixCS", radixTypes);
            create(KernelId::RadixScatter, "software_lbvh_radix_scatter.spv",
                "ScatterCS", radixTypes);
            create(KernelId::StableReset, "software_lbvh_radix_validate_reset.spv",
                "ResetStableIdValidationCS", radixTypes);
            create(KernelId::StableValidate, "software_lbvh_radix_validate.spv",
                "ValidateStableIdsCS", radixTypes);
            create(KernelId::HierarchyReset, "software_lbvh_reset.spv",
                "ResetCS", hierarchyTypes);
            create(KernelId::Hierarchy, "software_lbvh_hierarchy.spv",
                "HierarchyCS", hierarchyTypes);
            create(KernelId::BoundsReset, "software_lbvh_bounds_reset.spv",
                "ResetBoundsValidationCS", boundsTypes);
            create(KernelId::EmitLeaves, "software_lbvh_emit_leaves.spv",
                "EmitLeavesCS", boundsTypes);
            create(KernelId::ComputeDepths, "software_lbvh_depths.spv",
                "ComputeDepthsCS", boundsTypes);
            create(KernelId::InternalBounds, "software_lbvh_internal_bounds.spv",
                "InternalBoundsCS", boundsTypes);
            create(KernelId::ReorderCanonicalTriangles,
                "software_lbvh_reorder_v1.spv", "ReorderCanonicalTrianglesV1",
                reorderTypes);
        }

        [[nodiscard]] Kernel& KernelAt(const KernelId id)
        {
            return *kernels_[static_cast<std::size_t>(id)];
        }

        [[nodiscard]] std::uint32_t FindMemoryType(
            const std::uint32_t typeBits,
            const VkMemoryPropertyFlags properties) const
        {
            for (std::uint32_t index = 0u; index < memoryProperties_.memoryTypeCount; ++index)
            {
                if ((typeBits & (1u << index)) != 0u &&
                    (memoryProperties_.memoryTypes[index].propertyFlags & properties) == properties)
                {
                    return index;
                }
            }
            throw FaultError(GpuLbvhBuildFault::UnsupportedDevice,
                "no Vulkan memory type satisfies the LBVH buffer requirements");
        }

        [[nodiscard]] std::unique_ptr<Buffer> DeviceBuffer(const VkDeviceSize size)
        {
            return std::make_unique<Buffer>(*this, size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        [[nodiscard]] std::unique_ptr<Buffer> UniformBuffer(const VkDeviceSize size)
        {
            return std::make_unique<Buffer>(*this, size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        }

        void BufferBarrier(
            const VkCommandBuffer commandBuffer,
            const Buffer& buffer,
            const VkAccessFlags sourceAccess,
            const VkAccessFlags destinationAccess,
            const VkPipelineStageFlags sourceStage,
            const VkPipelineStageFlags destinationStage) const noexcept
        {
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = sourceAccess;
            barrier.dstAccessMask = destinationAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer.Handle();
            barrier.offset = 0u;
            barrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0u,
                0u, nullptr, 1u, &barrier, 0u, nullptr);
        }

        void ComputeBoundary(const VkCommandBuffer commandBuffer) const noexcept
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &barrier,
                0u, nullptr, 0u, nullptr);
        }

        [[nodiscard]] double Submit(
            const bool measure,
            const std::function<void(VkCommandBuffer)>& record)
        {
            VkCommandBufferAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocateInfo.commandPool = commandPool_;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            Check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
                "vkAllocateCommandBuffers(LBVH)");
            try
            {
                VkCommandBufferBeginInfo beginInfo{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                    "vkBeginCommandBuffer(LBVH)");
                const bool timed = measure && timestampPool_ != VK_NULL_HANDLE;
                if (timed)
                {
                    vkCmdResetQueryPool(commandBuffer, timestampPool_, 0u, 2u);
                    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        timestampPool_, 0u);
                }
                record(commandBuffer);
                if (timed)
                {
                    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        timestampPool_, 1u);
                }
                Check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(LBVH)");
                VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submitInfo.commandBufferCount = 1u;
                submitInfo.pCommandBuffers = &commandBuffer;
                Check(vkQueueSubmit(queue_, 1u, &submitInfo, VK_NULL_HANDLE),
                    "vkQueueSubmit(LBVH)");
                Check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(LBVH)");
                double milliseconds = 0.0;
                if (timed)
                {
                    std::array<std::uint64_t, 2> timestamps{};
                    Check(vkGetQueryPoolResults(device_, timestampPool_, 0u, 2u,
                        sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t),
                        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                        "vkGetQueryPoolResults(LBVH)");
                    const std::uint64_t mask = timestampValidBits_ >= 64u
                        ? (std::numeric_limits<std::uint64_t>::max)()
                        : (1ull << timestampValidBits_) - 1ull;
                    const std::uint64_t delta = (timestamps[1] - timestamps[0]) & mask;
                    milliseconds = static_cast<double>(delta) *
                        static_cast<double>(timestampPeriod_) * 1.0e-6;
                }
                vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
                return milliseconds;
            }
            catch (...)
            {
                static_cast<void>(vkQueueWaitIdle(queue_));
                vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
                throw;
            }
        }

        void Upload(Buffer& destination, const void* data, const std::size_t byteCount)
        {
            Buffer staging(*this, static_cast<VkDeviceSize>(byteCount),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            staging.Write(data, byteCount);
            static_cast<void>(Submit(false, [&](const VkCommandBuffer commandBuffer)
            {
                VkBufferCopy copy{};
                copy.size = static_cast<VkDeviceSize>(byteCount);
                vkCmdCopyBuffer(commandBuffer, staging.Handle(), destination.Handle(), 1u, &copy);
                BufferBarrier(commandBuffer, destination, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            }));
        }

        template <typename T>
        [[nodiscard]] T DownloadValue(const Buffer& source)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            Buffer staging(*this, sizeof(T), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            static_cast<void>(Submit(false, [&](const VkCommandBuffer commandBuffer)
            {
                BufferBarrier(commandBuffer, source, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
                VkBufferCopy copy{};
                copy.size = sizeof(T);
                vkCmdCopyBuffer(commandBuffer, source.Handle(), staging.Handle(), 1u, &copy);
                BufferBarrier(commandBuffer, staging, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT);
            }));
            T value{};
            staging.Read(&value, sizeof(value));
            return value;
        }

        void RequireZero(
            const Buffer& counter,
            const GpuLbvhBuildFault fault,
            const char* stage)
        {
            const std::uint32_t value = DownloadValue<std::uint32_t>(counter);
            if (value != 0u)
            {
                throw FaultError(fault, std::string(stage) + " counter is " +
                    std::to_string(value));
            }
        }

        VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
        VkDevice device_{VK_NULL_HANDLE};
        VkQueue queue_{VK_NULL_HANDLE};
        VkCommandPool commandPool_{VK_NULL_HANDLE};
        std::uint32_t queueFamilyIndex_{};
        VkPhysicalDeviceMemoryProperties memoryProperties_{};
        VkDescriptorSetLayout emptySetLayout_{VK_NULL_HANDLE};
        VkQueryPool timestampPool_{VK_NULL_HANDLE};
        std::uint32_t timestampValidBits_{};
        float timestampPeriod_{};
        std::uint32_t maximumGroupCountX_{};
        std::uint32_t maximumDepthLimit_{};
        std::filesystem::path shaderDirectory_;
        std::array<std::unique_ptr<Kernel>, static_cast<std::size_t>(KernelId::Count)> kernels_{};
        BuildResources resources_{};
        GpuLbvhBuildOutput output_{};
        bool created_{};
    };

    GpuLbvhBuildOwner::GpuLbvhBuildOwner()
        : impl_(std::make_unique<Impl>())
    {
    }

    GpuLbvhBuildOwner::~GpuLbvhBuildOwner() = default;
    GpuLbvhBuildOwner::GpuLbvhBuildOwner(GpuLbvhBuildOwner&&) noexcept = default;
    GpuLbvhBuildOwner& GpuLbvhBuildOwner::operator=(GpuLbvhBuildOwner&&) noexcept = default;

    GpuLbvhBuildOutput GpuLbvhBuildOwner::Create(
        const GpuLbvhBuildOwnerCreateInfo& createInfo) noexcept
    {
        if (impl_ == nullptr)
        {
            impl_ = std::make_unique<Impl>();
        }
        return impl_->Create(createInfo);
    }

    GpuLbvhBuildOutput GpuLbvhBuildOwner::Build(
        const std::span<const SoftwarePrimitiveRecord> canonicalPrimitives,
        const std::span<const Gpu::CanonicalTraversalTriangle> canonicalTriangles,
        const std::uint64_t sceneFingerprint,
        const std::uint32_t sceneGeneration) noexcept
    {
        return impl_ != nullptr
            ? impl_->Build(canonicalPrimitives, canonicalTriangles,
                sceneFingerprint, sceneGeneration)
            : Failure(GpuLbvhBuildFault::InvalidArgument,
                "GPU LBVH owner was moved from");
    }

    void GpuLbvhBuildOwner::Reset() noexcept
    {
        if (impl_ != nullptr)
        {
            impl_->Reset();
        }
    }

    bool GpuLbvhBuildOwner::IsCreated() const noexcept
    {
        return impl_ != nullptr && impl_->IsCreated();
    }

    const GpuLbvhBuildOutput& GpuLbvhBuildOwner::Output() const noexcept
    {
        static const GpuLbvhBuildOutput movedFrom = Failure(
            GpuLbvhBuildFault::InvalidArgument, "GPU LBVH owner was moved from");
        return impl_ != nullptr ? impl_->Output() : movedFrom;
    }
}
