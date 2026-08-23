#include "app/RuntimeConfig.hpp"

namespace RenderingEngine
{
    RunOptions MakeLegacyRunOptions(const RuntimeConfig& config) noexcept
    {
        RunOptions options;
        options.initialWidth = config.render.width;
        options.initialHeight = config.render.height;
        options.frameLimit = config.run.frameLimit;
        options.resizeTest = config.run.resizeTest;
        options.exposure = config.render.exposure;
        options.maximumTraceDepth = config.render.maximumBounce;
        options.targetSamplesPerPixel = config.render.targetSamplesPerPixel;
        options.baseSeed = config.render.baseSeed;
        options.verticalFovDegrees = config.render.verticalFovDegrees;
        options.vsync = config.render.vsync;
        options.validation = config.run.validation;
        options.integrator = config.integrator;
        options.shadowMethod = config.shadowMethod;
        options.debugView = config.debugView;
        return options;
    }

    RuntimeConfig MakeHeadlessMockRuntimeConfig()
    {
        RuntimeConfig config;
        config.render.width = 64;
        config.render.height = 64;
        config.run.frameLimit = 1;
        config.run.headless = true;
        config.run.validation = RuntimeToggle::Disabled;
        config.run.runIdentifier = "headless-mock";
        return config;
    }
}
