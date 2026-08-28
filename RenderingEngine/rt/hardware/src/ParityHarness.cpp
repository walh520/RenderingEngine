#include "ParityHarness.hpp"

#include "contracts/AbiVersion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace RenderingEngine::Rt::Hardware
{
    namespace
    {
        namespace Abi = RenderingEngine::Contracts::AbiV0;

        [[nodiscard]] bool IsMiss(const GpuHitV0& hit) noexcept
        {
            return hit.metadata.y == Abi::HitKindMiss;
        }

        [[nodiscard]] bool IsFinite(const Abi::AbiFloat4& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] bool IsZero(const Abi::AbiFloat4& value) noexcept
        {
            return value.x == 0.0f && value.y == 0.0f && value.z == 0.0f && value.w == 0.0f;
        }

        [[nodiscard]] bool IsZero(const Abi::AbiUInt4& value) noexcept
        {
            return value.x == 0u && value.y == 0u && value.z == 0u && value.w == 0u;
        }

        [[nodiscard]] bool IsKnownKind(const std::uint32_t kind) noexcept
        {
            return kind == Abi::HitKindMiss || kind == Abi::HitKindTriangle ||
                kind == Abi::HitKindLegacyAnalytic;
        }

        [[nodiscard]] bool IsInvalid(const GpuHitV0& hit) noexcept
        {
            return hit.metadata.y == Abi::HitKindInvalid || !IsKnownKind(hit.metadata.y) ||
                !IsFinite(hit.positionT) || !IsFinite(hit.geometricNormalBaryU) ||
                !IsFinite(hit.shadingNormalBaryV);
        }

        [[nodiscard]] bool HasCanonicalMetadata(const GpuRayV0& ray, const GpuHitV0& hit) noexcept
        {
            constexpr std::uint32_t knownHitFlags = Abi::HitFlagFrontFace | Abi::HitFlagAlphaTested;
            return hit.metadata.x == ray.query.x && hit.metadata.w == 0u &&
                (hit.metadata.z & ~knownHitFlags) == 0u && IsZero(hit.reserved0);
        }

        [[nodiscard]] bool IsCanonicalMiss(const GpuRayV0& ray, const GpuHitV0& hit) noexcept
        {
            return IsMiss(hit) && HasCanonicalMetadata(ray, hit) &&
                hit.positionT.x == 0.0f && hit.positionT.y == 0.0f && hit.positionT.z == 0.0f &&
                hit.positionT.w == ray.directionTMax.w && IsZero(hit.geometricNormalBaryU) &&
                IsZero(hit.shadingNormalBaryV) &&
                hit.ids.x == Abi::kInvalidId && hit.ids.y == Abi::kInvalidId &&
                hit.ids.z == Abi::kInvalidId && hit.ids.w == Abi::kInvalidId &&
                hit.metadata.z == Abi::HitFlagNone;
        }

        [[nodiscard]] HitIdentity Identity(const GpuHitV0& hit) noexcept
        {
            return HitIdentity{hit.ids.x, hit.ids.y, hit.ids.z, hit.ids.w};
        }

        [[nodiscard]] bool SameIdentity(const HitIdentity& left, const HitIdentity& right) noexcept
        {
            return left.instanceId == right.instanceId && left.primitiveId == right.primitiveId &&
                left.geometryId == right.geometryId && left.materialId == right.materialId;
        }

        [[nodiscard]] const EquivalentHitSet* FindEquivalentSet(
            std::uint32_t rayId,
            std::span<const EquivalentHitSet> sets) noexcept
        {
            const auto iterator = std::find_if(sets.begin(), sets.end(), [rayId](const EquivalentHitSet& set)
            {
                return set.rayId == rayId;
            });
            return iterator == sets.end() ? nullptr : &*iterator;
        }

        [[nodiscard]] float RelativeError(float reference, float candidate) noexcept
        {
            const float denominator = std::max(std::abs(reference), 1.0e-7f);
            return std::abs(reference - candidate) / denominator;
        }

        [[nodiscard]] float Maximum3(float x, float y, float z) noexcept
        {
            return std::max(x, std::max(y, z));
        }

        [[nodiscard]] GpuRayV0 MakeRay(
            std::uint32_t id,
            float originX,
            float originY,
            float originZ,
            float directionX,
            float directionY,
            float directionZ,
            std::uint32_t flags = Abi::RayFlagNone) noexcept
        {
            const float length = std::sqrt(
                directionX * directionX + directionY * directionY + directionZ * directionZ);
            GpuRayV0 ray{};
            ray.originTMin = {originX, originY, originZ, 1.0e-4f};
            ray.directionTMax = {
                directionX / length, directionY / length, directionZ / length, 10000.0f};
            ray.query = {id, 0xffu, flags, 0u};
            return ray;
        }
    }

    bool ParityReport::Passed() const noexcept
    {
        return comparedRayCount == matchingRayCount && mismatches.empty();
    }

    std::vector<GpuRayV0> BuildFixedHardwareRayCorpus()
    {
        // Scene-independent deterministic directions. L2/L3 map the rays to
        // their canonical scene and publish reference hits; L5 never invents
        // expected geometry IDs in this private adapter.
        return {
            MakeRay(0u, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, -1.0f),
            MakeRay(1u, 0.0f, 0.0f, 3.0f, 0.25f, 0.0f, -1.0f),
            MakeRay(2u, 0.0f, 0.0f, 3.0f, -0.25f, 0.0f, -1.0f),
            MakeRay(3u, 0.0f, 0.0f, 3.0f, 0.0f, 0.25f, -1.0f),
            MakeRay(4u, 0.0f, 0.0f, 3.0f, 0.0f, -0.25f, -1.0f),
            MakeRay(5u, 1.0f, 1.0f, 3.0f, -1.0f, -1.0f, -3.0f),
            MakeRay(6u, -1.0f, 1.0f, 3.0f, 1.0f, -1.0f, -3.0f),
            MakeRay(7u, 1.0f, -1.0f, 3.0f, -1.0f, 1.0f, -3.0f),
            MakeRay(8u, -1.0f, -1.0f, 3.0f, 1.0f, 1.0f, -3.0f),
            MakeRay(9u, 0.0f, 0.0f, -3.0f, 0.0f, 0.0f, 1.0f, Abi::RayFlagCullBackFace),
            MakeRay(10u, 0.0f, 0.0f, -3.0f, 0.0f, 0.0f, 1.0f, Abi::RayFlagCullFrontFace),
            MakeRay(11u, 0.0f, 0.0f, 3.0f, 1.0e-7f, 0.0f, -1.0f),
            MakeRay(12u, 0.0f, 0.0f, 3.0f, -1.0e-7f, 0.0f, -1.0f),
            MakeRay(13u, 0.0f, 0.0f, 3.0f, 0.0f, 1.0e-7f, -1.0f),
            MakeRay(14u, 0.0f, 0.0f, 3.0f, 0.0f, -1.0e-7f, -1.0f),
            MakeRay(15u, 1000.0f, 1000.0f, 1000.0f, 1.0f, 0.0f, 0.0f)};
    }

    ParityReport CompareHitCorpus(
        std::span<const GpuRayV0> rays,
        std::span<const GpuHitV0> referenceHits,
        std::span<const GpuHitV0> hardwareHits,
        std::span<const EquivalentHitSet> equivalentHitSets,
        const ParityTolerance& tolerance)
    {
        ParityReport report{};
        const std::size_t count = std::min(rays.size(), std::min(referenceHits.size(), hardwareHits.size()));
        report.comparedRayCount = count;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const GpuRayV0& ray = rays[index];
            const GpuHitV0& reference = referenceHits[index];
            const GpuHitV0& hardware = hardwareHits[index];
            const auto addMismatch = [&](HitMismatchKind kind, float error = 0.0f)
            {
                report.mismatches.push_back(HitMismatch{
                    static_cast<std::uint32_t>(index), ray.query.x, kind, error});
            };

            if (IsInvalid(reference) || IsInvalid(hardware))
            {
                addMismatch(HitMismatchKind::Invalid);
                continue;
            }
            if (IsMiss(reference) != IsMiss(hardware))
            {
                addMismatch(HitMismatchKind::HitMiss);
                continue;
            }
            if (IsMiss(reference))
            {
                if (!IsCanonicalMiss(ray, reference) || !IsCanonicalMiss(ray, hardware))
                {
                    addMismatch(HitMismatchKind::MissEncoding);
                    continue;
                }
                ++report.matchingRayCount;
                continue;
            }
            if (!HasCanonicalMetadata(ray, reference) || !HasCanonicalMetadata(ray, hardware))
            {
                addMismatch(HitMismatchKind::Invalid);
                continue;
            }
            if (reference.metadata.y != hardware.metadata.y)
            {
                addMismatch(HitMismatchKind::HitKind);
                continue;
            }

            const HitIdentity referenceIdentity = Identity(reference);
            const HitIdentity hardwareIdentity = Identity(hardware);
            bool identityMatches = SameIdentity(referenceIdentity, hardwareIdentity);
            if (!identityMatches)
            {
                if (const EquivalentHitSet* equivalents = FindEquivalentSet(ray.query.x, equivalentHitSets))
                {
                    identityMatches = std::any_of(
                        equivalents->acceptedIdentities.begin(), equivalents->acceptedIdentities.end(),
                        [&hardwareIdentity](const HitIdentity& accepted)
                        {
                            return SameIdentity(hardwareIdentity, accepted);
                        });
                    report.equivalentIdentityCount += identityMatches ? 1u : 0u;
                }
            }
            if (!identityMatches)
            {
                addMismatch(HitMismatchKind::Identity);
                continue;
            }

            const float tError = RelativeError(reference.positionT.w, hardware.positionT.w);
            report.maximumRelativeTError = std::max(report.maximumRelativeTError, tError);
            if (tError > tolerance.maximumRelativeTError)
            {
                addMismatch(HitMismatchKind::Distance, tError);
                continue;
            }
            const float barycentricError = std::max(
                std::abs(reference.geometricNormalBaryU.w - hardware.geometricNormalBaryU.w),
                std::abs(reference.shadingNormalBaryV.w - hardware.shadingNormalBaryV.w));
            report.maximumAbsoluteBarycentricError = std::max(
                report.maximumAbsoluteBarycentricError, barycentricError);
            if (barycentricError > tolerance.maximumAbsoluteBarycentricError)
            {
                addMismatch(HitMismatchKind::Barycentric, barycentricError);
                continue;
            }
            const bool referenceFrontFace = (reference.metadata.z & Abi::HitFlagFrontFace) != 0u;
            const bool hardwareFrontFace = (hardware.metadata.z & Abi::HitFlagFrontFace) != 0u;
            if (referenceFrontFace != hardwareFrontFace)
            {
                addMismatch(HitMismatchKind::FrontFace);
                continue;
            }
            if (reference.metadata.z != hardware.metadata.z)
            {
                addMismatch(HitMismatchKind::Flags);
                continue;
            }
            const float normalError = std::max(
                Maximum3(
                    std::abs(reference.geometricNormalBaryU.x - hardware.geometricNormalBaryU.x),
                    std::abs(reference.geometricNormalBaryU.y - hardware.geometricNormalBaryU.y),
                    std::abs(reference.geometricNormalBaryU.z - hardware.geometricNormalBaryU.z)),
                Maximum3(
                    std::abs(reference.shadingNormalBaryV.x - hardware.shadingNormalBaryV.x),
                    std::abs(reference.shadingNormalBaryV.y - hardware.shadingNormalBaryV.y),
                    std::abs(reference.shadingNormalBaryV.z - hardware.shadingNormalBaryV.z)));
            report.maximumAbsoluteNormalError = std::max(report.maximumAbsoluteNormalError, normalError);
            if (normalError > tolerance.maximumAbsoluteNormalError)
            {
                addMismatch(HitMismatchKind::Normal, normalError);
                continue;
            }
            ++report.matchingRayCount;
        }

        if (rays.size() != referenceHits.size() || rays.size() != hardwareHits.size())
        {
            report.mismatches.push_back(HitMismatch{
                static_cast<std::uint32_t>(count), 0xffffffffu, HitMismatchKind::Invalid, 0.0f});
        }
        return report;
    }

    Status ExecuteAndCompareClosest(
        IHardwareRayCorpusExecutor& executor,
        std::span<const GpuRayV0> rays,
        std::span<const GpuHitV0> referenceHits,
        std::span<const EquivalentHitSet> equivalentHitSets,
        ParityReport& output,
        const ParityTolerance& tolerance)
    {
        if (rays.size() != referenceHits.size())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Ray/reference corpus size mismatch");
        }
        std::vector<GpuHitV0> hardwareHits(rays.size());
        Status status = executor.ExecuteClosest(rays, hardwareHits);
        if (!status)
        {
            return status;
        }
        output = CompareHitCorpus(rays, referenceHits, hardwareHits, equivalentHitSets, tolerance);
        return Status::Success();
    }

    Status ExecuteAndCompareAny(
        IHardwareRayCorpusExecutor& executor,
        std::span<const GpuRayV0> rays,
        std::span<const GpuHitV0> referenceHits,
        std::span<const EquivalentHitSet> equivalentHitSets,
        ParityReport& output,
        const ParityTolerance& tolerance)
    {
        if (rays.size() != referenceHits.size())
        {
            return Status::Failure(VK_ERROR_INITIALIZATION_FAILED, "Ray/reference corpus size mismatch");
        }
        std::vector<GpuHitV0> hardwareHits(rays.size());
        Status status = executor.ExecuteAny(rays, hardwareHits);
        if (!status)
        {
            return status;
        }
        output = CompareHitCorpus(rays, referenceHits, hardwareHits, equivalentHitSets, tolerance);
        return Status::Success();
    }
}
