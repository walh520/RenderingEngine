#pragma once

#include "app/ArtifactLayout.hpp"
#include "app/RuntimeConfig.hpp"

namespace RenderingEngine
{
    void RunCpuReferenceRuntime(
        const RuntimeConfig& config,
        const ArtifactLayout& artifactLayout);
}
