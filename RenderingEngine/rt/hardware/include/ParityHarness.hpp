#pragma once

#include "HardwareRtStatus.hpp"

#include "contracts/RayHitAbiV0.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace RenderingEngine::Rt::Hardware
{
    using RenderingEngine::Contracts::AbiV0::GpuHitV0;
    using RenderingEngine::Contracts::AbiV0::GpuRayV0;

    struct HitIdentity final
    {
        std::uint32_t instanceId{0xffffffffu};
        std::uint32_t primitiveId{0xffffffffu};
        std::uint32_t geometryId{0xffffffffu};
        std::uint32_t materialId{0xffffffffu};
    };

    struct EquivalentHitSet final
    {
        std::uint32_t rayId{0u};
        std::vector<HitIdentity> acceptedIdentities;
    };

    struct ParityTolerance final
    {
        float maximumRelativeTError{1.0e-4f};
        float maximumAbsoluteBarycentricError{1.0e-4f};
        float maximumAbsoluteNormalError{2.0e-4f};
    };

    enum class HitMismatchKind : std::uint8_t
    {
        HitMiss,
        Invalid,
        MissEncoding,
        HitKind,
        Identity,
        Distance,
        Barycentric,
        FrontFace,
        Flags,
        Normal
    };

    struct HitMismatch final
    {
        std::uint32_t corpusIndex{0u};
        std::uint32_t rayId{0u};
        HitMismatchKind kind{HitMismatchKind::Invalid};
        float measuredError{0.0f};
    };

    struct ParityReport final
    {
        std::uint64_t comparedRayCount{0u};
        std::uint64_t matchingRayCount{0u};
        std::uint64_t equivalentIdentityCount{0u};
        float maximumRelativeTError{0.0f};
        float maximumAbsoluteBarycentricError{0.0f};
        float maximumAbsoluteNormalError{0.0f};
        std::vector<HitMismatch> mismatches;

        [[nodiscard]] bool Passed() const noexcept;
    };

    class IHardwareRayCorpusExecutor
    {
    public:
        virtual ~IHardwareRayCorpusExecutor() = default;
        [[nodiscard]] virtual Status ExecuteClosest(
            std::span<const GpuRayV0> rays,
            std::span<GpuHitV0> outputHits) = 0;
        [[nodiscard]] virtual Status ExecuteAny(
            std::span<const GpuRayV0> rays,
            std::span<GpuHitV0> outputHits) = 0;
    };

    [[nodiscard]] std::vector<GpuRayV0> BuildFixedHardwareRayCorpus();

    [[nodiscard]] ParityReport CompareHitCorpus(
        std::span<const GpuRayV0> rays,
        std::span<const GpuHitV0> referenceHits,
        std::span<const GpuHitV0> hardwareHits,
        std::span<const EquivalentHitSet> equivalentHitSets = {},
        const ParityTolerance& tolerance = {});

    [[nodiscard]] Status ExecuteAndCompareClosest(
        IHardwareRayCorpusExecutor& executor,
        std::span<const GpuRayV0> rays,
        std::span<const GpuHitV0> referenceHits,
        std::span<const EquivalentHitSet> equivalentHitSets,
        ParityReport& output,
        const ParityTolerance& tolerance = {});

    [[nodiscard]] Status ExecuteAndCompareAny(
        IHardwareRayCorpusExecutor& executor,
        std::span<const GpuRayV0> rays,
        std::span<const GpuHitV0> referenceHits,
        std::span<const EquivalentHitSet> equivalentHitSets,
        ParityReport& output,
        const ParityTolerance& tolerance = {});
}
