#include "renderers/Wave3FrameGraph.hpp"

#include "contracts/AbiVersionV2.hpp"

#include <algorithm>
#include <limits>

namespace RenderingEngine::Wave3
{
    namespace
    {
        [[nodiscard]] bool IsMixedScene(const ScenePreset scene) noexcept
        {
            return scene == ScenePreset::BaselineGallery
                || scene == ScenePreset::IntersectionBvhLab
                || scene == ScenePreset::WhittedOpticsRoom
                || scene == ScenePreset::CornellBox
                || scene == ScenePreset::GgxMisMaterialLab
                || scene == ScenePreset::EnvironmentSamplingDome
                || scene == ScenePreset::BackendParityBenchmark
                || scene == ScenePreset::TemporalStabilityCorridor
                || scene == ScenePreset::ManyLightsRestirArena;
        }

        [[nodiscard]] bool IsWave3Backend(const TraversalBackend backend) noexcept
        {
            return backend == TraversalBackend::CanonicalLinearGpu
                || backend == TraversalBackend::GpuFlattenedSahBvh
                || backend == TraversalBackend::GpuLbvh
                || backend == TraversalBackend::VulkanRayQuery
                || backend == TraversalBackend::VulkanRayTracingPipeline;
        }

        [[nodiscard]] bool IsWave3Transport(
            const TransportModel transport) noexcept
        {
            return transport == TransportModel::Pbr
                || transport == TransportModel::Whitted;
        }

        [[nodiscard]] bool IsWave3Execution(
            const ExecutionArchitecture architecture) noexcept
        {
            return architecture == ExecutionArchitecture::Staged
                || architecture == ExecutionArchitecture::Megakernel
                || architecture == ExecutionArchitecture::Wavefront;
        }

        [[nodiscard]] bool IsMixedEstimator(
            const DirectLightingEstimator estimator) noexcept
        {
            return estimator == DirectLightingEstimator::BsdfOnly
                || estimator == DirectLightingEstimator::NextEventEstimation
                || estimator == DirectLightingEstimator::MultipleImportanceSampling
                || estimator == DirectLightingEstimator::RestirDirectIllumination;
        }

        [[nodiscard]] bool IsMixedLightSelection(
            const LightSelectionStrategy selection) noexcept
        {
            return selection == LightSelectionStrategy::Uniform
                || selection == LightSelectionStrategy::PowerWeighted;
        }

        [[nodiscard]] bool IsMixedEnvironmentSampler(
            const EnvironmentDirectionSampler sampler) noexcept
        {
            return sampler == EnvironmentDirectionSampler::UniformSphere
                || sampler == EnvironmentDirectionSampler::ImportanceMap;
        }

        [[nodiscard]] bool IsMixedDebugView(const DebugView debugView) noexcept
        {
            return debugView == DebugView::Final
                || debugView == DebugView::BaseColor
                || debugView == DebugView::Normal
                || debugView == DebugView::Roughness
                || debugView == DebugView::Metallic
                || debugView == DebugView::Emissive
                || (debugView >= DebugView::Motion
                    && debugView <= DebugView::TemporalRejectReasons);
        }

        [[nodiscard]] bool UsesHistory(const ReconstructionMode mode) noexcept
        {
            return mode == ReconstructionMode::TemporalAccumulation
                || mode == ReconstructionMode::Svgf;
        }

        [[nodiscard]] bool HistoryMatches(
            const FrameRequest& request) noexcept
        {
            const HistoryIdentity& history = request.history;
            return request.frameIndex != 0u
                && history.valid
                && history.publishedFrame + 1u == request.frameIndex
                && history.configGeneration == request.configGeneration
                && history.sceneGeneration == request.sceneGeneration
                && history.resourceGeneration == request.resourceGeneration
                && history.width == request.config.render.width
                && history.height == request.config.render.height
                && history.backend == request.config.backend
                && history.transportModel == request.config.transportModel
                && history.executionArchitecture
                    == request.config.executionArchitecture
                && history.reconstruction == request.config.reconstruction;
        }

        [[nodiscard]] std::uint32_t HistoryPhysicalIndex(
            const std::uint64_t frameIndex,
            const std::uint32_t framesInFlight) noexcept
        {
            // Consume every flight slot in one logical generation before
            // advancing to the other. Frame parity aliases generation and slot
            // for even N (N=2 would otherwise produce 0,3,0,3...).
            const std::uint64_t generation =
                (frameIndex / framesInFlight) & 1ull;
            const std::uint64_t flightSlot = frameIndex % framesInFlight;
            return static_cast<std::uint32_t>(
                generation * framesInFlight + flightSlot);
        }

