#include "renderers/ReSTIRDIRuntime.hpp"

#include "contracts/AbiV3.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace RenderingEngine::Renderers
{
    namespace
    {
        inline constexpr std::uint64_t kCandidateStride =
            sizeof(Contracts::AbiV3::GpuRestirCandidateV3);
        inline constexpr std::uint64_t kPersistentSampleStride =
            sizeof(Contracts::AbiV3::GpuPersistentLightSampleV3);
        inline constexpr std::uint64_t kReservoirStride =
            sizeof(Contracts::AbiV3::GpuRestirReservoirV3);
        inline constexpr std::uint64_t kDebugStride =
            sizeof(Contracts::AbiV3::GpuRestirDebugV3);
        inline constexpr std::uint64_t kStatisticsBytes =
            sizeof(Contracts::AbiV3::GpuRestirStatisticsV3);
        inline constexpr std::uint64_t kParameterBytes =
            sizeof(Contracts::AbiV3::GpuRestirParametersV3);
        inline constexpr std::uint64_t kDirectSignalStride = 16u;
        inline constexpr std::uint64_t kDebugImageStride = 16u;
        inline constexpr std::uint64_t kLightMapEntryStride = 2u * sizeof(std::uint32_t);
        inline constexpr std::uint64_t kShadowRayStride =
            sizeof(Contracts::AbiV1::GpuRayQueueRecordV1);
        inline constexpr std::uint64_t kVisibilityResultStride =
            sizeof(Contracts::AbiV1::GpuHitQueueRecordV1);
        inline constexpr std::uint64_t kPrimarySurfaceStride =
            sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2);

        [[nodiscard]] bool CheckedMultiply(
            const std::uint64_t left,
            const std::uint64_t right,
            std::uint64_t& result) noexcept
        {
            if (left != 0u && right > std::numeric_limits<std::uint64_t>::max() / left)
            {
                return false;
            }
            result = left * right;
            return true;
        }

        [[nodiscard]] bool CheckedAdd(
            const std::uint64_t value,
            std::uint64_t& total) noexcept
        {
            if (value > std::numeric_limits<std::uint64_t>::max() - total)
            {
                return false;
            }
            total += value;
            return true;
        }

        [[nodiscard]] constexpr std::uint32_t ShadowRaysPerVisibility(
            const ShadowMethod method) noexcept
        {
            using namespace Contracts::AbiV3;
            switch (method)
            {
            case ShadowMethod::Pcf: return kRestirPcfFilterRayCount;
            case ShadowMethod::Pcss: return kRestirPcssVisibilityRayCount;
            case ShadowMethod::Physical:
                return kRestirPhysicalVisibilityRayCount;
            default: return 0u;
            }
        }

        void Reject(
            ReSTIRFramePlan& plan,
            const ReSTIRRuntimeStatusCode code,
            std::string reason)
        {
            plan.status.code = code;
            plan.status.reason = std::move(reason);
            plan.passes.clear();
        }

        [[nodiscard]] bool SameHistoryIdentity(
            const ReSTIRFrameRequest& request) noexcept
        {
            const ReSTIRFrameIdentity& current = request.identity;
            const ReSTIRHistoryIdentity& history = request.history;
            const ReSTIRFrameIdentity& prior = history.published;
            return history.valid
                && current.frameIndex > 0u
                && prior.frameIndex + 1u == current.frameIndex
                && prior.configGeneration == current.configGeneration
                && prior.sceneGeneration == current.sceneGeneration
                && prior.resourceGeneration == current.resourceGeneration
                && prior.lightGeneration == current.lightGeneration
                && prior.width == current.width
                && prior.height == current.height
                && prior.shadowMethod == current.shadowMethod
                && history.physicalIndex
                    < 2u * request.settings.framesInFlight;
        }

        [[nodiscard]] bool RuntimeSettingsMatchConfig(
            const ReSTIRFrameRequest& request) noexcept
        {
            const bool temporalExpected = UsesRestirTemporalReuse(request.config.restir.reuseStage);
            const bool spatialExpected = UsesRestirSpatialReuse(request.config.restir.reuseStage);
            const std::uint32_t neighborCount = spatialExpected
                ? request.config.restir.spatialNeighbors : 0u;
            const ReSTIREstimatorMode estimatorMode =
                request.config.restir.biasMode == RestirBiasMode::ReferenceCorrection
                    ? ReSTIREstimatorMode::ReferenceCorrection
                    : ReSTIREstimatorMode::ExplicitlyBiased;
            return request.settings.lightCount > 0u
                && request.settings.initialCandidateCount
                    == request.config.restir.initialCandidatesPerPixel
                && request.settings.spatialNeighborCount == neighborCount
                && request.settings.maximumReservoirM
                    == request.config.restir.maximumReservoirM
                && request.settings.maximumHistoryAge
                    == request.config.restir.maximumHistoryAge
                && request.settings.estimatorMode == estimatorMode
                && request.settings.temporalReuse == temporalExpected
                && request.settings.spatialReuse == spatialExpected;
        }

        [[nodiscard]] ReSTIRResourceFootprint BuildFootprint(
            const ReSTIRFrameIdentity& identity,
            const ReSTIRRuntimeSettings& settings,
            const std::uint32_t previousLightCount,
            bool& valid) noexcept
        {
            ReSTIRResourceFootprint footprint{};
            valid = CheckedMultiply(identity.width, identity.height, footprint.pixelCount);
            const std::uint64_t raysPerVisibility =
                ShadowRaysPerVisibility(identity.shadowMethod);
            std::uint64_t visibilityMultiplicity =
                settings.estimatorMode == ReSTIREstimatorMode::ReferenceCorrection
                    ? static_cast<std::uint64_t>(settings.spatialNeighborCount) + 2u
                    : 1u;
            std::uint64_t shadowRayMultiplicity =
                settings.estimatorMode == ReSTIREstimatorMode::ReferenceCorrection
                    ? static_cast<std::uint64_t>(settings.spatialNeighborCount) + 1u
                    : 1u;
            valid = valid && raysPerVisibility != 0u
                && CheckedMultiply(
                    footprint.pixelCount,
                    static_cast<std::uint64_t>(settings.initialCandidateCount),
                    footprint.candidateBytes)
                && CheckedMultiply(footprint.candidateBytes, kCandidateStride,
                    footprint.candidateBytes)
                && CheckedMultiply(footprint.pixelCount, kReservoirStride,
                    footprint.reservoirBytes)
                && CheckedMultiply(footprint.reservoirBytes, 3u,
                    footprint.reservoirBytes)
                && CheckedMultiply(footprint.pixelCount, kReservoirStride,
                    footprint.historyBytes)
                && CheckedMultiply(footprint.historyBytes,
                    2u * settings.framesInFlight,
                    footprint.historyBytes)
                && CheckedMultiply(visibilityMultiplicity, raysPerVisibility,
                    visibilityMultiplicity)
                && CheckedMultiply(footprint.pixelCount, visibilityMultiplicity,
                    footprint.visibilityBytes)
                && CheckedMultiply(footprint.visibilityBytes, kVisibilityResultStride,
                    footprint.visibilityBytes)
                && CheckedMultiply(
                    static_cast<std::uint64_t>(settings.lightCount)
                        + previousLightCount,
                    kLightMapEntryStride,
                    footprint.lightMappingBytes)
                && CheckedMultiply(
                    footprint.pixelCount,
                    static_cast<std::uint64_t>(settings.spatialNeighborCount),
                    footprint.neighborIndexBytes)
                && CheckedMultiply(footprint.neighborIndexBytes, sizeof(std::uint32_t),
                    footprint.neighborIndexBytes)
                && CheckedMultiply(shadowRayMultiplicity, raysPerVisibility,
                    shadowRayMultiplicity)
                && CheckedMultiply(footprint.pixelCount, shadowRayMultiplicity,
                    footprint.shadowRayBytes)
                && CheckedMultiply(footprint.shadowRayBytes, kShadowRayStride,
                    footprint.shadowRayBytes)
                && CheckedMultiply(footprint.pixelCount, 2u * kDirectSignalStride,
                    footprint.directSignalBytes)
                && CheckedMultiply(
                    footprint.pixelCount,
                    settings.writeDebug ? kDebugStride + kDebugImageStride : 0u,
                    footprint.debugBytes);
            footprint.statisticsBytes = kStatisticsBytes;
            footprint.parameterBytes = kParameterBytes;
            if (!valid)
            {
                return {};
            }
            valid = CheckedAdd(footprint.candidateBytes, footprint.totalBytes)
                && CheckedAdd(footprint.reservoirBytes, footprint.totalBytes)
                && CheckedAdd(footprint.historyBytes, footprint.totalBytes)
                && CheckedAdd(footprint.visibilityBytes, footprint.totalBytes)
                && CheckedAdd(footprint.lightMappingBytes, footprint.totalBytes)
                && CheckedAdd(footprint.neighborIndexBytes, footprint.totalBytes)
                && CheckedAdd(footprint.shadowRayBytes, footprint.totalBytes)
                && CheckedAdd(footprint.directSignalBytes, footprint.totalBytes)
                && CheckedAdd(footprint.debugBytes, footprint.totalBytes)
                && CheckedAdd(footprint.statisticsBytes, footprint.totalBytes)
                && CheckedAdd(footprint.parameterBytes, footprint.totalBytes);
            return valid ? footprint : ReSTIRResourceFootprint{};
        }

        [[nodiscard]] bool BuildMinimumSet5Ranges(
            ReSTIRFramePlan& plan) noexcept
        {
            using namespace Contracts::AbiV3;
            auto& ranges = plan.minimumSet5BufferRanges;
            ranges.fill(0u);

            std::uint64_t pixelReservoirBytes = 0u;
            std::uint64_t pixelSurfaceBytes = 0u;
            std::uint64_t pixelPersistentSampleBytes = 0u;
            std::uint64_t pixelDebugBytes = 0u;
            std::uint64_t pixelUIntBytes = 0u;
            std::uint64_t winnerVisibilityBytes = 0u;
            std::uint64_t referenceVisibilityBytes = 0u;
            std::uint64_t currentLightMapBytes = 0u;
            std::uint64_t previousLightMapBytes = 0u;
            std::uint64_t directAovBytes = 0u;
            if (!CheckedMultiply(plan.footprint.pixelCount, kReservoirStride,
                    pixelReservoirBytes)
                || !CheckedMultiply(plan.footprint.pixelCount, kPrimarySurfaceStride,
                    pixelSurfaceBytes)
                || !CheckedMultiply(plan.footprint.pixelCount, kPersistentSampleStride,
                    pixelPersistentSampleBytes)
                || !CheckedMultiply(plan.footprint.pixelCount, kDebugStride,
                    pixelDebugBytes)
                || !CheckedMultiply(plan.footprint.pixelCount,
                    sizeof(std::uint32_t), pixelUIntBytes)
                || !CheckedMultiply(plan.maximumWinnerVisibilityRays,
                    kVisibilityResultStride, winnerVisibilityBytes)
                || !CheckedMultiply(plan.maximumReferenceVisibilityRays,
                    kVisibilityResultStride, referenceVisibilityBytes)
                || !CheckedMultiply(plan.currentLightCount, kLightMapEntryStride,
                    currentLightMapBytes)
                || !CheckedMultiply(plan.previousLightCount, kLightMapEntryStride,
                    previousLightMapBytes)
                || !CheckedMultiply(plan.footprint.pixelCount, kDirectSignalStride,
                    directAovBytes))
            {
                return false;
            }

            ranges[RestirBinding::Parameters] = kParameterBytes;
            ranges[RestirBinding::Candidates] = plan.footprint.candidateBytes;
            ranges[RestirBinding::DebugRecord] = plan.writeDebug
                ? pixelDebugBytes : 0u;
            ranges[RestirBinding::Statistics] = kStatisticsBytes;
            ranges[RestirBinding::VisibilityResults] = winnerVisibilityBytes;
            ranges[RestirBinding::ValidationReasons] = pixelUIntBytes;
            ranges[RestirBinding::CurrentToPreviousLightIndex] = currentLightMapBytes;
            ranges[RestirBinding::NeighborIndices] = plan.footprint.neighborIndexBytes;
            ranges[RestirBinding::ShadowRayQueue] = plan.footprint.shadowRayBytes;
            ranges[RestirBinding::DirectDiffuse] = directAovBytes;
            ranges[RestirBinding::DirectSpecular] = directAovBytes;
            ranges[RestirBinding::InitialReservoir] = pixelReservoirBytes;
            ranges[RestirBinding::TemporalReservoir] = pixelReservoirBytes;
            ranges[RestirBinding::SpatialReservoir] = pixelReservoirBytes;
            ranges[RestirBinding::PublishedReservoir] = pixelReservoirBytes;

            if (plan.historyDecision == ReSTIRHistoryDecision::Reuse)
            {
                ranges[RestirBinding::SurfaceHistory] = pixelSurfaceBytes;
                ranges[RestirBinding::HistoryAtCurrent] = pixelPersistentSampleBytes;
                ranges[RestirBinding::PreviousToCurrentLightIndex] =
                    previousLightMapBytes;
                ranges[RestirBinding::PreviousPublishedReservoir] =
                    pixelReservoirBytes;
            }
            if (plan.maximumReferenceVisibilityRays != 0u)
            {
                ranges[RestirBinding::ReferenceVisibility] =
                    referenceVisibilityBytes;
            }
            return true;
        }

        [[nodiscard]] ReSTIRBarrier BarrierAfter(const ReSTIRPass pass) noexcept
        {
            switch (pass)
            {
            case ReSTIRPass::PrepareReferenceVisibility:
            case ReSTIRPass::PrepareWinnerVisibility:
                return ReSTIRBarrier::ComputeToTraversal;
            case ReSTIRPass::ResolveReferenceVisibility:
            case ReSTIRPass::ResolveWinnerVisibility:
                return ReSTIRBarrier::ComputeToCompute;
            case ReSTIRPass::PublishSplitDirectSignal:
                return ReSTIRBarrier::ComputeToReconstruction;
            default:
                return ReSTIRBarrier::ComputeToCompute;
            }
        }
    }

    ReSTIRFramePlan BuildReSTIRFramePlan(const ReSTIRFrameRequest& request)
    {
        ReSTIRFramePlan plan{};
        plan.status.code = ReSTIRRuntimeStatusCode::Ready;
        plan.identity = request.identity;
        plan.currentLightCount = request.settings.lightCount;
        plan.previousLightCount = request.settings.previousLightCount;
        plan.initialCandidateCount = request.settings.initialCandidateCount;
        plan.spatialNeighborCount = request.settings.spatialNeighborCount;
        plan.framesInFlight = request.settings.framesInFlight;
        plan.estimatorMode = request.settings.estimatorMode;
        plan.writeDebug = request.settings.writeDebug;
        plan.shadowMethod = request.config.shadowMethod;
        plan.shadowRaysPerVisibility =
            ShadowRaysPerVisibility(request.config.shadowMethod);

        if (request.config.version != kRuntimeConfigVersion
            || (request.config.backend != TraversalBackend::CanonicalLinearGpu
                && request.config.backend != TraversalBackend::GpuFlattenedSahBvh
                && request.config.backend != TraversalBackend::VulkanRayQuery))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR DI requires runtime-config-v2 and a built GPU traversal backend");
            return plan;
        }
        if (request.config.directLightingEstimator
            != DirectLightingEstimator::RestirDirectIllumination)
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIRDIRuntime requires the ReSTIR DI estimator");
            return plan;
        }
        if ((request.config.lightSelection != LightSelectionStrategy::Uniform
                && request.config.lightSelection
                    != LightSelectionStrategy::PowerWeighted)
            || (request.config.environmentSampler
                    != EnvironmentDirectionSampler::UniformSphere
                && request.config.environmentSampler
                    != EnvironmentDirectionSampler::ImportanceMap))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR DI requires explicit light-selection and environment-sampling strategies");
            return plan;
        }
        if (request.config.transportModel != TransportModel::Pbr
            || (request.config.executionArchitecture
                    != ExecutionArchitecture::Staged
                && request.config.executionArchitecture
                    != ExecutionArchitecture::Megakernel
                && request.config.executionArchitecture
                    != ExecutionArchitecture::Wavefront))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR DI requires PBR transport on a production GPU execution architecture");
            return plan;
        }
        if (request.identity.width == 0u || request.identity.height == 0u
            || request.identity.configGeneration == 0u
            || request.identity.sceneGeneration == 0u
            || request.identity.resourceGeneration == 0u
            || request.identity.lightGeneration == 0u)
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "frame and generation identity must be complete");
            return plan;
        }
        if (plan.shadowRaysPerVisibility == 0u
            || request.identity.shadowMethod != request.config.shadowMethod)
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR shadow method is invalid or drifted from the frame identity");
            return plan;
        }
        if (request.settings.lightCount == 0u
            || request.settings.initialCandidateCount == 0u
            || request.settings.initialCandidateCount > 64u
            || request.settings.spatialNeighborCount > 30u
            || request.settings.maximumReservoirM == 0u
            || request.settings.maximumReservoirM > 4096u
            || request.settings.maximumHistoryAge == 0u
            || request.settings.maximumHistoryAge > 4096u
            || request.settings.framesInFlight < 2u
            || request.settings.framesInFlight > 4u
            || request.settings.previousLightCount > request.settings.lightCount
            || (!request.settings.spatialReuse
                && request.settings.spatialNeighborCount != 0u))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR candidate, neighbor, M, history, light, or frame count is invalid");
            return plan;
        }
        if (!RuntimeSettingsMatchConfig(request))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR runtime settings drifted from the canonical RuntimeConfig tuple");
            return plan;
        }

        const ReSTIRProviderAvailability& providers = request.providers;
        if (!providers.abiV3 || !providers.primarySurfaceV2
            || !providers.currentLightDistribution
            || !providers.currentPreviousLightMapping
            || !providers.restirComputePipelines || !providers.traceAny
            || !providers.reconstructionSignalV2
            || !providers.reconstructionRecorder || !providers.historyStorage
            || (request.settings.writeDebug && !providers.debugStorage))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "abi-v3, primary surface, light mapping, compute, trace-any, reconstruction signal/recorder, history, and debug providers must all be attached");
            return plan;
        }

        plan.historyDecision = request.settings.temporalReuse
                && SameHistoryIdentity(request)
            ? ReSTIRHistoryDecision::Reuse
            : ReSTIRHistoryDecision::Reset;
        if (plan.historyDecision == ReSTIRHistoryDecision::Reuse
            && request.settings.previousLightCount == 0u)
        {
            Reject(plan, ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "exact temporal reuse requires the previous light-table count");
            return plan;
        }

        const std::uint64_t physicalHistoryCount =
            2ull * request.settings.framesInFlight;
        const std::uint64_t writePhysical =
            (request.identity.frameIndex & 1ull) * request.settings.framesInFlight
            + request.identity.frameIndex % request.settings.framesInFlight;
        if (writePhysical >= physicalHistoryCount)
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR history write index exceeded the 2N ring");
            return plan;
        }
        plan.historyWritePhysicalIndex = static_cast<std::uint32_t>(writePhysical);
        if (plan.historyDecision == ReSTIRHistoryDecision::Reuse)
        {
            const std::uint64_t previousFrame = request.identity.frameIndex - 1u;
            const std::uint64_t readPhysical =
                (previousFrame & 1ull) * request.settings.framesInFlight
                + previousFrame % request.settings.framesInFlight;
            if (readPhysical >= physicalHistoryCount
                || request.history.physicalIndex != readPhysical
                || readPhysical == writePhysical)
            {
                Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                    "ReSTIR prior history does not identify the exact 2N ring slot");
                return plan;
            }
            plan.historyReadPhysicalIndex = static_cast<std::uint32_t>(readPhysical);
        }

        bool footprintValid = false;
        plan.footprint = BuildFootprint(
            request.identity,
            request.settings,
            plan.historyDecision == ReSTIRHistoryDecision::Reuse
                ? request.settings.previousLightCount : 0u,
            footprintValid);
        if (!footprintValid)
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR resource footprint overflowed 64-bit accounting");
            return plan;
        }

        std::uint64_t candidateElementCount = 0u;
        std::uint64_t referenceElementCount = 0u;
        std::uint64_t referenceRayCount = 0u;
        std::uint64_t winnerRayCount = 0u;
        const bool indexedCountsValid = CheckedMultiply(
                plan.footprint.pixelCount,
                request.settings.initialCandidateCount,
                candidateElementCount)
            && CheckedMultiply(
                plan.footprint.pixelCount,
                static_cast<std::uint64_t>(
                    request.settings.spatialNeighborCount) + 1u,
                referenceElementCount)
            && CheckedMultiply(
                referenceElementCount,
                plan.shadowRaysPerVisibility,
                referenceRayCount)
            && CheckedMultiply(
                plan.footprint.pixelCount,
                plan.shadowRaysPerVisibility,
                winnerRayCount);
        if (!indexedCountsValid
            || plan.footprint.pixelCount > std::numeric_limits<std::uint32_t>::max()
            || candidateElementCount > std::numeric_limits<std::uint32_t>::max()
            || referenceRayCount > std::numeric_limits<std::uint32_t>::max()
            || winnerRayCount > std::numeric_limits<std::uint32_t>::max())
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "ReSTIR dispatch or traversal indexing exceeds the 32-bit shader contract");
            return plan;
        }

        plan.primaryDirectOwner = PrimaryDirectLightingOwner::ReSTIRDI;

        const std::uint32_t pixelGroups = static_cast<std::uint32_t>(
            (plan.footprint.pixelCount + 63u) / 64u);
        const std::uint32_t candidateGroups = static_cast<std::uint32_t>(
            (candidateElementCount + 63u) / 64u);
        const std::uint32_t referenceRayGroups = static_cast<std::uint32_t>(
            (referenceRayCount + 63u) / 64u);
        const std::uint32_t winnerRayGroups = static_cast<std::uint32_t>(
            (winnerRayCount + 63u) / 64u);
        const auto add = [&plan](
            const ReSTIRPass pass,
            const std::uint32_t groupsX,
            const std::uint32_t groupsY = 1u,
            const std::uint32_t groupsZ = 1u,
            const bool referenceTrace = false,
            const bool winnerTrace = false,
            const bool externalReconstruction = false)
        {
            plan.passes.push_back({
                pass, groupsX, groupsY, groupsZ, referenceTrace, winnerTrace,
                externalReconstruction });
        };

        add(ReSTIRPass::ClearStatistics, 1u);
        add(ReSTIRPass::GenerateCandidates, candidateGroups);
        add(ReSTIRPass::InitialReservoir, pixelGroups);
        // These two passes are always recorded. With reuse disabled or history
        // reset they perform explicit non-aliasing copies, so every published
        // reservoir has a defined Initial -> Temporal -> Spatial lineage.
        add(ReSTIRPass::TemporalReuse, pixelGroups);
        add(ReSTIRPass::SpatialReuse, pixelGroups);
        if (request.settings.estimatorMode == ReSTIREstimatorMode::ReferenceCorrection)
        {
            add(ReSTIRPass::PrepareReferenceVisibility,
                referenceRayGroups, 1u, 1u, true, false);
            add(ReSTIRPass::ResolveReferenceVisibility, pixelGroups);
            plan.maximumReferenceVisibilityRays = referenceRayCount;
        }
        add(ReSTIRPass::PrepareWinnerVisibility,
            winnerRayGroups, 1u, 1u, false, true);
        add(ReSTIRPass::ResolveWinnerVisibility, pixelGroups);
        add(ReSTIRPass::PublishSplitDirectSignal, pixelGroups);
        add(ReSTIRPass::Reconstruction, 1u, 1u, 1u, false, false, true);
        add(ReSTIRPass::PublishHistory, pixelGroups);
        if (request.settings.writeDebug)
        {
            add(ReSTIRPass::WriteDebug,
                (request.identity.width + 7u) / 8u,
                (request.identity.height + 7u) / 8u);
        }
        plan.maximumWinnerVisibilityRays = winnerRayCount;
        if (!BuildMinimumSet5Ranges(plan))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "abi-v3 per-binding minimum ranges overflowed");
            return plan;
        }

        if (!ValidateReSTIRFramePlan(plan))
        {
            Reject(plan, ReSTIRRuntimeStatusCode::InvalidRequest,
                "internal ReSTIR frame-plan validation failed");
        }
        return plan;
    }

    bool ValidateReSTIRFramePlan(const ReSTIRFramePlan& plan) noexcept
    {
        if (!plan.IsReady() || plan.passes.empty()
            || plan.primaryDirectOwner != PrimaryDirectLightingOwner::ReSTIRDI
            || plan.identity.width == 0u || plan.identity.height == 0u
            || plan.currentLightCount == 0u
            || plan.initialCandidateCount == 0u
            || plan.framesInFlight < 2u || plan.framesInFlight > 4u
            || plan.identity.shadowMethod != plan.shadowMethod
            || plan.shadowRaysPerVisibility == 0u
            || plan.shadowRaysPerVisibility
                != ShadowRaysPerVisibility(plan.shadowMethod))
        {
            return false;
        }

        const std::uint64_t pixelCount =
            static_cast<std::uint64_t>(plan.identity.width) * plan.identity.height;
        std::uint64_t candidateCount = 0u;
        std::uint64_t referenceCount = 0u;
        std::uint64_t referenceRayCount = 0u;
        std::uint64_t winnerRayCount = 0u;
        const bool indexedCountsValid = CheckedMultiply(
                pixelCount, plan.initialCandidateCount, candidateCount)
            && CheckedMultiply(
                pixelCount,
                static_cast<std::uint64_t>(plan.spatialNeighborCount) + 1u,
                referenceCount)
            && CheckedMultiply(
                referenceCount,
                plan.shadowRaysPerVisibility,
                referenceRayCount)
            && CheckedMultiply(
                pixelCount,
                plan.shadowRaysPerVisibility,
                winnerRayCount);
        if (!indexedCountsValid || pixelCount == 0u
            || pixelCount > std::numeric_limits<std::uint32_t>::max()
            || candidateCount > std::numeric_limits<std::uint32_t>::max()
            || referenceRayCount > std::numeric_limits<std::uint32_t>::max()
            || winnerRayCount > std::numeric_limits<std::uint32_t>::max()
            || plan.footprint.pixelCount != pixelCount
            || plan.maximumWinnerVisibilityRays != winnerRayCount)
        {
            return false;
        }

        ReSTIRRuntimeSettings footprintSettings{};
        footprintSettings.lightCount = plan.currentLightCount;
        footprintSettings.previousLightCount = plan.previousLightCount;
        footprintSettings.initialCandidateCount = plan.initialCandidateCount;
        footprintSettings.spatialNeighborCount = plan.spatialNeighborCount;
        footprintSettings.framesInFlight = plan.framesInFlight;
        footprintSettings.estimatorMode = plan.estimatorMode;
        footprintSettings.writeDebug = plan.writeDebug;
        bool expectedFootprintValid = false;
        const ReSTIRResourceFootprint expectedFootprint = BuildFootprint(
            plan.identity,
            footprintSettings,
            plan.historyDecision == ReSTIRHistoryDecision::Reuse
                ? plan.previousLightCount : 0u,
            expectedFootprintValid);
        const auto SameFootprint = [](const ReSTIRResourceFootprint& a,
            const ReSTIRResourceFootprint& b) noexcept
        {
            return a.pixelCount == b.pixelCount
                && a.candidateBytes == b.candidateBytes
                && a.reservoirBytes == b.reservoirBytes
                && a.historyBytes == b.historyBytes
                && a.visibilityBytes == b.visibilityBytes
                && a.lightMappingBytes == b.lightMappingBytes
                && a.neighborIndexBytes == b.neighborIndexBytes
                && a.shadowRayBytes == b.shadowRayBytes
                && a.directSignalBytes == b.directSignalBytes
                && a.debugBytes == b.debugBytes
                && a.statisticsBytes == b.statisticsBytes
                && a.parameterBytes == b.parameterBytes
                && a.totalBytes == b.totalBytes;
        };
        if (!expectedFootprintValid
            || !SameFootprint(plan.footprint, expectedFootprint))
        {
            return false;
        }

        const std::uint64_t physicalCount = 2ull * plan.framesInFlight;
        const std::uint64_t expectedWrite =
            (plan.identity.frameIndex & 1ull) * plan.framesInFlight
            + plan.identity.frameIndex % plan.framesInFlight;
        if (plan.historyWritePhysicalIndex != expectedWrite
            || expectedWrite >= physicalCount)
        {
            return false;
        }
        if (plan.historyDecision == ReSTIRHistoryDecision::Reuse)
        {
            if (plan.identity.frameIndex == 0u || plan.previousLightCount == 0u)
            {
                return false;
            }
            const std::uint64_t previousFrame = plan.identity.frameIndex - 1u;
            const std::uint64_t expectedRead =
                (previousFrame & 1ull) * plan.framesInFlight
                + previousFrame % plan.framesInFlight;
            if (plan.historyReadPhysicalIndex != expectedRead
                || expectedRead >= physicalCount || expectedRead == expectedWrite)
            {
                return false;
            }
        }
        else if (plan.historyReadPhysicalIndex != 0xffffffffu)
        {
            return false;
        }

        const std::uint64_t expectedReferenceRays =
            plan.estimatorMode == ReSTIREstimatorMode::ReferenceCorrection
                ? referenceRayCount : 0u;
        if (plan.maximumReferenceVisibilityRays != expectedReferenceRays)
        {
            return false;
        }

        const std::uint32_t pixelGroups = static_cast<std::uint32_t>(
            (pixelCount + 63u) / 64u);
        const std::uint32_t candidateGroups = static_cast<std::uint32_t>(
            (candidateCount + 63u) / 64u);
        const std::uint32_t referenceRayGroups = static_cast<std::uint32_t>(
            (referenceRayCount + 63u) / 64u);
        const std::uint32_t winnerRayGroups = static_cast<std::uint32_t>(
            (winnerRayCount + 63u) / 64u);
        std::array<ReSTIRScheduledPass, 13u> expected{};
        std::size_t expectedCount = 0u;
        const auto add = [&expected, &expectedCount](
            const ReSTIRPass pass,
            const std::uint32_t x,
            const std::uint32_t y = 1u,
            const std::uint32_t z = 1u,
            const bool reference = false,
            const bool winner = false,
            const bool reconstruction = false)
        {
            expected[expectedCount++] = {
                pass, x, y, z, reference, winner, reconstruction};
        };
        add(ReSTIRPass::ClearStatistics, 1u);
        add(ReSTIRPass::GenerateCandidates, candidateGroups);
        add(ReSTIRPass::InitialReservoir, pixelGroups);
        add(ReSTIRPass::TemporalReuse, pixelGroups);
        add(ReSTIRPass::SpatialReuse, pixelGroups);
        if (plan.estimatorMode == ReSTIREstimatorMode::ReferenceCorrection)
        {
            add(ReSTIRPass::PrepareReferenceVisibility,
                referenceRayGroups, 1u, 1u, true, false);
            add(ReSTIRPass::ResolveReferenceVisibility, pixelGroups);
        }
        add(ReSTIRPass::PrepareWinnerVisibility,
            winnerRayGroups, 1u, 1u, false, true);
        add(ReSTIRPass::ResolveWinnerVisibility, pixelGroups);
        add(ReSTIRPass::PublishSplitDirectSignal, pixelGroups);
        add(ReSTIRPass::Reconstruction, 1u, 1u, 1u, false, false, true);
        add(ReSTIRPass::PublishHistory, pixelGroups);
        if (plan.writeDebug)
        {
            add(ReSTIRPass::WriteDebug,
                (plan.identity.width + 7u) / 8u,
                (plan.identity.height + 7u) / 8u);
        }
        if (plan.passes.size() != expectedCount)
        {
            return false;
        }
        for (std::size_t index = 0u; index < expectedCount; ++index)
        {
            const ReSTIRScheduledPass& actual = plan.passes[index];
            const ReSTIRScheduledPass& wanted = expected[index];
            if (actual.pass != wanted.pass
                || actual.dispatchGroupCountX != wanted.dispatchGroupCountX
                || actual.dispatchGroupCountY != wanted.dispatchGroupCountY
                || actual.dispatchGroupCountZ != wanted.dispatchGroupCountZ
                || actual.recordsReferenceTraceAny != wanted.recordsReferenceTraceAny
                || actual.recordsWinnerTraceAny != wanted.recordsWinnerTraceAny
                || actual.recordsExternalReconstruction
                    != wanted.recordsExternalReconstruction)
            {
                return false;
            }
        }

        ReSTIRFramePlan expectedRanges{};
        expectedRanges.footprint = plan.footprint;
        expectedRanges.maximumWinnerVisibilityRays =
            plan.maximumWinnerVisibilityRays;
        expectedRanges.maximumReferenceVisibilityRays =
            plan.maximumReferenceVisibilityRays;
        expectedRanges.currentLightCount = plan.currentLightCount;
        expectedRanges.previousLightCount = plan.previousLightCount;
        expectedRanges.historyDecision = plan.historyDecision;
        expectedRanges.writeDebug = plan.writeDebug;
        if (!BuildMinimumSet5Ranges(expectedRanges)
            || expectedRanges.minimumSet5BufferRanges
                != plan.minimumSet5BufferRanges)
        {
            return false;
        }
        return true;
    }

    ReSTIRRuntimeStatus ReSTIRDIRuntime::RecordFrame(
        const ReSTIRFramePlan& plan,
        IReSTIRGpuRecorder& recorder,
        Rt::Gpu::IGpuTraversalBackend& traversalBackend) const
    {
        if (!ValidateReSTIRFramePlan(plan))
        {
            return { ReSTIRRuntimeStatusCode::InvalidRequest,
                "refusing to record an invalid ReSTIR frame plan" };
        }
        const Rt::Gpu::GpuTraversalBackendDescriptor traversal =
            traversalBackend.Descriptor();
        if (!traversal.supportsAny)
        {
            return { ReSTIRRuntimeStatusCode::ProviderUnavailable,
                "selected traversal backend does not support trace-any" };
        }

        if (ReSTIRRuntimeStatus status = recorder.BeginFrame(plan); !status)
        {
            return status;
        }
        const auto AbortWith = [&recorder](ReSTIRRuntimeStatus status)
        {
            recorder.AbortFrame();
            return status;
        };

        for (const ReSTIRScheduledPass& scheduled : plan.passes)
        {
            if (scheduled.recordsExternalReconstruction)
            {
                if (ReSTIRRuntimeStatus status =
                    recorder.RecordReconstruction(plan); !status)
                {
                    return AbortWith(std::move(status));
                }
                if (ReSTIRRuntimeStatus status = recorder.RecordBarrier(
                    ReSTIRBarrier::ReconstructionToCompute); !status)
                {
                    return AbortWith(std::move(status));
                }
                continue;
            }
            if (ReSTIRRuntimeStatus status = recorder.RecordCompute(
                scheduled.pass,
                scheduled.dispatchGroupCountX,
                scheduled.dispatchGroupCountY,
                scheduled.dispatchGroupCountZ); !status)
            {
                return AbortWith(std::move(status));
            }

            if (scheduled.recordsReferenceTraceAny || scheduled.recordsWinnerTraceAny)
            {
                if (ReSTIRRuntimeStatus status = recorder.RecordBarrier(
                    ReSTIRBarrier::ComputeToTraversal); !status)
                {
                    return AbortWith(std::move(status));
                }
                const std::uint64_t maximumRayCount = scheduled.recordsReferenceTraceAny
                    ? plan.maximumReferenceVisibilityRays
                    : plan.maximumWinnerVisibilityRays;
                if (maximumRayCount > std::numeric_limits<std::uint32_t>::max())
                {
                    return AbortWith({ ReSTIRRuntimeStatusCode::InvalidRequest,
                        "trace-any batch exceeds the 32-bit traversal batch contract" });
                }
                const Rt::Gpu::GpuTraceBatch batch = recorder.BuildTraceAnyBatch(
                    scheduled.pass, static_cast<std::uint32_t>(maximumRayCount));
                if (batch.rayCount != maximumRayCount)
                {
                    return AbortWith({ ReSTIRRuntimeStatusCode::RecordingFailed,
                        "recorder did not produce the exact trace-any batch required by the frame plan" });
                }
                const Rt::Gpu::GpuTraversalStatus traceStatus =
                    traversalBackend.RecordTraceAnyBatch(batch);
                if (!traceStatus)
                {
                    return AbortWith({ ReSTIRRuntimeStatusCode::RecordingFailed,
                        "trace-any recording failed: " + traceStatus.message });
                }
                if (ReSTIRRuntimeStatus status = recorder.RecordBarrier(
                    ReSTIRBarrier::TraversalToCompute); !status)
                {
                    return AbortWith(std::move(status));
                }
                continue;
            }

            const bool publishHistoryNeedsDebugBarrier =
                scheduled.pass == ReSTIRPass::PublishHistory
                && !plan.passes.empty()
                && plan.passes.back().pass == ReSTIRPass::WriteDebug;
            if ((scheduled.pass != ReSTIRPass::PublishHistory
                    && scheduled.pass != ReSTIRPass::WriteDebug)
                || publishHistoryNeedsDebugBarrier)
            {
                if (ReSTIRRuntimeStatus status = recorder.RecordBarrier(
                    BarrierAfter(scheduled.pass)); !status)
                {
                    return AbortWith(std::move(status));
                }
            }
        }
        ReSTIRRuntimeStatus endStatus = recorder.EndFrame(plan);
        if (!endStatus)
        {
            recorder.AbortFrame();
        }
        return endStatus;
    }

    std::string_view ToString(const ReSTIRPass pass) noexcept
    {
        switch (pass)
        {
        case ReSTIRPass::ClearStatistics: return "restir-clear-statistics";
        case ReSTIRPass::GenerateCandidates: return "restir-generate-candidates";
        case ReSTIRPass::InitialReservoir: return "restir-initial-reservoir";
        case ReSTIRPass::TemporalReuse: return "restir-temporal-reuse";
        case ReSTIRPass::PrepareReferenceVisibility: return "restir-prepare-reference-visibility";
        case ReSTIRPass::ResolveReferenceVisibility: return "restir-resolve-reference-visibility";
        case ReSTIRPass::SpatialReuse: return "restir-spatial-reuse";
        case ReSTIRPass::PrepareWinnerVisibility: return "restir-prepare-winner-visibility";
        case ReSTIRPass::ResolveWinnerVisibility: return "restir-resolve-winner-visibility";
        case ReSTIRPass::PublishSplitDirectSignal: return "restir-publish-split-direct";
        case ReSTIRPass::Reconstruction: return "external-reconstruction";
        case ReSTIRPass::PublishHistory: return "restir-publish-history";
        case ReSTIRPass::WriteDebug: return "restir-write-debug";
        }
        return "restir-unknown";
    }

    std::string_view ToString(const ReSTIRHistoryDecision decision) noexcept
    {
        switch (decision)
        {
        case ReSTIRHistoryDecision::Reset: return "reset";
        case ReSTIRHistoryDecision::Reuse: return "reuse";
        }
        return "unknown";
    }
}
