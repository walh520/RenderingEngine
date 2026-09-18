#pragma once

#include "DeviceBuffer.hpp"
#include "HardwareRtStatus.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Rt::Hardware
{
    enum class AccelerationStructureState : std::uint8_t
    {
        Empty,
        Allocated,
        BuildRecorded,
        Ready
    };

    struct TriangleGeometryInput final
    {
        VkDeviceAddress vertexAddress{0u};
        VkDeviceSize vertexStride{0u};
        std::uint32_t maxVertex{0u};
        VkFormat positionFormat{VK_FORMAT_R32G32B32_SFLOAT};
        VkDeviceAddress indexAddress{0u};
        VkIndexType indexType{VK_INDEX_TYPE_UINT32};
        VkDeviceAddress transformAddress{0u};
        std::uint32_t primitiveCount{0u};
        std::uint32_t primitiveOffset{0u};
        std::uint32_t firstVertex{0u};
        std::uint32_t transformOffset{0u};
        VkGeometryFlagsKHR geometryFlags{0u};
    };

    // Vulkan UPDATE permits changing vertex/transform contents, but not the
    // geometry description. L5 captures that immutable description after a
    // BUILD and requires an exact match before recording UPDATE.
    struct TriangleGeometryBuildSignature final
    {
        VkDeviceAddress vertexAddress{0u};
        VkDeviceSize vertexStride{0u};
        std::uint32_t maxVertex{0u};
        VkFormat positionFormat{VK_FORMAT_UNDEFINED};
        VkDeviceAddress indexAddress{0u};
        VkIndexType indexType{VK_INDEX_TYPE_MAX_ENUM};
        VkDeviceAddress transformAddress{0u};
        std::uint32_t primitiveCount{0u};
        std::uint32_t primitiveOffset{0u};
        std::uint32_t firstVertex{0u};
        std::uint32_t transformOffset{0u};
        VkGeometryFlagsKHR geometryFlags{0u};

        [[nodiscard]] bool operator==(const TriangleGeometryBuildSignature&) const noexcept = default;
    };

    [[nodiscard]] TriangleGeometryBuildSignature CaptureTriangleGeometryBuildSignature(
        const TriangleGeometryInput& input) noexcept;

    [[nodiscard]] bool IsTriangleGeometryInputValid(
        const TriangleGeometryInput& input) noexcept;

    [[nodiscard]] bool IsBottomLevelUpdateCompatible(
        std::span<const TriangleGeometryBuildSignature> built,
        std::span<const TriangleGeometryInput> update) noexcept;

    [[nodiscard]] bool AreCompactionStatesValid(
        AccelerationStructureState source,
        AccelerationStructureState destination) noexcept;

    struct SceneBuildFingerprint final
    {
        std::uint64_t topologyHash{0u};
        std::uint64_t transformHash{0u};
        std::uint32_t instanceCount{0u};
        bool allowUpdate{false};
    };

    enum class SceneUpdateDecision : std::uint8_t
    {
        NoBuild,
        RefitTlas,
        RebuildTlas,
        RebuildBlasAndTlas
    };

    [[nodiscard]] SceneUpdateDecision ChooseSceneUpdate(
        const SceneBuildFingerprint* previous,
        const SceneBuildFingerprint& current) noexcept;

    // Single-sided geometry is deliberately non-opaque so both Ray Query
    // candidate confirmation and RT any-hit can reject its back face.
    [[nodiscard]] VkGeometryFlagsKHR ChooseTriangleGeometryFlags(
        bool alphaMasked,
        bool doubleSided) noexcept;

    class AccelerationStructureResource final
    {
    public:
        AccelerationStructureResource() = default;
        ~AccelerationStructureResource();
        AccelerationStructureResource(const AccelerationStructureResource&) = delete;
        AccelerationStructureResource& operator=(const AccelerationStructureResource&) = delete;
        AccelerationStructureResource(AccelerationStructureResource&& other) noexcept;
        AccelerationStructureResource& operator=(AccelerationStructureResource&& other) noexcept;

        void Reset() noexcept;
        void MarkReady() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkAccelerationStructureKHR Handle() const noexcept;
        [[nodiscard]] VkDeviceAddress Address() const noexcept;
        [[nodiscard]] VkAccelerationStructureTypeKHR Type() const noexcept;
        [[nodiscard]] VkDeviceSize BuildScratchSize() const noexcept;
        [[nodiscard]] VkDeviceSize UpdateScratchSize() const noexcept;
        [[nodiscard]] VkDeviceSize StorageSize() const noexcept;
        [[nodiscard]] bool AllowsUpdate() const noexcept;
        [[nodiscard]] AccelerationStructureState State() const noexcept;

    private:
        friend class AccelerationStructureBuilder;
        VkDevice device_{VK_NULL_HANDLE};
        const DeviceDispatch* dispatch_{nullptr};
        VkAccelerationStructureKHR accelerationStructure_{VK_NULL_HANDLE};
        DeviceBuffer storage_{};
        VkAccelerationStructureTypeKHR type_{VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR};
        VkDeviceAddress address_{0u};
        VkDeviceSize buildScratchSize_{0u};
        VkDeviceSize updateScratchSize_{0u};
        VkDeviceSize storageSize_{0u};
        bool allowsUpdate_{false};
        VkBuildAccelerationStructureFlagsKHR buildFlags_{0u};
        std::vector<std::uint32_t> primitiveCapacities_{};
        std::vector<TriangleGeometryBuildSignature> lastBlasBuildSignature_{};
        std::uint32_t lastTlasInstanceCount_{0u};
        bool hasBuildSignature_{false};
        AccelerationStructureState state_{AccelerationStructureState::Empty};
    };

    class AccelerationStructureBuilder final
    {
    public:
        AccelerationStructureBuilder(
            const DeviceBufferAllocator& allocator,
            VkDeviceSize scratchAlignment) noexcept;

        [[nodiscard]] Status CreateBottomLevel(
            std::span<const TriangleGeometryInput> geometry,
            VkBuildAccelerationStructureFlagsKHR flags,
            AccelerationStructureResource& output) const noexcept;

        [[nodiscard]] Status CreateTopLevel(
            std::uint32_t maxInstanceCount,
            VkBuildAccelerationStructureFlagsKHR flags,
            AccelerationStructureResource& output) const noexcept;

        [[nodiscard]] Status CreateInstanceBuffer(
            std::span<const VkAccelerationStructureInstanceKHR> instances,
            DeviceBuffer& output) const noexcept;

        [[nodiscard]] Status CreateScratchBuffer(
            VkDeviceSize requestedSize,
            DeviceBuffer& output) const noexcept;

        [[nodiscard]] Status RecordBottomLevelBuild(
            VkCommandBuffer commandBuffer,
            std::span<const TriangleGeometryInput> geometry,
            const DeviceBuffer& scratch,
            AccelerationStructureResource& destination,
            VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR) const noexcept;

        [[nodiscard]] Status RecordTopLevelBuild(
            VkCommandBuffer commandBuffer,
            VkDeviceAddress instanceBufferAddress,
            std::uint32_t instanceCount,
            const DeviceBuffer& scratch,
            AccelerationStructureResource& destination,
            VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR) const noexcept;

        [[nodiscard]] Status CreateCompactionQueryPool(
            std::uint32_t queryCount,
            VkQueryPool& output) const noexcept;

        void RecordCompactedSizeQuery(
            VkCommandBuffer commandBuffer,
            const AccelerationStructureResource& source,
            VkQueryPool queryPool,
            std::uint32_t queryIndex) const noexcept;

        [[nodiscard]] Status CreateCompactedCopy(
            const AccelerationStructureResource& source,
            VkDeviceSize compactedSize,
            AccelerationStructureResource& output) const noexcept;

        [[nodiscard]] Status RecordCompaction(
            VkCommandBuffer commandBuffer,
            const AccelerationStructureResource& source,
            AccelerationStructureResource& destination) const noexcept;

        void RecordBuildToBuildBarrier(VkCommandBuffer commandBuffer) const noexcept;
        // Ray Query executes in Compute and is the Wave 2 default.  Callers
        // must opt into the Ray Tracing Shader stage only after enabling the
        // rayTracingPipeline feature for the Wave 3 pipeline path.
        void RecordBuildToTraceBarrier(
            VkCommandBuffer commandBuffer,
            bool includeRayTracingPipelineStage = false) const noexcept;

    private:
        [[nodiscard]] Status CreateResource(
            VkAccelerationStructureTypeKHR type,
            const VkAccelerationStructureBuildSizesInfoKHR& sizes,
            VkBuildAccelerationStructureFlagsKHR flags,
            AccelerationStructureResource& output) const noexcept;

        [[nodiscard]] bool ValidateScratch(
            const DeviceBuffer& scratch,
            VkDeviceSize requiredSize) const noexcept;

        [[nodiscard]] VkDeviceAddress AlignedScratchAddress(
            const DeviceBuffer& scratch) const noexcept;

        const DeviceBufferAllocator* allocator_{nullptr};
        VkDeviceSize scratchAlignment_{1u};
    };
}
