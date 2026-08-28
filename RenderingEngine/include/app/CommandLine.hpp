#pragma once

#include "app/RuntimeConfig.hpp"

#include <stdexcept>
#include <string_view>

namespace RenderingEngine
{
    class CommandLineError final : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    enum class CommandLineAction
    {
        Run,
        ShowHelp,
        ShowVersion,
        ShowIntegrationStatus
    };

    struct CommandLineParseResult
    {
        RuntimeConfig config;
        CommandLineAction action = CommandLineAction::Run;
    };

    [[nodiscard]] CommandLineParseResult ParseCommandLine(int argumentCount, char** arguments);
    [[nodiscard]] std::string_view CommandLineHelpText() noexcept;
    [[nodiscard]] std::string_view RuntimeVersionText() noexcept;
}
