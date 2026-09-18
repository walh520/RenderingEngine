#pragma once

#include <cstdint>
#include <limits>

namespace RenderingEngine::Restir
{
    inline constexpr std::uint32_t kInvalidStableId = 0xffffffffu;

    struct Float3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    [[nodiscard]] constexpr Float3 operator+(const Float3 a, const Float3 b) noexcept
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    [[nodiscard]] constexpr Float3 operator-(const Float3 a, const Float3 b) noexcept
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    [[nodiscard]] constexpr Float3 operator*(const Float3 value, const float scale) noexcept
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    [[nodiscard]] constexpr Float3 operator*(const float scale, const Float3 value) noexcept
    {
        return value * scale;
    }

    [[nodiscard]] constexpr Float3 operator/(const Float3 value, const float scale) noexcept
    {
        return { value.x / scale, value.y / scale, value.z / scale };
    }

    [[nodiscard]] constexpr float Dot(const Float3 a, const Float3 b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    [[nodiscard]] constexpr Float3 Cross(const Float3 a, const Float3 b) noexcept
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    enum class CandidateSource : std::uint32_t
    {
        UniformLight = 0u,
        PowerWeightedLight = 1u,
        EmissiveTriangle = 2u,
        Environment = 3u,
        ReusedTemporal = 4u,
        ReusedSpatial = 5u,
        Invalid = 0xffffffffu
    };

    enum class EstimatorMode : std::uint32_t
    {
        Biased = 0u,
        ReferenceCorrection = 1u
    };

    enum ReservoirFlags : std::uint32_t
    {
        ReservoirFlagNone = 0u,
        ReservoirFlagValid = 1u << 0u,
        ReservoirFlagMClamped = 1u << 1u,
        ReservoirFlagTemporalAccepted = 1u << 2u,
        ReservoirFlagSpatialAccepted = 1u << 3u,
        ReservoirFlagReferenceMode = 1u << 4u,
        ReservoirFlagFinalVisibilityEvaluated = 1u << 5u
    };

    enum class RejectionReason : std::uint32_t
    {
        None = 0u,
        EmptyReservoir = 1u << 0u,
        ReprojectionOutside = 1u << 1u,
        MotionInvalid = 1u << 2u,
        CameraCut = 1u << 3u,
        Resize = 1u << 4u,
        DepthMismatch = 1u << 5u,
        NormalMismatch = 1u << 6u,
        InstanceMismatch = 1u << 7u,
        ThinGeometryMismatch = 1u << 8u,
        SceneGenerationMismatch = 1u << 9u,
        LightGenerationMismatch = 1u << 10u,
        LightDeleted = 1u << 11u,
        HistoryExpired = 1u << 12u,
        InvalidCandidate = 1u << 13u,
        MaterialMismatch = 1u << 14u,
        PositionMismatch = 1u << 15u
    };

    [[nodiscard]] constexpr RejectionReason operator|(const RejectionReason a, const RejectionReason b) noexcept
    {
        return static_cast<RejectionReason>(
            static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    constexpr RejectionReason& operator|=(RejectionReason& a, const RejectionReason b) noexcept
    {
        a = a | b;
        return a;
    }

    [[nodiscard]] constexpr bool HasReason(const RejectionReason value, const RejectionReason reason) noexcept
    {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(reason)) != 0u;
    }

    struct CandidateIdentity
    {
        CandidateSource source = CandidateSource::Invalid;
        std::uint32_t lightId = kInvalidStableId;
        std::uint32_t primitiveId = kInvalidStableId;
        std::uint32_t sampleId = 0u;
        std::uint32_t generation = 0u;
    };

    struct Candidate
    {
        CandidateIdentity identity{};
        // Original proposal family stays in identity.source. Reuse provenance
        // is separate so temporal/spatial remapping never destroys identity.
        CandidateSource reuseSource = CandidateSource::Invalid;
        Float3 directionToLight{};
        float distance = std::numeric_limits<float>::max();
        Float3 unshadowedContribution{};
        float target = 0.0f;
        float proposalPdf = 0.0f;
        float support = 0.0f;
        float correction = 1.0f;
        // Zero is the current pixel. Temporal/spatial source reservoirs use
        // stable non-zero indices so reference correction can identify the
        // surface on which the selected representative originated.
        std::uint32_t sourceSurfaceIndex = 0u;
    };

    struct Reservoir
    {
        Candidate selected{};
        double weightSum = 0.0;
        std::uint32_t M = 0u;
        std::uint32_t age = 0u;
        float normalizationWeight = 0.0f;
        std::uint32_t flags = ReservoirFlagNone;
    };

    struct SurfaceRecord
    {
        Float3 position{};
        Float3 normal{ 0.0f, 1.0f, 0.0f };
        float linearDepth = 0.0f;
        std::uint32_t instanceId = kInvalidStableId;
        std::uint32_t primitiveId = kInvalidStableId;
        std::uint32_t materialId = kInvalidStableId;
        std::uint32_t sceneGeneration = 0u;
        bool thinGeometry = false;
    };

    struct ReservoirStatistics
    {
        std::uint64_t candidatesSeen = 0u;
        std::uint64_t candidatesRejected = 0u;
        std::uint64_t zeroTargetCandidates = 0u;
        std::uint64_t zeroProposalCandidates = 0u;
        std::uint64_t zeroSupportCandidates = 0u;
        std::uint64_t temporalAccepted = 0u;
        std::uint64_t temporalRejected = 0u;
        std::uint64_t spatialAccepted = 0u;
        std::uint64_t spatialRejected = 0u;
        std::uint64_t referenceVisibilityRays = 0u;
        std::uint64_t finalVisibilityRays = 0u;
        std::uint64_t duplicateFinalVisibilityRequests = 0u;
        std::uint64_t mClampEvents = 0u;
    };

    struct ReservoirDebugRecord
    {
        CandidateIdentity selectedIdentity{};
        CandidateSource reuseSource = CandidateSource::Invalid;
        std::uint32_t M = 0u;
        std::uint32_t age = 0u;
        std::uint32_t flags = 0u;
        RejectionReason temporalRejection = RejectionReason::None;
        RejectionReason spatialRejection = RejectionReason::None;
        float weightSum = 0.0f;
        float target = 0.0f;
        float proposalPdf = 0.0f;
        float correction = 0.0f;
        float normalizationWeight = 0.0f;
        std::uint32_t referenceVisibilityRays = 0u;
        std::uint32_t finalVisibilityRays = 0u;
        std::uint32_t invalidCandidates = 0u;
        std::uint32_t zeroTargetCandidates = 0u;
        std::uint32_t zeroProposalCandidates = 0u;
        std::uint32_t zeroSupportCandidates = 0u;
        std::uint32_t mClampEvents = 0u;
    };
}
