#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RenderingEngine::Rt::Hardware
{
    inline constexpr std::array<const char*, 5> kRayQueryRequiredDeviceExtensions{
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

    inline constexpr std::array<const char*, 1> kRtPipelineAdditionalDeviceExtensions{
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME};

    struct HardwareRtLimits final
    {
        VkDeviceSize minAccelerationStructureScratchOffsetAlignment{1u};
        std::uint32_t shaderGroupHandleSize{0u};
        std::uint32_t shaderGroupHandleAlignment{1u};
        std::uint32_t shaderGroupBaseAlignment{1u};
        std::uint32_t maxShaderGroupStride{0u};
        std::uint32_t maxRayRecursionDepth{0u};
        std::uint32_t maxRayDispatchInvocationCount{0u};
        std::uint32_t maxRayHitAttributeSize{0u};
        std::uint32_t maxComputeWorkGroupCountX{0u};
    };

    struct HardwareRtCapabilityReport final
    {
        bool accelerationStructureExtension{false};
        bool rayQueryExtension{false};
        bool deferredHostOperationsExtension{false};
        bool bufferDeviceAddressExtensionOrCore{false};
        bool synchronization2ExtensionOrCore{false};
        bool rtPipelineExtension{false};
        bool accelerationStructureFeature{false};
        bool rayQueryFeature{false};
        bool bufferDeviceAddressFeature{false};
        bool synchronization2Feature{false};
        bool rtPipelineFeature{false};
        HardwareRtLimits limits{};
        std::vector<std::string> missingRayQueryRequirements;
        std::vector<std::string> missingRtPipelineRequirements;

        [[nodiscard]] bool SupportsRayQuery() const noexcept;
        [[nodiscard]] bool SupportsRtPipeline() const noexcept;
    };

    // Owns the pNext chain used at vkCreateDevice. The object is deliberately
    // non-copyable because every pNext points into this instance.
    struct HardwareRtDeviceFeatureChain final
    {
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeline{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        VkPhysicalDeviceSynchronization2Features synchronization2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};

        explicit HardwareRtDeviceFeatureChain(bool enableRtPipeline) noexcept;
        HardwareRtDeviceFeatureChain(const HardwareRtDeviceFeatureChain&) = delete;
        HardwareRtDeviceFeatureChain& operator=(const HardwareRtDeviceFeatureChain&) = delete;
    };

    [[nodiscard]] HardwareRtCapabilityReport QueryHardwareRtCapabilities(VkPhysicalDevice physicalDevice);

    [[nodiscard]] std::vector<const char*> RequiredHardwareRtDeviceExtensions(
        bool enableRtPipeline,
        std::uint32_t physicalDeviceApiVersion);
}