        void Fail(
            FramePlan& plan,
            const PlanStatus status,
            std::string reason)
        {
            plan.status = status;
            plan.reason = std::move(reason);
            plan.passes.clear();
            plan.timestampQueryCount = 0u;
        }

        class PassBuilder final
        {
        public:
            PassBuilder(FramePlan& plan, const bool profile) noexcept
                : plan_(plan), profile_(profile)
            {
            }

            void Add(
                const PassKind kind,
                const PassDomain domain,
                const std::string_view lane,
                const std::uint32_t bounce = 0u,
                const std::uint32_t iteration = 0u,
                const bool writesIndirect = false,
                const bool readsIndirect = false)
            {
                ScheduledPass pass{};
                pass.kind = kind;
                pass.domain = domain;
                pass.lane = lane;
                pass.bounce = bounce;
                pass.iteration = iteration;
                pass.consumesPreviousPass = !plan_.passes.empty();
                pass.writesIndirectArguments = writesIndirect;
                pass.readsIndirectArguments = readsIndirect;
                // vkQueuePresentKHR is not a command-buffer scope. A timestamp
                // immediately before present would measure rendering completion,
                // not presentation, so leave that pass explicitly unmeasured.
                if (profile_ && domain != PassDomain::Present)
                {
                    pass.beginTimestampQuery = nextQuery_++;
                    pass.endTimestampQuery = nextQuery_++;
                }
                plan_.passes.push_back(pass);
            }

            [[nodiscard]] std::uint32_t QueryCount() const noexcept
            {
                return nextQuery_;
            }

        private:
            FramePlan& plan_;
            bool profile_ = false;
            std::uint32_t nextQuery_ = 0u;
        };

        void AddBackendPasses(
            PassBuilder& builder,
            const TraversalBackend backend)
        {
            switch (backend)
            {
            case TraversalBackend::CanonicalLinearGpu:
                // Canonical linear traversal consumes the shared triangle
                // stream directly and therefore needs no acceleration build.
                break;
            case TraversalBackend::GpuFlattenedSahBvh:
                builder.Add(PassKind::SoftwareSahUpload, PassDomain::Transfer, "L4");
                break;
            case TraversalBackend::GpuLbvh:
                builder.Add(PassKind::LbvhMorton, PassDomain::Compute, "L4");
                builder.Add(PassKind::LbvhRadixSort, PassDomain::Compute, "L4");
                builder.Add(PassKind::LbvhHierarchy, PassDomain::Compute, "L4");
                builder.Add(PassKind::LbvhBounds, PassDomain::Compute, "L4");
                break;
            case TraversalBackend::VulkanRayQuery:
                builder.Add(PassKind::HardwareBlasBuild,
                    PassDomain::AccelerationStructureBuild, "L5");
                builder.Add(PassKind::HardwareTlasBuild,
                    PassDomain::AccelerationStructureBuild, "L5");
                break;
            case TraversalBackend::VulkanRayTracingPipeline:
                builder.Add(PassKind::HardwareBlasBuild,
                    PassDomain::AccelerationStructureBuild, "L5");
                builder.Add(PassKind::HardwareTlasBuild,
                    PassDomain::AccelerationStructureBuild, "L5");
                builder.Add(PassKind::RtPipelineSbtPrepare,
                    PassDomain::RayTracing, "L5");
                break;
            default:
                break;
            }
            builder.Add(PassKind::BackendTraceProbe,
                backend == TraversalBackend::VulkanRayTracingPipeline
                    ? PassDomain::RayTracing
                    : PassDomain::Compute,
                backend == TraversalBackend::CanonicalLinearGpu
                    || backend == TraversalBackend::GpuFlattenedSahBvh
                        || backend == TraversalBackend::GpuLbvh
                    ? "L4"
                    : "L5");
        }

        void AddMegakernel(PassBuilder& builder)
        {
            builder.Add(PassKind::MegakernelIntegrate, PassDomain::Compute, "L6");
        }

