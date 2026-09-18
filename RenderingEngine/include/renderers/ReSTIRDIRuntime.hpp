#pragma once

#include "app/RuntimeConfig.hpp"
#include "rt/gpu/IGpuTraversalBackend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Renderers
{
    inline constexpr std::size_t kReSTIRSet5BindingCount = 31u;

    enum class ReSTIRRuntimeStatusCode : std::uint32_t
    {
        Ready = 0u,
        InvalidRequest,
        ProviderUnavailable,
        RecordingFailed
    };

    struct ReSTIRRuntimeStatus final
    {
        ReSTIRRuntimeStatusCode code = ReSTIRRuntimeStatusCode::Ready;
        std::string reason;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return code == ReSTIRRuntimeStatusCode::Ready;
        }
    };

    enum class ReSTIREstimatorMode : std::uint32_t
    {
        ExplicitlyBiased = 0u,
        ReferenceCorrection = 1u
    };

    enum class ReSTIRPass : std::uint32_t
    {
        ClearStatistics = 0u,
        GenerateCandidates,
        InitialReservoir,
        TemporalReuse,
        SpatialReuse,
        PrepareReferenceVisibility,
        ResolveReferenceVisibility,
        PrepareWinnerVisibility,
        ResolveWinnerVisibility,
        PublishSplitDirectSignal,
        Reconstruction,
        PublishHistory,
        WriteDebug
    };

    enum class ReSTIRBarrier : std::uint32_t
    {
        ComputeToCompute = 0u,
        ComputeToTraversal,
        TraversalToCompute,
        ComputeToReconstruction,
        ReconstructionToCompute
    };

    enum class ReSTIRHistoryDecision : std::uint32_t
    {
        Reset = 0u,
        Reuse
    };

    enum class PrimaryDirectLightingOwner : std::uint32_t
    {
        ConventionalNeeMis = 0u,
        ReSTIRDI
    };

    struct ReSTIRProviderAvailability final
    {
        bool abiV3 = false;
        bool primarySurfaceV2 = false;
        bool currentLightDistribution = false;
        bool currentPreviousLightMapping = false;
        bool restirComputePipelines = false;
        bool traceAny = false;
        bool reconstructionSignalV2 = false;
        bool reconstructionRecorder = false;
        bool historyStorage = false;
        bool debugStorage = false;
    };

    struct ReSTIRFrameIdentity final
    {
        std::uint64_t frameIndex = 0u;
        std::uint64_t configGeneration = 0u;
        std::uint64_t sceneGeneration = 0u;
        std::uint64_t resourceGeneration = 0u;
        std::uint64_t lightGeneration = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
    };

    struct ReSTIRHistoryIdentity final
    {
        bool valid = false;
        ReSTIRFrameIdentity published{};
        std::uint32_t physicalIndex = 0u;
    };

    struct ReSTIRRuntimeSettings final
    {
        std::uint32_t lightCount = 0u;
        // The previous table may differ after topology changes. It is required
        // only when an exact prior-frame reservoir is reused.
        std::uint32_t previousLightCount = 0u;
        std::uint32_t initialCandidateCount = 1u;
        std::uint32_t spatialNeighborCount = 0u;
        std::uint32_t maximumReservoirM = 32u;
        std::uint32_t maximumHistoryAge = 20u;
        std::uint32_t framesInFlight = 2u;
        ReSTIREstimatorMode estimatorMode = ReSTIREstimatorMode::ExplicitlyBiased;
        bool temporalReuse = true;
        bool spatialReuse = true;
        bool writeDebug = true;
    };

    struct ReSTIRResourceFootprint final
    {
        std::uint64_t pixelCount = 0u;
        std::uint64_t candidateBytes = 0u;
        std::uint64_t reservoirBytes = 0u;
        std::uint64_t historyBytes = 0u;
        std::uint64_t visibilityBytes = 0u;
        std::uint64_t lightMappingBytes = 0u;
        std::uint64_t neighborIndexBytes = 0u;
        std::uint64_t shadowRayBytes = 0u;
        std::uint64_t directSignalBytes = 0u;
        std::uint64_t debugBytes = 0u;
        std::uint64_t statisticsBytes = 0u;
        std::uint64_t parameterBytes = 0u;
        std::uint64_t totalBytes = 0u;
    };

    struct ReSTIRScheduledPass final
    {
        ReSTIRPass pass = ReSTIRPass::ClearStatistics;
        std::uint32_t dispatchGroupCountX = 0u;
        std::uint32_t dispatchGroupCountY = 1u;
        std::uint32_t dispatchGroupCountZ = 1u;
        bool recordsReferenceTraceAny = false;
        bool recordsWinnerTraceAny = false;
        bool recordsExternalReconstruction = false;
    };

    struct ReSTIRFramePlan final
    {
        ReSTIRRuntimeStatus status{};
        ReSTIRFrameIdentity identity{};
        ReSTIRHistoryDecision historyDecision = ReSTIRHistoryDecision::Reset;
        PrimaryDirectLightingOwner primaryDirectOwner =
            PrimaryDirectLightingOwner::ConventionalNeeMis;
        std::uint32_t historyReadPhysicalIndex = 0xffffffffu;
        std::uint32_t historyWritePhysicalIndex = 0xffffffffu;
        std::uint64_t maximumReferenceVisibilityRays = 0u;
        std::uint64_t maximumWinnerVisibilityRays = 0u;
        ShadowMethod shadowMethod = ShadowMethod::Physical;
        std::uint32_t shadowRaysPerVisibility = 1u;
        std::uint32_t currentLightCount = 0u;
        std::uint32_t previousLightCount = 0u;
        std::uint32_t initialCandidateCount = 0u;
        std::uint32_t spatialNeighborCount = 0u;
        std::uint32_t framesInFlight = 0u;
        ReSTIREstimatorMode estimatorMode = ReSTIREstimatorMode::ExplicitlyBiased;
        bool writeDebug = false;
        ReSTIRResourceFootprint footprint{};
        // Zero means that the production V3 shaders do not access this
        // binding for this plan. Non-zero entries are exact minimum byte
        // ranges for buffer descriptors; binding 16 is a storage image.
        std::array<std::uint64_t, kReSTIRSet5BindingCount>
            minimumSet5BufferRanges{};
        std::vector<ReSTIRScheduledPass> passes;

        [[nodiscard]] bool IsReady() const noexcept
        {
            return static_cast<bool>(status);
        }
    };

    struct ReSTIRFrameRequest final
    {
        RuntimeConfig config{};
        ReSTIRProviderAvailability providers{};
        ReSTIRFrameIdentity identity{};
        ReSTIRHistoryIdentity history{};
        ReSTIRRuntimeSettings settings{};
    };

    // The recorder owns Vulkan pipelines, descriptor sets, buffers and their
    // lifetimes. ReSTIRDIRuntime owns ordering and the exactly-once traversal
    // contract. A failed callback aborts the frame before history publication.
    class IReSTIRGpuRecorder
    {
    public:
        virtual ~IReSTIRGpuRecorder() = default;

        [[nodiscard]] virtual ReSTIRRuntimeStatus BeginFrame(
            const ReSTIRFramePlan& plan) = 0;
        [[nodiscard]] virtual ReSTIRRuntimeStatus RecordCompute(
            ReSTIRPass pass,
            std::uint32_t dispatchGroupCountX,
            std::uint32_t dispatchGroupCountY,
            std::uint32_t dispatchGroupCountZ) = 0;
        [[nodiscard]] virtual ReSTIRRuntimeStatus RecordBarrier(
            ReSTIRBarrier barrier) = 0;
        [[nodiscard]] virtual ReSTIRRuntimeStatus RecordReconstruction(
            const ReSTIRFramePlan& plan) = 0;
        [[nodiscard]] virtual Rt::Gpu::GpuTraceBatch BuildTraceAnyBatch(
            ReSTIRPass preparePass,
            std::uint32_t maximumRayCount) = 0;
        [[nodiscard]] virtual ReSTIRRuntimeStatus EndFrame(
            const ReSTIRFramePlan& plan) = 0;
        // Discards recorder-side state after a partial command sequence. The
        // owner must discard the command buffer; no submitted work is undone.
        virtual void AbortFrame() noexcept = 0;
    };

    [[nodiscard]] ReSTIRFramePlan BuildReSTIRFramePlan(
        const ReSTIRFrameRequest& request);
    [[nodiscard]] bool ValidateReSTIRFramePlan(
        const ReSTIRFramePlan& plan) noexcept;

    class ReSTIRDIRuntime final
    {
    public:
        [[nodiscard]] ReSTIRRuntimeStatus RecordFrame(
            const ReSTIRFramePlan& plan,
            IReSTIRGpuRecorder& recorder,
            Rt::Gpu::IGpuTraversalBackend& traversalBackend) const;
    };

    [[nodiscard]] std::string_view ToString(ReSTIRPass pass) noexcept;
    [[nodiscard]] std::string_view ToString(ReSTIRHistoryDecision decision) noexcept;
}
