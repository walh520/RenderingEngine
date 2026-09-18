#pragma once

#include "app/CapabilityTable.hpp"
#include "app/RuntimeConfig.hpp"
#include "ui/ActionMap.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Ui
{
    enum class ResetResource : std::uint8_t
    {
        None = 0,
        Accumulation = 1u << 0u,              // A
        TemporalHistory = 1u << 1u,           // T
        ReservoirHistory = 1u << 2u,          // Q
        ProfilerStatistics = 1u << 3u,         // P
        AccelerationStructures = 1u << 4u      // AS
    };

    using ResetMask = ResetResource;

    [[nodiscard]] constexpr ResetMask operator|(ResetMask left, ResetMask right) noexcept
    {
        return static_cast<ResetMask>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr ResetMask operator&(ResetMask left, ResetMask right) noexcept
    {
        return static_cast<ResetMask>(
            static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    constexpr ResetMask& operator|=(ResetMask& left, ResetMask right) noexcept
    {
        left = left | right;
        return left;
    }

    [[nodiscard]] constexpr bool HasReset(ResetMask mask, ResetResource resource) noexcept
    {
        return (mask & resource) != ResetResource::None;
    }

    enum class ResetCause : std::uint8_t
    {
        SceneChanged,
        BackendChanged,
        TransportExecutionOrSamplingChanged,
        ReconstructionChanged,
        ResolutionRenderScaleOrFovChanged,
        CameraDiscontinuity,
        ProgressiveCameraMotion,
        RealtimeCameraMotion,
        StableRigidBodyOrLightMotion,
        TopologyOrStableIdChanged,
        ShadingParameterChanged,
        SamplingParameterChanged,
        ShaderReloadSucceeded,
        DisplayOnly
    };

    // This is a reset request only.  The Wave-1 harness owns no renderer,
    // history image, profiler, reservoir, or acceleration structure.
    [[nodiscard]] constexpr ResetMask ResetMaskFor(ResetCause cause) noexcept
    {
        constexpr ResetMask atqp = ResetResource::Accumulation
            | ResetResource::TemporalHistory
            | ResetResource::ReservoirHistory
            | ResetResource::ProfilerStatistics;

        switch (cause)
        {
        case ResetCause::SceneChanged:
        case ResetCause::TopologyOrStableIdChanged:
            return atqp | ResetResource::AccelerationStructures;
        case ResetCause::BackendChanged:
            return atqp | ResetResource::AccelerationStructures;
        case ResetCause::TransportExecutionOrSamplingChanged:
        case ResetCause::ResolutionRenderScaleOrFovChanged:
        case ResetCause::CameraDiscontinuity:
        case ResetCause::ShadingParameterChanged:
        case ResetCause::SamplingParameterChanged:
        case ResetCause::ShaderReloadSucceeded:
            return atqp;
        case ResetCause::ReconstructionChanged:
            return ResetResource::TemporalHistory | ResetResource::ProfilerStatistics;
        case ResetCause::ProgressiveCameraMotion:
            return ResetResource::Accumulation;
        case ResetCause::RealtimeCameraMotion:
        case ResetCause::StableRigidBodyOrLightMotion:
        case ResetCause::DisplayOnly:
            return ResetResource::None;
        }
        return ResetResource::None;
    }

    enum class RoutedCommand : std::uint16_t
    {
        None,
        MoveForward,
        MoveBackward,
        MoveLeft,
        MoveRight,
        MoveDown,
        MoveUp,
        FastMovementModifier,
        FineMovementModifier,
        Look,
        AdjustMovementSpeed,
        TogglePointerCapture,
        ReleasePointerCapture,
        RequestExit,
        ResetShowcaseCamera,
        ToggleAnimationPause,
        StepAnimationFrame,
        ResetHistories,
        ToggleComparisonLock,
        ToggleHelp,
        ToggleAlgorithmPanel,
        ToggleProfilerPanel,
        RequestCapture,
        RequestShaderReload,
        ToggleSplitScreenComparison,
        ToggleDebugLegend,
        RequestBenchmark,
        RequestReferenceComparison,
        PrintCurrentReview
    };

    enum class ActionApplyStatus : std::uint8_t
    {
        ConfigCommitted,
        AcceptedNoConfigChange,
        RoutedToOwner,
        Rejected,
        IgnoredInputPhase
    };

    struct QueuedAction
    {
        std::uint64_t sequence = 0;
        ActionEvent event;
    };

    class ActionQueue final
    {
    public:
        std::uint64_t Push(ActionEvent event);
        std::uint64_t Push(
            SemanticAction action,
            ActionPhase phase = ActionPhase::Pressed,
            float valueX = 0.0f,
            float valueY = 0.0f);

        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;

    private:
        friend struct ActionQueueAccess;

        std::deque<QueuedAction> actions_;
        std::uint64_t nextSequence_ = 1;
    };

    struct ActionApplyResult
    {
        QueuedAction queued;
        ActionApplyStatus status = ActionApplyStatus::AcceptedNoConfigChange;
        ResetMask requestedResets = ResetResource::None;
        RoutedCommand routedCommand = RoutedCommand::None;
        CapabilityStatus capabilityStatus = CapabilityStatus::Supported;
        std::string_view reason;
        // Immutable tuple identity at this action's exact FIFO position. This
        // prevents an earlier capture/benchmark request from observing a later
        // config mutation in the same drained batch.
        RuntimeConfig effectiveRuntimeConfig;
    };

    struct ActionBatchResult
    {
        std::vector<ActionApplyResult> actions;
        ResetMask requestedResets = ResetResource::None;
    };

    // The only L10 API that mutates the supplied live RuntimeConfig.  It drains
    // the queue in FIFO order at the caller's frame-start fixed point.  Every
    // config mutation is first made on a candidate copy, evaluated as a whole,
    // and committed only on Supported.
    using SceneVariantCatalog = std::function<std::vector<std::string>(const RuntimeConfig&)>;

    [[nodiscard]] ActionBatchResult ApplyQueuedActions(
        ActionQueue& queue,
        RuntimeConfig& liveConfig,
        const SceneVariantCatalog& sceneVariants = {});
}