        void AddWavefront(PassBuilder& builder, const std::uint32_t bounceCount)
        {
            builder.Add(PassKind::WavefrontReset, PassDomain::Transfer, "L7");
            builder.Add(PassKind::WavefrontRayGen, PassDomain::Compute, "L7");
            for (std::uint32_t bounce = 0u; bounce < bounceCount; ++bounce)
            {
                builder.Add(PassKind::WavefrontIntersect,
                    PassDomain::IndirectCompute, "L7", bounce, 0u, false, true);
                builder.Add(PassKind::WavefrontShade,
                    PassDomain::IndirectCompute, "L7", bounce, 0u, true, true);
                builder.Add(PassKind::WavefrontTraceShadow,
                    PassDomain::IndirectCompute, "L7", bounce, 0u, false, true);
                builder.Add(PassKind::WavefrontNextBounce,
                    PassDomain::IndirectCompute, "L7", bounce, 0u, true, true);
            }
            builder.Add(PassKind::WavefrontResolve, PassDomain::Compute, "L7");
        }

        void AddReconstruction(
            PassBuilder& builder,
            const ReconstructionMode reconstruction,
            const std::uint32_t atrousIterations)
        {
            builder.Add(PassKind::PrimarySurfaceExport, PassDomain::Compute, "L0/L8");
            if (reconstruction == ReconstructionMode::ProgressiveMean
                || reconstruction == ReconstructionMode::CurrentFrame)
            {
                builder.Add(PassKind::ComposeRaw, PassDomain::Compute, "L8");
                return;
            }

            if (reconstruction == ReconstructionMode::SpatialFixedAtrous)
            {
                builder.Add(PassKind::PrepareSignal, PassDomain::Compute, "L8");
                builder.Add(PassKind::VarianceBootstrap, PassDomain::Compute, "L8");
                for (std::uint32_t iteration = 0u;
                    iteration < atrousIterations; ++iteration)
                {
                    builder.Add(PassKind::AtrousIteration,
                        PassDomain::Compute, "L8", 0u, iteration);
                }
                builder.Add(PassKind::ComposeAtrous, PassDomain::Compute, "L8");
                return;
            }

            builder.Add(PassKind::MotionVectors, PassDomain::Compute, "L8");
            builder.Add(PassKind::PrepareSignal, PassDomain::Compute, "L8");
            builder.Add(PassKind::TemporalAccumulation, PassDomain::Compute, "L8");
            if (reconstruction == ReconstructionMode::TemporalAccumulation)
            {
                builder.Add(PassKind::ComposeTemporal, PassDomain::Compute, "L8");
            }
            else
            {
                builder.Add(PassKind::VarianceBootstrap, PassDomain::Compute, "L8");
                for (std::uint32_t iteration = 0u;
                    iteration < atrousIterations;
                    ++iteration)
                {
                    builder.Add(PassKind::AtrousIteration,
                        PassDomain::Compute, "L8", 0u, iteration);
                }
                builder.Add(
                    reconstruction == ReconstructionMode::Svgf
                        ? PassKind::ComposeSvgf
                        : PassKind::ComposeAtrous,
                    PassDomain::Compute,
                    "L8");
            }
            builder.Add(PassKind::PublishHistory, PassDomain::Compute, "L8");
        }
    }

