#include "DeviceDispatch.hpp"

#include <string>

namespace RenderingEngine::Rt::Hardware
{
    namespace
    {
        template <typename FunctionType>
        void LoadFunction(VkDevice device, const char* name, FunctionType& output) noexcept
        {
            output = reinterpret_cast<FunctionType>(vkGetDeviceProcAddr(device, name));
        }
    }

    Status DeviceDispatch::Load(VkDevice device, bool requireRtPipeline, DeviceDispatch& output) noexcept
    {
        if (device == VK_NULL_HANDLE)
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "DeviceDispatch requires a valid device");
        }

        DeviceDispatch loaded{};
        LoadFunction(device, "vkGetBufferDeviceAddress", loaded.getBufferDeviceAddress);
        if (loaded.getBufferDeviceAddress == nullptr)
        {
            LoadFunction(device, "vkGetBufferDeviceAddressKHR", loaded.getBufferDeviceAddress);
        }
        LoadFunction(device, "vkCreateAccelerationStructureKHR", loaded.createAccelerationStructure);
        LoadFunction(device, "vkDestroyAccelerationStructureKHR", loaded.destroyAccelerationStructure);
        LoadFunction(device, "vkGetAccelerationStructureBuildSizesKHR", loaded.getAccelerationStructureBuildSizes);
        LoadFunction(device, "vkGetAccelerationStructureDeviceAddressKHR", loaded.getAccelerationStructureDeviceAddress);
        LoadFunction(device, "vkCmdBuildAccelerationStructuresKHR", loaded.cmdBuildAccelerationStructures);
        LoadFunction(device, "vkCmdCopyAccelerationStructureKHR", loaded.cmdCopyAccelerationStructure);
        LoadFunction(device, "vkCmdWriteAccelerationStructuresPropertiesKHR",
            loaded.cmdWriteAccelerationStructuresProperties);
        LoadFunction(device, "vkCmdPipelineBarrier2", loaded.cmdPipelineBarrier2);
        if (loaded.cmdPipelineBarrier2 == nullptr)
        {
            LoadFunction(device, "vkCmdPipelineBarrier2KHR", loaded.cmdPipelineBarrier2);
        }
        LoadFunction(device, "vkCreateRayTracingPipelinesKHR", loaded.createRayTracingPipelines);
        LoadFunction(device, "vkGetRayTracingShaderGroupHandlesKHR", loaded.getRayTracingShaderGroupHandles);
        LoadFunction(device, "vkCmdTraceRaysKHR", loaded.cmdTraceRays);

        if (!loaded.HasRayQueryBuildFunctions())
        {
            return Status::Failure(VK_ERROR_EXTENSION_NOT_PRESENT,
                "Missing acceleration-structure or synchronization device function");
        }
        if (requireRtPipeline && !loaded.HasRtPipelineFunctions())
        {
            return Status::Failure(VK_ERROR_EXTENSION_NOT_PRESENT,
                "Missing ray-tracing-pipeline device function");
        }
        output = loaded;
        return Status::Success();
    }

    bool DeviceDispatch::HasRayQueryBuildFunctions() const noexcept
    {
        return getBufferDeviceAddress != nullptr && createAccelerationStructure != nullptr &&
            destroyAccelerationStructure != nullptr && getAccelerationStructureBuildSizes != nullptr &&
            getAccelerationStructureDeviceAddress != nullptr && cmdBuildAccelerationStructures != nullptr &&
            cmdCopyAccelerationStructure != nullptr && cmdWriteAccelerationStructuresProperties != nullptr &&
            cmdPipelineBarrier2 != nullptr;
    }

    bool DeviceDispatch::HasRtPipelineFunctions() const noexcept
    {
        return createRayTracingPipelines != nullptr && getRayTracingShaderGroupHandles != nullptr &&
            cmdTraceRays != nullptr;
    }
}
