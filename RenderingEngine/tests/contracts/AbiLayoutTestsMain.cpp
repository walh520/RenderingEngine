#include <cstdlib>
#include <iostream>

bool RunRuntimeControlTests();
bool RunReSTIRDIRuntimeTests();
bool RunReSTIRFrameParametersTests();
bool RunVulkanReSTIRRecorderContractTests();
bool RunWave3FrameGraphTests();
bool RunWave3AcceptanceTests();
bool RunWave3ExecutionTests();
bool RunWave4FrameGraphTests();
bool RunWave4AcceptanceTests();

int main()
{
    if (!RunRuntimeControlTests()
        || !RunReSTIRDIRuntimeTests()
        || !RunReSTIRFrameParametersTests()
        || !RunVulkanReSTIRRecorderContractTests()
        || !RunWave3FrameGraphTests()
        || !RunWave3AcceptanceTests()
        || !RunWave3ExecutionTests()
        || !RunWave4FrameGraphTests()
        || !RunWave4AcceptanceTests())
    {
        return EXIT_FAILURE;
    }
    std::cout << "ABI v0-v3 C++ layout and Wave 4 contract checks passed.\n";
    return EXIT_SUCCESS;
}
