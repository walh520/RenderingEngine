#pragma once

namespace RenderingEngine
{
    enum class ExitCode : int
    {
        Success = 0,
        InvalidCommandLine = 2,
        UnsupportedConfiguration = 4,
        RuntimeFailure = 10
    };

    [[nodiscard]] constexpr int ToProcessExitCode(ExitCode exitCode) noexcept
    {
        return static_cast<int>(exitCode);
    }
}
