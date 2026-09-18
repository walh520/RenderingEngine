#include "AccelerationStructures.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <utility>

namespace RenderingEngine::Rt::Hardware
{
    namespace
    {
        [[nodiscard]] VkAccelerationStructureGeometryKHR MakeTriangleGeometry(
            const TriangleGeometryInput& input) noexcept
        {
            VkAccelerationStructureGeometryTrianglesDataKHR triangles{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
            triangles.vertexFormat = input.positionFormat;
            triangles.vertexData.deviceAddress = input.vertexAddress;
            triangles.vertexStride = input.vertexStride;
            triangles.maxVertex = input.maxVertex;
            triangles.indexType = input.indexType;
            triangles.indexData.deviceAddress = input.indexAddress;
            triangles.transformData.deviceAddress = input.transformAddress;

            VkAccelerationStructureGeometryKHR geometry{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles = triangles;
            geometry.flags = input.geometryFlags;
            return geometry;
        }

        [[nodiscard]] VkAccelerationStructureBuildRangeInfoKHR MakeBuildRange(
            const TriangleGeometryInput& input) noexcept
        {
            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = input.primitiveCount;
            range.primitiveOffset = input.primitiveOffset;
            range.firstVertex = input.firstVertex;
            range.transformOffset = input.transformOffset;
            return range;
        }

        [[nodiscard]] VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept
        {
            const VkDeviceSize safeAlignment = std::max<VkDeviceSize>(alignment, 1u);
            return ((value + safeAlignment - 1u) / safeAlignment) * safeAlignment;
        }
    }

    TriangleGeometryBuildSignature CaptureTriangleGeometryBuildSignature(
        const TriangleGeometryInput& input) noexcept
    {
        return TriangleGeometryBuildSignature{
            input.vertexAddress,
            input.vertexStride,
            input.maxVertex,
            input.positionFormat,
            input.indexAddress,
            input.indexType,
            input.transformAddress,
            input.primitiveCount,
            input.primitiveOffset,
            input.firstVertex,
            input.transformOffset,
            input.geometryFlags};
    }

    bool IsTriangleGeometryInputValid(const TriangleGeometryInput& input) noexcept
    {
        return input.vertexAddress != 0u &&
            input.vertexStride != 0u &&
            input.positionFormat != VK_FORMAT_UNDEFINED &&
            input.indexAddress != 0u &&
            input.indexType != VK_INDEX_TYPE_NONE_KHR &&
            input.indexType != VK_INDEX_TYPE_MAX_ENUM &&
            input.primitiveCount != 0u &&
            (input.transformAddress == 0u || (input.transformOffset & 0xfu) == 0u);
    }

    bool IsBottomLevelUpdateCompatible(
        std::span<const TriangleGeometryBuildSignature> built,
        std::span<const TriangleGeometryInput> update) noexcept
    {
        if (built.size() != update.size())
        {
            return false;
        }
        for (std::size_t index = 0u; index < built.size(); ++index)
        {
            if (!(built[index] == CaptureTriangleGeometryBuildSignature(update[index])))
            {
                return false;
            }
        }
        return true;
    }

    bool AreCompactionStatesValid(
        AccelerationStructureState source,
        AccelerationStructureState destination) noexcept
    {
        return source == AccelerationStructureState::Ready &&
            destination == AccelerationStructureState::Allocated;
    }

    SceneUpdateDecision ChooseSceneUpdate(
        const SceneBuildFingerprint* previous,
        const SceneBuildFingerprint& current) noexcept
    {
        if (previous == nullptr)
        {
            return SceneUpdateDecision::RebuildBlasAndTlas;
        }
        if (previous->topologyHash != current.topologyHash)
        {
            return SceneUpdateDecision::RebuildBlasAndTlas;
        }
        if (previous->instanceCount != current.instanceCount)
        {
            return SceneUpdateDecision::RebuildTlas;
        }
        if (previous->allowUpdate != current.allowUpdate)
        {
            return SceneUpdateDecision::RebuildTlas;
        }
        if (previous->transformHash == current.transformHash)
        {
            return SceneUpdateDecision::NoBuild;
        }
        if (previous->allowUpdate && current.allowUpdate)
        {
            return SceneUpdateDecision::RefitTlas;
        }
        return SceneUpdateDecision::RebuildTlas;
    }

    VkGeometryFlagsKHR ChooseTriangleGeometryFlags(bool alphaMasked, bool doubleSided) noexcept
    {
        return !alphaMasked && doubleSided ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0u;
    }

    AccelerationStructureResource::~AccelerationStructureResource()
    {
        Reset();
    }

    AccelerationStructureResource::AccelerationStructureResource(
        AccelerationStructureResource&& other) noexcept
    {
        *this = std::move(other);
    }

    AccelerationStructureResource& AccelerationStructureResource::operator=(
        AccelerationStructureResource&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            dispatch_ = std::exchange(other.dispatch_, nullptr);
            accelerationStructure_ = std::exchange(other.accelerationStructure_, VK_NULL_HANDLE);
            storage_ = std::move(other.storage_);
            type_ = std::exchange(other.type_, VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR);
            address_ = std::exchange(other.address_, 0u);
            buildScratchSize_ = std::exchange(other.buildScratchSize_, 0u);
            updateScratchSize_ = std::exchange(other.updateScratchSize_, 0u);
            storageSize_ = std::exchange(other.storageSize_, 0u);
            allowsUpdate_ = std::exchange(other.allowsUpdate_, false);
            buildFlags_ = std::exchange(other.buildFlags_, 0u);
            primitiveCapacities_ = std::move(other.primitiveCapacities_);
            lastBlasBuildSignature_ = std::move(other.lastBlasBuildSignature_);
            lastTlasInstanceCount_ = std::exchange(other.lastTlasInstanceCount_, 0u);
            hasBuildSignature_ = std::exchange(other.hasBuildSignature_, false);
            state_ = std::exchange(other.state_, AccelerationStructureState::Empty);
        }
        return *this;
    }

