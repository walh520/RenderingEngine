#pragma once

#include <cstdint>
#include <memory>

namespace RenderingEngine
{
    enum class Integrator : std::uint32_t
    {
        Pbr = 0,
        Whitted = 1
    };

    enum class ShadowMethod : std::uint32_t
    {
        Pcf = 0,
        Pcss = 1,
        Physical = 2
    };

    enum class DebugView : std::uint32_t
    {
        Final = 0,
        BaseColor = 1,
        Normal = 2,
        Roughness = 3,
        Metallic = 4,
        Emissive = 5
    };

    struct RunOptions
    {
        std::uint32_t frameLimit = 0; // Zero keeps the window open until the user exits.
        bool resizeTest = false;
        float exposure = 1.0f;
        std::uint32_t maximumTraceDepth = 8;
        Integrator integrator = Integrator::Whitted;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
        DebugView debugView = DebugView::Final;
    };

    class VulkanWhittedRenderer final
    {
    public:
        VulkanWhittedRenderer();
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
