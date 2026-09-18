#pragma once

#include "app/CapabilityTable.hpp"
#include "app/RuntimeConfig.hpp"
#include "ui/ActionMap.hpp"
#include "ui/DebugProfilerModel.hpp"
#include "ui/RuntimeConfigHarness.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Ui
{
    struct ModeTupleViewModel
    {
        std::string_view scene;
        std::string_view backend;
        std::string_view transportModel;
        std::string_view executionArchitecture;
        std::string_view directEstimator;
        std::string_view lightSelection;
        std::string_view environmentSampler;
        std::string_view reconstruction;
        std::string_view debugView;
        std::string_view shadowMethod;
    };

    struct CapabilityOptionViewModel
    {
        std::uint32_t value = 0;
        std::string_view token;
        std::string_view label;
        std::string_view owner;
        bool selected = false;
        bool built = false;
        bool enabled = false;
        CapabilityStatus status = CapabilityStatus::Unsupported;
        std::string_view reason;
    };

    struct CapabilityDimensionViewModel
    {
        std::string_view id;
        std::string_view label;
        std::vector<CapabilityOptionViewModel> options;
    };

    struct HelpEntryViewModel
    {
        const ActionBinding* binding = nullptr;
        bool enabled = false;
        std::string_view owner;
        std::string_view reason;
    };

    struct ShowcaseRuntimeResolution
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    // These are observed runtime values supplied by the composition/provider
    // layer. Resolution/seed/bounce may override their config-owned values when
    // an effective runtime observation exists. Frame/SPP/GPU time have no
    // RuntimeConfig equivalent and therefore remain visibly unavailable when
    // omitted.
    struct ShowcaseRuntimeStatus
    {
        std::string_view providerId;
        std::string_view reason;
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::optional<ShowcaseRuntimeResolution> resolution;
        std::optional<std::uint64_t> seed;
        std::optional<std::uint64_t> frameIndex;
        std::optional<std::uint32_t> progressiveFilmSpp;
        std::optional<std::uint32_t> currentFramePathsPerPixel;
        std::optional<std::uint32_t> referenceSpp;
        std::optional<std::uint32_t> temporalHistoryLength;
        std::optional<std::uint32_t> reservoirM;
        std::optional<std::uint32_t> reservoirAge;
        std::optional<std::uint64_t> reservoirCandidates;
        std::optional<std::uint64_t> visibilityRays;
        std::optional<std::uint64_t> totalTracedRays;
        std::optional<std::uint32_t> maximumBounce;
        std::optional<double> gpuFrameMilliseconds;
    };

    struct ShowcaseRuntimeGenerationTuple
    {
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
    };

    struct RuntimeStatusLineViewModel
    {
        std::string resolution;
        std::string seed;
        std::string frame;
        std::string progressiveFilmSpp;
        std::string currentFramePathsPerPixel;
        std::string referenceSpp;
        std::string temporalHistoryLength;
        std::string reservoirM;
        std::string reservoirAge;
        std::string reservoirCandidates;
        std::string visibilityRays;
        std::string totalTracedRays;
        std::string bounce;
        std::string gpuMilliseconds;
        std::string providerId;
        std::string reason;
        TelemetryProvenance provenance = TelemetryProvenance::LiveRuntime;
        TelemetryAvailability availability = TelemetryAvailability::Unavailable;
        std::uint64_t configGeneration = 0;
        std::uint64_t sceneGeneration = 0;
        std::uint64_t resourceGeneration = 0;
        std::string text;
    };

    struct CurrentTupleCapabilityViewModel
    {
        CapabilityStatus status = CapabilityStatus::Supported;
        std::string_view statusLabel;
        std::string_view reason;
        std::string text;
    };

    struct SceneRecommendationViewModel
    {
        bool registered = false;
        bool matches = false;
        std::string_view stableId;
        std::string text;
    };

    struct ShowcaseViewModel
    {
        ModeTupleViewModel tuple;
        std::string tupleText;
        RuntimeStatusLineViewModel runtimeStatus;
        CurrentTupleCapabilityViewModel currentTupleCapability;
        SceneRecommendationViewModel sceneRecommendation;
        std::vector<CapabilityDimensionViewModel> dimensions;
        std::vector<HelpEntryViewModel> help;
    };

    // Runtime composition publishes only the providers that are actually
    // connected.  Panel/help rendering is always lane-local; requests that
    // need platform, scene, renderer, or evidence producers stay disabled
    // until their provider is attached.
    struct ShowcaseFeatureAvailability
    {
        bool platformCommands = false;
        bool sceneCommands = false;
        bool historyResetConsumer = false;
        bool captureProvider = false;
        bool shaderReloadProvider = false;
        bool splitScreenProvider = false;
        bool benchmarkProvider = false;
        bool referenceProvider = false;
    };

    [[nodiscard]] ModeTupleViewModel BuildModeTupleViewModel(
        const RuntimeConfig& config) noexcept;
    [[nodiscard]] std::string FormatModeTuple(const ModeTupleViewModel& tuple);
    [[nodiscard]] ShowcaseViewModel BuildShowcaseViewModel(const RuntimeConfig& config);
    [[nodiscard]] ShowcaseViewModel BuildShowcaseViewModel(
        const RuntimeConfig& config,
        const ShowcaseFeatureAvailability& availability);
    [[nodiscard]] ShowcaseViewModel BuildShowcaseViewModel(
        const RuntimeConfig& config,
        const ShowcaseFeatureAvailability& availability,
        const ShowcaseRuntimeStatus& runtimeStatus);
    [[nodiscard]] ShowcaseViewModel BuildShowcaseViewModel(
        const RuntimeConfig& config,
        const ShowcaseFeatureAvailability& availability,
        const ShowcaseRuntimeStatus& runtimeStatus,
        const ShowcaseRuntimeGenerationTuple& expectedGenerations);
}
