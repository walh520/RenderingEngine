#include "VulkanSmoke.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace L4Smoke = RenderingEngine::Rt::SoftwareGpu::VulkanSmoke;

namespace
{
    void PrintPath(const char* const name, const L4Smoke::PathEvidence& evidence)
    {
        std::cout << "[VULKAN] " << name << ": rays=" << evidence.rayCount
                  << ", buildPassed=" << (evidence.buildPassed ? "true" : "false")
                  << ", tracePassed=" << (evidence.tracePassed ? "true" : "false")
                  << ", hits=" << evidence.hitCount
                  << ", nodeTests=" << evidence.counters.slots[0]
                  << ", triangleTests=" << evidence.counters.slots[1]
                  << ", stackOverflow=" << evidence.counters.slots[2]
                  << ", invalidRay=" << evidence.counters.slots[3]
                  << ", invalidHit=" << evidence.counters.slots[4]
                  << ", maxStack=" << evidence.counters.slots[5]
                  << ", leafVisits=" << evidence.counters.slots[6]
                  << ", leafPrimitives=" << evidence.counters.slots[7]
                  << ", maxLeafOccupancy=" << evidence.counters.slots[8]
                  << ", bvhDepth=" << evidence.maximumDepth
                  << ", uploadBytes=" << evidence.uploadBytes
                  << ", readbackBytes=" << evidence.readbackBytes
                  << ", gpuTimestampsMeasured="
                  << (evidence.gpuTimestampsMeasured ? "true" : "false") << '\n';
    }
}

int main(const int argc, char** const argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: RenderingEngine.SoftwareGpu.VulkanSmoke <shader-directory>\n";
        return 2;
    }

    const L4Smoke::Report report = L4Smoke::Run(std::filesystem::path(argv[1]));
    for (const std::string& message : report.validationMessages)
    {
        std::cout << "[VALIDATION] " << message << '\n';
    }
    std::cout << "[VULKAN] device=" << (report.deviceName.empty() ? "<none>" : report.deviceName)
              << ", validationAvailable=" << (report.validationLayerAvailable ? "true" : "false")
              << ", validationEnabled=" << (report.validationLayerEnabled ? "true" : "false")
              << ", syncValidation="
              << (report.synchronizationValidationEnabled ? "true" : "false")
              << ", warnings=" << report.validationWarnings
              << ", errors=" << report.validationErrors << '\n';

    if (report.status == L4Smoke::Status::Skipped)
    {
        std::cout << "[SKIP] L4 Vulkan execution smoke: " << report.reason << '\n';
        return 0;
    }
    if (report.status == L4Smoke::Status::Failed)
    {
        std::cerr << "[FAIL] L4 Vulkan execution smoke: " << report.reason << '\n';
        return 1;
    }

    PrintPath("flattened-sah", report.flattenedSah);
    PrintPath("gpu-lbvh", report.gpuLbvh);
    std::cout << "[PASS] L4 Vulkan execution smoke and CPU readback parity\n";
    return 0;
}
