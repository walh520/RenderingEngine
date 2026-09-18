#include "rt/gpu/IGpuTraversalBackend.hpp"
#include "rt/gpu/TraversalFixtures.hpp"

#include "contracts/AbiVersionV1.hpp"
#include "contracts/RayHitAbiV0.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace RenderingEngine::Rt::Gpu
{
    namespace
    {
        [[nodiscard]] GpuTraversalStatus Failure(
            const GpuTraversalStatusCode code,
            std::string message)
        {
            return { code, std::move(message) };
        }

        [[nodiscard]] GpuTraversalStatus ValidateSceneRequest(
            const GpuSceneBuildRequest& request,
            const bool requireHandles)
        {
            if (request.sceneFingerprint == 0u || request.sceneGeneration == 0u)
            {
                return Failure(
                    GpuTraversalStatusCode::InvalidArgument,
                    "A traversal scene requires a non-zero fingerprint and generation.");
            }
            if (requireHandles
                && (request.commandBuffer == VK_NULL_HANDLE
                    || request.canonicalSceneSet == VK_NULL_HANDLE))
            {
                return Failure(
                    GpuTraversalStatusCode::InvalidArgument,
                    "Strict traversal recording requires command-buffer and canonical-scene handles.");
            }
            return {};
        }

        [[nodiscard]] GpuTraversalStatus ValidateTraceBatch(
            const GpuTraceBatch& batch,
            const bool requireHandles)
        {
            if (batch.rayCount == 0u
                || batch.sceneFingerprint == 0u
                || batch.sceneGeneration == 0u)
            {
                return Failure(
                    GpuTraversalStatusCode::InvalidArgument,
                    "A traversal batch requires a non-zero count, scene fingerprint, and generation.");
            }
            if (batch.rayOffset > std::numeric_limits<std::uint32_t>::max() - batch.rayCount
                || batch.hitOffset > std::numeric_limits<std::uint32_t>::max() - batch.rayCount)
            {
                return Failure(
                    GpuTraversalStatusCode::InvalidArgument,
                    "Traversal batch record range overflows uint32.");
            }
            if (requireHandles
                && (batch.commandBuffer == VK_NULL_HANDLE
                    || batch.canonicalSceneSet == VK_NULL_HANDLE
                    || batch.traversalSet == VK_NULL_HANDLE))
            {
                return Failure(
                    GpuTraversalStatusCode::InvalidArgument,
                    "Strict traversal recording requires command-buffer, scene, and traversal handles.");
            }
            return {};
        }
    }

    LegacyAnalyticTraversalAdapter::LegacyAnalyticTraversalAdapter(
        const LegacyAnalyticTraversalCallbacks callbacks) noexcept
        : callbacks_(callbacks)
    {
    }

    bool LegacyAnalyticTraversalAdapter::IsConfigured() const noexcept
    {
        return callbacks_.buildOrUpdateScene != nullptr
            && callbacks_.recordTraceClosestBatch != nullptr
            && callbacks_.recordTraceAnyBatch != nullptr;
    }

    GpuTraversalBackendDescriptor LegacyAnalyticTraversalAdapter::Descriptor() const noexcept
    {
        return {
            "canonical-linear-gpu",
            "Legacy analytic GPU traversal adapter",
            true,
            true,
            false
        };
    }

    GpuTraversalStatus LegacyAnalyticTraversalAdapter::BuildOrUpdateScene(
        const GpuSceneBuildRequest& request)
    {
        if (callbacks_.buildOrUpdateScene == nullptr)
        {
            return Failure(
                GpuTraversalStatusCode::Unsupported,
                "The legacy analytic adapter has no scene callback.");
        }
        const GpuTraversalStatus validation = ValidateSceneRequest(request, true);
        return validation ? callbacks_.buildOrUpdateScene(callbacks_.userData, request) : validation;
    }

    GpuTraversalStatus LegacyAnalyticTraversalAdapter::RecordTraceClosestBatch(
        const GpuTraceBatch& batch)
    {
        if (callbacks_.recordTraceClosestBatch == nullptr)
        {
            return Failure(
                GpuTraversalStatusCode::Unsupported,
                "The legacy analytic adapter has no closest-hit callback.");
        }
        const GpuTraversalStatus validation = ValidateTraceBatch(batch, true);
        return validation
            ? callbacks_.recordTraceClosestBatch(callbacks_.userData, batch)
            : validation;
    }

    GpuTraversalStatus LegacyAnalyticTraversalAdapter::RecordTraceAnyBatch(
        const GpuTraceBatch& batch)
    {
        if (callbacks_.recordTraceAnyBatch == nullptr)
        {
            return Failure(
                GpuTraversalStatusCode::Unsupported,
                "The legacy analytic adapter has no any-hit callback.");
        }
        const GpuTraversalStatus validation = ValidateTraceBatch(batch, true);
        return validation ? callbacks_.recordTraceAnyBatch(callbacks_.userData, batch) : validation;
    }

    GpuTraversalBackendMock::GpuTraversalBackendMock(
        const GpuTraversalMockPolicy policy) noexcept
        : policy_(policy)
    {
    }

    GpuTraversalBackendDescriptor GpuTraversalBackendMock::Descriptor() const noexcept
    {
        return {
            "abi-v1-traversal-mock",
            "ABI v1 traversal recording mock",
            true,
            true,
            false
        };
    }

    GpuTraversalStatus GpuTraversalBackendMock::BuildOrUpdateScene(
        const GpuSceneBuildRequest& request)
    {
        const GpuTraversalStatus validation = ValidateSceneRequest(
            request, policy_.requireVulkanHandles);
        if (!validation)
        {
            return validation;
        }
        sceneFingerprint_ = request.sceneFingerprint;
        sceneGeneration_ = request.sceneGeneration;
        calls_.push_back({
            MockTraversalOperation::BuildOrUpdateScene,
            sceneFingerprint_,
            sceneGeneration_,
            0u,
            0u,
            0u,
            request.topologyChanged
        });
        return {};
    }

    GpuTraversalStatus GpuTraversalBackendMock::RecordTraceClosestBatch(
        const GpuTraceBatch& batch)
    {
        return RecordTrace(MockTraversalOperation::TraceClosest, batch);
    }

    GpuTraversalStatus GpuTraversalBackendMock::RecordTraceAnyBatch(
        const GpuTraceBatch& batch)
    {
        return RecordTrace(MockTraversalOperation::TraceAny, batch);
    }

    std::span<const MockTraversalCall> GpuTraversalBackendMock::Calls() const noexcept
    {
        return calls_;
    }

    void GpuTraversalBackendMock::Reset() noexcept
    {
        calls_.clear();
        sceneFingerprint_ = 0u;
        sceneGeneration_ = 0u;
    }

    GpuTraversalStatus GpuTraversalBackendMock::RecordTrace(
        const MockTraversalOperation operation,
        const GpuTraceBatch& batch)
    {
        if (sceneFingerprint_ == 0u || sceneGeneration_ == 0u)
        {
            return Failure(
                GpuTraversalStatusCode::MissingScene,
                "BuildOrUpdateScene must precede traversal recording.");
        }
        const GpuTraversalStatus validation = ValidateTraceBatch(
            batch, policy_.requireVulkanHandles);
        if (!validation)
        {
            return validation;
        }
        if (batch.sceneFingerprint != sceneFingerprint_
            || batch.sceneGeneration != sceneGeneration_)
        {
            return Failure(
                GpuTraversalStatusCode::MissingScene,
                "Traversal batch scene identity does not match the built scene.");
        }
        calls_.push_back({
            operation,
            sceneFingerprint_,
            sceneGeneration_,
            batch.rayOffset,
            batch.hitOffset,
            batch.rayCount,
            false
        });
        return {};
    }

    FixedHitFixtureV1 BuildFixedHitFixtureV1() noexcept
    {
        using namespace Contracts;
        using namespace Contracts::AbiV1;
        using namespace Scene;

        FixedHitFixtureV1 fixture;
        fixture.sceneStableId = kWave1CanonicalTriangleStableId;
        fixture.sceneGeneration = kWave1CanonicalTriangleGeneration;
        fixture.trianglePositions = kWave1CanonicalTrianglePositions;
        fixture.triangleIndices = kWave1CanonicalTriangleIndices;
        fixture.triangleIdentity = { 0u, 0u, 0u, 0u };
        fixture.rays[0] = {
            { 0.0f, 0.0f, 2.0f, 0.001f },
            { 0.0f, 0.0f, -1.0f, 100.0f },
            { 0u, 0u, 0u, 0xffu },
            { 1u, 0u, 0u, 0x46495831u }
        };
        fixture.rays[1] = {
            { 2.0f, 2.0f, 2.0f, 0.001f },
            { 0.0f, 0.0f, -1.0f, 100.0f },
            { 1u, 1u, 0u, 0xffu },
            { 1u, 0u, 0u, 0x46495831u }
        };
        fixture.rays[2] = {
            { 0.0f, 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 1.0f },
            { 2u, 2u, 0u, 0xffu },
            { 1u, 0u, 0u, 0x46495831u }
        };

        fixture.hits[0] = {
            { 0.0f, 0.0f, 0.0f, 2.0f },
            { 0.0f, 0.0f, 1.0f, 0.30f },
            { 0.0f, 0.0f, 1.0f, 0.40f },
            { 0u, 0u, 0u, 0u },
            { 0u, AbiV0::HitKindTriangle, AbiV0::HitFlagFrontFace, 0u },
            { 0u, 0u, 0u, 0u }
        };
        fixture.hits[1] = {
            { 0.0f, 0.0f, 0.0f, 100.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { AbiV1::kInvalidId, AbiV1::kInvalidId, AbiV1::kInvalidId, AbiV1::kInvalidId },
            { 1u, AbiV0::HitKindMiss, AbiV0::HitFlagNone, 1u },
            { 0u, 0u, 0u, 0u }
        };
        fixture.hits[2] = {
            { 0.0f, 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f },
            { AbiV1::kInvalidId, AbiV1::kInvalidId, AbiV1::kInvalidId, AbiV1::kInvalidId },
            { 2u, AbiV0::HitKindInvalid, AbiV0::HitFlagNone, 2u },
            { 0u, 0u, 0u, 0u }
        };
        fixture.anyHit = {
            AnyHitExpectationV1::Occluded,
            AnyHitExpectationV1::Unoccluded,
            AnyHitExpectationV1::Invalid
        };
        return fixture;
    }

    bool ValidateFixedHitFixtureV1(
        const FixedHitFixtureV1& fixture,
        std::string& reason) noexcept
    {
        using namespace Contracts;
        using namespace Contracts::AbiV1;
        using namespace Scene;

        reason.clear();
        const auto sameFloat4 = [](const AbiFloat4& lhs, const AbiFloat4& rhs) noexcept
        {
            return lhs.x == rhs.x
                && lhs.y == rhs.y
                && lhs.z == rhs.z
                && lhs.w == rhs.w;
        };
        if (fixture.sceneStableId != kWave1CanonicalTriangleStableId
            || fixture.sceneGeneration != kWave1CanonicalTriangleGeneration
            || !sameFloat4(fixture.trianglePositions[0], kWave1CanonicalTrianglePositions[0])
            || !sameFloat4(fixture.trianglePositions[1], kWave1CanonicalTrianglePositions[1])
            || !sameFloat4(fixture.trianglePositions[2], kWave1CanonicalTrianglePositions[2])
            || fixture.triangleIndices != kWave1CanonicalTriangleIndices
            || fixture.triangleIdentity.x != 0u
            || fixture.triangleIdentity.y != 0u
            || fixture.triangleIdentity.z != 0u
            || fixture.triangleIdentity.w != 0u)
        {
            reason = "Fixed fixture triangle geometry or identity is invalid.";
            return false;
        }
        for (std::size_t index = 0; index < fixture.rays.size(); ++index)
        {
            const GpuRayQueueRecordV1& ray = fixture.rays[index];
            const GpuHitQueueRecordV1& hit = fixture.hits[index];
            const std::uint32_t stableIndex = static_cast<std::uint32_t>(index);
            if (ray.identity.x != stableIndex || hit.metadata.x != ray.identity.x
                || hit.metadata.w != ray.identity.y)
            {
                reason = "Fixed fixture ray/path identity is inconsistent.";
                return false;
            }
            if (!std::isfinite(ray.originTMin.w)
                || !std::isfinite(ray.directionTMax.w)
                || ray.originTMin.w > ray.directionTMax.w)
            {
                reason = "Fixed fixture ray interval is invalid.";
                return false;
            }
        }
        if (fixture.hits[0].metadata.y != AbiV0::HitKindTriangle
            || fixture.hits[0].ids.x == AbiV1::kInvalidId
            || fixture.hits[0].positionT.w <= fixture.rays[0].originTMin.w
            || fixture.hits[0].positionT.w >= fixture.rays[0].directionTMax.w
            || fixture.hits[0].geometricNormalBaryU.w != 0.30f
            || fixture.hits[0].shadingNormalBaryV.w != 0.40f
            || fixture.anyHit[0] != AnyHitExpectationV1::Occluded)
        {
            reason = "Fixed fixture triangle-hit encoding is invalid.";
            return false;
        }
        if (fixture.hits[1].metadata.y != AbiV0::HitKindMiss
            || fixture.hits[1].ids.x != AbiV1::kInvalidId
            || fixture.hits[1].positionT.w != fixture.rays[1].directionTMax.w
            || fixture.anyHit[1] != AnyHitExpectationV1::Unoccluded)
        {
            reason = "Fixed fixture miss encoding is invalid.";
            return false;
        }
        if (fixture.hits[2].metadata.y != AbiV0::HitKindInvalid
            || fixture.hits[2].ids.x != AbiV1::kInvalidId
            || fixture.rays[2].originTMin.w != fixture.rays[2].directionTMax.w
            || fixture.anyHit[2] != AnyHitExpectationV1::Invalid)
        {
            reason = "Fixed fixture invalid encoding is invalid.";
            return false;
        }
        return true;
    }
}
