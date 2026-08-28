#include "rt/software_gpu/SoftwareGpu.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

namespace RenderingEngine::Rt::SoftwareGpu
{
    namespace
    {
        constexpr std::size_t kSahBinCount = 16u;

        [[nodiscard]] constexpr Float3 Subtract(const Float3 a, const Float3 b) noexcept
        {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        [[nodiscard]] constexpr float Dot(const Float3 a, const Float3 b) noexcept
        {
            return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
        }

        [[nodiscard]] constexpr Float3 Cross(const Float3 a, const Float3 b) noexcept
        {
            return {
                (a.y * b.z) - (a.z * b.y),
                (a.z * b.x) - (a.x * b.z),
                (a.x * b.y) - (a.y * b.x)};
        }

        [[nodiscard]] bool IsFinite(const Float3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] constexpr Float3 Minimum(const Float3 a, const Float3 b) noexcept
        {
            return {
                a.x < b.x ? a.x : b.x,
                a.y < b.y ? a.y : b.y,
                a.z < b.z ? a.z : b.z};
        }

        [[nodiscard]] constexpr Float3 Maximum(const Float3 a, const Float3 b) noexcept
        {
            return {
                a.x > b.x ? a.x : b.x,
                a.y > b.y ? a.y : b.y,
                a.z > b.z ? a.z : b.z};
        }

        [[nodiscard]] constexpr float Axis(const Float3 value, const std::uint32_t axis) noexcept
        {
            return axis == 0u ? value.x : (axis == 1u ? value.y : value.z);
        }

        [[nodiscard]] Aabb EmptyBounds() noexcept
        {
            const float infinity = (std::numeric_limits<float>::infinity)();
            return {{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
        }

        void Expand(Aabb& bounds, const Float3 point) noexcept
        {
            if (!bounds.IsValid())
            {
                bounds = {point, point};
                return;
            }
            bounds.minimum = Minimum(bounds.minimum, point);
            bounds.maximum = Maximum(bounds.maximum, point);
        }

        void Expand(Aabb& bounds, const Aabb other) noexcept
        {
            if (!other.IsValid())
            {
                return;
            }
            if (!bounds.IsValid())
            {
                bounds = other;
                return;
            }
            bounds.minimum = Minimum(bounds.minimum, other.minimum);
            bounds.maximum = Maximum(bounds.maximum, other.maximum);
        }

        [[nodiscard]] constexpr Float3 Centroid(const Aabb bounds) noexcept
        {
            return {
                std::midpoint(bounds.minimum.x, bounds.maximum.x),
                std::midpoint(bounds.minimum.y, bounds.maximum.y),
                std::midpoint(bounds.minimum.z, bounds.maximum.z)};
        }

        [[nodiscard]] constexpr Float3 Extent(const Aabb bounds) noexcept
        {
            return Subtract(bounds.maximum, bounds.minimum);
        }

        [[nodiscard]] float SurfaceArea(const Aabb bounds) noexcept
        {
            if (!bounds.IsValid())
            {
                return 0.0f;
            }
            const Float3 extent = Extent(bounds);
            return 2.0f * ((extent.x * extent.y) + (extent.y * extent.z) +
                           (extent.z * extent.x));
        }

        [[nodiscard]] bool Contains(const Aabb outer, const Aabb inner) noexcept
        {
            if (!outer.IsValid() || !inner.IsValid())
            {
                return false;
            }
            const Float3 extent = Extent(outer);
            const float scale = (std::max)({1.0f, extent.x, extent.y, extent.z});
            const float tolerance = scale * 1.0e-5f;
            return inner.minimum.x >= outer.minimum.x - tolerance &&
                   inner.minimum.y >= outer.minimum.y - tolerance &&
                   inner.minimum.z >= outer.minimum.z - tolerance &&
                   inner.maximum.x <= outer.maximum.x + tolerance &&
                   inner.maximum.y <= outer.maximum.y + tolerance &&
                   inner.maximum.z <= outer.maximum.z + tolerance;
        }

        [[nodiscard]] Float3 Vertex(const SoftwarePrimitiveRecord& primitive, const int index) noexcept
        {
            const Float4* value = index == 0 ? &primitive.v0 : (index == 1 ? &primitive.v1 : &primitive.v2);
            return {value->x, value->y, value->z};
        }

        [[nodiscard]] bool IsPrimitiveValid(const SoftwarePrimitiveRecord& primitive) noexcept
        {
            const Float3 v0 = Vertex(primitive, 0);
            const Float3 v1 = Vertex(primitive, 1);
            const Float3 v2 = Vertex(primitive, 2);
            if (primitive.identity.x == kInvalidIndex || !IsFinite(v0) || !IsFinite(v1) ||
                !IsFinite(v2))
            {
                return false;
            }
            const Float3 edge0 = Subtract(v1, v0);
            const Float3 edge1 = Subtract(v2, v0);
            const Float3 cross = Cross(edge0, edge1);
            const float areaSquared = Dot(cross, cross);
            const float edgeScale = (std::max)(Dot(edge0, edge0), Dot(edge1, edge1));
            return std::isfinite(areaSquared) && areaSquared > edgeScale * edgeScale * 1.0e-14f;
        }

        [[nodiscard]] Aabb ComputeSceneBounds(
            const std::span<const SoftwarePrimitiveRecord> primitives) noexcept
        {
            Aabb result = EmptyBounds();
            for (const SoftwarePrimitiveRecord& primitive : primitives)
            {
                Expand(result, PrimitiveBounds(primitive));
            }
            return result;
        }

        [[nodiscard]] bool HasUniqueIds(
            const std::span<const SoftwarePrimitiveRecord> primitives) noexcept
        {
            std::vector<std::uint32_t> ids{};
            ids.reserve(primitives.size());
            for (const SoftwarePrimitiveRecord& primitive : primitives)
            {
                ids.push_back(primitive.identity.x);
            }
            std::sort(ids.begin(), ids.end());
            return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
        }

        [[nodiscard]] constexpr std::uint32_t ExpandMortonBits(std::uint32_t value) noexcept
        {
            value &= 0x000003ffu;
            value = (value | (value << 16u)) & 0x030000ffu;
            value = (value | (value << 8u)) & 0x0300f00fu;
            value = (value | (value << 4u)) & 0x030c30c3u;
            value = (value | (value << 2u)) & 0x09249249u;
            return value;
        }

        [[nodiscard]] std::uint32_t QuantizeMortonAxis(
            const float value,
            const float minimum,
            const float maximum) noexcept
        {
            const double extent = static_cast<double>(maximum) - static_cast<double>(minimum);
            const double normalized = extent > 0.0
                                          ? (static_cast<double>(value) -
                                             static_cast<double>(minimum)) /
                                                extent
                                          : 0.5;
            const double clamped = (std::clamp)(normalized, 0.0, 1.0);
            return (std::min)(static_cast<std::uint32_t>(clamped * 1024.0), 1023u);
        }

        void StableRadixPass(
            std::vector<MortonPair>& values,
            std::vector<MortonPair>& scratch,
            const std::uint32_t shift,
            const bool useMortonCode)
        {
            std::array<std::size_t, 16u> counts{};
            for (const MortonPair& value : values)
            {
                const std::uint32_t field = useMortonCode ? value.code : value.stablePrimitiveId;
                ++counts[(field >> shift) & 0x0fu];
            }

            std::array<std::size_t, 16u> offsets{};
            for (std::size_t bucket = 1u; bucket < offsets.size(); ++bucket)
            {
                offsets[bucket] = offsets[bucket - 1u] + counts[bucket - 1u];
            }
            for (const MortonPair& value : values)
            {
                const std::uint32_t field = useMortonCode ? value.code : value.stablePrimitiveId;
                const std::size_t bucket = (field >> shift) & 0x0fu;
                scratch[offsets[bucket]++] = value;
            }
            values.swap(scratch);
        }

        struct PrimitiveReference
        {
            Aabb bounds{};
            Float3 centroid{};
            std::uint32_t sourceIndex{};
            std::uint32_t stablePrimitiveId{};
        };

        struct SahSplit
        {
            bool valid{};
            std::uint32_t axis{};
            std::size_t lastLeftBin{};
            double cost{};
        };

        [[nodiscard]] std::size_t BinIndex(
            const float centroid,
            const float minimum,
            const float extent) noexcept
        {
            if (!(extent > 0.0f))
            {
                return 0u;
            }
            const float normalized = (centroid - minimum) / extent;
            const float scaled = (std::clamp)(normalized, 0.0f, 1.0f) *
                                 static_cast<float>(kSahBinCount);
            return (std::min)(static_cast<std::size_t>(scaled), kSahBinCount - 1u);
        }

        class SahBuilder
        {
        public:
            SahBuilder(
                const std::span<const SoftwarePrimitiveRecord> primitives,
                const BuildOptions options)
                : primitives_(primitives), options_(options)
            {
            }

            [[nodiscard]] SahBuildResult Build()
            {
                SahBuildResult result{};
                if (options_.maximumLeafSize == 0u || options_.maximumDepth == 0u ||
                    options_.maximumDepth > kMaximumTraversalStack)
                {
                    result.status = BuildStatus::InvalidInput;
                    result.message = "invalid SAH build options";
                    return result;
                }
                if (primitives_.empty())
                {
                    return result;
                }
                for (const SoftwarePrimitiveRecord& primitive : primitives_)
                {
                    if (!IsPrimitiveValid(primitive))
                    {
                        result.status = BuildStatus::InvalidInput;
                        result.message = "SAH input contains an invalid triangle";
                        return result;
                    }
                }
                if (!HasUniqueIds(primitives_))
                {
                    result.status = BuildStatus::DuplicatePrimitiveId;
                    result.message = "stable primitive IDs must be unique";
                    return result;
                }

                references_.reserve(primitives_.size());
                for (std::size_t index = 0u; index < primitives_.size(); ++index)
                {
                    const Aabb bounds = PrimitiveBounds(primitives_[index]);
                    references_.push_back({
                        bounds,
                        Centroid(bounds),
                        static_cast<std::uint32_t>(index),
                        primitives_[index].identity.x});
                }
                tree_.sourcePrimitives.assign(primitives_.begin(), primitives_.end());
                if (!BuildRange(0u, references_.size(), 1u))
                {
                    result.status = BuildStatus::DepthOverflow;
                    result.message = "SAH hierarchy exceeds its configured maximum depth";
                    return result;
                }
                tree_.primitiveOrder.reserve(references_.size());
                for (const PrimitiveReference& reference : references_)
                {
                    tree_.primitiveOrder.push_back(reference.sourceIndex);
                }
                result.tree = std::move(tree_);
                return result;
            }

        private:
            [[nodiscard]] Aabb RangeBounds(const std::size_t begin, const std::size_t end) const
            {
                Aabb bounds = EmptyBounds();
                for (std::size_t index = begin; index < end; ++index)
                {
                    Expand(bounds, references_[index].bounds);
                }
                return bounds;
            }

            [[nodiscard]] Aabb CentroidBounds(const std::size_t begin, const std::size_t end) const
            {
                Aabb bounds = EmptyBounds();
                for (std::size_t index = begin; index < end; ++index)
                {
                    Expand(bounds, references_[index].centroid);
                }
                return bounds;
            }

            [[nodiscard]] SahSplit FindSplit(
                const std::size_t begin,
                const std::size_t end,
                const Aabb centroidBounds) const
            {
                SahSplit best{};
                const Float3 centroidExtent = Extent(centroidBounds);
                for (std::uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    const float axisExtent = Axis(centroidExtent, axis);
                    if (!(axisExtent > 0.0f))
                    {
                        continue;
                    }
                    struct Bin
                    {
                        Aabb bounds{EmptyBounds()};
                        std::size_t count{};
                    };
                    std::array<Bin, kSahBinCount> bins{};
                    for (std::size_t index = begin; index < end; ++index)
                    {
                        const std::size_t bin = BinIndex(
                            Axis(references_[index].centroid, axis),
                            Axis(centroidBounds.minimum, axis),
                            axisExtent);
                        ++bins[bin].count;
                        Expand(bins[bin].bounds, references_[index].bounds);
                    }

                    for (std::size_t split = 0u; split + 1u < kSahBinCount; ++split)
                    {
                        Aabb leftBounds = EmptyBounds();
                        Aabb rightBounds = EmptyBounds();
                        std::size_t leftCount{};
                        std::size_t rightCount{};
                        for (std::size_t bin = 0u; bin <= split; ++bin)
                        {
                            leftCount += bins[bin].count;
                            Expand(leftBounds, bins[bin].bounds);
                        }
                        for (std::size_t bin = split + 1u; bin < kSahBinCount; ++bin)
                        {
                            rightCount += bins[bin].count;
                            Expand(rightBounds, bins[bin].bounds);
                        }
                        if (leftCount == 0u || rightCount == 0u)
                        {
                            continue;
                        }
                        const double cost =
                            static_cast<double>(SurfaceArea(leftBounds)) *
                                static_cast<double>(leftCount) +
                            static_cast<double>(SurfaceArea(rightBounds)) *
                                static_cast<double>(rightCount);
                        if (!best.valid || cost < best.cost)
                        {
                            best = {true, axis, split, cost};
                        }
                    }
                }
                return best;
            }

            [[nodiscard]] std::size_t StableMedian(
                const std::size_t begin,
                const std::size_t end,
                const Aabb centroidBounds)
            {
                const Float3 extent = Extent(centroidBounds);
                std::uint32_t axis = 0u;
                if (extent.y > extent.x)
                {
                    axis = 1u;
                }
                if (extent.z > Axis(extent, axis))
                {
                    axis = 2u;
                }
                std::stable_sort(
                    references_.begin() + static_cast<std::ptrdiff_t>(begin),
                    references_.begin() + static_cast<std::ptrdiff_t>(end),
                    [axis](const PrimitiveReference& lhs, const PrimitiveReference& rhs)
                    {
                        const float lhsValue = Axis(lhs.centroid, axis);
                        const float rhsValue = Axis(rhs.centroid, axis);
                        if (lhsValue != rhsValue)
                        {
                            return lhsValue < rhsValue;
                        }
                        if (lhs.stablePrimitiveId != rhs.stablePrimitiveId)
                        {
                            return lhs.stablePrimitiveId < rhs.stablePrimitiveId;
                        }
                        return lhs.sourceIndex < rhs.sourceIndex;
                    });
                return begin + ((end - begin) / 2u);
            }

            [[nodiscard]] bool BuildRange(
                const std::size_t begin,
                const std::size_t end,
                const std::uint32_t depth)
            {
                if (depth > options_.maximumDepth)
                {
                    return false;
                }
                tree_.maximumDepth = (std::max)(tree_.maximumDepth, depth);
                const std::uint32_t nodeIndex = static_cast<std::uint32_t>(tree_.nodes.size());
                tree_.nodes.push_back({});
                const Aabb bounds = RangeBounds(begin, end);
                const std::size_t count = end - begin;
                if (count <= options_.maximumLeafSize)
                {
                    CpuSahNodeInput& leaf = tree_.nodes[nodeIndex];
                    leaf.bounds = bounds;
                    leaf.firstPrimitive = static_cast<std::uint32_t>(begin);
                    leaf.primitiveCount = static_cast<std::uint32_t>(count);
                    return true;
                }

                const Aabb centroidBounds = CentroidBounds(begin, end);
                const SahSplit split = FindSplit(begin, end, centroidBounds);
                std::size_t middle = begin;
                if (split.valid)
                {
                    const float minimum = Axis(centroidBounds.minimum, split.axis);
                    const float axisExtent = Axis(Extent(centroidBounds), split.axis);
                    const auto iterator = std::stable_partition(
                        references_.begin() + static_cast<std::ptrdiff_t>(begin),
                        references_.begin() + static_cast<std::ptrdiff_t>(end),
                        [minimum, axisExtent, split](const PrimitiveReference& reference)
                        {
                            return BinIndex(
                                       Axis(reference.centroid, split.axis),
                                       minimum,
                                       axisExtent) <= split.lastLeftBin;
                        });
                    middle = static_cast<std::size_t>(iterator - references_.begin());
                }
                if (middle == begin || middle == end)
                {
                    middle = StableMedian(begin, end, centroidBounds);
                }

                const std::uint32_t left = static_cast<std::uint32_t>(tree_.nodes.size());
                if (!BuildRange(begin, middle, depth + 1u))
                {
                    return false;
                }
                const std::uint32_t right = static_cast<std::uint32_t>(tree_.nodes.size());
                if (!BuildRange(middle, end, depth + 1u))
                {
                    return false;
                }
                CpuSahNodeInput& interior = tree_.nodes[nodeIndex];
                interior.bounds = bounds;
                interior.leftChild = left;
                interior.rightChild = right;
                return true;
            }

            std::span<const SoftwarePrimitiveRecord> primitives_{};
            BuildOptions options_{};
            std::vector<PrimitiveReference> references_{};
            CpuSahTree tree_{};
        };

        [[nodiscard]] SoftwareNodeRecord EncodeNode(
            const Aabb bounds,
            const Uint4 links) noexcept
        {
            return {
                {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f},
                {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f},
                links};
        }

        [[nodiscard]] Aabb DecodeBounds(const SoftwareNodeRecord& node) noexcept
        {
            return {
                {node.boundsMin.x, node.boundsMin.y, node.boundsMin.z},
                {node.boundsMax.x, node.boundsMax.y, node.boundsMax.z}};
        }

        [[nodiscard]] std::uint64_t MortonKey(const MortonPair pair) noexcept
        {
            return (static_cast<std::uint64_t>(pair.code) << 32u) |
                   static_cast<std::uint64_t>(pair.stablePrimitiveId);
        }

        [[nodiscard]] std::size_t FindKarrasSplit(
            const std::span<const MortonPair> sorted,
            const std::size_t first,
            const std::size_t last) noexcept
        {
            const std::uint64_t firstKey = MortonKey(sorted[first]);
            const std::uint64_t lastKey = MortonKey(sorted[last - 1u]);
            const int commonPrefix = std::countl_zero(firstKey ^ lastKey);
            std::size_t split = first;
            std::size_t step = last - first;
            do
            {
                step = (step + 1u) >> 1u;
                const std::size_t candidate = split + step;
                if (candidate < last - 1u)
                {
                    const int candidatePrefix =
                        std::countl_zero(firstKey ^ MortonKey(sorted[candidate]));
                    if (candidatePrefix > commonPrefix)
                    {
                        split = candidate;
                    }
                }
            } while (step > 1u);
            return split + 1u;
        }

        [[nodiscard]] bool IsRayValid(const SoftwareRayRecord& ray) noexcept
        {
            const Float3 origin{ray.originTMin.x, ray.originTMin.y, ray.originTMin.z};
            const Float3 direction{
                ray.directionTMax.x,
                ray.directionTMax.y,
                ray.directionTMax.z};
            const float lengthSquared = Dot(direction, direction);
            return IsFinite(origin) && IsFinite(direction) && std::isfinite(lengthSquared) &&
                   std::fabs(lengthSquared - 1.0f) <= 1.0e-4f &&
                   !std::isnan(ray.originTMin.w) && !std::isnan(ray.directionTMax.w) &&
                   ray.originTMin.w < ray.directionTMax.w &&
                   ray.query.y <= static_cast<std::uint32_t>(QueryMode::Any);
        }

        [[nodiscard]] bool IsRayModeCompatible(
            const SoftwareRayRecord& ray,
            const QueryMode mode) noexcept
        {
            return ray.query.y == static_cast<std::uint32_t>(mode);
        }

        [[nodiscard]] bool IntersectBounds(
            const SoftwareRayRecord& ray,
            const Aabb bounds,
            const float distanceLimit,
            const bool includeDistanceLimit,
            float& outNear) noexcept
        {
            if (!bounds.IsValid())
            {
                return false;
            }
            const Float3 origin{ray.originTMin.x, ray.originTMin.y, ray.originTMin.z};
            const Float3 direction{
                ray.directionTMax.x,
                ray.directionTMax.y,
                ray.directionTMax.z};
            float nearDistance = -(std::numeric_limits<float>::infinity)();
            float farDistance = (std::numeric_limits<float>::infinity)();
            for (std::uint32_t axis = 0u; axis < 3u; ++axis)
            {
                const float axisDirection = Axis(direction, axis);
                const float axisOrigin = Axis(origin, axis);
                const float axisMinimum = Axis(bounds.minimum, axis);
                const float axisMaximum = Axis(bounds.maximum, axis);
                if (axisDirection == 0.0f)
                {
                    if (axisOrigin < axisMinimum || axisOrigin > axisMaximum)
                    {
                        return false;
                    }
                    continue;
                }
                float axisNear = (axisMinimum - axisOrigin) / axisDirection;
                float axisFar = (axisMaximum - axisOrigin) / axisDirection;
                if (axisNear > axisFar)
                {
                    std::swap(axisNear, axisFar);
                }
                nearDistance = (std::max)(nearDistance, axisNear);
                farDistance = (std::min)(farDistance, axisFar);
                if (farDistance < nearDistance)
                {
                    return false;
                }
            }
            const bool withinRay = nearDistance < ray.directionTMax.w;
            const bool withinLimit = includeDistanceLimit ? nearDistance <= distanceLimit
                                                          : nearDistance < distanceLimit;
            if (!(farDistance > ray.originTMin.w && withinRay && withinLimit))
            {
                return false;
            }
            outNear = (std::max)(nearDistance, ray.originTMin.w);
            return true;
        }

        struct TriangleCandidate
        {
            bool hit{};
            float t{};
            float u{};
            float v{};
            bool frontFace{};
            std::uint32_t primitiveId{kInvalidIndex};
        };

        [[nodiscard]] TriangleCandidate IntersectTriangle(
            const SoftwareRayRecord& ray,
            const SoftwarePrimitiveRecord& primitive,
            const float distanceLimit,
            const bool includeDistanceLimit) noexcept
        {
            const Float3 origin{ray.originTMin.x, ray.originTMin.y, ray.originTMin.z};
            const Float3 direction{
                ray.directionTMax.x,
                ray.directionTMax.y,
                ray.directionTMax.z};
            const Float3 v0 = Vertex(primitive, 0);
            const Float3 edge1 = Subtract(Vertex(primitive, 1), v0);
            const Float3 edge2 = Subtract(Vertex(primitive, 2), v0);
            const Float3 p = Cross(direction, edge2);
            const float determinant = Dot(edge1, p);
            if (determinant == 0.0f || !std::isfinite(determinant))
            {
                return {};
            }
            const float inverseDeterminant = 1.0f / determinant;
            const Float3 translated = Subtract(origin, v0);
            const float u = Dot(translated, p) * inverseDeterminant;
            if (!(u >= 0.0f && u <= 1.0f))
            {
                return {};
            }
            const Float3 q = Cross(translated, edge1);
            const float v = Dot(direction, q) * inverseDeterminant;
            if (!(v >= 0.0f && u + v <= 1.0f))
            {
                return {};
            }
            const float t = Dot(edge2, q) * inverseDeterminant;
            const bool withinRay = t < ray.directionTMax.w;
            const bool withinLimit = includeDistanceLimit ? t <= distanceLimit
                                                          : t < distanceLimit;
            if (!std::isfinite(t) || !(t > ray.originTMin.w && withinRay && withinLimit))
            {
                return {};
            }
            return {true, t, u, v, determinant > 0.0f, primitive.identity.x};
        }

        [[nodiscard]] SoftwareHitRecord MakeMiss(const SoftwareRayRecord& ray) noexcept
        {
            return {
                {ray.directionTMax.w, 0.0f, 0.0f, 0.0f},
                {kInvalidIndex, ray.query.x, 0u, 0u}};
        }

        [[nodiscard]] SoftwareHitRecord MakeHit(
            const SoftwareRayRecord& ray,
            const TriangleCandidate& candidate) noexcept
        {
            return {
                {candidate.t, candidate.u, candidate.v, candidate.frontFace ? 1.0f : 0.0f},
                {candidate.primitiveId, ray.query.x, 0u, 0u}};
        }

        void MarkFailure(TraceResult& result, const TraceStatus status) noexcept
        {
            result.status = status;
            result.hit.identity.x = kInvalidIndex;
            result.hit.identity.z = static_cast<std::uint32_t>(status);
        }

        [[nodiscard]] BenchmarkPathResult BenchmarkPath(
            const bool lbvh,
            const std::span<const SoftwarePrimitiveRecord> primitives,
            const std::span<const SoftwareRayRecord> rays,
            const std::uint32_t iterations,
            const BuildOptions sahOptions)
        {
            BenchmarkPathResult result{};
            const auto buildStart = std::chrono::steady_clock::now();
            FlatBuildResult build = lbvh ? BuildKarrasLbvh(primitives, sahOptions.maximumDepth)
                                         : BuildFlattenedSah(primitives, sahOptions);
            const auto buildEnd = std::chrono::steady_clock::now();
            result.buildStatus = build.status;
            result.buildMilliseconds =
                std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
            if (!build.Succeeded())
            {
                return result;
            }
            result.memoryBytes = build.bvh.MemoryBytes();

            const auto traceStart = std::chrono::steady_clock::now();
            for (std::uint32_t iteration = 0u; iteration < iterations; ++iteration)
            {
                for (const SoftwareRayRecord& ray : rays)
                {
                    const TraceResult trace = Trace(build.bvh, ray, QueryMode::Closest);
                    result.counters.Accumulate(trace.counters);
                    result.resultChecksum ^= static_cast<std::uint64_t>(trace.hit.identity.x) +
                                             0x9e3779b97f4a7c15ull +
                                             (result.resultChecksum << 6u) +
                                             (result.resultChecksum >> 2u);
                    result.resultChecksum ^= static_cast<std::uint64_t>(
                        std::bit_cast<std::uint32_t>(trace.hit.tBary.x));
                }
            }
            const auto traceEnd = std::chrono::steady_clock::now();
            result.traceMilliseconds =
                std::chrono::duration<double, std::milli>(traceEnd - traceStart).count();
            const double totalRays = static_cast<double>(rays.size()) *
                                     static_cast<double>(iterations);
            if (result.traceMilliseconds > 0.0)
            {
                result.millionRaysPerSecond = totalRays / (result.traceMilliseconds * 1000.0);
            }
            return result;
        }
    }

    bool Aabb::IsValid() const noexcept
    {
        return IsFinite(minimum) && IsFinite(maximum) && minimum.x <= maximum.x &&
               minimum.y <= maximum.y && minimum.z <= maximum.z;
    }

    std::size_t FlatBvh::MemoryBytes() const noexcept
    {
        return nodes.size() * sizeof(SoftwareNodeRecord) +
               primitives.size() * sizeof(SoftwarePrimitiveRecord) +
               mortonOrder.size() * sizeof(MortonPair);
    }

    void TraversalCounters::Accumulate(const TraversalCounters& other) noexcept
    {
        nodeTests += other.nodeTests;
        triangleTests += other.triangleTests;
        rays += other.rays;
        hits += other.hits;
        stackOverflows += other.stackOverflows;
        invalidRays += other.invalidRays;
        invalidHits += other.invalidHits;
        leafVisits += other.leafVisits;
        accumulatedLeafPrimitives += other.accumulatedLeafPrimitives;
        maximumStackDepth = (std::max)(maximumStackDepth, other.maximumStackDepth);
        maximumLeafOccupancy =
            (std::max)(maximumLeafOccupancy, other.maximumLeafOccupancy);
    }

    TraversalCounters DecodeGpuTraversalCounters(
        const GpuTraversalCounterReadback& readback,
        const std::uint64_t rayCount,
        const std::uint64_t hitCount) noexcept
    {
        TraversalCounters result{};
        result.nodeTests = readback[GpuTraversalCounterSlot::NodeTests];
        result.triangleTests = readback[GpuTraversalCounterSlot::TriangleTests];
        result.rays = rayCount;
        result.hits = hitCount;
        result.stackOverflows = readback[GpuTraversalCounterSlot::StackOverflows];
        result.invalidRays = readback[GpuTraversalCounterSlot::InvalidRays];
        result.invalidHits = readback[GpuTraversalCounterSlot::InvalidHits];
        result.maximumStackDepth = readback[GpuTraversalCounterSlot::MaximumStackDepth];
        result.leafVisits = readback[GpuTraversalCounterSlot::LeafVisits];
        result.accumulatedLeafPrimitives =
            readback[GpuTraversalCounterSlot::AccumulatedLeafPrimitives];
        result.maximumLeafOccupancy =
            readback[GpuTraversalCounterSlot::MaximumLeafOccupancy];
        return result;
    }

    SoftwarePrimitiveRecord MakeTriangle(
        const Float3 v0,
        const Float3 v1,
        const Float3 v2,
        const std::uint32_t stablePrimitiveId) noexcept
    {
        return {
            {v0.x, v0.y, v0.z, 1.0f},
            {v1.x, v1.y, v1.z, 1.0f},
            {v2.x, v2.y, v2.z, 1.0f},
            {stablePrimitiveId, 0u, 0u, 0u}};
    }

    SoftwareRayRecord MakeRay(
        const Float3 origin,
        const Float3 direction,
        const float tMin,
        const float tMax,
        const std::uint32_t rayId,
        const QueryMode mode) noexcept
    {
        return {
            {origin.x, origin.y, origin.z, tMin},
            {direction.x, direction.y, direction.z, tMax},
            {rayId, static_cast<std::uint32_t>(mode), 0xffffffffu, 0u}};
    }

    Aabb PrimitiveBounds(const SoftwarePrimitiveRecord& primitive) noexcept
    {
        Aabb bounds = EmptyBounds();
        Expand(bounds, Vertex(primitive, 0));
        Expand(bounds, Vertex(primitive, 1));
        Expand(bounds, Vertex(primitive, 2));
        return bounds;
    }

    std::uint32_t MortonCode(const Float3 centroid, const Aabb& sceneBounds) noexcept
    {
        if (!sceneBounds.IsValid() || !IsFinite(centroid))
        {
            return 0u;
        }
        const std::uint32_t x = QuantizeMortonAxis(
            centroid.x,
            sceneBounds.minimum.x,
            sceneBounds.maximum.x);
        const std::uint32_t y = QuantizeMortonAxis(
            centroid.y,
            sceneBounds.minimum.y,
            sceneBounds.maximum.y);
        const std::uint32_t z = QuantizeMortonAxis(
            centroid.z,
            sceneBounds.minimum.z,
            sceneBounds.maximum.z);
        return (ExpandMortonBits(x) << 2u) | (ExpandMortonBits(y) << 1u) |
               ExpandMortonBits(z);
    }

    std::vector<MortonPair> StableRadixSortMorton(
        const std::span<const SoftwarePrimitiveRecord> primitives,
        BuildStatus* const outStatus)
    {
        if (outStatus != nullptr)
        {
            *outStatus = BuildStatus::Success;
        }
        for (const SoftwarePrimitiveRecord& primitive : primitives)
        {
            if (!IsPrimitiveValid(primitive))
            {
                if (outStatus != nullptr)
                {
                    *outStatus = BuildStatus::InvalidInput;
                }
                return {};
            }
        }
        if (!HasUniqueIds(primitives))
        {
            if (outStatus != nullptr)
            {
                *outStatus = BuildStatus::DuplicatePrimitiveId;
            }
            return {};
        }

        const Aabb sceneBounds = ComputeSceneBounds(primitives);
        std::vector<MortonPair> result{};
        result.reserve(primitives.size());
        for (std::size_t index = 0u; index < primitives.size(); ++index)
        {
            const Float3 centroid = Centroid(PrimitiveBounds(primitives[index]));
            result.push_back({
                MortonCode(centroid, sceneBounds),
                primitives[index].identity.x,
                static_cast<std::uint32_t>(index),
                0u});
        }
        std::vector<MortonPair> scratch(result.size());
        // LSD stable radix over the full deterministic key: stable ID first,
        // then Morton. The final order is lexicographic (morton, stable ID).
        for (std::uint32_t shift = 0u; shift < 32u; shift += 4u)
        {
            StableRadixPass(result, scratch, shift, false);
        }
        for (std::uint32_t shift = 0u; shift < 32u; shift += 4u)
        {
            StableRadixPass(result, scratch, shift, true);
        }
        return result;
    }

    SahBuildResult BuildPrivateBinnedSah(
        const std::span<const SoftwarePrimitiveRecord> primitives,
        const BuildOptions options)
    {
        return SahBuilder(primitives, options).Build();
    }

    FlatBuildResult FlattenCpuSah(const CpuSahTree& tree)
    {
        FlatBuildResult result{};
        result.bvh.kind = BuildKind::FlattenedCpuSah;
        if (tree.sourcePrimitives.empty())
        {
            if (!tree.nodes.empty() || !tree.primitiveOrder.empty())
            {
                result.status = BuildStatus::MalformedHierarchy;
                result.message = "empty CPU SAH input has non-empty hierarchy data";
            }
            return result;
        }
        if (tree.nodes.empty() || tree.primitiveOrder.size() != tree.sourcePrimitives.size())
        {
            result.status = BuildStatus::MalformedHierarchy;
            result.message = "CPU SAH node/order cardinality is malformed";
            return result;
        }
        for (const SoftwarePrimitiveRecord& primitive : tree.sourcePrimitives)
        {
            if (!IsPrimitiveValid(primitive))
            {
                result.status = BuildStatus::InvalidInput;
                result.message = "CPU SAH source primitive is invalid";
                return result;
            }
        }
        if (!HasUniqueIds(tree.sourcePrimitives))
        {
            result.status = BuildStatus::DuplicatePrimitiveId;
            result.message = "CPU SAH source IDs are not unique";
            return result;
        }

        std::vector<std::uint8_t> nodeState(tree.nodes.size(), 0u);
        std::vector<std::uint8_t> orderSeen(tree.primitiveOrder.size(), 0u);
        std::vector<std::uint8_t> sourceSeen(tree.sourcePrimitives.size(), 0u);
        std::vector<std::uint32_t> parents(tree.nodes.size(), kInvalidIndex);
        std::uint32_t maximumDepth{};
        bool malformed{};
        std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> visit =
            [&](const std::uint32_t nodeIndex,
                const std::uint32_t parent,
                const std::uint32_t depth)
        {
            if (malformed || nodeIndex >= tree.nodes.size() || depth > kMaximumTraversalStack ||
                nodeState[nodeIndex] != 0u)
            {
                malformed = true;
                return;
            }
            nodeState[nodeIndex] = 1u;
            parents[nodeIndex] = parent;
            maximumDepth = (std::max)(maximumDepth, depth);
            const CpuSahNodeInput& node = tree.nodes[nodeIndex];
            if (!node.bounds.IsValid())
            {
                malformed = true;
                return;
            }
            if (node.IsLeaf())
            {
                const std::uint64_t end = static_cast<std::uint64_t>(node.firstPrimitive) +
                                          static_cast<std::uint64_t>(node.primitiveCount);
                if (node.leftChild != kInvalidIndex || node.rightChild != kInvalidIndex ||
                    end > tree.primitiveOrder.size())
                {
                    malformed = true;
                    return;
                }
                Aabb leafBounds = EmptyBounds();
                for (std::uint32_t offset = 0u; offset < node.primitiveCount; ++offset)
                {
                    const std::uint32_t orderIndex = node.firstPrimitive + offset;
                    const std::uint32_t sourceIndex = tree.primitiveOrder[orderIndex];
                    if (sourceIndex >= tree.sourcePrimitives.size() || orderSeen[orderIndex] != 0u ||
                        sourceSeen[sourceIndex] != 0u)
                    {
                        malformed = true;
                        return;
                    }
                    orderSeen[orderIndex] = 1u;
                    sourceSeen[sourceIndex] = 1u;
                    Expand(leafBounds, PrimitiveBounds(tree.sourcePrimitives[sourceIndex]));
                }
                if (!Contains(node.bounds, leafBounds))
                {
                    malformed = true;
                    return;
                }
            }
            else
            {
                if (node.leftChild == kInvalidIndex || node.rightChild == kInvalidIndex ||
                    node.leftChild == node.rightChild || node.leftChild == nodeIndex ||
                    node.rightChild == nodeIndex)
                {
                    malformed = true;
                    return;
                }
                visit(node.leftChild, nodeIndex, depth + 1u);
                visit(node.rightChild, nodeIndex, depth + 1u);
                if (!malformed &&
                    (!Contains(node.bounds, tree.nodes[node.leftChild].bounds) ||
                     !Contains(node.bounds, tree.nodes[node.rightChild].bounds)))
                {
                    malformed = true;
                }
            }
            nodeState[nodeIndex] = 2u;
        };
        visit(0u, kInvalidIndex, 1u);
        if (malformed ||
            std::find(nodeState.begin(), nodeState.end(), static_cast<std::uint8_t>(2u)) ==
                nodeState.end() ||
            std::any_of(nodeState.begin(), nodeState.end(), [](const std::uint8_t state)
            {
                return state != 2u;
            }) ||
            std::any_of(orderSeen.begin(), orderSeen.end(), [](const std::uint8_t seen)
            {
                return seen == 0u;
            }) ||
            std::any_of(sourceSeen.begin(), sourceSeen.end(), [](const std::uint8_t seen)
            {
                return seen == 0u;
            }))
        {
            result.status = BuildStatus::MalformedHierarchy;
            result.message = "CPU SAH hierarchy failed structural validation";
            return result;
        }

        result.bvh.nodes.reserve(tree.nodes.size());
        for (std::size_t index = 0u; index < tree.nodes.size(); ++index)
        {
            const CpuSahNodeInput& node = tree.nodes[index];
            const Uint4 links = node.IsLeaf()
                                    ? Uint4{
                                          node.firstPrimitive,
                                          node.primitiveCount,
                                          kInvalidIndex,
                                          parents[index]}
                                    : Uint4{
                                          node.leftChild,
                                          0u,
                                          node.rightChild,
                                          parents[index]};
            result.bvh.nodes.push_back(EncodeNode(node.bounds, links));
        }
        result.bvh.primitives.reserve(tree.primitiveOrder.size());
        for (const std::uint32_t sourceIndex : tree.primitiveOrder)
        {
            result.bvh.primitives.push_back(tree.sourcePrimitives[sourceIndex]);
        }
        result.bvh.maximumDepth = maximumDepth;
        return result;
    }

    FlatBuildResult BuildFlattenedSah(
        const std::span<const SoftwarePrimitiveRecord> primitives,
        const BuildOptions options)
    {
        SahBuildResult hierarchy = BuildPrivateBinnedSah(primitives, options);
        if (!hierarchy.Succeeded())
        {
            return {hierarchy.status, std::move(hierarchy.message), {}};
        }
        return FlattenCpuSah(hierarchy.tree);
    }

    FlatBuildResult BuildKarrasLbvh(
        const std::span<const SoftwarePrimitiveRecord> primitives,
        const std::uint32_t maximumDepth)
    {
        FlatBuildResult result{};
        result.bvh.kind = BuildKind::KarrasLbvh;
        if (maximumDepth == 0u || maximumDepth > kMaximumTraversalStack)
        {
            result.status = BuildStatus::InvalidInput;
            result.message = "invalid LBVH maximum depth";
            return result;
        }
        BuildStatus sortStatus{};
        result.bvh.mortonOrder = StableRadixSortMorton(primitives, &sortStatus);
        if (sortStatus != BuildStatus::Success)
        {
            result.status = sortStatus;
            result.message = sortStatus == BuildStatus::DuplicatePrimitiveId
                                 ? "LBVH stable primitive IDs must be unique"
                                 : "LBVH input contains an invalid triangle";
            return result;
        }
        if (primitives.empty())
        {
            return result;
        }
        result.bvh.primitives.reserve(primitives.size());
        for (const MortonPair pair : result.bvh.mortonOrder)
        {
            result.bvh.primitives.push_back(primitives[pair.sourceIndex]);
        }
        result.bvh.nodes.reserve((primitives.size() * 2u) - 1u);

        bool depthOverflow{};
        std::function<std::uint32_t(std::size_t, std::size_t, std::uint32_t, std::uint32_t)>
            buildRange =
                [&](const std::size_t first,
                    const std::size_t last,
                    const std::uint32_t parent,
                    const std::uint32_t depth) -> std::uint32_t
        {
            if (depth > maximumDepth)
            {
                depthOverflow = true;
                return kInvalidIndex;
            }
            result.bvh.maximumDepth = (std::max)(result.bvh.maximumDepth, depth);
            const std::uint32_t nodeIndex = static_cast<std::uint32_t>(result.bvh.nodes.size());
            result.bvh.nodes.push_back(EncodeNode(EmptyBounds(), {kInvalidIndex, 0u, kInvalidIndex, parent}));
            if (last - first == 1u)
            {
                result.bvh.nodes[nodeIndex].links = {
                    static_cast<std::uint32_t>(first),
                    1u,
                    kInvalidIndex,
                    parent};
                return nodeIndex;
            }
            const std::size_t split = FindKarrasSplit(result.bvh.mortonOrder, first, last);
            const std::uint32_t left = buildRange(first, split, nodeIndex, depth + 1u);
            const std::uint32_t right = buildRange(split, last, nodeIndex, depth + 1u);
            result.bvh.nodes[nodeIndex].links = {left, 0u, right, parent};
            return nodeIndex;
        };
        static_cast<void>(buildRange(0u, primitives.size(), kInvalidIndex, 1u));
        if (depthOverflow)
        {
            result.status = BuildStatus::DepthOverflow;
            result.message = "LBVH hierarchy exceeds its configured maximum depth";
            result.bvh = {};
            return result;
        }
        if (RefitBottomUp(result.bvh) != BuildStatus::Success)
        {
            result.status = BuildStatus::MalformedHierarchy;
            result.message = "LBVH bottom-up bounds refit failed";
            result.bvh = {};
            return result;
        }
        return result;
    }

    BuildStatus RefitBottomUp(FlatBvh& bvh) noexcept
    {
        if (bvh.nodes.empty())
        {
            return bvh.primitives.empty() ? BuildStatus::Success
                                          : BuildStatus::MalformedHierarchy;
        }
        const std::size_t nodeCount = bvh.nodes.size();
        if (bvh.nodes[0].links.w != kInvalidIndex)
        {
            return BuildStatus::MalformedHierarchy;
        }

        std::vector<std::uint32_t> childReferences(nodeCount, 0u);
        for (std::size_t index = 0u; index < nodeCount; ++index)
        {
            const SoftwareNodeRecord& node = bvh.nodes[index];
            if (index != 0u &&
                (node.links.w == kInvalidIndex || node.links.w >= nodeCount))
            {
                return BuildStatus::MalformedHierarchy;
            }
            if (node.IsLeaf())
            {
                if (node.links.z != kInvalidIndex)
                {
                    return BuildStatus::MalformedHierarchy;
                }
                continue;
            }
            if (node.links.x >= nodeCount || node.links.z >= nodeCount ||
                node.links.x == node.links.z || node.links.x == index ||
                node.links.z == index)
            {
                return BuildStatus::MalformedHierarchy;
            }
            const std::uint32_t children[] = {node.links.x, node.links.z};
            for (const std::uint32_t child : children)
            {
                if (++childReferences[child] != 1u ||
                    bvh.nodes[child].links.w != index)
                {
                    return BuildStatus::MalformedHierarchy;
                }
            }
        }
        if (childReferences[0] != 0u)
        {
            return BuildStatus::MalformedHierarchy;
        }
        for (std::size_t index = 1u; index < nodeCount; ++index)
        {
            if (childReferences[index] != 1u)
            {
                return BuildStatus::MalformedHierarchy;
            }
        }

        std::vector<std::uint8_t> visited(nodeCount, 0u);
        std::vector<std::uint32_t> pending{0u};
        std::size_t visitedCount{};
        while (!pending.empty())
        {
            const std::uint32_t index = pending.back();
            pending.pop_back();
            if (visited[index] != 0u)
            {
                return BuildStatus::MalformedHierarchy;
            }
            visited[index] = 1u;
            ++visitedCount;
            const SoftwareNodeRecord& node = bvh.nodes[index];
            if (!node.IsLeaf())
            {
                pending.push_back(node.links.z);
                pending.push_back(node.links.x);
            }
        }
        if (visitedCount != nodeCount)
        {
            return BuildStatus::MalformedHierarchy;
        }

        std::vector<std::uint32_t> arrivals(bvh.nodes.size(), 0u);
        std::vector<std::uint32_t> leaves{};
        std::vector<std::uint8_t> primitiveReferences(bvh.primitives.size(), 0u);
        leaves.reserve(bvh.primitives.size());
        for (std::uint32_t index = 0u; index < bvh.nodes.size(); ++index)
        {
            SoftwareNodeRecord& node = bvh.nodes[index];
            if (node.IsLeaf())
            {
                const std::uint64_t end = static_cast<std::uint64_t>(node.links.x) +
                                          static_cast<std::uint64_t>(node.links.y);
                if (end > bvh.primitives.size())
                {
                    return BuildStatus::MalformedHierarchy;
                }
                Aabb bounds = EmptyBounds();
                for (std::uint32_t offset = 0u; offset < node.links.y; ++offset)
                {
                    const std::size_t primitiveIndex =
                        static_cast<std::size_t>(node.links.x) + offset;
                    const SoftwarePrimitiveRecord& primitive = bvh.primitives[primitiveIndex];
                    if (!IsPrimitiveValid(primitive) ||
                        ++primitiveReferences[primitiveIndex] != 1u)
                    {
                        return BuildStatus::MalformedHierarchy;
                    }
                    Expand(bounds, PrimitiveBounds(primitive));
                }
                node.boundsMin = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f};
                node.boundsMax = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f};
                leaves.push_back(index);
            }
            else
            {
                node.boundsMin = {
                    (std::numeric_limits<float>::infinity)(),
                    (std::numeric_limits<float>::infinity)(),
                    (std::numeric_limits<float>::infinity)(),
                    0.0f};
                node.boundsMax = {
                    -(std::numeric_limits<float>::infinity)(),
                    -(std::numeric_limits<float>::infinity)(),
                    -(std::numeric_limits<float>::infinity)(),
                    0.0f};
            }
        }
        if (leaves.empty() ||
            std::find(primitiveReferences.begin(), primitiveReferences.end(), 0u) !=
                primitiveReferences.end())
        {
            return BuildStatus::MalformedHierarchy;
        }

        for (const std::uint32_t leaf : leaves)
        {
            std::uint32_t parent = bvh.nodes[leaf].links.w;
            while (parent != kInvalidIndex)
            {
                if (parent >= bvh.nodes.size())
                {
                    return BuildStatus::MalformedHierarchy;
                }
                SoftwareNodeRecord& node = bvh.nodes[parent];
                if (node.IsLeaf() || node.links.x >= bvh.nodes.size() ||
                    node.links.z >= bvh.nodes.size())
                {
                    return BuildStatus::MalformedHierarchy;
                }
                ++arrivals[parent];
                if (arrivals[parent] == 1u)
                {
                    break;
                }
                if (arrivals[parent] != 2u)
                {
                    return BuildStatus::MalformedHierarchy;
                }
                Aabb bounds = DecodeBounds(bvh.nodes[node.links.x]);
                Expand(bounds, DecodeBounds(bvh.nodes[node.links.z]));
                if (!bounds.IsValid())
                {
                    return BuildStatus::MalformedHierarchy;
                }
                node.boundsMin = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f};
                node.boundsMax = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f};
                parent = node.links.w;
            }
        }
        for (std::size_t index = 0u; index < bvh.nodes.size(); ++index)
        {
            if (!bvh.nodes[index].IsLeaf() && arrivals[index] != 2u)
            {
                return BuildStatus::MalformedHierarchy;
            }
        }
        return DecodeBounds(bvh.nodes[0]).IsValid() ? BuildStatus::Success
                                                    : BuildStatus::MalformedHierarchy;
    }

    TraceResult Trace(
        const FlatBvh& bvh,
        const SoftwareRayRecord& ray,
        const QueryMode mode,
        const std::uint32_t stackCapacity) noexcept
    {
        TraceResult result{};
        result.hit = MakeMiss(ray);
        result.counters.rays = 1u;
        if (!IsRayValid(ray) || !IsRayModeCompatible(ray, mode) || stackCapacity == 0u ||
            stackCapacity > kMaximumTraversalStack)
        {
            ++result.counters.invalidRays;
            MarkFailure(result, TraceStatus::InvalidInput);
            return result;
        }
        if (bvh.nodes.empty())
        {
            if (!bvh.primitives.empty())
            {
                ++result.counters.invalidHits;
                MarkFailure(result, TraceStatus::MalformedBvh);
            }
            return result;
        }

        struct StackEntry
        {
            std::uint32_t nodeIndex{};
            float nearDistance{};
        };
        std::array<StackEntry, kMaximumTraversalStack> stack{};
        std::uint32_t stackSize{};
        auto push = [&](const StackEntry entry) -> bool
        {
            if (stackSize >= stackCapacity)
            {
                ++result.counters.stackOverflows;
                return false;
            }
            stack[stackSize++] = entry;
            result.counters.maximumStackDepth =
                (std::max)(result.counters.maximumStackDepth, stackSize);
            return true;
        };

        float rootNear{};
        ++result.counters.nodeTests;
        if (!IntersectBounds(
                ray,
                DecodeBounds(bvh.nodes[0]),
                ray.directionTMax.w,
                false,
                rootNear))
        {
            return result;
        }
        if (!push({0u, rootNear}))
        {
            MarkFailure(result, TraceStatus::StackOverflow);
            return result;
        }

        TriangleCandidate closest{};
        float distanceLimit = ray.directionTMax.w;
        std::size_t visitedNodes{};
        while (stackSize != 0u)
        {
            const StackEntry entry = stack[--stackSize];
            if (++visitedNodes > bvh.nodes.size())
            {
                ++result.counters.invalidHits;
                MarkFailure(result, TraceStatus::MalformedBvh);
                return result;
            }
            if (entry.nodeIndex >= bvh.nodes.size())
            {
                ++result.counters.invalidHits;
                MarkFailure(result, TraceStatus::MalformedBvh);
                return result;
            }
            if (closest.hit ? entry.nearDistance > distanceLimit
                            : entry.nearDistance >= distanceLimit)
            {
                continue;
            }
            const SoftwareNodeRecord& node = bvh.nodes[entry.nodeIndex];
            if (node.IsLeaf())
            {
                ++result.counters.leafVisits;
                result.counters.accumulatedLeafPrimitives += node.links.y;
                result.counters.maximumLeafOccupancy =
                    (std::max)(result.counters.maximumLeafOccupancy, node.links.y);
                const std::uint64_t end = static_cast<std::uint64_t>(node.links.x) +
                                          static_cast<std::uint64_t>(node.links.y);
                if (end > bvh.primitives.size())
                {
                    ++result.counters.invalidHits;
                    MarkFailure(result, TraceStatus::MalformedBvh);
                    return result;
                }
                for (std::uint32_t offset = 0u; offset < node.links.y; ++offset)
                {
                    ++result.counters.triangleTests;
                    const TriangleCandidate candidate = IntersectTriangle(
                        ray,
                        bvh.primitives[node.links.x + offset],
                        distanceLimit,
                        closest.hit);
                    if (!candidate.hit)
                    {
                        continue;
                    }
                    if (mode == QueryMode::Any)
                    {
                        result.hit = MakeHit(ray, candidate);
                        result.counters.hits = 1u;
                        return result;
                    }
                    if (!closest.hit || candidate.t < closest.t ||
                        (candidate.t == closest.t &&
                         candidate.primitiveId < closest.primitiveId))
                    {
                        closest = candidate;
                        distanceLimit = candidate.t;
                    }
                }
                continue;
            }

            if (node.links.x >= bvh.nodes.size() || node.links.z >= bvh.nodes.size())
            {
                ++result.counters.invalidHits;
                MarkFailure(result, TraceStatus::MalformedBvh);
                return result;
            }
            float leftNear{};
            float rightNear{};
            ++result.counters.nodeTests;
            const bool hitLeft = IntersectBounds(
                ray,
                DecodeBounds(bvh.nodes[node.links.x]),
                distanceLimit,
                closest.hit,
                leftNear);
            ++result.counters.nodeTests;
            const bool hitRight = IntersectBounds(
                ray,
                DecodeBounds(bvh.nodes[node.links.z]),
                distanceLimit,
                closest.hit,
                rightNear);
            if (hitLeft && hitRight)
            {
                const bool leftFirst = leftNear < rightNear ||
                                       (!(rightNear < leftNear) && node.links.x < node.links.z);
                const StackEntry nearEntry = leftFirst ? StackEntry{node.links.x, leftNear}
                                                       : StackEntry{node.links.z, rightNear};
                const StackEntry farEntry = leftFirst ? StackEntry{node.links.z, rightNear}
                                                      : StackEntry{node.links.x, leftNear};
                if (!push(farEntry) || !push(nearEntry))
                {
                    MarkFailure(result, TraceStatus::StackOverflow);
                    return result;
                }
            }
            else if (hitLeft)
            {
                if (!push({node.links.x, leftNear}))
                {
                    MarkFailure(result, TraceStatus::StackOverflow);
                    return result;
                }
            }
            else if (hitRight && !push({node.links.z, rightNear}))
            {
                MarkFailure(result, TraceStatus::StackOverflow);
                return result;
            }
        }
        if (closest.hit)
        {
            result.hit = MakeHit(ray, closest);
            result.counters.hits = 1u;
        }
        return result;
    }

    TraceResult BruteForce(
        const std::span<const SoftwarePrimitiveRecord> primitives,
        const SoftwareRayRecord& ray,
        const QueryMode mode) noexcept
    {
        TraceResult result{};
        result.hit = MakeMiss(ray);
        result.counters.rays = 1u;
        if (!IsRayValid(ray) || !IsRayModeCompatible(ray, mode))
        {
            ++result.counters.invalidRays;
            MarkFailure(result, TraceStatus::InvalidInput);
            return result;
        }
        TriangleCandidate closest{};
        float distanceLimit = ray.directionTMax.w;
        for (const SoftwarePrimitiveRecord& primitive : primitives)
        {
            ++result.counters.triangleTests;
            const TriangleCandidate candidate =
                IntersectTriangle(ray, primitive, distanceLimit, closest.hit);
            if (!candidate.hit)
            {
                continue;
            }
            if (mode == QueryMode::Any)
            {
                result.hit = MakeHit(ray, candidate);
                result.counters.hits = 1u;
                return result;
            }
            if (!closest.hit || candidate.t < closest.t ||
                (candidate.t == closest.t && candidate.primitiveId < closest.primitiveId))
            {
                closest = candidate;
                distanceLimit = candidate.t;
            }
        }
        if (closest.hit)
        {
            result.hit = MakeHit(ray, closest);
            result.counters.hits = 1u;
        }
        return result;
    }

    BatchTraceResult TraceBatch(
        const FlatBvh& bvh,
        const std::span<const SoftwareRayRecord> rays,
        const QueryMode mode,
        const std::uint32_t stackCapacity) noexcept
    {
        BatchTraceResult result{};
        result.rays.reserve(rays.size());
        for (const SoftwareRayRecord& ray : rays)
        {
            result.rays.push_back(Trace(bvh, ray, mode, stackCapacity));
            result.counters.Accumulate(result.rays.back().counters);
        }
        return result;
    }

    BenchmarkResult RunCpuMirrorBenchmark(
        const std::span<const SoftwarePrimitiveRecord> primitives,
        const std::span<const SoftwareRayRecord> rays,
        const std::uint32_t iterations,
        const BuildOptions sahOptions)
    {
        BenchmarkResult result{};
        result.iterations = iterations;
        result.rayCount = rays.size();
        result.primitiveCount = primitives.size();
        result.flattenedSah = BenchmarkPath(false, primitives, rays, iterations, sahOptions);
        result.lbvh = BenchmarkPath(true, primitives, rays, iterations, sahOptions);
        return result;
    }
}
