#pragma once

#include "platform/IPlatformHost.hpp"

namespace RenderingEngine
{
    [[nodiscard]] std::unique_ptr<IPlatformHost> CreateWin32PlatformHost(
        const PlatformCreateInfo& createInfo);
}
