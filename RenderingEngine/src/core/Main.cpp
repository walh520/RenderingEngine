#include "app/Application.hpp"
#include "platform/glfw/GlfwPlatformHost.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace
{
    void ConfigureUtf8Console() noexcept
    {
#if defined(_WIN32)
        DWORD mode = 0u;
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
        if ((output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode) != FALSE)
            || (error != INVALID_HANDLE_VALUE && GetConsoleMode(error, &mode) != FALSE))
        {
            static_cast<void>(SetConsoleOutputCP(CP_UTF8));
            static_cast<void>(SetConsoleCP(CP_UTF8));
        }
#endif
    }
}

int main(int argumentCount, char** arguments)
{
    ConfigureUtf8Console();
    return RenderingEngine::RunApplication(
        argumentCount,
        arguments,
        &RenderingEngine::CreateGlfwPlatformHost);
}
