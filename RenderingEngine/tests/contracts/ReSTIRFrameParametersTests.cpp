#include "renderers/ReSTIRFrameParameters.hpp"

#include <iostream>
#include <limits>
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
                std::cerr << "ReSTIR frame-parameter test failed: "
                    << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept { return failures_ == 0; }

    private:
        int failures_ = 0;
    };

    [[nodiscard]] RenderingEngine::Renderers::ReSTIRFrameRequest MakeRequest()
    {
        using namespace RenderingEngine;
        Renderers::ReSTIRFrameRequest request{};
        request.config.scene = ScenePreset::ManyLightsRestirArena;
        request.config.backend = TraversalBackend::VulkanRayQuery;
        request.config.executionArchitecture = ExecutionArchitecture::Wavefront;
        request.config.directLightingEstimator =
            DirectLightingEstimator::RestirDirectIllumination;
        request.config.lightSelection =
            LightSelectionStrategy::PowerWeighted;
        request.config.reconstruction = ReconstructionMode::Svgf;
        request.config.render.width = 320u;
        request.config.render.height = 180u;
        request.config.restir.manyLightsTier = ManyLightsTier::Lights1000;
        request.providers = {
            true, true, true, true, true, true, true, true, true, true};
        request.identity = {
            (2ull << 32u) + 8u, 3u, 5u, 7u, 11u, 320u, 180u};
        request.settings.lightCount = 1000u;
        request.settings.initialCandidateCount = 8u;
        request.settings.spatialNeighborCount = 5u;
        request.settings.maximumReservoirM = 32u;
        request.settings.maximumHistoryAge = 20u;
        request.settings.framesInFlight = 3u;
        return request;
    }
}

bool RunReSTIRFrameParametersTests()
{
    using namespace RenderingEngine::Renderers;

    TestContext tests;
    ReSTIRFrameRequest request = MakeRequest();
    ReSTIRFramePlan plan = BuildReSTIRFramePlan(request);
    ReSTIRFrameParameterSources sources{};
    sources.currentLightCount = 1000u;
    sources.previousLightCount = 1000u;
    sources.historyGeneration = 19u;
    sources.cameraPosition = {1.0f, 2.0f, 3.0f, 0.0f};

    ReSTIRFrameParameterResult result =
        BuildReSTIRFrameParameters(request, plan, sources);
    tests.Expect(result.IsReady(), "validated request must build abi-v3 constants");
    tests.Expect(result.parameters.extentAndCandidates.z == 320u * 180u
            && result.parameters.extentAndCandidates.w == 8u,
        "extent and candidate count must match the immutable plan");
    tests.Expect(result.parameters.modeAndFlags.y == 2u
            && result.parameters.modeAndFlags.z == 0u
            && result.parameters.modeAndFlags.w == 2u,
        "candidate source, reset state, and frame high word must be explicit");
    tests.Expect(result.parameters.historyGenerations.x == 3u
            && result.parameters.historyGenerations.y == 7u
            && result.parameters.historyGenerations.w
                == static_cast<std::uint32_t>(
                    RenderingEngine::ShadowMethod::Physical),
        "camera/config, resolution/resource, and shadow identities must reach the shader ABI");

    request = MakeRequest();
    request.config.shadowMethod = RenderingEngine::ShadowMethod::Pcss;
    request.identity.shadowMethod = RenderingEngine::ShadowMethod::Pcss;
    plan = BuildReSTIRFramePlan(request);
    result = BuildReSTIRFrameParameters(request, plan, sources);
    tests.Expect(result.IsReady()
            && result.parameters.historyGenerations.w
                == static_cast<std::uint32_t>(
                    RenderingEngine::ShadowMethod::Pcss),
        "PCSS selection must be serialized into the versioned ReSTIR constants");

    request = MakeRequest();
    request.history.valid = true;
    request.history.published = request.identity;
    --request.history.published.frameIndex;
    request.history.physicalIndex = 3u;
    request.settings.previousLightCount = 1000u;
    plan = BuildReSTIRFramePlan(request);
    result = BuildReSTIRFrameParameters(request, plan, sources);
    tests.Expect(result.IsReady() && result.parameters.modeAndFlags.z == 1u,
        "only an exact previous identity may enable shader history reuse");

    sources.previousLightCount = 0u;
    result = BuildReSTIRFrameParameters(request, plan, sources);
    tests.Expect(!result.IsReady(),
        "history reuse without a previous light table must fail closed");

    sources.previousLightCount = 1000u;
    request = MakeRequest();
    plan = BuildReSTIRFramePlan(request);
    request.identity.sceneGeneration =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1u;
    plan.identity = request.identity;
    result = BuildReSTIRFrameParameters(request, plan, sources);
    tests.Expect(!result.IsReady(),
        "generation identities wider than the shader ABI must fail closed");

    if (tests.Passed())
    {
        std::cout << "ReSTIR abi-v3 frame-parameter checks passed.\n";
    }
    return tests.Passed();
}
