#include "RtPipelineTraversalAdapter.hpp"

#include <limits>

namespace RenderingEngine::Rt::Hardware
{
    namespace
    {
        [[nodiscard]] Gpu::GpuTraversalStatus ToGpuStatus(
            const Status& status) noexcept
        {
            if (status)
            {
                return {};
            }
            return {
                status.result == VK_ERROR_VALIDATION_FAILED_EXT
                    ? Gpu::GpuTraversalStatusCode::InvalidArgument
                    : Gpu::GpuTraversalStatusCode::RecordingFailed,
                status.message};
        }
    }

    RtPipelineTraversalAdapter::RtPipelineTraversalAdapter(
        RtPipelineBackend& backend,
        const std::uint32_t alphaAtlasLayerCount,
        const std::uint32_t alphaSamplerId) noexcept
        : backend_(&backend)
        , alphaAtlasLayerCount_(alphaAtlasLayerCount)
        , alphaSamplerId_(alphaSamplerId)
    {
    }

    Gpu::GpuTraversalBackendDescriptor
        RtPipelineTraversalAdapter::Descriptor() const noexcept
    {
        return {"hardware-rt-pipeline", "Vulkan Hardware RT Pipeline",
            true, true, true};
    }

    Gpu::GpuTraversalStatus RtPipelineTraversalAdapter::BuildOrUpdateScene(
        const Gpu::GpuSceneBuildRequest& request)
    {
        if (backend_ == nullptr || request.commandBuffer == VK_NULL_HANDLE ||
            request.canonicalSceneSet == VK_NULL_HANDLE ||
            request.sceneFingerprint == 0u || request.sceneGeneration == 0u)
        {
            return {Gpu::GpuTraversalStatusCode::InvalidArgument,
                "RT Pipeline scene build requires command buffer, canonical scene set, fingerprint, and generation"};
        }
        canonicalSceneSet_ = request.canonicalSceneSet;
        sceneFingerprint_ = request.sceneFingerprint;
        sceneGeneration_ = request.sceneGeneration;
        sceneReady_ = true;
        return {};
    }

    Gpu::GpuTraversalStatus RtPipelineTraversalAdapter::ValidateTrace(
        const Gpu::GpuTraceBatch& batch) const
    {
        if (backend_ == nullptr)
        {
            return {Gpu::GpuTraversalStatusCode::Unsupported,
                "RT Pipeline backend adapter is not configured"};
        }
        if (!sceneReady_)
        {
            return {Gpu::GpuTraversalStatusCode::MissingScene,
                "RT Pipeline trace was requested before a scene identity was accepted"};
        }
        if (batch.commandBuffer == VK_NULL_HANDLE ||
            batch.canonicalSceneSet == VK_NULL_HANDLE ||
            batch.traversalSet == VK_NULL_HANDLE || batch.rayCount == 0u)
        {
            return {Gpu::GpuTraversalStatusCode::InvalidArgument,
                "RT Pipeline trace requires command buffer, scene/traversal sets, and non-zero ray count"};
        }
        if (batch.sceneFingerprint == 0u || batch.sceneGeneration == 0u)
        {
            return {Gpu::GpuTraversalStatusCode::InvalidArgument,
                "RT Pipeline trace requires a non-zero scene fingerprint and generation"};
        }
        if (batch.canonicalSceneSet != canonicalSceneSet_ ||
            batch.sceneFingerprint != sceneFingerprint_ ||
            batch.sceneGeneration != sceneGeneration_)
        {
            return {Gpu::GpuTraversalStatusCode::MissingScene,
                "RT Pipeline trace scene identity does not match the accepted canonical scene"};
        }
        const std::uint64_t rayEnd =
            static_cast<std::uint64_t>(batch.rayOffset) + batch.rayCount;
        const std::uint64_t hitEnd =
            static_cast<std::uint64_t>(batch.hitOffset) + batch.rayCount;
        if (rayEnd > (std::numeric_limits<std::uint32_t>::max)() ||
            hitEnd > (std::numeric_limits<std::uint32_t>::max)())
        {
            return {Gpu::GpuTraversalStatusCode::InvalidArgument,
                "RT Pipeline trace record range overflows uint32"};
        }
        return {};
    }

    Gpu::GpuTraversalStatus RtPipelineTraversalAdapter::ForwardTrace(
        const Gpu::GpuTraceBatch& batch,
        const RayBatchQuery query)
    {
        const Gpu::GpuTraversalStatus validation = ValidateTrace(batch);
        if (!validation)
        {
            return validation;
        }
        const Status status = query == RayBatchQuery::Closest
            ? backend_->RecordTraceClosestBatch(batch.commandBuffer,
                batch.canonicalSceneSet, batch.traversalSet, batch.rayOffset,
                batch.hitOffset, batch.rayCount, alphaAtlasLayerCount_,
                alphaSamplerId_)
            : backend_->RecordTraceAnyBatch(batch.commandBuffer,
                batch.canonicalSceneSet, batch.traversalSet, batch.rayOffset,
                batch.hitOffset, batch.rayCount, alphaAtlasLayerCount_,
                alphaSamplerId_);
        return ToGpuStatus(status);
    }

    Gpu::GpuTraversalStatus RtPipelineTraversalAdapter::RecordTraceClosestBatch(
        const Gpu::GpuTraceBatch& batch)
    {
        return ForwardTrace(batch, RayBatchQuery::Closest);
    }

    Gpu::GpuTraversalStatus RtPipelineTraversalAdapter::RecordTraceAnyBatch(
        const Gpu::GpuTraceBatch& batch)
    {
        return ForwardTrace(batch, RayBatchQuery::Any);
    }
}
