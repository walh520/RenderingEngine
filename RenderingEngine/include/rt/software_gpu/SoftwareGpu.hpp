#pragma once

#include "rt/cpu/Bvh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace RenderingEngine::Rt::SoftwareGpu
{
    // L4-private/provisional records. These are deliberately not shared ABI
    // declarations; L0 can replace the adapter when abi-v1 is published.
    inline constexpr std::uint32_t kPrivateLayoutVersion = 0x4c340001u;
    inline constexpr std::uint32_t kInvalidIndex = 0xffffffffu;
    inline constexpr std::uint32_t kMaximumTraversalStack = 128u;

    struct Float3
    {
        float x{};
        float y{};
        float z{};

        [[nodiscard]] friend constexpr bool operator==(const Float3&, const Float3&) noexcept =
            default;
    };

    struct alignas(16) Float4
    {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    struct alignas(16) Uint4
    {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t z{};
        std::uint32_t w{};
    };

    // boundsMin.xyz / boundsMax.xyz hold the node AABB. For a leaf, links.x is
    // the first reordered primitive and links.y is the count. For an interior
    // node links.x/z are the left/right children and links.y is zero. links.w
    // is the parent or kInvalidIndex for the root.
    struct alignas(16) SoftwareNodeRecord
    {
        Float4 boundsMin{};
        Float4 boundsMax{};
        Uint4 links{kInvalidIndex, 0u, kInvalidIndex, kInvalidIndex};

        [[nodiscard]] constexpr bool IsLeaf() const noexcept
        {
            return links.y != 0u;
        }
    };

    struct alignas(16) SoftwarePrimitiveRecord
    {
        Float4 v0{};
        Float4 v1{};
        Float4 v2{};
        Uint4 identity{kInvalidIndex, 0u, 0u, 0u};
    };

    struct alignas(16) SoftwareRayRecord
    {
        Float4 originTMin{};
        Float4 directionTMax{0.0f, 0.0f, -1.0f, 1.0e30f};
        Uint4 query{}; // ray ID, QueryMode value, visibility mask, reserved
    };

    struct alignas(16) SoftwareHitRecord
    {
        Float4 tBary{}; // t, barycentric u, barycentric v, front-face (0/1)
        Uint4 identity{kInvalidIndex, kInvalidIndex, 0u, 0u}; // primitive, ray, status, reserved
    };

    static_assert(sizeof(Float4) == 16u);
    static_assert(sizeof(Uint4) == 16u);
    static_assert(sizeof(SoftwareNodeRecord) == 48u);
    static_assert(sizeof(SoftwarePrimitiveRecord) == 64u);
    static_assert(sizeof(SoftwareRayRecord) == 48u);
    static_assert(sizeof(SoftwareHitRecord) == 32u);
    static_assert(alignof(SoftwareNodeRecord) == 16u);
    static_assert(alignof(SoftwarePrimitiveRecord) == 16u);

    struct Aabb
    {
        Float3 minimum{};
        Float3 maximum{};

        [[nodiscard]] bool IsValid() const noexcept;
    };

    struct CpuSahNodeInput
    {
        Aabb bounds{};
        std::uint32_t leftChild{kInvalidIndex};
        std::uint32_t rightChild{kInvalidIndex};
        std::uint32_t firstPrimitive{};
        std::uint32_t primitiveCount{};

        [[nodiscard]] constexpr bool IsLeaf() const noexcept
        {
            return primitiveCount != 0u;
        }
    };

    struct CpuSahTree
    {
        std::vector<SoftwarePrimitiveRecord> sourcePrimitives{};
        std::vector<CpuSahNodeInput> nodes{};
        std::vector<std::uint32_t> primitiveOrder{};
        std::uint32_t maximumDepth{};
    };

    enum class BuildStatus : std::uint32_t
    {
        Success = 0u,
        InvalidInput,
        DuplicatePrimitiveId,
        MalformedHierarchy,
        DepthOverflow,
    };

    enum class BuildKind : std::uint32_t
    {
        FlattenedCpuSah = 0u,
        KarrasLbvh = 1u,
    };

    struct BuildOptions
    {
        std::uint32_t maximumLeafSize{4u};
        std::uint32_t maximumDepth{kMaximumTraversalStack};
    };

    struct SahBuildResult
    {
        BuildStatus status{BuildStatus::Success};
        std::string message{};
        CpuSahTree tree{};

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return status == BuildStatus::Success;
        }
    };

    struct MortonPair
    {
        std::uint32_t code{};
        std::uint32_t stablePrimitiveId{kInvalidIndex};
        std::uint32_t sourceIndex{kInvalidIndex};
        std::uint32_t reserved{};

        [[nodiscard]] friend constexpr bool operator==(
            const MortonPair&,
            const MortonPair&) noexcept = default;
    };

    struct FlatBvh
    {
        std::vector<SoftwareNodeRecord> nodes{};
        std::vector<SoftwarePrimitiveRecord> primitives{};
        std::vector<MortonPair> mortonOrder{};
        BuildKind kind{BuildKind::FlattenedCpuSah};
        std::uint32_t maximumDepth{};

        [[nodiscard]] std::size_t MemoryBytes() const noexcept;
    };

    struct FlatBuildResult
    {
        BuildStatus status{BuildStatus::Success};
        std::string message{};
        FlatBvh bvh{};

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return status == BuildStatus::Success;
        }
    };

    enum class TraceStatus : std::uint32_t
    {
        Success = 0u,
        InvalidInput,
        StackOverflow,
        MalformedBvh,
    };

    enum class QueryMode : std::uint32_t
    {
        Closest = 0u,
        Any = 1u,
    };

    // Private readback layout shared with software_trace.hlsl. Additive counters
    // saturate at UINT32_MAX in the shader; maximum counters use InterlockedMax.
    enum class GpuTraversalCounterSlot : std::uint32_t
    {
        NodeTests = 0u,
        TriangleTests = 1u,
        StackOverflows = 2u,
        InvalidRays = 3u,
        InvalidHits = 4u,
        MaximumStackDepth = 5u,
        LeafVisits = 6u,
        AccumulatedLeafPrimitives = 7u,
        MaximumLeafOccupancy = 8u,
        Count = 9u,
    };

    inline constexpr std::size_t kGpuTraversalCounterCount =
        static_cast<std::size_t>(GpuTraversalCounterSlot::Count);
    inline constexpr std::uint32_t kGpuTraversalCounterSaturated = 0xffffffffu;

    struct GpuTraversalCounterReadback
    {
        std::array<std::uint32_t, kGpuTraversalCounterCount> slots{};

        [[nodiscard]] constexpr std::uint32_t operator[](
            const GpuTraversalCounterSlot slot) const noexcept
        {
            return slots[static_cast<std::size_t>(slot)];
        }
    };

    static_assert(sizeof(GpuTraversalCounterReadback) == 9u * sizeof(std::uint32_t));

    struct TraversalCounters
    {
        std::uint64_t nodeTests{};
        std::uint64_t triangleTests{};
        std::uint64_t rays{};
        std::uint64_t hits{};
        std::uint64_t stackOverflows{};
        std::uint64_t invalidRays{};
        std::uint64_t invalidHits{};
        std::uint64_t leafVisits{};
        std::uint64_t accumulatedLeafPrimitives{};
        std::uint32_t maximumStackDepth{};
        std::uint32_t maximumLeafOccupancy{};

        void Accumulate(const TraversalCounters& other) noexcept;
    };

    [[nodiscard]] TraversalCounters DecodeGpuTraversalCounters(
        const GpuTraversalCounterReadback& readback,
        std::uint64_t rayCount = 0u,
        std::uint64_t hitCount = 0u) noexcept;

    struct TraceResult
    {
        TraceStatus status{TraceStatus::Success};
        SoftwareHitRecord hit{};
        TraversalCounters counters{};

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return status == TraceStatus::Success;
        }

        [[nodiscard]] constexpr bool IsHit() const noexcept
        {
            return hit.identity.x != kInvalidIndex;
        }
    };

    struct BatchTraceResult
    {
        std::vector<TraceResult> rays{};
        TraversalCounters counters{};
    };

    enum class MeasurementDomain : std::uint32_t
    {
        CpuMirror = 0u,
        VulkanGpuTimestamp = 1u,
    };

    struct GpuBenchmarkReportInput
    {
        bool measured{};
        double buildGpuMilliseconds{};
        double traceGpuMilliseconds{};
        std::uint64_t uploadBytes{};
        std::uint64_t readbackBytes{};
        std::uint64_t tracedRays{};
    };

    struct BenchmarkPathResult
    {
        MeasurementDomain measurementDomain{MeasurementDomain::CpuMirror};
        bool gpuMeasured{};
        BuildStatus buildStatus{BuildStatus::Success};
        double buildMilliseconds{};
        double traceMilliseconds{};
        double millionRaysPerSecond{};
        std::size_t memoryBytes{};
        std::uint64_t resultChecksum{};
        TraversalCounters counters{};
    };

    struct BenchmarkResult
    {
        BenchmarkPathResult flattenedSah{};
        BenchmarkPathResult lbvh{};
        std::uint32_t iterations{};
        std::size_t rayCount{};
        std::size_t primitiveCount{};
    };

    [[nodiscard]] SoftwarePrimitiveRecord MakeTriangle(
        Float3 v0,
        Float3 v1,
        Float3 v2,
        std::uint32_t stablePrimitiveId) noexcept;

    [[nodiscard]] SoftwareRayRecord MakeRay(
        Float3 origin,
        Float3 direction,
        float tMin,
        float tMax,
        std::uint32_t rayId = 0u,
        QueryMode mode = QueryMode::Closest) noexcept;

    [[nodiscard]] Aabb PrimitiveBounds(const SoftwarePrimitiveRecord& primitive) noexcept;
    [[nodiscard]] std::uint32_t MortonCode(Float3 centroid, const Aabb& sceneBounds) noexcept;

    [[nodiscard]] std::vector<MortonPair> StableRadixSortMorton(
        std::span<const SoftwarePrimitiveRecord> primitives,
        BuildStatus* outStatus = nullptr);

    [[nodiscard]] SahBuildResult BuildPrivateBinnedSah(
        std::span<const SoftwarePrimitiveRecord> primitives,
        BuildOptions options = {});

    [[nodiscard]] FlatBuildResult FlattenCpuSah(const CpuSahTree& tree);

    // Adapt the already-built L3 binned-SAH topology without rebuilding it.
    // sourcePrimitives must be in the exact input order used to construct
    // cpuBvh. The resulting records remain L4-private; this seam only
    // transfers L3 topology/order/bounds into the private flattened storage.
    [[nodiscard]] FlatBuildResult FlattenCanonicalL3BinnedSah(
        const Cpu::Bvh<float>& cpuBvh,
        std::span<const SoftwarePrimitiveRecord> sourcePrimitives);

    [[nodiscard]] FlatBuildResult BuildFlattenedSah(
        std::span<const SoftwarePrimitiveRecord> primitives,
        BuildOptions options = {});

    [[nodiscard]] FlatBuildResult BuildKarrasLbvh(
        std::span<const SoftwarePrimitiveRecord> primitives,
        std::uint32_t maximumDepth = kMaximumTraversalStack);

    [[nodiscard]] BuildStatus RefitBottomUp(FlatBvh& bvh) noexcept;

    [[nodiscard]] TraceResult Trace(
        const FlatBvh& bvh,
        const SoftwareRayRecord& ray,
        QueryMode mode,
        std::uint32_t stackCapacity = 64u) noexcept;

    [[nodiscard]] TraceResult BruteForce(
        std::span<const SoftwarePrimitiveRecord> primitives,
        const SoftwareRayRecord& ray,
        QueryMode mode) noexcept;

    [[nodiscard]] BatchTraceResult TraceBatch(
        const FlatBvh& bvh,
        std::span<const SoftwareRayRecord> rays,
        QueryMode mode,
        std::uint32_t stackCapacity = 64u) noexcept;

    [[nodiscard]] BenchmarkResult RunCpuMirrorBenchmark(
        std::span<const SoftwarePrimitiveRecord> primitives,
        std::span<const SoftwareRayRecord> rays,
        std::uint32_t iterations,
        BuildOptions sahOptions = {});
}
