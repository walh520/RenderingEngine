#include <exception>
#include <iostream>

namespace RenderingEngine::Wavefront::Tests
{
    void RunWavefrontCpuOracleSelfTests();
}

int main()
{
    try
    {
        RenderingEngine::Wavefront::Tests::RunWavefrontCpuOracleSelfTests();
        std::cout << "L7 wavefront CPU self-tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "L7 wavefront CPU self-tests failed: " << exception.what() << '\n';
        return 1;
    }
}