    void AccelerationStructureResource::Reset() noexcept
    {
        if (device_ != VK_NULL_HANDLE && accelerationStructure_ != VK_NULL_HANDLE && dispatch_ != nullptr)
        {
            dispatch_->destroyAccelerationStructure(device_, accelerationStructure_, nullptr);
        }
        accelerationStructure_ = VK_NULL_HANDLE;
        storage_.Reset();
        device_ = VK_NULL_HANDLE;
        dispatch_ = nullptr;
        type_ = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
        address_ = 0u;
        buildScratchSize_ = 0u;
        updateScratchSize_ = 0u;
        storageSize_ = 0u;
        allowsUpdate_ = false;
        buildFlags_ = 0u;
        primitiveCapacities_.clear();
        lastBlasBuildSignature_.clear();
        lastTlasInstanceCount_ = 0u;
        hasBuildSignature_ = false;
        state_ = AccelerationStructureState::Empty;
    }

    void AccelerationStructureResource::MarkReady() noexcept
    {
        if (state_ == AccelerationStructureState::BuildRecorded)
        {
            state_ = AccelerationStructureState::Ready;
        }
    }

    bool AccelerationStructureResource::IsValid() const noexcept { return accelerationStructure_ != VK_NULL_HANDLE; }
    VkAccelerationStructureKHR AccelerationStructureResource::Handle() const noexcept { return accelerationStructure_; }
    VkDeviceAddress AccelerationStructureResource::Address() const noexcept { return address_; }
    VkAccelerationStructureTypeKHR AccelerationStructureResource::Type() const noexcept { return type_; }
    VkDeviceSize AccelerationStructureResource::BuildScratchSize() const noexcept { return buildScratchSize_; }
    VkDeviceSize AccelerationStructureResource::UpdateScratchSize() const noexcept { return updateScratchSize_; }
    VkDeviceSize AccelerationStructureResource::StorageSize() const noexcept { return storageSize_; }
    bool AccelerationStructureResource::AllowsUpdate() const noexcept { return allowsUpdate_; }
    AccelerationStructureState AccelerationStructureResource::State() const noexcept { return state_; }

