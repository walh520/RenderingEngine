#include "app/Application.hpp"

#include "app/ArtifactLayout.hpp"
#include "app/CapabilityTable.hpp"
#include "app/CommandLine.hpp"
#include "app/ExitCode.hpp"
#include "app/RuntimeConfig.hpp"
#include "renderers/VulkanWhittedRenderer.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace RenderingEngine
{
    int RunApplication(
        int argumentCount,
        char** arguments,
        PlatformHostFactory platformHostFactory) noexcept
    {
        try
        {
            const CommandLineParseResult commandLine = ParseCommandLine(argumentCount, arguments);
            if (commandLine.action == CommandLineAction::ShowHelp)
            {
                std::cout << CommandLineHelpText();
                return ToProcessExitCode(ExitCode::Success);
            }
            if (commandLine.action == CommandLineAction::ShowVersion)
            {
                std::cout << RuntimeVersionText() << '\n';
                return ToProcessExitCode(ExitCode::Success);
            }

            const CapabilityDecision capability = CapabilityTable::Evaluate(commandLine.config);
            if (capability.status == CapabilityStatus::InvalidConfiguration)
            {
                std::cerr << "Invalid configuration: " << capability.reason << '\n';
                return ToProcessExitCode(ExitCode::InvalidCommandLine);
            }
            if (!capability.IsSupported())
            {
                std::cerr << "Unsupported configuration: " << capability.reason << '\n';
                return ToProcessExitCode(ExitCode::UnsupportedConfiguration);
            }

            if (platformHostFactory == nullptr)
            {
                throw std::invalid_argument("RunApplication requires a platform-host factory.");
            }

            // Resolve the Wave 0 artifact contract without creating anything on disk.
            const ArtifactLayout artifactLayout = ResolveArtifactLayout(
                commandLine.config.run.artifactRoot,
                commandLine.config.run.runIdentifier);
            (void)artifactLayout;

            PlatformCreateInfo platformCreateInfo;
            platformCreateInfo.clientExtent = {
                commandLine.config.render.width,
                commandLine.config.render.height
            };
            platformCreateInfo.title = "Vulkan HLSL Rendering Engine";
            platformCreateInfo.captureCursorOnStart = true;

            std::unique_ptr<IPlatformHost> platform = platformHostFactory(platformCreateInfo);
            if (platform == nullptr)
            {
                throw std::runtime_error("The platform-host factory returned null.");
            }

            VulkanWhittedRenderer renderer(std::move(platform));
            renderer.Run(MakeLegacyRunOptions(commandLine.config));
            return ToProcessExitCode(ExitCode::Success);
        }
        catch (const CommandLineError& error)
        {
            std::cerr << "Invalid command line: " << error.what() << '\n'
                << "Use --help to list valid options.\n";
            return ToProcessExitCode(ExitCode::InvalidCommandLine);
        }
        catch (const std::exception& error)
        {
            std::cerr << "Runtime failure: " << error.what() << '\n';
            return ToProcessExitCode(ExitCode::RuntimeFailure);
        }
        catch (...)
        {
            std::cerr << "Runtime failure: unknown exception.\n";
            return ToProcessExitCode(ExitCode::RuntimeFailure);
        }
    }
}
