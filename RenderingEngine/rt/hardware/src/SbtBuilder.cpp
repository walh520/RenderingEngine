#include "SbtBuilder.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace RenderingEngine::Rt::Hardware
{
    namespace
    {
        [[nodiscard]] VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept
        {
            const VkDeviceSize safeAlignment = std::max<VkDeviceSize>(alignment, 1u);
            return ((value + safeAlignment - 1u) / safeAlignment) * safeAlignment;
        }

        [[nodiscard]] VkDeviceSize RegionSize(
            std::uint32_t count,
            VkDeviceSize stride) noexcept
        {
            return static_cast<VkDeviceSize>(count) * stride;
        }
    }

    std::uint32_t SbtGroupCounts::Total() const noexcept
    {
        return rayGeneration + miss + hit + callable;
    }

    Status ComputeSbtLayout(
        const HardwareRtLimits& limits,
        const SbtGroupCounts& counts,
        std::span<const SbtRecordData> records,
        SbtComputedLayout& output) noexcept
    {
        if (counts.rayGeneration != 1u || counts.Total() == 0u ||
            records.size() != counts.Total() || limits.shaderGroupHandleSize == 0u ||
            limits.shaderGroupHandleAlignment == 0u || limits.shaderGroupBaseAlignment == 0u)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Invalid SBT limits, counts, or records");
        }
        VkDeviceSize maximumInlineData = 0u;
        for (const SbtRecordData& record : records)
        {
            maximumInlineData = std::max<VkDeviceSize>(maximumInlineData, record.inlineData.size_bytes());
        }
        const VkDeviceSize stride = AlignUp(
            static_cast<VkDeviceSize>(limits.shaderGroupHandleSize) + maximumInlineData,
            limits.shaderGroupHandleAlignment);
        if (stride > limits.maxShaderGroupStride)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "SBT record stride exceeds device limit");
        }

        SbtComputedLayout layout{};
        layout.recordStride = stride;
        layout.maxInlineDataSize = maximumInlineData;
        layout.rayGenerationOffset = 0u;
        layout.missOffset = AlignUp(
            layout.rayGenerationOffset + RegionSize(counts.rayGeneration, stride),
            limits.shaderGroupBaseAlignment);
        layout.hitOffset = AlignUp(
            layout.missOffset + RegionSize(counts.miss, stride),
            limits.shaderGroupBaseAlignment);
        layout.callableOffset = AlignUp(
            layout.hitOffset + RegionSize(counts.hit, stride),
            limits.shaderGroupBaseAlignment);
        layout.totalSize = layout.callableOffset + RegionSize(counts.callable, stride);
        if (counts.callable == 0u)
        {
            layout.totalSize = layout.callableOffset;
        }
        if (layout.totalSize == 0u || layout.totalSize > std::numeric_limits<std::uint32_t>::max())
        {
            return Status::Failure(VK_ERROR_OUT_OF_DEVICE_MEMORY, "SBT byte size is invalid");
        }
        output = layout;
        return Status::Success();
    }

    bool ShaderBindingTable::IsValid() const noexcept { return buffer_.IsValid(); }
    const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::RayGenerationRegion() const noexcept { return rayGeneration_; }
    const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::MissRegion() const noexcept { return miss_; }
    const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::HitRegion() const noexcept { return hit_; }
    const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::CallableRegion() const noexcept { return callable_; }
    const DeviceBuffer& ShaderBindingTable::Buffer() const noexcept { return buffer_; }

    SbtBuilder::SbtBuilder(
        const DeviceBufferAllocator& allocator,
        const DeviceDispatch& dispatch,
        HardwareRtLimits limits) noexcept
        : allocator_(&allocator), dispatch_(&dispatch), limits_(limits)
    {
    }

    Status SbtBuilder::Build(
        VkPipeline pipeline,
        const SbtGroupCounts& counts,
        std::span<const SbtRecordData> records,
        ShaderBindingTable& output) const noexcept
    {
        if (pipeline == VK_NULL_HANDLE || dispatch_ == nullptr || !dispatch_->HasRtPipelineFunctions())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "SBT requires a ray-tracing pipeline and dispatch");
        }
        SbtComputedLayout layout{};
        Status status = ComputeSbtLayout(limits_, counts, records, layout);
        if (!status)
        {
            return status;
        }

        std::vector<std::byte> handles(
            static_cast<std::size_t>(counts.Total()) * limits_.shaderGroupHandleSize);
        const VkResult handleResult = dispatch_->getRayTracingShaderGroupHandles(
            allocator_->Device(), pipeline, 0u, counts.Total(), handles.size(), handles.data());
        if (handleResult != VK_SUCCESS)
        {
            return Status::Failure(handleResult, "vkGetRayTracingShaderGroupHandlesKHR failed");
        }

        ShaderBindingTable table{};
        status = allocator_->Create(
            layout.totalSize + limits_.shaderGroupBaseAlignment,
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true,
            true,
            table.buffer_);
        if (!status)
        {
            return status;
        }

        const VkDeviceSize basePadding = AlignUp(table.buffer_.Address(), limits_.shaderGroupBaseAlignment) -
            table.buffer_.Address();
        auto* mapped = static_cast<std::byte*>(table.buffer_.MappedData()) + basePadding;
        std::memset(mapped, 0, static_cast<std::size_t>(layout.totalSize));
        std::uint32_t groupIndex = 0u;
        const auto writeRegion = [&](VkDeviceSize regionOffset, std::uint32_t count)
        {
            for (std::uint32_t localIndex = 0u; localIndex < count; ++localIndex, ++groupIndex)
            {
                std::byte* destination = mapped + regionOffset + localIndex * layout.recordStride;
                const std::byte* sourceHandle = handles.data() +
                    static_cast<std::size_t>(groupIndex) * limits_.shaderGroupHandleSize;
                std::memcpy(destination, sourceHandle, limits_.shaderGroupHandleSize);
                const std::span<const std::byte> inlineData = records[groupIndex].inlineData;
                if (!inlineData.empty())
                {
                    std::memcpy(destination + limits_.shaderGroupHandleSize,
                        inlineData.data(), inlineData.size_bytes());
                }
            }
        };
        writeRegion(layout.rayGenerationOffset, counts.rayGeneration);
        writeRegion(layout.missOffset, counts.miss);
        writeRegion(layout.hitOffset, counts.hit);
        writeRegion(layout.callableOffset, counts.callable);

        const VkDeviceAddress baseAddress = table.buffer_.Address() + basePadding;
        table.rayGeneration_ = VkStridedDeviceAddressRegionKHR{
            baseAddress + layout.rayGenerationOffset,
            layout.recordStride,
            RegionSize(counts.rayGeneration, layout.recordStride)};
        table.miss_ = counts.miss == 0u ? VkStridedDeviceAddressRegionKHR{} : VkStridedDeviceAddressRegionKHR{
            baseAddress + layout.missOffset,
            layout.recordStride,
            RegionSize(counts.miss, layout.recordStride)};
        table.hit_ = counts.hit == 0u ? VkStridedDeviceAddressRegionKHR{} : VkStridedDeviceAddressRegionKHR{
            baseAddress + layout.hitOffset,
            layout.recordStride,
            RegionSize(counts.hit, layout.recordStride)};
        table.callable_ = counts.callable == 0u ? VkStridedDeviceAddressRegionKHR{} : VkStridedDeviceAddressRegionKHR{
            baseAddress + layout.callableOffset,
            layout.recordStride,
            RegionSize(counts.callable, layout.recordStride)};
        output = std::move(table);
        return Status::Success();
    }
}