    AccelerationStructureBuilder::AccelerationStructureBuilder(
        const DeviceBufferAllocator& allocator,
        VkDeviceSize scratchAlignment) noexcept
        : allocator_(&allocator), scratchAlignment_(std::max<VkDeviceSize>(scratchAlignment, 1u))
    {
    }

    Status AccelerationStructureBuilder::CreateBottomLevel(
        std::span<const TriangleGeometryInput> geometry,
        VkBuildAccelerationStructureFlagsKHR flags,
        AccelerationStructureResource& output) const noexcept
    {
        if (geometry.empty())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "BLAS requires at least one geometry");
        }
        std::vector<VkAccelerationStructureGeometryKHR> vkGeometry;
        std::vector<std::uint32_t> primitiveCounts;
        vkGeometry.reserve(geometry.size());
        primitiveCounts.reserve(geometry.size());
        for (const TriangleGeometryInput& input : geometry)
        {
            if (!IsTriangleGeometryInputValid(input))
            {
                return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "BLAS geometry description is invalid");
            }
            vkGeometry.push_back(MakeTriangleGeometry(input));
            primitiveCounts.push_back(input.primitiveCount);
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = flags;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<std::uint32_t>(vkGeometry.size());
        buildInfo.pGeometries = vkGeometry.data();
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        allocator_->Dispatch().getAccelerationStructureBuildSizes(
            allocator_->Device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, primitiveCounts.data(), &sizes);
        Status status = CreateResource(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes, flags, output);
        if (status)
        {
            output.primitiveCapacities_ = std::move(primitiveCounts);
        }
        return status;
    }

    Status AccelerationStructureBuilder::CreateTopLevel(
        std::uint32_t maxInstanceCount,
        VkBuildAccelerationStructureFlagsKHR flags,
        AccelerationStructureResource& output) const noexcept
    {
        if (maxInstanceCount == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "TLAS requires at least one instance");
        }
        VkAccelerationStructureGeometryInstancesDataKHR instances{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instances;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = flags;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1u;
        buildInfo.pGeometries = &geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        allocator_->Dispatch().getAccelerationStructureBuildSizes(
            allocator_->Device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, &maxInstanceCount, &sizes);
        Status status = CreateResource(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes, flags, output);
        if (status)
        {
            output.primitiveCapacities_ = {maxInstanceCount};
        }
        return status;
    }

    Status AccelerationStructureBuilder::CreateInstanceBuffer(
        std::span<const VkAccelerationStructureInstanceKHR> instances,
        DeviceBuffer& output) const noexcept
    {
        if (instances.empty())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Instance buffer cannot be empty");
        }
        DeviceBuffer buffer{};
        const VkDeviceSize byteCount = instances.size_bytes();
        Status status = allocator_->Create(
            byteCount,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true,
            true,
            buffer);
        if (!status)
        {
            return status;
        }
        const auto bytes = std::as_bytes(instances);
        status = allocator_->Upload(buffer, bytes);
        if (!status)
        {
            return status;
        }
        output = std::move(buffer);
        return Status::Success();
    }

    Status AccelerationStructureBuilder::CreateScratchBuffer(
        VkDeviceSize requestedSize,
        DeviceBuffer& output) const noexcept
    {
        return allocator_->Create(
            AlignUp(requestedSize, scratchAlignment_) + scratchAlignment_ - 1u,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            true,
            false,
            output);
    }

    Status AccelerationStructureBuilder::RecordBottomLevelBuild(
        VkCommandBuffer commandBuffer,
        std::span<const TriangleGeometryInput> geometry,
        const DeviceBuffer& scratch,
        AccelerationStructureResource& destination,
        VkBuildAccelerationStructureModeKHR mode) const noexcept
    {
        if (commandBuffer == VK_NULL_HANDLE || !destination.IsValid() ||
            destination.Type() != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR || geometry.empty())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid BLAS build arguments");
        }
        if (mode != VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR &&
            mode != VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Unknown BLAS build mode");
        }
        if (geometry.size() != destination.primitiveCapacities_.size())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "BLAS geometry count exceeds sized capacity");
        }
        for (std::size_t index = 0u; index < geometry.size(); ++index)
        {
            if (!IsTriangleGeometryInputValid(geometry[index]) ||
                geometry[index].primitiveCount > destination.primitiveCapacities_[index])
            {
                return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                    "BLAS geometry is invalid or exceeds sized capacity");
            }
        }
        const bool isUpdate = mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        const VkDeviceSize requiredScratch = isUpdate
            ? destination.UpdateScratchSize()
            : destination.BuildScratchSize();
        if (!ValidateScratch(scratch, requiredScratch) ||
            (isUpdate && (!destination.AllowsUpdate() ||
                destination.State() != AccelerationStructureState::Ready ||
                !destination.hasBuildSignature_ ||
                !IsBottomLevelUpdateCompatible(destination.lastBlasBuildSignature_, geometry))))
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "BLAS scratch, state, or immutable UPDATE signature is invalid");
        }

        std::vector<VkAccelerationStructureGeometryKHR> vkGeometry;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers;
        vkGeometry.reserve(geometry.size());
        ranges.reserve(geometry.size());
        rangePointers.reserve(geometry.size());
        for (const TriangleGeometryInput& input : geometry)
        {
            vkGeometry.push_back(MakeTriangleGeometry(input));
            ranges.push_back(MakeBuildRange(input));
        }
        for (const VkAccelerationStructureBuildRangeInfoKHR& range : ranges)
        {
            rangePointers.push_back(&range);
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = destination.Type();
        buildInfo.flags = destination.buildFlags_;
        buildInfo.mode = mode;
        buildInfo.srcAccelerationStructure = mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
            ? destination.Handle() : VK_NULL_HANDLE;
        buildInfo.dstAccelerationStructure = destination.Handle();
        buildInfo.geometryCount = static_cast<std::uint32_t>(vkGeometry.size());
        buildInfo.pGeometries = vkGeometry.data();
        buildInfo.scratchData.deviceAddress = AlignedScratchAddress(scratch);
        allocator_->Dispatch().cmdBuildAccelerationStructures(commandBuffer, 1u, &buildInfo, rangePointers.data());
        if (!isUpdate)
        {
            destination.lastBlasBuildSignature_.clear();
            destination.lastBlasBuildSignature_.reserve(geometry.size());
            for (const TriangleGeometryInput& input : geometry)
            {
                destination.lastBlasBuildSignature_.push_back(
                    CaptureTriangleGeometryBuildSignature(input));
            }
            destination.hasBuildSignature_ = true;
        }
        destination.state_ = AccelerationStructureState::BuildRecorded;
        return Status::Success();
    }

    Status AccelerationStructureBuilder::RecordTopLevelBuild(
        VkCommandBuffer commandBuffer,
        VkDeviceAddress instanceBufferAddress,
        std::uint32_t instanceCount,
        const DeviceBuffer& scratch,
        AccelerationStructureResource& destination,
        VkBuildAccelerationStructureModeKHR mode) const noexcept
    {
        if (commandBuffer == VK_NULL_HANDLE || instanceBufferAddress == 0u || instanceCount == 0u ||
            !destination.IsValid() || destination.Type() != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid TLAS build arguments");
        }
        if (mode != VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR &&
            mode != VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Unknown TLAS build mode");
        }
        if (destination.primitiveCapacities_.empty() || instanceCount > destination.primitiveCapacities_[0])
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "TLAS instance count exceeds sized capacity");
        }
        const bool isUpdate = mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        const VkDeviceSize requiredScratch = isUpdate
            ? destination.UpdateScratchSize() : destination.BuildScratchSize();
        if (!ValidateScratch(scratch, requiredScratch) ||
            (isUpdate && (!destination.AllowsUpdate() ||
                destination.State() != AccelerationStructureState::Ready ||
                !destination.hasBuildSignature_ ||
                instanceCount != destination.lastTlasInstanceCount_)))
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "TLAS scratch, state, or immutable UPDATE signature is invalid");
        }

        VkAccelerationStructureGeometryInstancesDataKHR instances{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
        instances.data.deviceAddress = instanceBufferAddress;
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instances;
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = instanceCount;
        const VkAccelerationStructureBuildRangeInfoKHR* rangePointer = &range;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = destination.Type();
        buildInfo.flags = destination.buildFlags_;
        buildInfo.mode = mode;
        buildInfo.srcAccelerationStructure = mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
            ? destination.Handle() : VK_NULL_HANDLE;
        buildInfo.dstAccelerationStructure = destination.Handle();
        buildInfo.geometryCount = 1u;
        buildInfo.pGeometries = &geometry;
        buildInfo.scratchData.deviceAddress = AlignedScratchAddress(scratch);
        allocator_->Dispatch().cmdBuildAccelerationStructures(commandBuffer, 1u, &buildInfo, &rangePointer);
        if (!isUpdate)
        {
            destination.lastTlasInstanceCount_ = instanceCount;
            destination.hasBuildSignature_ = true;
        }
        destination.state_ = AccelerationStructureState::BuildRecorded;
        return Status::Success();
    }

    Status AccelerationStructureBuilder::CreateCompactionQueryPool(
        std::uint32_t queryCount,
        VkQueryPool& output) const noexcept
    {
        if (queryCount == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Compaction query count is zero");
        }
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        info.queryCount = queryCount;
        const VkResult result = vkCreateQueryPool(allocator_->Device(), &info, nullptr, &output);
        return result == VK_SUCCESS ? Status::Success() : Status::Failure(result, "vkCreateQueryPool failed");
    }

    void AccelerationStructureBuilder::RecordCompactedSizeQuery(
        VkCommandBuffer commandBuffer,
        const AccelerationStructureResource& source,
        VkQueryPool queryPool,
        std::uint32_t queryIndex) const noexcept
    {
        if (commandBuffer == VK_NULL_HANDLE || queryPool == VK_NULL_HANDLE || !source.IsValid() ||
            source.State() != AccelerationStructureState::Ready || !source.hasBuildSignature_ ||
            (source.buildFlags_ & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) == 0u)
        {
            return;
        }
        const VkAccelerationStructureKHR handle = source.Handle();
        allocator_->Dispatch().cmdWriteAccelerationStructuresProperties(
            commandBuffer, 1u, &handle,
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, queryIndex);
    }

    Status AccelerationStructureBuilder::CreateCompactedCopy(
        const AccelerationStructureResource& source,
        VkDeviceSize compactedSize,
        AccelerationStructureResource& output) const noexcept
    {
        if (&source == &output || !source.IsValid() || compactedSize == 0u ||
            compactedSize > source.StorageSize() ||
            source.State() != AccelerationStructureState::Ready || !source.hasBuildSignature_ ||
            (source.buildFlags_ & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid compacted acceleration-structure size");
        }
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        sizes.accelerationStructureSize = compactedSize;
        sizes.buildScratchSize = source.BuildScratchSize();
        sizes.updateScratchSize = source.UpdateScratchSize();
        const VkBuildAccelerationStructureFlagsKHR retainedFlags = source.buildFlags_;
        Status status = CreateResource(source.Type(), sizes, retainedFlags, output);
        if (status)
        {
            output.primitiveCapacities_ = source.primitiveCapacities_;
            output.lastBlasBuildSignature_ = source.lastBlasBuildSignature_;
            output.lastTlasInstanceCount_ = source.lastTlasInstanceCount_;
            output.hasBuildSignature_ = source.hasBuildSignature_;
        }
        return status;
    }

    Status AccelerationStructureBuilder::RecordCompaction(
        VkCommandBuffer commandBuffer,
        const AccelerationStructureResource& source,
        AccelerationStructureResource& destination) const noexcept
    {
        if (commandBuffer == VK_NULL_HANDLE || !source.IsValid() || !destination.IsValid() ||
            source.Type() != destination.Type() || destination.StorageSize() > source.StorageSize() ||
            !source.hasBuildSignature_ ||
            !AreCompactionStatesValid(source.State(), destination.State()) ||
            (source.buildFlags_ & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid acceleration-structure compaction");
        }
        VkCopyAccelerationStructureInfoKHR copyInfo{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
        copyInfo.src = source.Handle();
        copyInfo.dst = destination.Handle();
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        allocator_->Dispatch().cmdCopyAccelerationStructure(commandBuffer, &copyInfo);
        destination.lastBlasBuildSignature_ = source.lastBlasBuildSignature_;
        destination.lastTlasInstanceCount_ = source.lastTlasInstanceCount_;
        destination.hasBuildSignature_ = source.hasBuildSignature_;
        destination.state_ = AccelerationStructureState::BuildRecorded;
        return Status::Success();
    }

    void AccelerationStructureBuilder::RecordBuildToBuildBarrier(VkCommandBuffer commandBuffer) const noexcept
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1u;
        dependency.pMemoryBarriers = &barrier;
        allocator_->Dispatch().cmdPipelineBarrier2(commandBuffer, &dependency);
    }

    void AccelerationStructureBuilder::RecordBuildToTraceBarrier(
        const VkCommandBuffer commandBuffer,
        const bool includeRayTracingPipelineStage) const noexcept
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        if (includeRayTracingPipelineStage)
        {
            barrier.dstStageMask |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        }
        barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1u;
        dependency.pMemoryBarriers = &barrier;
        allocator_->Dispatch().cmdPipelineBarrier2(commandBuffer, &dependency);
    }

    Status AccelerationStructureBuilder::CreateResource(
        VkAccelerationStructureTypeKHR type,
        const VkAccelerationStructureBuildSizesInfoKHR& sizes,
        VkBuildAccelerationStructureFlagsKHR flags,
        AccelerationStructureResource& output) const noexcept
    {
        if (sizes.accelerationStructureSize == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Acceleration-structure size is zero");
        }
        AccelerationStructureResource created{};
        created.device_ = allocator_->Device();
        created.dispatch_ = &allocator_->Dispatch();
        created.type_ = type;
        created.buildScratchSize_ = sizes.buildScratchSize;
        created.updateScratchSize_ = sizes.updateScratchSize;
        created.storageSize_ = sizes.accelerationStructureSize;
        created.allowsUpdate_ = (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) != 0u;
        created.buildFlags_ = flags;

        Status status = allocator_->Create(
            sizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            true,
            false,
            created.storage_);
        if (!status)
        {
            return status;
        }
        VkAccelerationStructureCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer = created.storage_.Handle();
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = type;
        VkResult result = allocator_->Dispatch().createAccelerationStructure(
            allocator_->Device(), &createInfo, nullptr, &created.accelerationStructure_);
        if (result != VK_SUCCESS)
        {
            return Status::Failure(result, "vkCreateAccelerationStructureKHR failed");
        }
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        addressInfo.accelerationStructure = created.accelerationStructure_;
        created.address_ = allocator_->Dispatch().getAccelerationStructureDeviceAddress(
            allocator_->Device(), &addressInfo);
        if (created.address_ == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED,
                "Acceleration-structure device address is zero");
        }
        created.state_ = AccelerationStructureState::Allocated;
        output = std::move(created);
        return Status::Success();
    }

    bool AccelerationStructureBuilder::ValidateScratch(
        const DeviceBuffer& scratch,
        VkDeviceSize requiredSize) const noexcept
    {
        if (!scratch.IsValid() || scratch.Address() == 0u)
        {
            return false;
        }
        const VkDeviceSize padding = AlignedScratchAddress(scratch) - scratch.Address();
        return scratch.Size() >= padding && scratch.Size() - padding >= requiredSize;
    }

    VkDeviceAddress AccelerationStructureBuilder::AlignedScratchAddress(
        const DeviceBuffer& scratch) const noexcept
    {
        return AlignUp(scratch.Address(), scratchAlignment_);
    }
}