    FramePlan BuildFramePlan(const FrameRequest& request)
    {
        FramePlan plan{};
        if (Contracts::AbiV2::kAbiVersion != 3u)
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "Wave 3 requires human abi-v2 with numeric version 3.");
            return plan;
        }
        const RuntimeConfig& config = request.config;
        if (config.version != kRuntimeConfigVersion
            || config.render.width == 0u || config.render.height == 0u
            || config.render.samplesPerFrame != 1u
            || config.render.maximumBounce == 0u
            || config.render.maximumBounce > 12u
            || request.framesInFlight == 0u || request.framesInFlight > 8u
            || request.atrousIterations == 0u || request.atrousIterations > 8u)
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "Wave 3 frame dimensions, 1-SPP scheduling, bounce count, history slots, or A-Trous iterations are invalid.");
            return plan;
        }
        if (!IsMixedScene(config.scene) || !IsWave3Backend(config.backend)
            || !IsWave3Transport(config.transportModel)
            || !IsWave3Execution(config.executionArchitecture)
            || !IsMixedEstimator(config.directLightingEstimator)
            || !IsMixedLightSelection(config.lightSelection)
            || !IsMixedEnvironmentSampler(config.environmentSampler)
            || !IsMixedDebugView(config.debugView))
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "The request contains an algorithm value that is not built by the mixed renderer.");
            return plan;
        }

        const ProviderAvailability& providers = request.providers;
        const bool backendAvailable =
            ((config.backend == TraversalBackend::CanonicalLinearGpu
                    || config.backend == TraversalBackend::GpuFlattenedSahBvh)
                && providers.flattenedSah)
            || (config.backend == TraversalBackend::GpuLbvh && providers.gpuLbvh)
            || (config.backend == TraversalBackend::VulkanRayQuery
                && providers.rayQuery)
            || (config.backend == TraversalBackend::VulkanRayTracingPipeline
                && providers.rtPipeline);
        if ((config.transportModel == TransportModel::Whitted
                && config.executionArchitecture != ExecutionArchitecture::Staged)
            || (config.transportModel == TransportModel::Whitted
                && config.directLightingEstimator
                    == DirectLightingEstimator::RestirDirectIllumination))
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "Whitted transport requires staged execution and does not support ReSTIR DI.");
            return plan;
        }
        const bool executionAvailable =
            ((config.executionArchitecture == ExecutionArchitecture::Staged
                    || config.executionArchitecture
                        == ExecutionArchitecture::Megakernel)
                && providers.megakernel)
            || (config.executionArchitecture == ExecutionArchitecture::Wavefront
                && providers.wavefront);
        const bool reconstructionAvailable =
            config.reconstruction == ReconstructionMode::ProgressiveMean
            || config.reconstruction == ReconstructionMode::CurrentFrame
            || (config.reconstruction == ReconstructionMode::TemporalAccumulation
                && providers.temporal)
            || (config.reconstruction == ReconstructionMode::SpatialFixedAtrous
                && providers.svgf)
            || (config.reconstruction == ReconstructionMode::Svgf
                && providers.temporal && providers.svgf);
        if (!backendAvailable || !executionAvailable || !reconstructionAvailable
            || (request.includeComparisonBaseline
                && (!providers.megakernel || !providers.wavefront)))
        {
            Fail(plan, PlanStatus::ProviderUnavailable,
                "A selected Wave 3 backend, execution architecture, reconstruction path, or comparison baseline is unavailable.");
            return plan;
        }

        plan.historyDecision = UsesHistory(config.reconstruction)
            ? (HistoryMatches(request) ? HistoryDecision::Reuse : HistoryDecision::Reset)
            : HistoryDecision::Unused;
        const std::uint64_t physicalCount = 2ull * request.framesInFlight;
        const std::uint64_t writePhysical = HistoryPhysicalIndex(
            request.frameIndex, request.framesInFlight);
        if (writePhysical >= physicalCount
            || writePhysical > std::numeric_limits<std::uint32_t>::max())
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "Wave 3 history write index overflowed.");
            return plan;
        }
        plan.historyWritePhysicalIndex = UsesHistory(config.reconstruction)
            ? static_cast<std::uint32_t>(writePhysical)
            : 0xffffffffu;
        if (plan.historyDecision == HistoryDecision::Reuse)
        {
            const std::uint64_t previousFrame = request.frameIndex - 1u;
            const std::uint64_t readPhysical = HistoryPhysicalIndex(
                previousFrame, request.framesInFlight);
            plan.historyReadPhysicalIndex = static_cast<std::uint32_t>(readPhysical);
        }

        PassBuilder builder(plan, request.includeProfiler);
        builder.Add(PassKind::FrameReset, PassDomain::Transfer, "L0");
        AddBackendPasses(builder, config.backend);
        if (request.includeComparisonBaseline)
        {
            AddMegakernel(builder);
            AddWavefront(builder, config.render.maximumBounce);
        }
        else if (config.executionArchitecture != ExecutionArchitecture::Wavefront)
        {
            AddMegakernel(builder);
        }
        else
        {
            AddWavefront(builder, config.render.maximumBounce);
        }
        AddReconstruction(
            builder, config.reconstruction, request.atrousIterations);
        builder.Add(PassKind::Present, PassDomain::Present, "L0");
        plan.timestampQueryCount = builder.QueryCount();
        plan.status = PlanStatus::Ready;
        plan.reason.clear();
        if (!ValidateFramePlan(plan))
        {
            Fail(plan, PlanStatus::InvalidRequest,
                "The generated Wave 3 frame plan failed its internal dependency audit.");
        }
        return plan;
    }

    bool ValidateFramePlan(const FramePlan& plan) noexcept
    {
        if (!plan.IsReady() || plan.passes.size() < 4u
            || plan.passes.front().kind != PassKind::FrameReset
            || plan.passes.back().kind != PassKind::Present)
        {
            return false;
        }
        bool sawIntegrator = false;
        bool sawPrimarySurface = false;
        bool sawCompose = false;
        std::uint32_t expectedQuery = 0u;
        const bool profiled = plan.timestampQueryCount != 0u;
        for (std::size_t index = 0u; index < plan.passes.size(); ++index)
        {
            const ScheduledPass& pass = plan.passes[index];
            if (index == 0u ? pass.consumesPreviousPass : !pass.consumesPreviousPass)
            {
                return false;
            }
            if (profiled && pass.domain != PassDomain::Present)
            {
                if (pass.beginTimestampQuery != expectedQuery++
                    || pass.endTimestampQuery != expectedQuery++)
                {
                    return false;
                }
            }
            else if (pass.beginTimestampQuery != 0xffffffffu
                || pass.endTimestampQuery != 0xffffffffu)
            {
                return false;
            }
            sawIntegrator = sawIntegrator
                || pass.kind == PassKind::MegakernelIntegrate
                || pass.kind == PassKind::WavefrontResolve;
            if (pass.kind == PassKind::PrimarySurfaceExport)
            {
                if (!sawIntegrator) return false;
                sawPrimarySurface = true;
            }
            if (pass.kind == PassKind::ComposeRaw
                || pass.kind == PassKind::ComposeTemporal
                || pass.kind == PassKind::ComposeAtrous
                || pass.kind == PassKind::ComposeSvgf)
            {
                if (!sawPrimarySurface) return false;
                sawCompose = true;
            }
            if (pass.kind == PassKind::PublishHistory && !sawCompose)
            {
                return false;
            }
        }
        return sawIntegrator && sawPrimarySurface && sawCompose
            && expectedQuery == plan.timestampQueryCount;
    }

    bool HasTimestampQueries(const ScheduledPass& pass) noexcept
    {
        return pass.beginTimestampQuery != 0xffffffffu
            && pass.endTimestampQuery != 0xffffffffu;
    }

    std::string_view ToString(const PassKind kind) noexcept
    {
        switch (kind)
        {
        case PassKind::FrameReset: return "frame-reset";
        case PassKind::SoftwareSahUpload: return "software-sah-upload";
        case PassKind::LbvhMorton: return "lbvh-morton";
        case PassKind::LbvhRadixSort: return "lbvh-radix-sort";
        case PassKind::LbvhHierarchy: return "lbvh-hierarchy";
        case PassKind::LbvhBounds: return "lbvh-bounds";
        case PassKind::HardwareBlasBuild: return "hardware-blas-build";
        case PassKind::HardwareTlasBuild: return "hardware-tlas-build";
        case PassKind::RtPipelineSbtPrepare: return "rt-pipeline-sbt-prepare";
        case PassKind::BackendTraceProbe: return "backend-trace-probe";
        case PassKind::MegakernelIntegrate: return "megakernel-integrate";
        case PassKind::WavefrontReset: return "wavefront-reset";
        case PassKind::WavefrontRayGen: return "wavefront-raygen";
        case PassKind::WavefrontIntersect: return "wavefront-intersect";
        case PassKind::WavefrontShade: return "wavefront-shade";
        case PassKind::WavefrontTraceShadow: return "wavefront-trace-shadow";
        case PassKind::WavefrontNextBounce: return "wavefront-next-bounce";
        case PassKind::WavefrontResolve: return "wavefront-resolve";
        case PassKind::PrimarySurfaceExport: return "primary-surface-export";
        case PassKind::MotionVectors: return "motion-vectors";
        case PassKind::PrepareSignal: return "prepare-signal";
        case PassKind::TemporalAccumulation: return "temporal-accumulation";
        case PassKind::VarianceBootstrap: return "variance-bootstrap";
        case PassKind::AtrousIteration: return "atrous-iteration";
        case PassKind::ComposeRaw: return "compose-raw";
        case PassKind::ComposeTemporal: return "compose-temporal";
        case PassKind::ComposeAtrous: return "compose-atrous";
        case PassKind::ComposeSvgf: return "compose-svgf";
        case PassKind::PublishHistory: return "publish-history";
        case PassKind::Present: return "present";
        default: return "invalid";
        }
    }

    std::string_view ToString(const HistoryDecision decision) noexcept
    {
        switch (decision)
        {
        case HistoryDecision::Unused: return "unused";
        case HistoryDecision::Reset: return "reset";
        case HistoryDecision::Reuse: return "reuse";
        default: return "invalid";
        }
    }
}
