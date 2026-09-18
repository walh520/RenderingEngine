#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "core/ExecutablePath.hpp"

#include <array>
#include <stdexcept>

namespace RenderingEngine
{
    std::filesystem::path ExecutableDirectory()
    {
        std::array<wchar_t, 32768> pathBuffer{};
        const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
        if (length == 0 || length == pathBuffer.size())
        {
            throw std::runtime_error("Unable to resolve the executable directory.");
        }
        return std::filesystem::path(pathBuffer.data(), pathBuffer.data() + length).parent_path();
    }
}
