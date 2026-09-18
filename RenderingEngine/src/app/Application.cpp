#include "app/Application.hpp"

#include "app/ArtifactLayout.hpp"
#include "app/CapabilityTable.hpp"
#include "app/CommandLine.hpp"
#include "app/CpuReferenceRuntime.hpp"
#include "app/ExitCode.hpp"
#include "app/IntegratedModuleRegistry.hpp"
#include "app/RuntimeConfig.hpp"
#include "app/RuntimeStatusText.hpp"
#include "renderers/VulkanWhittedRenderer.hpp"
#include "scene/CanonicalScene.hpp"
#include "scene/ExperimentScenes.hpp"

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
            if (commandLine.action == CommandLineAction::ShowIntegrationStatus)
            {
                std::cout << IntegrationStatusText();
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

            // Resolve the Wave 0 artifact contract without creating anything on disk.
            const ArtifactLayout artifactLayout = ResolveArtifactLayout(
                commandLine.config.run.artifactRoot,
                commandLine.config.run.runIdentifier);

            if (commandLine.config.executionArchitecture
                == ExecutionArchitecture::CpuReference)
            {
                RunCpuReferenceRuntime(commandLine.config, artifactLayout);
                return ToProcessExitCode(ExitCode::Success);
            }

            Scene::ExperimentSceneBuildOptions sceneBuildOptions;
            sceneBuildOptions.variantStableId = commandLine.config.sceneVariant;
            sceneBuildOptions.manyLightsCount = ResolveManyLightsCount(
                commandLine.config.restir.manyLightsTier);
            sceneBuildOptions.animateLights = commandLine.config.restir.animateLights;
            sceneBuildOptions.animateCamera = commandLine.config.restir.animateLights;
            sceneBuildOptions.animateRigidOccluders =
                commandLine.config.restir.animateRigidOccluders;
            const Scene::ExperimentScene experimentScene = Scene::BuildExperimentScene(
                static_cast<Scene::ExperimentScenePreset>(commandLine.config.scene),
                sceneBuildOptions);
            const Scene::CanonicalScene& canonicalScene = experimentScene.canonical;
            const Scene::CanonicalSceneValidation canonicalValidation =
                Scene::ValidateCanonicalScene(canonicalScene);
            if (!canonicalValidation)
            {
                throw std::runtime_error(
                    "Production canonical scene provider is invalid: "
                    + canonicalValidation.reason);
            }

            std::cout << "[启动] 单窗口 GLFW / Vulkan 演示器已就绪。\n"
                << "[当前配置] " << FormatRuntimeConfigStatus(commandLine.config) << '\n'
                << GlfwKeyHelpText() << "\n\n";

            if (platformHostFactory == nullptr)
            {
                throw std::invalid_argument("RunApplication requires a platform-host factory.");
            }

            PlatformCreateInfo platformCreateInfo;
            platformCreateInfo.clientExtent = {
                commandLine.config.render.width,
                commandLine.config.render.height
            };
            platformCreateInfo.title = "Vulkan RT Wave 5 Debug 实验展示场";
            platformCreateInfo.captureCursorOnStart = true;

            std::unique_ptr<IPlatformHost> platform = platformHostFactory(platformCreateInfo);
            if (platform == nullptr)
            {
                throw std::runtime_error("The platform-host factory returned null.");
            }

            VulkanWhittedRenderer renderer(std::move(platform));
            renderer.Run(commandLine.config, canonicalScene);
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
