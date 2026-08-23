#include <cstdlib>
#include <iostream>

bool RunRuntimeControlTests();

int main()
{
    if (!RunRuntimeControlTests())
    {
        return EXIT_FAILURE;
    }
    std::cout << "ABI v0 C++ layout checks passed.\n";
    return EXIT_SUCCESS;
}
