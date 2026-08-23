#include "renderers/VulkanWhittedRenderer.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    [[nodiscard]] RenderingEngine::RunOptions ParseOptions(int argumentCount, char** arguments)
    {
        RenderingEngine::RunOptions options;
        for (int index = 1; index < argumentCount; ++index)
        {
            const std::string argument = arguments[index];
            if (argument == "--help")
            {
                std::cout
                    << "Vulkan HLSL Whitted Renderer\n"
                    << "  --frames N   Render N frames and exit (useful for validation).\n"
                    << "  --integrator M  Select whitted (default) or pbr.\n"
                    << "  --max-depth N  Set recursion/path depth from 1 to 12 (default 8).\n"
                    << "  --exposure X   Set linear exposure from 0.01 to 64 (default 1).\n"
                    << "  --shadow M   Select physical, pcf, or pcss shadow integration.\n"
                    << "  --debug-view V  Select final, base-color, normal, roughness, metallic, or emissive.\n"
                    << "  --resize-test Exercise swapchain recreation during a finite run.\n"
                    << "Controls: WASD move, Space/Ctrl vertical, Shift sprint, mouse look, "
                       "wheel zoom, 1 PCF, 2 PCSS, 3 physical, Tab release mouse, Esc exit.\n";
                std::exit(0);
            }

            if (argument == "--integrator")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::invalid_argument("--integrator requires pbr or whitted.");
                }

                const std::string integrator = arguments[++index];
                if (integrator == "pbr")
                {
                    options.integrator = RenderingEngine::Integrator::Pbr;
                }
                else if (integrator == "whitted")
                {
                    options.integrator = RenderingEngine::Integrator::Whitted;
                }
                else
                {
                    throw std::invalid_argument(
                        "Unknown integrator: " + integrator + " (expected pbr or whitted).");
                }
                continue;
            }

            if (argument == "--frames")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::invalid_argument("--frames requires a positive integer.");
                }
                const unsigned long value = std::stoul(arguments[++index]);
                if (value == 0 || value > std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::out_of_range("--frames is outside the uint32 range.");
                }
                options.frameLimit = static_cast<std::uint32_t>(value);
                continue;
            }

            if (argument == "--resize-test")
            {
                options.resizeTest = true;
                continue;
            }

            if (argument == "--max-depth")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::invalid_argument("--max-depth requires an integer from 1 to 12.");
                }
                const unsigned long value = std::stoul(arguments[++index]);
                if (value < 1 || value > 12)
                {
                    throw std::out_of_range("--max-depth must be from 1 to 12.");
                }
                options.maximumTraceDepth = static_cast<std::uint32_t>(value);
                continue;
            }

            if (argument == "--exposure")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::invalid_argument("--exposure requires a value from 0.01 to 64.");
                }
                const float value = std::stof(arguments[++index]);
                if (!(value >= 0.01f && value <= 64.0f))
                {
                    throw std::out_of_range("--exposure must be from 0.01 to 64.");
                }
                options.exposure = value;
                continue;
            }

            if (argument == "--shadow")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::invalid_argument("--shadow requires physical, pcf, or pcss.");
                }

                const std::string method = arguments[++index];
                if (method == "pcf")
                {
                    options.shadowMethod = RenderingEngine::ShadowMethod::Pcf;
                }
                else if (method == "pcss")
                {
                    options.shadowMethod = RenderingEngine::ShadowMethod::Pcss;
                }
                else if (method == "physical")
                {
                    options.shadowMethod = RenderingEngine::ShadowMethod::Physical;
                }
                else
                {
                    throw std::invalid_argument(
                        "Unknown shadow method: " + method + " (expected physical, pcf, or pcss).");
                }
                continue;
            }

            if (argument == "--debug-view")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::invalid_argument("--debug-view requires a view name.");
                }

                const std::string view = arguments[++index];
                if (view == "final")
                {
                    options.debugView = RenderingEngine::DebugView::Final;
                }
                else if (view == "base-color")
                {
                    options.debugView = RenderingEngine::DebugView::BaseColor;
                }
                else if (view == "normal")
                {
                    options.debugView = RenderingEngine::DebugView::Normal;
                }
                else if (view == "roughness")
                {
                    options.debugView = RenderingEngine::DebugView::Roughness;
                }
                else if (view == "metallic")
                {
                    options.debugView = RenderingEngine::DebugView::Metallic;
                }
                else if (view == "emissive")
                {
                    options.debugView = RenderingEngine::DebugView::Emissive;
                }
                else
                {
                    throw std::invalid_argument("Unknown debug view: " + view + '.');
                }
                continue;
            }

            throw std::invalid_argument("Unknown command-line argument: " + argument);
        }
        return options;
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        RenderingEngine::VulkanWhittedRenderer renderer;
        renderer.Run(ParseOptions(argumentCount, arguments));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
