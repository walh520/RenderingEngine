#include "restir/LightHistory.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
    class TestContext final
    {
    public:
        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                std::cerr << "L9 light-history test failed: " << message << '\n';
                ++failures_;
            }
        }
        [[nodiscard]] bool Passed() const noexcept { return failures_ == 0; }
    private:
        int failures_ = 0;
    };
}

bool RunLightHistoryTests()
{
    using namespace RenderingEngine::Restir;

    TestContext tests;
    constexpr std::array previous{
        ProductionLightIdentity{ 10u, 100u, 1u, 0u },
        ProductionLightIdentity{ 20u, 200u, 3u, 0u },
        ProductionLightIdentity{ 30u, 300u, 2u, 0u }
    };
    constexpr std::array current{
        ProductionLightIdentity{ 30u, 300u, 2u, 0u },
        ProductionLightIdentity{ 10u, 100u, 2u, 0u },
        ProductionLightIdentity{ 40u, 400u, 1u, 0u }
    };

    const LightHistoryMapping mapping =
        BuildLightHistoryMapping(previous, current, 7u, 8u);
    tests.Expect(mapping.valid, "valid unique light tables must map");
    tests.Expect(mapping.retained == 1u && mapping.generationChanged == 1u
            && mapping.added == 1u && mapping.deleted == 1u,
        "mapping must classify retained, changed, added, and deleted lights");
    tests.Expect(mapping.currentToPrevious[0] == 2u
            && mapping.currentToPrevious[1] == 0u
            && mapping.currentToPrevious[2] == kInvalidLightIndex,
        "current-to-previous indices must follow stable identity, not array order");
    tests.Expect(mapping.currentToPreviousGpu[1].index == 0u
            && mapping.currentToPreviousGpu[1].generation == 2u
            && mapping.previousToCurrentGpu[2].index == 0u
            && mapping.previousToCurrentGpu[2].generation == 2u,
        "GPU mapping records must carry current per-light generation");
    tests.Expect(IsHistoryLightReusable(mapping, previous, current, 2u),
        "retained identity and generation must remain reusable");
    tests.Expect(!IsHistoryLightReusable(mapping, previous, current, 0u),
        "generation-changed light must invalidate its reservoir");
    tests.Expect(!IsHistoryLightReusable(mapping, previous, current, 1u),
        "deleted light must invalidate its reservoir");

    constexpr std::array duplicate{
        ProductionLightIdentity{ 1u, 9u, 1u, 0u },
        ProductionLightIdentity{ 1u, 9u, 2u, 0u }
    };
    const LightHistoryMapping rejected =
        BuildLightHistoryMapping(previous, duplicate, 1u, 2u);
    tests.Expect(!rejected.valid && !rejected.reason.empty(),
        "duplicate stable identity must fail closed");

    if (tests.Passed())
    {
        std::cout << "L9 light generation and current/previous mapping checks passed.\n";
    }
    return tests.Passed();
}
