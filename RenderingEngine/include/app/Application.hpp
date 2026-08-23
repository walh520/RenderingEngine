#pragma once

#include "platform/IPlatformHost.hpp"

namespace RenderingEngine
{
    [[nodiscard]] int RunApplication(
        int argumentCount,
        char** arguments,
        PlatformHostFactory platformHostFactory) noexcept;
}
