#pragma once

#include "app/RuntimeConfig.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Wave3
{
    enum class PlanStatus : std::uint32_t
    {
        Ready = 0u,
        InvalidRequest,
        ProviderUnavailable
    };

    enum class HistoryDecision : std::uint32_t
    {
        Unused = 0u,
        Reset,
        Reuse
    };

    enum class PassDomain : std::uint32_t
    {
        Transfer = 0u,
        AccelerationStructureBuild,
        Compute,
        RayTracing,
        IndirectCompute,
        Present
    };

    enum class PassKind : std::uint32_t
    {
        FrameReset = 0u,
        SoftwareSahUpload,
        LbvhMorton,
        LbvhRadixSort,
        LbvhHierarchy,
        LbvhBounds,
        HardwareBlasBuild,
        HardwareTlasBuild,
        RtPipelineSbtPrepare,
        BackendTraceProbe,
        MegakernelIntegrate,
        WavefrontReset,
        WavefrontRayGen,
        WavefrontIntersect,
        WavefrontShade,
        WavefrontTraceShadow,
        WavefrontNextBounce,
        WavefrontResolve,
        PrimarySurfaceExport,
        MotionVectors,
        PrepareSignal,
        TemporalAccumulation,
        VarianceBootstrap,
        AtrousIteration,
        ComposeRaw,
        ComposeTemporal,
        ComposeAtrous,
        ComposeSvgf,
        PublishHistory,
        Present
    };

    struct ProviderAvailability final
    {
        bool flattenedSah = true;
        bool gpuLbvh = false;
        bool rayQuery = true;
        bool rtPipeline = false;
        bool megakernel = true;
        bool wavefront = false;
        bool temporal = false;
        bool svgf = false;
    };

    struct HistoryIdentity final
    {
        bool valid = false;
        std::uint64_t publishedFrame = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        TraversalBackend backend = TraversalBackend::GpuFlattenedSahBvh;
        TransportModel transportModel = TransportModel::Pbr;
        ExecutionArchitecture executionArchitecture =
            ExecutionArchitecture::Megakernel;
        ReconstructionMode reconstruction = ReconstructionMode::ProgressiveMean;
    };

    struct FrameRequest final
    {
        RuntimeConfig config{};
        ProviderAvailability providers{};
        HistoryIdentity history{};
        std::uint64_t frameIndex = 0u;
        std::uint64_t configGeneration = 1u;
        std::uint64_t sceneGeneration = 1u;
        std::uint64_t resourceGeneration = 1u;
        std::uint32_t framesInFlight = 2u;
        std::uint32_t atrousIterations = 5u;
        bool includeProfiler = true;
        bool includeComparisonBaseline = false;
    };

    struct ScheduledPass final
    {
        PassKind kind = PassKind::FrameReset;
        PassDomain domain = PassDomain::Compute;
        std::string_view lane{};
        std::uint32_t bounce = 0u;
        std::uint32_t iteration = 0u;
        std::uint32_t beginTimestampQuery = 0xffffffffu;
        std::uint32_t endTimestampQuery = 0xffffffffu;
        bool consumesPreviousPass = true;
        bool writesIndirectArguments = false;
        bool readsIndirectArguments = false;
    };

    struct FramePlan final
    {
        PlanStatus status = PlanStatus::InvalidRequest;
        HistoryDecision historyDecision = HistoryDecision::Unused;
        std::uint32_t historyReadPhysicalIndex = 0xffffffffu;
        std::uint32_t historyWritePhysicalIndex = 0xffffffffu;
        std::uint32_t timestampQueryCount = 0u;
        std::vector<ScheduledPass> passes{};
        std::string reason{};

        [[nodiscard]] bool IsReady() const noexcept
        {
            return status == PlanStatus::Ready;
        }
    };

    [[nodiscard]] FramePlan BuildFramePlan(const FrameRequest& request);
    [[nodiscard]] bool ValidateFramePlan(const FramePlan& plan) noexcept;
    [[nodiscard]] bool HasTimestampQueries(const ScheduledPass& pass) noexcept;
    [[nodiscard]] std::string_view ToString(PassKind kind) noexcept;
    [[nodiscard]] std::string_view ToString(HistoryDecision decision) noexcept;
}
