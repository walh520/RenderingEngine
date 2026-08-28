#include "HardwareRtCapabilities.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace RenderingEngine::Rt::Hardware
{
    namespace
    {
        [[nodiscard]] bool HasExtension(
            const std::unordered_set<std::string>& extensions,
            const char* name)
        {
            return extensions.contains(name);
        }

        void AddMissing(bool available, const char* name, std::vector<std::string>& output)
        {
            if (!available)
            {
                output.emplace_back(name);
            }
        }
    }

    bool HardwareRtCapabilityReport::SupportsRayQuery() const noexcept
    {
        return missingRayQueryRequirements.empty();
    }

    bool HardwareRtCapabilityReport::SupportsRtPipeline() const noexcept
    {
        return SupportsRayQuery() && missingRtPipelineRequirements.empty();
    }

    HardwareRtDeviceFeatureChain::HardwareRtDeviceFeatureChain(bool enableRtPipeline) noexcept
    {
        features2.pNext = &bufferDeviceAddress;
        bufferDeviceAddress.pNext = &accelerationStructure;
        accelerationStructure.pNext = &rayQuery;
        rayQuery.pNext = enableRtPipeline ? static_cast<void*>(&rtPipeline) : &synchronization2;
        rtPipeline.pNext = &synchronization2;

        bufferDeviceAddress.bufferDeviceAddress = VK_TRUE;
        accelerationStructure.accelerationStructure = VK_TRUE;
        rayQuery.rayQuery = VK_TRUE;
        rtPipeline.rayTracingPipeline = enableRtPipeline ? VK_TRUE : VK_FALSE;
        synchronization2.synchronization2 = VK_TRUE;
    }

    HardwareRtCapabilityReport QueryHardwareRtCapabilities(VkPhysicalDevice physicalDevice)
    {
        HardwareRtCapabilityReport report{};
        if (physicalDevice == VK_NULL_HANDLE)
        {
            report.missingRayQueryRequirements.emplace_back("valid VkPhysicalDevice");
            report.missingRtPipelineRequirements.emplace_back("valid VkPhysicalDevice");
            return report;
        }

        std::uint32_t extensionCount = 0u;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
        {
            report.missingRayQueryRequirements.emplace_back("device extension enumeration");
            report.missingRtPipelineRequirements.emplace_back("device extension enumeration");
            return report;
        }

        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        if (vkEnumerateDeviceExtensionProperties(
                physicalDevice,
                nullptr,
                &extensionCount,
                extensionProperties.data()) != VK_SUCCESS)
        {
            report.missingRayQueryRequirements.emplace_back("device extension enumeration");
            report.missingRtPipelineRequirements.emplace_back("device extension enumeration");
            return report;
        }

        std::unordered_set<std::string> extensions;
        extensions.reserve(extensionProperties.size());
        for (const VkExtensionProperties& extension : extensionProperties)
        {
            extensions.emplace(extension.extensionName);
        }

        VkPhysicalDeviceProperties baseProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &baseProperties);
        const bool bufferDeviceAddressCore = VK_API_VERSION_MAJOR(baseProperties.apiVersion) > 1u ||
            (VK_API_VERSION_MAJOR(baseProperties.apiVersion) == 1u &&
             VK_API_VERSION_MINOR(baseProperties.apiVersion) >= 2u);
        const bool synchronization2Core = VK_API_VERSION_MAJOR(baseProperties.apiVersion) > 1u ||
            (VK_API_VERSION_MAJOR(baseProperties.apiVersion) == 1u &&
             VK_API_VERSION_MINOR(baseProperties.apiVersion) >= 3u);

        report.accelerationStructureExtension = HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        report.rayQueryExtension = HasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);
        report.deferredHostOperationsExtension = HasExtension(extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        report.bufferDeviceAddressExtensionOrCore = bufferDeviceAddressCore ||
            HasExtension(extensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        report.synchronization2ExtensionOrCore = synchronization2Core ||
            HasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        report.rtPipelineExtension = HasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeline{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        VkPhysicalDeviceSynchronization2Features synchronization2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        features2.pNext = &bufferDeviceAddress;
        bufferDeviceAddress.pNext = &accelerationStructure;
        accelerationStructure.pNext = &rayQuery;
        rayQuery.pNext = &rtPipeline;
        rtPipeline.pNext = &synchronization2;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

        report.bufferDeviceAddressFeature = bufferDeviceAddress.bufferDeviceAddress == VK_TRUE;
        report.accelerationStructureFeature = accelerationStructure.accelerationStructure == VK_TRUE;
        report.rayQueryFeature = rayQuery.rayQuery == VK_TRUE;
        report.rtPipelineFeature = rtPipeline.rayTracingPipeline == VK_TRUE;
        report.synchronization2Feature = synchronization2.synchronization2 == VK_TRUE;

        VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
        properties2.pNext = &accelerationStructureProperties;
        accelerationStructureProperties.pNext = &rtPipelineProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

        report.limits.minAccelerationStructureScratchOffsetAlignment =
            accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
        report.limits.shaderGroupHandleSize = rtPipelineProperties.shaderGroupHandleSize;
        report.limits.shaderGroupHandleAlignment = rtPipelineProperties.shaderGroupHandleAlignment;
        report.limits.shaderGroupBaseAlignment = rtPipelineProperties.shaderGroupBaseAlignment;
        report.limits.maxShaderGroupStride = rtPipelineProperties.maxShaderGroupStride;
        report.limits.maxRayRecursionDepth = rtPipelineProperties.maxRayRecursionDepth;
        report.limits.maxRayDispatchInvocationCount = rtPipelineProperties.maxRayDispatchInvocationCount;
        report.limits.maxRayHitAttributeSize = rtPipelineProperties.maxRayHitAttributeSize;
        report.limits.maxComputeWorkGroupCountX = baseProperties.limits.maxComputeWorkGroupCount[0];

        AddMissing(report.accelerationStructureExtension, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            report.missingRayQueryRequirements);
        AddMissing(report.rayQueryExtension, VK_KHR_RAY_QUERY_EXTENSION_NAME, report.missingRayQueryRequirements);
        AddMissing(report.deferredHostOperationsExtension, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            report.missingRayQueryRequirements);
        AddMissing(report.bufferDeviceAddressExtensionOrCore, "bufferDeviceAddress extension/core 1.2",
            report.missingRayQueryRequirements);
        AddMissing(report.accelerationStructureFeature, "accelerationStructure feature",
            report.missingRayQueryRequirements);
        AddMissing(report.rayQueryFeature, "rayQuery feature", report.missingRayQueryRequirements);
        AddMissing(report.bufferDeviceAddressFeature, "bufferDeviceAddress feature",
            report.missingRayQueryRequirements);
        AddMissing(report.synchronization2ExtensionOrCore, "synchronization2 extension/core 1.3",
            report.missingRayQueryRequirements);
        AddMissing(report.synchronization2Feature, "synchronization2 feature",
            report.missingRayQueryRequirements);

        AddMissing(report.rtPipelineExtension, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            report.missingRtPipelineRequirements);
        AddMissing(report.rtPipelineFeature, "rayTracingPipeline feature", report.missingRtPipelineRequirements);
        return report;
    }

    std::vector<const char*> RequiredHardwareRtDeviceExtensions(
        bool enableRtPipeline,
        std::uint32_t physicalDeviceApiVersion)
    {
        std::vector<const char*> result;
        result.reserve(kRayQueryRequiredDeviceExtensions.size() + kRtPipelineAdditionalDeviceExtensions.size());
        for (const char* extension : kRayQueryRequiredDeviceExtensions)
        {
            const std::uint32_t major = VK_API_VERSION_MAJOR(physicalDeviceApiVersion);
            const std::uint32_t minor = VK_API_VERSION_MINOR(physicalDeviceApiVersion);
            if (std::strcmp(extension, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0 &&
                (major > 1u || (major == 1u && minor >= 2u)))
            {
                continue;
            }
            if (std::strcmp(extension, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0 &&
                (major > 1u || (major == 1u && minor >= 3u)))
            {
                continue;
            }
            result.push_back(extension);
        }
        if (enableRtPipeline)
        {
            result.insert(result.end(), kRtPipelineAdditionalDeviceExtensions.begin(),
                kRtPipelineAdditionalDeviceExtensions.end());
        }
        return result;
    }
}
