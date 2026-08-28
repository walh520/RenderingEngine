#pragma once

#include "Geometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RenderingEngine::Rt::Cpu
{
    template <FloatingPoint T>
    [[nodiscard]] inline Hit<T> TraceClosestBruteForce(
        const std::span<const Triangle<T>> triangles,
        const Ray<T>& ray) noexcept
    {
        Hit<T> closest{};
        for (const Triangle<T>& triangle : triangles)
        {
            Hit<T> candidate{};
            if (IntersectTriangle(ray, triangle, candidate) && IsBetterHit(candidate, closest))
            {
                closest = candidate;
            }
        }
        return closest;
    }

    template <FloatingPoint T>
    [[nodiscard]] inline bool TraceAnyBruteForce(
        const std::span<const Triangle<T>> triangles,
        const Ray<T>& ray) noexcept
    {
        for (const Triangle<T>& triangle : triangles)
        {
            Hit<T> candidate{};
            if (IntersectTriangle(ray, triangle, candidate))
            {
                return true;
            }
        }
        return false;
    }

    template <FloatingPoint T>
    [[nodiscard]] inline Hit<T> IntersectBruteForce(
        const std::span<const Triangle<T>> triangles,
        const Ray<T>& ray) noexcept
    {
        return TraceClosestBruteForce(triangles, ray);
    }

    enum class BvhBuildMethod : std::uint8_t
    {
        Median,
        BinnedSah,
    };

    template <FloatingPoint T>
    class Bvh final
    {
    public:
        static constexpr std::size_t kSahBinCount = 16u;
        static constexpr std::size_t kDefaultMaximumLeafSize = 4u;
        static constexpr std::size_t kTraversalStackCapacity = 128u;

        Bvh() = default;

        explicit Bvh(
            const std::span<const Triangle<T>> triangles,
            const BvhBuildMethod buildMethod = BvhBuildMethod::BinnedSah,
            const std::size_t maximumLeafSize = kDefaultMaximumLeafSize)
        {
            Rebuild(triangles, buildMethod, maximumLeafSize);
        }

        void Rebuild(
            const std::span<const Triangle<T>> triangles,
            const BvhBuildMethod buildMethod = BvhBuildMethod::BinnedSah,
            const std::size_t maximumLeafSize = kDefaultMaximumLeafSize)
        {
            if (maximumLeafSize == 0u)
            {
                throw std::invalid_argument("A BVH leaf must allow at least one primitive");
            }

            for (const Triangle<T>& triangle : triangles)
            {
                if (!IsFinite(triangle.v0) || !IsFinite(triangle.v1) || !IsFinite(triangle.v2))
                {
                    throw std::invalid_argument("A BVH cannot contain a non-finite triangle");
                }
            }

            triangles_.assign(triangles.begin(), triangles.end());
            nodes_.clear();
            primitiveOrder_.clear();
            buildMethod_ = buildMethod;
            maximumLeafSize_ = maximumLeafSize;
            maximumDepth_ = 0u;

            if (triangles_.empty())
            {
                return;
            }

            std::vector<PrimitiveReference> references{};
            references.reserve(triangles_.size());
            for (std::size_t triangleIndex = 0u; triangleIndex < triangles_.size(); ++triangleIndex)
            {
                const Triangle<T>& triangle = triangles_[triangleIndex];
                references.push_back(PrimitiveReference{
                    triangleIndex,
                    triangle.primitiveId,
                    triangle.Bounds(),
                    triangle.Centroid()});
            }

            nodes_.reserve((triangles_.size() * 2u) - 1u);
            static_cast<void>(BuildRange(references, 0u, references.size(), 1u));

            primitiveOrder_.resize(references.size());
            for (std::size_t referenceIndex = 0u; referenceIndex < references.size();
                 ++referenceIndex)
            {
                primitiveOrder_[referenceIndex] = references[referenceIndex].triangleIndex;
            }
        }

        [[nodiscard]] Hit<T> TraceClosest(const Ray<T>& ray) const
        {
            Hit<T> closest{};
            if (nodes_.empty())
            {
                return closest;
            }

            TraversalStack stack{};
            stack.Push(0u);

            while (!stack.Empty())
            {
                const std::size_t nodeIndex = stack.Pop();
                const Node& node = nodes_[nodeIndex];

                T nodeNear{};
                T nodeFar{};
                if (!IntersectAabb(ray, node.bounds, nodeNear, nodeFar) ||
                    (closest.IsHit() && nodeNear > closest.t))
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    const std::size_t end = node.firstPrimitive + node.primitiveCount;
                    for (std::size_t orderedIndex = node.firstPrimitive; orderedIndex < end;
                         ++orderedIndex)
                    {
                        const Triangle<T>& triangle = triangles_[primitiveOrder_[orderedIndex]];
                        Hit<T> candidate{};
                        if (IntersectTriangle(ray, triangle, candidate) &&
                            IsBetterHit(candidate, closest))
                        {
                            closest = candidate;
                        }
                    }
                    continue;
                }

                PushChildrenNearFirst(ray, node, closest, stack);
            }

            return closest;
        }

        [[nodiscard]] bool TraceAny(const Ray<T>& ray) const
        {
            if (nodes_.empty())
            {
                return false;
            }

            TraversalStack stack{};
            stack.Push(0u);

            while (!stack.Empty())
            {
                const std::size_t nodeIndex = stack.Pop();
                const Node& node = nodes_[nodeIndex];
                if (!IntersectAabb(ray, node.bounds))
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    const std::size_t end = node.firstPrimitive + node.primitiveCount;
                    for (std::size_t orderedIndex = node.firstPrimitive; orderedIndex < end;
                         ++orderedIndex)
                    {
                        Hit<T> candidate{};
                        if (IntersectTriangle(
                                ray,
                                triangles_[primitiveOrder_[orderedIndex]],
                                candidate))
                        {
                            return true;
                        }
                    }
                    continue;
                }

                const Node& leftNode = nodes_[node.leftChild];
                const Node& rightNode = nodes_[node.rightChild];
                T leftNear{};
                T leftFar{};
                T rightNear{};
                T rightFar{};
                const bool intersectsLeft = IntersectAabb(ray, leftNode.bounds, leftNear, leftFar);
                const bool intersectsRight =
                    IntersectAabb(ray, rightNode.bounds, rightNear, rightFar);
                PushIntersectedChildren(
                    node.leftChild,
                    leftNear,
                    intersectsLeft,
                    node.rightChild,
                    rightNear,
                    intersectsRight,
                    stack);
            }

            return false;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return triangles_.empty();
        }

        [[nodiscard]] std::size_t PrimitiveCount() const noexcept
        {
            return triangles_.size();
        }

        [[nodiscard]] std::size_t NodeCount() const noexcept
        {
            return nodes_.size();
        }

        [[nodiscard]] std::size_t MaximumDepth() const noexcept
        {
            return maximumDepth_;
        }

        [[nodiscard]] std::size_t MaximumLeafSize() const noexcept
        {
            return maximumLeafSize_;
        }

        [[nodiscard]] BvhBuildMethod BuildMethod() const noexcept
        {
            return buildMethod_;
        }

        [[nodiscard]] Aabb<T> RootBounds() const noexcept
        {
            return nodes_.empty() ? Aabb<T>{} : nodes_.front().bounds;
        }

        // Input indices in CPU leaf order are exposed only for determinism tests.
        // This is not a GPU node layout or a serialization/flattening contract.
        [[nodiscard]] std::span<const std::size_t> PrimitiveOrder() const noexcept
        {
            return primitiveOrder_;
        }

    private:
        static constexpr std::size_t kNoNode = (std::numeric_limits<std::size_t>::max)();

        struct PrimitiveReference
        {
            std::size_t triangleIndex{};
            std::uint32_t primitiveId{kInvalidPrimitiveId};
            Aabb<T> bounds{};
            Vec3<T> centroid{};
        };

        struct Node
        {
            Aabb<T> bounds{};
            std::size_t leftChild{kNoNode};
            std::size_t rightChild{kNoNode};
            std::size_t firstPrimitive{};
            std::size_t primitiveCount{};

            [[nodiscard]] bool IsLeaf() const noexcept
            {
                return primitiveCount != 0u;
            }
        };

        struct SahBin
        {
            Aabb<T> bounds{};
            std::size_t primitiveCount{};
        };

        struct SahSplit
        {
            bool valid{false};
            std::size_t axis{};
            std::size_t lastLeftBin{};
            Detail::PromotedFloat<T> cost{
                (std::numeric_limits<Detail::PromotedFloat<T>>::infinity)()};
        };

        class TraversalStack final
        {
        public:
            void Push(const std::size_t nodeIndex)
            {
                if (size_ >= entries_.size())
                {
                    throw std::overflow_error("CPU BVH traversal stack capacity exceeded");
                }
                entries_[size_] = nodeIndex;
                ++size_;
            }

            [[nodiscard]] std::size_t Pop() noexcept
            {
                --size_;
                return entries_[size_];
            }

            [[nodiscard]] bool Empty() const noexcept
            {
                return size_ == 0u;
            }

        private:
            std::array<std::size_t, kTraversalStackCapacity> entries_{};
            std::size_t size_{};
        };

        [[nodiscard]] static Aabb<T> ComputeBounds(
            const std::vector<PrimitiveReference>& references,
            const std::size_t begin,
            const std::size_t end) noexcept
        {
            Aabb<T> bounds{};
            for (std::size_t index = begin; index < end; ++index)
            {
                bounds.Expand(references[index].bounds);
            }
            return bounds;
        }

        [[nodiscard]] static Aabb<T> ComputeCentroidBounds(
            const std::vector<PrimitiveReference>& references,
            const std::size_t begin,
            const std::size_t end) noexcept
        {
            Aabb<T> bounds{};
            for (std::size_t index = begin; index < end; ++index)
            {
                bounds.Expand(references[index].centroid);
            }
            return bounds;
        }

        [[nodiscard]] static Detail::PromotedFloat<T> SurfaceAreaPromoted(
            const Aabb<T>& bounds) noexcept
        {
            using P = Detail::PromotedFloat<T>;
            if (!bounds.IsValid())
            {
                return P{0};
            }
            const P extentX = static_cast<P>(bounds.maximum.x) -
                              static_cast<P>(bounds.minimum.x);
            const P extentY = static_cast<P>(bounds.maximum.y) -
                              static_cast<P>(bounds.minimum.y);
            const P extentZ = static_cast<P>(bounds.maximum.z) -
                              static_cast<P>(bounds.minimum.z);
            return P{2} * ((extentX * extentY) + (extentY * extentZ) +
                           (extentZ * extentX));
        }

        [[nodiscard]] static std::size_t ComputeBinIndex(
            const T centroid,
            const T centroidMinimum,
            const T centroidExtent) noexcept
        {
            using P = Detail::PromotedFloat<T>;
            const P normalized =
                (static_cast<P>(centroid) - static_cast<P>(centroidMinimum)) /
                static_cast<P>(centroidExtent);
            const P scaled = normalized * static_cast<P>(kSahBinCount);
            if (!(scaled > P{0}))
            {
                return 0u;
            }
            if (scaled >= static_cast<P>(kSahBinCount))
            {
                return kSahBinCount - 1u;
            }
            return static_cast<std::size_t>(scaled);
        }

        [[nodiscard]] static SahSplit FindSahSplit(
            const std::vector<PrimitiveReference>& references,
            const std::size_t begin,
            const std::size_t end,
            const Aabb<T>& centroidBounds) noexcept
        {
            SahSplit best{};
            const Vec3<T> centroidExtent = centroidBounds.Extent();

            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                if (!(centroidExtent[axis] > T{0}))
                {
                    continue;
                }

                std::array<SahBin, kSahBinCount> bins{};
                for (std::size_t index = begin; index < end; ++index)
                {
                    const std::size_t binIndex = ComputeBinIndex(
                        references[index].centroid[axis],
                        centroidBounds.minimum[axis],
                        centroidExtent[axis]);
                    SahBin& bin = bins[binIndex];
                    ++bin.primitiveCount;
                    bin.bounds.Expand(references[index].bounds);
                }

                for (std::size_t splitBin = 0u; splitBin + 1u < kSahBinCount;
                     ++splitBin)
                {
                    Aabb<T> leftBounds{};
                    Aabb<T> rightBounds{};
                    std::size_t leftCount = 0u;
                    std::size_t rightCount = 0u;

                    for (std::size_t binIndex = 0u; binIndex <= splitBin; ++binIndex)
                    {
                        leftCount += bins[binIndex].primitiveCount;
                        leftBounds.Expand(bins[binIndex].bounds);
                    }
                    for (std::size_t binIndex = splitBin + 1u; binIndex < kSahBinCount;
                         ++binIndex)
                    {
                        rightCount += bins[binIndex].primitiveCount;
                        rightBounds.Expand(bins[binIndex].bounds);
                    }

                    if (leftCount == 0u || rightCount == 0u)
                    {
                        continue;
                    }

                    using P = Detail::PromotedFloat<T>;
                    const P cost =
                        (SurfaceAreaPromoted(leftBounds) * static_cast<P>(leftCount)) +
                        (SurfaceAreaPromoted(rightBounds) * static_cast<P>(rightCount));
                    // Axis and bin loops are ascending; strict comparison makes
                    // equal-cost decisions deterministic (lowest axis/bin wins).
                    if (!best.valid || cost < best.cost)
                    {
                        best.valid = true;
                        best.axis = axis;
                        best.lastLeftBin = splitBin;
                        best.cost = cost;
                    }
                }
            }

            return best;
        }

        [[nodiscard]] static std::size_t MedianPartition(
            std::vector<PrimitiveReference>& references,
            const std::size_t begin,
            const std::size_t end,
            const Aabb<T>& centroidBounds)
        {
            const std::size_t axis = LongestAxis(centroidBounds.Extent());
            std::stable_sort(
                references.begin() + static_cast<std::ptrdiff_t>(begin),
                references.begin() + static_cast<std::ptrdiff_t>(end),
                [axis](const PrimitiveReference& lhs, const PrimitiveReference& rhs) noexcept
                {
                    if (lhs.centroid[axis] < rhs.centroid[axis])
                    {
                        return true;
                    }
                    if (lhs.centroid[axis] > rhs.centroid[axis])
                    {
                        return false;
                    }
                    if (lhs.primitiveId < rhs.primitiveId)
                    {
                        return true;
                    }
                    if (lhs.primitiveId > rhs.primitiveId)
                    {
                        return false;
                    }
                    return lhs.triangleIndex < rhs.triangleIndex;
                });
            return begin + ((end - begin) / 2u);
        }

        [[nodiscard]] static std::size_t SahPartition(
            std::vector<PrimitiveReference>& references,
            const std::size_t begin,
            const std::size_t end,
            const Aabb<T>& centroidBounds,
            const SahSplit& split)
        {
            const T centroidMinimum = centroidBounds.minimum[split.axis];
            const T centroidExtent = centroidBounds.Extent()[split.axis];
            const auto middle = std::stable_partition(
                references.begin() + static_cast<std::ptrdiff_t>(begin),
                references.begin() + static_cast<std::ptrdiff_t>(end),
                [centroidMinimum, centroidExtent, split](
                    const PrimitiveReference& reference) noexcept
                {
                    return ComputeBinIndex(
                               reference.centroid[split.axis],
                               centroidMinimum,
                               centroidExtent) <= split.lastLeftBin;
                });
            return static_cast<std::size_t>(middle - references.begin());
        }

        [[nodiscard]] std::size_t BuildRange(
            std::vector<PrimitiveReference>& references,
            const std::size_t begin,
            const std::size_t end,
            const std::size_t depth)
        {
            if (depth > kTraversalStackCapacity)
            {
                throw std::length_error(
                    "CPU BVH depth exceeds the fixed traversal stack capacity");
            }
            const std::size_t nodeIndex = nodes_.size();
            nodes_.push_back(Node{});
            if (depth > maximumDepth_)
            {
                maximumDepth_ = depth;
            }

            const Aabb<T> bounds = ComputeBounds(references, begin, end);
            const std::size_t primitiveCount = end - begin;
            if (primitiveCount <= maximumLeafSize_)
            {
                Node& leaf = nodes_[nodeIndex];
                leaf.bounds = bounds;
                leaf.firstPrimitive = begin;
                leaf.primitiveCount = primitiveCount;
                return nodeIndex;
            }

            const Aabb<T> centroidBounds = ComputeCentroidBounds(references, begin, end);
            std::size_t middle = begin;
            if (buildMethod_ == BvhBuildMethod::BinnedSah)
            {
                const SahSplit split = FindSahSplit(references, begin, end, centroidBounds);
                if (split.valid)
                {
                    middle = SahPartition(references, begin, end, centroidBounds, split);
                }
            }

            // Identical centroids, an empty SAH side, or a numerically unusable
            // split falls back to a stable ID/index median and always progresses.
            if (middle == begin || middle == end)
            {
                middle = MedianPartition(references, begin, end, centroidBounds);
            }

            const std::size_t leftChild = BuildRange(references, begin, middle, depth + 1u);
            const std::size_t rightChild = BuildRange(references, middle, end, depth + 1u);
            Node& interior = nodes_[nodeIndex];
            interior.bounds = bounds;
            interior.leftChild = leftChild;
            interior.rightChild = rightChild;
            return nodeIndex;
        }

        static void PushIntersectedChildren(
            const std::size_t leftChild,
            const T leftNear,
            const bool intersectsLeft,
            const std::size_t rightChild,
            const T rightNear,
            const bool intersectsRight,
            TraversalStack& stack)
        {
            if (intersectsLeft && intersectsRight)
            {
                const bool leftFirst =
                    leftNear < rightNear ||
                    (!(rightNear < leftNear) && leftChild < rightChild);
                stack.Push(leftFirst ? rightChild : leftChild);
                stack.Push(leftFirst ? leftChild : rightChild);
                return;
            }
            if (intersectsLeft)
            {
                stack.Push(leftChild);
            }
            else if (intersectsRight)
            {
                stack.Push(rightChild);
            }
        }

        void PushChildrenNearFirst(
            const Ray<T>& ray,
            const Node& node,
            const Hit<T>& closest,
            TraversalStack& stack) const
        {
            const Node& leftNode = nodes_[node.leftChild];
            const Node& rightNode = nodes_[node.rightChild];
            T leftNear{};
            T leftFar{};
            T rightNear{};
            T rightFar{};
            bool intersectsLeft = IntersectAabb(ray, leftNode.bounds, leftNear, leftFar);
            bool intersectsRight = IntersectAabb(ray, rightNode.bounds, rightNear, rightFar);
            if (closest.IsHit())
            {
                intersectsLeft = intersectsLeft && leftNear <= closest.t;
                intersectsRight = intersectsRight && rightNear <= closest.t;
            }
            PushIntersectedChildren(
                node.leftChild,
                leftNear,
                intersectsLeft,
                node.rightChild,
                rightNear,
                intersectsRight,
                stack);
        }

        std::vector<Triangle<T>> triangles_{};
        std::vector<Node> nodes_{};
        std::vector<std::size_t> primitiveOrder_{};
        BvhBuildMethod buildMethod_{BvhBuildMethod::BinnedSah};
        std::size_t maximumLeafSize_{kDefaultMaximumLeafSize};
        std::size_t maximumDepth_{};
    };
}
