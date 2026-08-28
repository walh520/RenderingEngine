#pragma once

#include "HardwareRtStatus.hpp"

#include <vulkan/vulkan.h>

namespace RenderingEngine::Rt::Hardware
{
    struct DeviceDispatch final
    {
        PFN_vkGetBufferDeviceAddress getBufferDeviceAddress{nullptr};
        PFN_vkCreateAccelerationStructureKHR createAccelerationStructure{nullptr};
        PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure{nullptr};
        PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizes{nullptr};
        PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddress{nullptr};
        PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructures{nullptr};
        PFN_vkCmdCopyAccelerationStructureKHR cmdCopyAccelerationStructure{nullptr};
        PFN_vkCmdWriteAccelerationStructuresPropertiesKHR cmdWriteAccelerationStructuresProperties{nullptr};
        PFN_vkCmdPipelineBarrier2 cmdPipelineBarrier2{nullptr};
        PFN_vkCreateRayTracingPipelinesKHR createRayTracingPipelines{nullptr};
        PFN_vkGetRayTracingShaderGroupHandlesKHR getRayTracingShaderGroupHandles{nullptr};
        PFN_vkCmdTraceRaysKHR cmdTraceRays{nullptr};

        [[nodiscard]] static Status Load(
            VkDevice device,
            bool requireRtPipeline,
            DeviceDispatch& output) noexcept;

        [[nodiscard]] bool HasRayQueryBuildFunctions() const noexcept;
        [[nodiscard]] bool HasRtPipelineFunctions() const noexcept;
    };
}
