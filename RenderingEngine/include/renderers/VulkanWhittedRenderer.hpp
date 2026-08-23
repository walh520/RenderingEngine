#pragma once

#include "app/RuntimeConfig.hpp"

#include <cstdint>
#include <memory>

namespace RenderingEngine
{
    class IPlatformHost;

    class VulkanWhittedRenderer final
    {
    public:
        explicit VulkanWhittedRenderer(std::unique_ptr<IPlatformHost> platform);
        ~VulkanWhittedRenderer();

        VulkanWhittedRenderer(const VulkanWhittedRenderer&) = delete;
        VulkanWhittedRenderer& operator=(const VulkanWhittedRenderer&) = delete;

        VulkanWhittedRenderer(VulkanWhittedRenderer&&) noexcept;
        VulkanWhittedRenderer& operator=(VulkanWhittedRenderer&&) noexcept;

        void Run(const RunOptions& options = {});

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
