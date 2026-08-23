#include "app/Application.hpp"
#include "platform/win32/Win32PlatformHost.hpp"

int main(int argumentCount, char** arguments)
{
    return RenderingEngine::RunApplication(
        argumentCount,
        arguments,
        &RenderingEngine::CreateWin32PlatformHost);
}
