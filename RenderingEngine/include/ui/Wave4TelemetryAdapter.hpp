#pragma once

#include "demos/ManyLightsWave4.hpp"
#include "ui/DebugProfilerModel.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace RenderingEngine::Ui
{
    enum class Wave4PublishCode : std::uint8_t
    {
        Accepted,
        RejectedInvalidSnapshot,
        RejectedGenerationRegression,
        RejectedSyntheticProvider
    };

    struct Wave4PublishResult final
    {
        Wave4PublishCode code = Wave4PublishCode::RejectedInvalidSnapshot;
        std::string reason;

        [[nodiscard]] bool Accepted() const noexcept
        {
            return code == Wave4PublishCode::Accepted;
        }
    };

    // L9 owns reservoir buffers, light tables, query pools and reference
    // images.  This adapter only retains the provider-owned immutable snapshot
    // and maps its explicit values into the existing UI telemetry model.
    class Wave4TelemetryAdapter final : public IDebugProfilerProvider
    {
    public:
        explicit Wave4TelemetryAdapter(
            std::string providerId = "l9.many-lights.runtime");

        [[nodiscard]] Wave4PublishResult Publish(
            Demos::ManyLightsWave4ProviderSnapshot snapshot);

        void Clear() noexcept;

        [[nodiscard]] std::string_view ProviderId() const noexcept;
        [[nodiscard]] const Demos::ManyLightsWave4ProviderSnapshot* LatestSnapshot() const noexcept;
        [[nodiscard]] DebugProfilerSnapshot ReadSnapshot() override;

    private:
        std::string providerId_;
        std::optional<Demos::ManyLightsWave4ProviderSnapshot> latestSnapshot_;
    };
}
