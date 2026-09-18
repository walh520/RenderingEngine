#include <exception>
#include <iostream>

namespace RenderingEngine::Wavefront::Tests
{
    void RunWavefrontCpuOracleSelfTests();
    void RunWavefrontShadowPolicySelfTests();
    void RunVulkanWavefrontRecorderSelfTests();
}

int main()
{
    try
    {
        RenderingEngine::Wavefront::Tests::RunWavefrontCpuOracleSelfTests();
        RenderingEngine::Wavefront::Tests::RunWavefrontShadowPolicySelfTests();
        RenderingEngine::Wavefront::Tests::RunVulkanWavefrontRecorderSelfTests();
        std::cout << "L7 wavefront CPU, shadow-policy, shader, and Vulkan command-recorder self-tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "L7 wavefront CPU self-tests failed: " << exception.what() << '\n';
        return 1;
    }
}
