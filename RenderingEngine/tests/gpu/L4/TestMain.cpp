#include "rt/software_gpu/SoftwareGpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace L4 = RenderingEngine::Rt::SoftwareGpu;

namespace
{
    std::uint32_t gAssertions{};

    void Require(const bool condition, const std::string_view message)
    {
        ++gAssertions;
        if (!condition)
        {
            throw std::runtime_error(message.data());
        }
    }

    [[nodiscard]] std::vector<L4::SoftwarePrimitiveRecord> MakeGrid(const std::uint32_t count)
    {
        std::vector<L4::SoftwarePrimitiveRecord> result{};
        result.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const float x = static_cast<float>(index % 4u) * 2.0f;
            const float y = static_cast<float>(index / 4u) * 2.0f;
            result.push_back(L4::MakeTriangle(
                {x - 0.5f, y - 0.5f, 0.0f},
                {x + 0.5f, y - 0.5f, 0.0f},
                {x, y + 0.5f, 0.0f},
                1000u + index));
        }
        return result;
    }

    [[nodiscard]] std::vector<L4::SoftwareRayRecord> MakeCorpus(const std::uint32_t hitCount)
    {
        std::vector<L4::SoftwareRayRecord> result{};
        for (std::uint32_t index = 0u; index < hitCount; ++index)
        {
            const float x = static_cast<float>(index % 4u) * 2.0f;
            const float y = static_cast<float>(index / 4u) * 2.0f;
            result.push_back(L4::MakeRay(
                {x, y, 2.0f},
                {0.0f, 0.0f, -1.0f},
                0.001f,
                100.0f,
                index));
        }
        result.push_back(L4::MakeRay(
            {100.0f, 100.0f, 2.0f},
            {0.0f, 0.0f, -1.0f},
            0.001f,
            100.0f,
            9000u));
        result.push_back(L4::MakeRay(
            {-2.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            0.001f,
            100.0f,
            9001u));
        result.push_back(L4::MakeRay(
            {0.0f, 0.0f, -2.0f},
            {0.0f, 0.0f, 1.0f},
            0.001f,
            100.0f,
            9002u));
        return result;
    }

    [[nodiscard]] std::vector<RenderingEngine::Rt::Cpu::Triangle<float>> ToCpuTriangles(
        const std::span<const L4::SoftwarePrimitiveRecord> primitives)
    {
        std::vector<RenderingEngine::Rt::Cpu::Triangle<float>> result{};
        result.reserve(primitives.size());
        for (const L4::SoftwarePrimitiveRecord& primitive : primitives)
        {
            result.emplace_back(
                RenderingEngine::Rt::Cpu::Vec3<float>{
                    primitive.v0.x, primitive.v0.y, primitive.v0.z},
                RenderingEngine::Rt::Cpu::Vec3<float>{
                    primitive.v1.x, primitive.v1.y, primitive.v1.z},
                RenderingEngine::Rt::Cpu::Vec3<float>{
                    primitive.v2.x, primitive.v2.y, primitive.v2.z},
                primitive.identity.x);
        }
        return result;
    }

    [[nodiscard]] RenderingEngine::Rt::Cpu::Ray<float> ToCpuRay(
        const L4::SoftwareRayRecord& ray)
    {
        return {
            {ray.originTMin.x, ray.originTMin.y, ray.originTMin.z},
            {ray.directionTMax.x, ray.directionTMax.y, ray.directionTMax.z},
            ray.originTMin.w,
            ray.directionTMax.w};
    }

    void CheckL3ClosestAndAnyParity(
        const RenderingEngine::Rt::Cpu::Bvh<float>& cpuBvh,
        const L4::FlatBvh& flattened,
        const std::span<const L4::SoftwareRayRecord> rays)
    {
        for (const L4::SoftwareRayRecord& ray : rays)
        {
            const RenderingEngine::Rt::Cpu::Hit<float> expected =
                cpuBvh.TraceClosest(ToCpuRay(ray));
            const L4::TraceResult actual = L4::Trace(
                flattened,
                ray,
                L4::QueryMode::Closest);
            Require(actual.Succeeded(), "L3-adapted closest query must succeed");
            Require(actual.IsHit() == expected.IsHit(), "L3 closest hit/miss parity failed");
            if (expected.IsHit())
            {
                Require(
                    actual.hit.identity.x == expected.primitiveId,
                    "L3 closest stable primitive ID parity failed");
                Require(
                    std::fabs(actual.hit.tBary.x - expected.t) <= 1.0e-5f,
                    "L3 closest distance parity failed");
                Require(
                    std::fabs(actual.hit.tBary.y - expected.barycentric.y) <= 1.0e-5f &&
                        std::fabs(actual.hit.tBary.z - expected.barycentric.z) <= 1.0e-5f,
                    "L3 closest barycentric parity failed");
            }

            L4::SoftwareRayRecord anyRay = ray;
            anyRay.query.y = static_cast<std::uint32_t>(L4::QueryMode::Any);
            const L4::TraceResult actualAny = L4::Trace(
                flattened,
                anyRay,
                L4::QueryMode::Any);
            Require(actualAny.Succeeded(), "L3-adapted any query must succeed");
            Require(
                actualAny.IsHit() == cpuBvh.TraceAny(ToCpuRay(anyRay)),
                "L3 any-hit parity failed");
        }
    }

    void CheckClosestParity(
        const L4::FlatBvh& bvh,
        const std::span<const L4::SoftwarePrimitiveRecord> source,
        const std::span<const L4::SoftwareRayRecord> rays)
    {
        for (const L4::SoftwareRayRecord& ray : rays)
        {
            const L4::TraceResult expected = L4::BruteForce(source, ray, L4::QueryMode::Closest);
            const L4::TraceResult actual = L4::Trace(bvh, ray, L4::QueryMode::Closest);
            Require(expected.Succeeded(), "brute-force query must succeed");
            Require(actual.Succeeded(), "flattened query must succeed");
            Require(actual.IsHit() == expected.IsHit(), "closest hit/miss parity failed");
            if (expected.IsHit())
            {
                Require(
                    actual.hit.identity.x == expected.hit.identity.x,
                    "closest stable primitive ID parity failed");
                Require(
                    std::fabs(actual.hit.tBary.x - expected.hit.tBary.x) <= 1.0e-5f,
                    "closest distance parity failed");
                Require(
                    std::fabs(actual.hit.tBary.y - expected.hit.tBary.y) <= 1.0e-5f &&
                        std::fabs(actual.hit.tBary.z - expected.hit.tBary.z) <= 1.0e-5f,
                    "closest barycentric parity failed");
            }

            L4::SoftwareRayRecord anyRay = ray;
            anyRay.query.y = static_cast<std::uint32_t>(L4::QueryMode::Any);
            const L4::TraceResult expectedAny =
                L4::BruteForce(source, anyRay, L4::QueryMode::Any);
            const L4::TraceResult actualAny = L4::Trace(bvh, anyRay, L4::QueryMode::Any);
            Require(expectedAny.Succeeded() && actualAny.Succeeded(), "any-hit query must succeed");
            Require(actualAny.IsHit() == expectedAny.IsHit(), "any-hit parity failed");
        }
    }

    void TestFlattenedSahAndParity()
    {
        const std::vector<L4::SoftwarePrimitiveRecord> primitives = MakeGrid(12u);
        const L4::SahBuildResult hierarchy = L4::BuildPrivateBinnedSah(primitives, {1u, 64u});
        Require(hierarchy.Succeeded(), "private binned SAH build failed");
        Require(!hierarchy.tree.nodes.empty(), "SAH hierarchy is empty");
        Require(hierarchy.tree.primitiveOrder.size() == primitives.size(), "SAH order size drifted");

        const L4::FlatBuildResult flattened = L4::FlattenCpuSah(hierarchy.tree);
        Require(flattened.Succeeded(), "CPU SAH converter failed");
        Require(flattened.bvh.kind == L4::BuildKind::FlattenedCpuSah, "wrong build kind");
        Require(flattened.bvh.nodes.size() == 23u, "leaf-size-one SAH node count is wrong");
        Require(flattened.bvh.primitives.size() == primitives.size(), "primitive reorder count drifted");
        Require(flattened.bvh.nodes[0].links.w == L4::kInvalidIndex, "root parent is not invalid");
        Require(flattened.bvh.maximumDepth > 1u, "SAH depth counter was not populated");

        const std::vector<L4::SoftwareRayRecord> rays = MakeCorpus(12u);
        CheckClosestParity(flattened.bvh, primitives, rays);
        const L4::BatchTraceResult batch =
            L4::TraceBatch(flattened.bvh, rays, L4::QueryMode::Closest);
        Require(batch.counters.rays == rays.size(), "batch ray counter drifted");
        Require(batch.counters.nodeTests != 0u, "node counter remained zero");
        Require(batch.counters.triangleTests != 0u, "triangle counter remained zero");
        Require(batch.counters.leafVisits != 0u, "leaf visit counter remained zero");
        Require(
            batch.counters.accumulatedLeafPrimitives != 0u,
            "accumulated leaf occupancy remained zero");
        Require(batch.counters.maximumLeafOccupancy == 1u, "maximum leaf occupancy drifted");
        Require(batch.counters.stackOverflows == 0u, "normal SAH traversal overflowed");
        Require(batch.counters.maximumStackDepth != 0u, "maximum stack depth was not reported");
    }

    void TestCanonicalL3BinnedSahAdapter()
    {
        const std::vector<L4::SoftwarePrimitiveRecord> primitives = MakeGrid(12u);
        const std::vector<RenderingEngine::Rt::Cpu::Triangle<float>> cpuTriangles =
            ToCpuTriangles(primitives);
        const RenderingEngine::Rt::Cpu::Bvh<float> cpuBvh(
            cpuTriangles,
            RenderingEngine::Rt::Cpu::BvhBuildMethod::BinnedSah,
            2u);
        const L4::FlatBuildResult flattened = L4::FlattenCanonicalL3BinnedSah(
            cpuBvh,
            primitives);
        Require(flattened.Succeeded(), "canonical L3 SAH adapter failed");
        Require(
            flattened.bvh.nodes.size() == cpuBvh.NodeCount(),
            "canonical L3 node count was not preserved");
        Require(
            flattened.bvh.maximumDepth == cpuBvh.MaximumDepth(),
            "canonical L3 maximum depth was not preserved");
        Require(
            flattened.bvh.primitives.size() == cpuBvh.PrimitiveCount(),
            "canonical L3 primitive count was not preserved");

        const auto cpuNodes = cpuBvh.Nodes();
        for (std::size_t index = 0u; index < cpuNodes.size(); ++index)
        {
            const auto& cpuNode = cpuNodes[index];
            const L4::SoftwareNodeRecord& gpuNode = flattened.bvh.nodes[index];
            Require(
                gpuNode.boundsMin.x == cpuNode.bounds.minimum.x &&
                    gpuNode.boundsMin.y == cpuNode.bounds.minimum.y &&
                    gpuNode.boundsMin.z == cpuNode.bounds.minimum.z &&
                    gpuNode.boundsMax.x == cpuNode.bounds.maximum.x &&
                    gpuNode.boundsMax.y == cpuNode.bounds.maximum.y &&
                    gpuNode.boundsMax.z == cpuNode.bounds.maximum.z,
                "canonical L3 node bounds changed during flattening");
            if (cpuNode.IsLeaf())
            {
                Require(gpuNode.IsLeaf(), "canonical L3 leaf changed to interior");
                Require(
                    gpuNode.links.x == cpuNode.firstPrimitive &&
                        gpuNode.links.y == cpuNode.primitiveCount,
                    "canonical L3 leaf range changed during flattening");
            }
            else
            {
                Require(!gpuNode.IsLeaf(), "canonical L3 interior changed to leaf");
                Require(
                    gpuNode.links.x == cpuNode.leftChild &&
                        gpuNode.links.z == cpuNode.rightChild,
                    "canonical L3 child order changed during flattening");
            }
        }
        const auto cpuOrder = cpuBvh.PrimitiveOrder();
        for (std::size_t orderedIndex = 0u; orderedIndex < cpuOrder.size(); ++orderedIndex)
        {
            const std::size_t sourceIndex = cpuOrder[orderedIndex];
            Require(
                flattened.bvh.primitives[orderedIndex].identity.x ==
                    primitives[sourceIndex].identity.x,
                "canonical L3 primitive order changed during flattening");
        }
        CheckL3ClosestAndAnyParity(cpuBvh, flattened.bvh, MakeCorpus(12u));

        const std::vector<L4::SoftwarePrimitiveRecord> sharedEdgePrimitives{
            L4::MakeTriangle({-1.0f, -1.0f, -2.0f}, {1.0f, -1.0f, -2.0f}, {1.0f, 1.0f, -2.0f}, 17u),
            L4::MakeTriangle({-1.0f, -1.0f, -2.0f}, {1.0f, 1.0f, -2.0f}, {-1.0f, 1.0f, -2.0f}, 11u)};
        const std::vector<RenderingEngine::Rt::Cpu::Triangle<float>> sharedEdgeCpuTriangles =
            ToCpuTriangles(sharedEdgePrimitives);
        const RenderingEngine::Rt::Cpu::Bvh<float> sharedEdgeCpuBvh(
            sharedEdgeCpuTriangles,
            RenderingEngine::Rt::Cpu::BvhBuildMethod::BinnedSah,
            1u);
        const L4::FlatBuildResult sharedEdgeFlattened = L4::FlattenCanonicalL3BinnedSah(
            sharedEdgeCpuBvh,
            sharedEdgePrimitives);
        Require(sharedEdgeFlattened.Succeeded(), "shared-edge L3 adapter failed");
        const L4::SoftwareRayRecord sharedEdgeRay = L4::MakeRay(
            {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, -1.0f},
            0.0f,
            100.0f,
            55u);
        const L4::TraceResult sharedEdgeHit = L4::Trace(
            sharedEdgeFlattened.bvh,
            sharedEdgeRay,
            L4::QueryMode::Closest);
        Require(sharedEdgeHit.Succeeded() && sharedEdgeHit.IsHit(), "shared-edge L4 query missed");
        Require(sharedEdgeHit.hit.identity.x == 11u, "shared-edge stable ID parity failed");

        const RenderingEngine::Rt::Cpu::Bvh<float> emptyCpuBvh(
            std::span<const RenderingEngine::Rt::Cpu::Triangle<float>>{},
            RenderingEngine::Rt::Cpu::BvhBuildMethod::BinnedSah,
            1u);
        const L4::FlatBuildResult empty = L4::FlattenCanonicalL3BinnedSah(emptyCpuBvh, {});
        Require(empty.Succeeded() && empty.bvh.nodes.empty() && empty.bvh.primitives.empty(),
            "empty canonical L3 scene was not preserved");
        Require(
            L4::FlattenCanonicalL3BinnedSah(emptyCpuBvh, primitives).status ==
                L4::BuildStatus::InvalidInput,
            "empty L3 scene accepted mismatched canonical primitives");

        const RenderingEngine::Rt::Cpu::Bvh<float> medianCpuBvh(
            cpuTriangles,
            RenderingEngine::Rt::Cpu::BvhBuildMethod::Median,
            2u);
        Require(
            L4::FlattenCanonicalL3BinnedSah(medianCpuBvh, primitives).status ==
                L4::BuildStatus::InvalidInput,
            "non-SAH L3 BVH was accepted by canonical adapter");

        std::vector<L4::SoftwarePrimitiveRecord> wrongId = primitives;
        wrongId[0].identity.x += 10000u;
        Require(
            L4::FlattenCanonicalL3BinnedSah(cpuBvh, wrongId).status ==
                L4::BuildStatus::InvalidInput,
            "canonical adapter accepted an ID mismatch");
        std::vector<L4::SoftwarePrimitiveRecord> wrongCount = primitives;
        wrongCount.pop_back();
        Require(
            L4::FlattenCanonicalL3BinnedSah(cpuBvh, wrongCount).status ==
                L4::BuildStatus::InvalidInput,
            "canonical adapter accepted a primitive-count mismatch");
        std::vector<L4::SoftwarePrimitiveRecord> wrongOrder = primitives;
        std::swap(wrongOrder[0], wrongOrder[1]);
        Require(
            L4::FlattenCanonicalL3BinnedSah(cpuBvh, wrongOrder).status ==
                L4::BuildStatus::InvalidInput,
            "canonical adapter accepted an input-order mismatch");
    }

    void TestExplicitStackOverflow()
    {
        std::vector<L4::SoftwarePrimitiveRecord> primitives{};
        primitives.push_back(L4::MakeTriangle(
            {-1.0f, -1.0f, 0.0f},
            {1.0f, -1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            8u));
        primitives.push_back(L4::MakeTriangle(
            {-1.0f, -1.0f, -0.1f},
            {1.0f, -1.0f, -0.1f},
            {0.0f, 1.0f, -0.1f},
            4u));
        const L4::FlatBuildResult build = L4::BuildFlattenedSah(primitives, {1u, 64u});
        Require(build.Succeeded(), "overflow fixture build failed");
        const L4::SoftwareRayRecord ray = L4::MakeRay(
            {0.0f, 0.0f, 2.0f},
            {0.0f, 0.0f, -1.0f},
            0.001f,
            100.0f,
            17u);
        const L4::TraceResult overflow = L4::Trace(
            build.bvh,
            ray,
            L4::QueryMode::Closest,
            1u);
        Require(overflow.status == L4::TraceStatus::StackOverflow, "overflow was not explicit");
        Require(overflow.counters.stackOverflows == 1u, "overflow counter drifted");
        Require(!overflow.IsHit(), "overflow returned a plausible hit");
        Require(overflow.hit.identity.z == 2u, "overflow status was not encoded");
    }

    void TestMortonStableRadixAndDuplicateCentroids()
    {
        std::vector<L4::SoftwarePrimitiveRecord> primitives{};
        for (const std::uint32_t id : {9u, 2u, 5u})
        {
            primitives.push_back(L4::MakeTriangle(
                {-1.0f, -1.0f, 0.0f},
                {1.0f, -1.0f, 0.0f},
                {0.0f, 1.0f, 0.0f},
                id));
        }
        L4::BuildStatus status{};
        const std::vector<L4::MortonPair> first = L4::StableRadixSortMorton(primitives, &status);
        Require(status == L4::BuildStatus::Success, "Morton radix sort failed");
        const std::vector<L4::MortonPair> second = L4::StableRadixSortMorton(primitives, &status);
        Require(first == second, "Morton radix sort is not deterministic");
        Require(first.size() == 3u, "Morton pair count drifted");
        Require(first[0].code == first[1].code && first[1].code == first[2].code, "fixture Morton codes differ");
        Require(
            first[0].stablePrimitiveId == 2u && first[1].stablePrimitiveId == 5u &&
                first[2].stablePrimitiveId == 9u,
            "duplicate Morton tie-break is not stable ID order");

        const L4::Aabb zeroSize{{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
        const std::uint32_t codeA = L4::MortonCode({1.0f, 1.0f, 1.0f}, zeroSize);
        const std::uint32_t codeB = L4::MortonCode({8.0f, -4.0f, 2.0f}, zeroSize);
        Require(codeA == codeB, "zero-size scene bounds are not deterministic");
        const float maximumFloat = (std::numeric_limits<float>::max)();
        const L4::Aabb extreme{
            {-maximumFloat, -maximumFloat, -maximumFloat},
            {maximumFloat, maximumFloat, maximumFloat}};
        Require(
            L4::MortonCode({0.0f, 0.0f, 0.0f}, extreme) == codeA,
            "finite extreme scene bounds overflowed Morton normalization");

        primitives[1].identity.x = primitives[0].identity.x;
        static_cast<void>(L4::StableRadixSortMorton(primitives, &status));
        Require(status == L4::BuildStatus::DuplicatePrimitiveId, "duplicate logical key was accepted");
    }

    void TestKarrasLbvhTopologyAndParity()
    {
        const L4::FlatBuildResult empty = L4::BuildKarrasLbvh({});
        Require(empty.Succeeded(), "empty LBVH failed");
        Require(empty.bvh.nodes.empty() && empty.bvh.primitives.empty(), "empty LBVH is not empty");

        const std::vector<L4::SoftwarePrimitiveRecord> single = MakeGrid(1u);
        const L4::FlatBuildResult one = L4::BuildKarrasLbvh(single);
        Require(one.Succeeded(), "single-primitive LBVH failed");
        Require(one.bvh.nodes.size() == 1u && one.bvh.nodes[0].IsLeaf(), "single root is not a leaf");
        Require(one.bvh.nodes[0].links.w == L4::kInvalidIndex, "single root parent is not invalid");

        const std::vector<L4::SoftwarePrimitiveRecord> primitives = MakeGrid(17u);
        const L4::FlatBuildResult build = L4::BuildKarrasLbvh(primitives);
        Require(build.Succeeded(), "Karras LBVH failed");
        Require(build.bvh.nodes.size() == 33u, "LBVH N-1/N node invariant failed");
        Require(build.bvh.primitives.size() == 17u, "LBVH leaf count drifted");
        Require(build.bvh.nodes[0].links.w == L4::kInvalidIndex, "LBVH root parent drifted");

        std::vector<std::uint32_t> parentReferences(build.bvh.nodes.size(), 0u);
        std::uint32_t leaves{};
        std::uint32_t interiors{};
        for (std::size_t index = 0u; index < build.bvh.nodes.size(); ++index)
        {
            const L4::SoftwareNodeRecord& node = build.bvh.nodes[index];
            if (node.IsLeaf())
            {
                ++leaves;
            }
            else
            {
                ++interiors;
                Require(node.links.x < build.bvh.nodes.size(), "left child out of range");
                Require(node.links.z < build.bvh.nodes.size(), "right child out of range");
                ++parentReferences[node.links.x];
                ++parentReferences[node.links.z];
            }
            std::uint32_t cursor = static_cast<std::uint32_t>(index);
            std::uint32_t steps{};
            while (build.bvh.nodes[cursor].links.w != L4::kInvalidIndex)
            {
                cursor = build.bvh.nodes[cursor].links.w;
                Require(cursor < build.bvh.nodes.size(), "parent out of range");
                Require(++steps <= build.bvh.nodes.size(), "parent cycle detected");
            }
            Require(cursor == 0u, "node does not terminate at root");
        }
        Require(leaves == 17u && interiors == 16u, "LBVH internal/leaf counts drifted");
        Require(parentReferences[0] == 0u, "root is referenced as a child");
        for (std::size_t index = 1u; index < parentReferences.size(); ++index)
        {
            Require(parentReferences[index] == 1u, "node does not have exactly one parent");
        }
        CheckClosestParity(build.bvh, primitives, MakeCorpus(17u));

        L4::FlatBvh refit = build.bvh;
        refit.primitives[0] = L4::MakeTriangle(
            {-10.0f, -10.0f, 1.0f},
            {-9.0f, -10.0f, 1.0f},
            {-10.0f, -9.0f, 1.0f},
            refit.primitives[0].identity.x);
        Require(L4::RefitBottomUp(refit) == L4::BuildStatus::Success, "bottom-up refit failed");
        Require(refit.nodes[0].boundsMin.x <= -10.0f, "bottom-up root bounds were not updated");
    }

    void TestMalformedAdapterAndInputs()
    {
        const std::vector<L4::SoftwarePrimitiveRecord> primitives = MakeGrid(4u);
        L4::SahBuildResult hierarchy = L4::BuildPrivateBinnedSah(primitives, {1u, 64u});
        Require(hierarchy.Succeeded(), "malformed fixture build failed");
        hierarchy.tree.nodes[0].leftChild = 0u;
        Require(
            L4::FlattenCpuSah(hierarchy.tree).status == L4::BuildStatus::MalformedHierarchy,
            "self-cycle hierarchy was accepted");

        hierarchy = L4::BuildPrivateBinnedSah(primitives, {1u, 64u});
        hierarchy.tree.primitiveOrder[1] = hierarchy.tree.primitiveOrder[0];
        Require(
            L4::FlattenCpuSah(hierarchy.tree).status == L4::BuildStatus::MalformedHierarchy,
            "duplicate primitive order entry was accepted");

        std::vector<L4::SoftwarePrimitiveRecord> duplicateIds = primitives;
        duplicateIds[1].identity.x = duplicateIds[0].identity.x;
        Require(
            L4::BuildKarrasLbvh(duplicateIds).status == L4::BuildStatus::DuplicatePrimitiveId,
            "LBVH accepted a duplicate full logical key");
        Require(
            L4::BuildFlattenedSah(duplicateIds).status == L4::BuildStatus::DuplicatePrimitiveId,
            "SAH accepted a duplicate stable ID");

        const L4::FlatBuildResult shallow = L4::BuildKarrasLbvh(primitives, 1u);
        Require(shallow.status == L4::BuildStatus::DepthOverflow, "depth overflow was not explicit");

        L4::FlatBvh forest{};
        forest.primitives = {primitives[0], primitives[1]};
        forest.nodes.resize(2u);
        forest.nodes[0].links = {0u, 1u, L4::kInvalidIndex, L4::kInvalidIndex};
        forest.nodes[1].links = {1u, 1u, L4::kInvalidIndex, L4::kInvalidIndex};
        Require(
            L4::RefitBottomUp(forest) == L4::BuildStatus::MalformedHierarchy,
            "disconnected leaf forest was accepted");

        L4::FlatBvh badRoot = L4::BuildKarrasLbvh(primitives).bvh;
        badRoot.nodes[0].links.w = 1u;
        Require(
            L4::RefitBottomUp(badRoot) == L4::BuildStatus::MalformedHierarchy,
            "root with a parent was accepted");

        L4::FlatBvh badReciprocity = L4::BuildKarrasLbvh(primitives).bvh;
        const std::uint32_t leftChild = badReciprocity.nodes[0].links.x;
        badReciprocity.nodes[leftChild].links.w = L4::kInvalidIndex;
        Require(
            L4::RefitBottomUp(badReciprocity) == L4::BuildStatus::MalformedHierarchy,
            "non-reciprocal child parent was accepted");

        L4::FlatBvh duplicateCoverage = L4::BuildKarrasLbvh(primitives).bvh;
        std::vector<std::uint32_t> leafIndices{};
        for (std::uint32_t index = 0u; index < duplicateCoverage.nodes.size(); ++index)
        {
            if (duplicateCoverage.nodes[index].IsLeaf())
            {
                leafIndices.push_back(index);
            }
        }
        duplicateCoverage.nodes[leafIndices[1]].links.x =
            duplicateCoverage.nodes[leafIndices[0]].links.x;
        Require(
            L4::RefitBottomUp(duplicateCoverage) == L4::BuildStatus::MalformedHierarchy,
            "duplicate primitive coverage was accepted");
    }

    void TestKarrasCardinalitySweep()
    {
        for (std::uint32_t count = 1u; count <= 128u; ++count)
        {
            const std::vector<L4::SoftwarePrimitiveRecord> primitives = MakeGrid(count);
            const L4::FlatBuildResult build = L4::BuildKarrasLbvh(primitives);
            Require(build.Succeeded(), "LBVH cardinality sweep build failed");
            Require(
                build.bvh.nodes.size() == (static_cast<std::size_t>(count) * 2u) - 1u,
                "LBVH cardinality sweep node count drifted");
            Require(build.bvh.nodes[0].links.w == L4::kInvalidIndex, "sweep root has a parent");
            for (std::size_t index = 0u; index < build.bvh.nodes.size(); ++index)
            {
                std::uint32_t cursor = static_cast<std::uint32_t>(index);
                std::size_t steps{};
                while (build.bvh.nodes[cursor].links.w != L4::kInvalidIndex)
                {
                    cursor = build.bvh.nodes[cursor].links.w;
                    Require(cursor < build.bvh.nodes.size(), "sweep parent out of range");
                    Require(++steps <= build.bvh.nodes.size(), "sweep parent cycle detected");
                }
                Require(cursor == 0u, "sweep node does not terminate at root");
            }
            const std::vector<L4::SoftwareRayRecord> rays = MakeCorpus(count);
            const L4::SoftwareRayRecord selected[] = {rays.front(), rays[count - 1u], rays[count]};
            CheckClosestParity(build.bvh, primitives, selected);
        }

        std::vector<L4::SoftwarePrimitiveRecord> duplicateCentroids{};
        for (std::uint32_t id = 64u; id > 0u; --id)
        {
            duplicateCentroids.push_back(L4::MakeTriangle(
                {-1.0f, -1.0f, 0.0f},
                {1.0f, -1.0f, 0.0f},
                {0.0f, 1.0f, 0.0f},
                id - 1u));
        }
        const L4::FlatBuildResult duplicateBuild = L4::BuildKarrasLbvh(duplicateCentroids);
        Require(duplicateBuild.Succeeded(), "repeated-centroid LBVH failed");
        Require(duplicateBuild.bvh.nodes.size() == 127u, "repeated-centroid topology drifted");
        const L4::FlatBuildResult duplicateSah = L4::BuildFlattenedSah(duplicateCentroids);
        Require(duplicateSah.Succeeded(), "repeated-centroid SAH failed");
        const L4::SoftwareRayRecord ray = L4::MakeRay(
            {0.0f, 0.0f, 2.0f},
            {0.0f, 0.0f, -1.0f},
            0.001f,
            100.0f,
            77u);
        const L4::TraceResult hit = L4::Trace(
            duplicateBuild.bvh,
            ray,
            L4::QueryMode::Closest);
        Require(hit.Succeeded() && hit.IsHit(), "repeated-centroid LBVH did not trace");
        Require(hit.hit.identity.x == 0u, "equal-distance stable ID tie-break drifted");
        const L4::TraceResult sahHit =
            L4::Trace(duplicateSah.bvh, ray, L4::QueryMode::Closest);
        const L4::TraceResult bruteHit =
            L4::BruteForce(duplicateCentroids, ray, L4::QueryMode::Closest);
        Require(sahHit.Succeeded() && sahHit.IsHit(), "repeated-centroid SAH did not trace");
        Require(bruteHit.Succeeded() && bruteHit.IsHit(), "descending brute force did not trace");
        Require(sahHit.hit.identity.x == 0u, "SAH equal-distance stable ID tie-break drifted");
        Require(
            bruteHit.hit.identity.x == 0u,
            "descending brute-force equal-distance stable ID tie-break drifted");
    }

    void TestTraceInputAndMalformedBvh()
    {
        const L4::SoftwareRayRecord valid = L4::MakeRay(
            {0.0f, 0.0f, 2.0f},
            {0.0f, 0.0f, -1.0f},
            0.001f,
            100.0f,
            1u);
        L4::FlatBvh empty{};
        const L4::TraceResult emptyResult = L4::Trace(empty, valid, L4::QueryMode::Closest);
        Require(emptyResult.Succeeded() && !emptyResult.IsHit(), "empty BVH did not miss cleanly");

        const L4::SoftwareRayRecord invalid = L4::MakeRay(
            {0.0f, 0.0f, 2.0f},
            {0.0f, 0.0f, -2.0f},
            0.001f,
            100.0f,
            2u);
        const L4::TraceResult invalidResult = L4::Trace(empty, invalid, L4::QueryMode::Closest);
        Require(invalidResult.status == L4::TraceStatus::InvalidInput, "non-unit ray was accepted");
        Require(invalidResult.counters.invalidRays == 1u, "invalid ray counter drifted");

        L4::SoftwareRayRecord modeMismatch = valid;
        modeMismatch.query.y = static_cast<std::uint32_t>(L4::QueryMode::Any);
        Require(
            L4::Trace(empty, modeMismatch, L4::QueryMode::Closest).status ==
                L4::TraceStatus::InvalidInput,
            "record/API query-mode mismatch was accepted");
        modeMismatch.query.y = 99u;
        Require(
            L4::BruteForce({}, modeMismatch, L4::QueryMode::Closest).status ==
                L4::TraceStatus::InvalidInput,
            "unknown record query mode was accepted");

        const std::vector<L4::SoftwarePrimitiveRecord> boundaryTriangle = {
            L4::MakeTriangle({-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4u)};
        const L4::SoftwareRayRecord boundaryRay = L4::MakeRay(
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, -1.0f},
            0.0f,
            1.0f,
            3u);
        Require(
            !L4::BruteForce(boundaryTriangle, boundaryRay, L4::QueryMode::Closest).IsHit(),
            "original tMax stopped being exclusive");

        L4::GpuTraversalCounterReadback gpuCounters{};
        for (std::size_t index = 0u; index < gpuCounters.slots.size(); ++index)
        {
            gpuCounters.slots[index] = static_cast<std::uint32_t>(10u + index);
        }
        const L4::TraversalCounters decoded = L4::DecodeGpuTraversalCounters(gpuCounters, 23u, 7u);
        Require(decoded.nodeTests == 10u && decoded.triangleTests == 11u, "GPU counter slots drifted");
        Require(decoded.rays == 23u && decoded.hits == 7u, "GPU dispatch totals drifted");
        Require(
            decoded.maximumLeafOccupancy == 18u &&
                L4::kGpuTraversalCounterSaturated == 0xffffffffu,
            "GPU occupancy/saturation contract drifted");

        const L4::FlatBuildResult build = L4::BuildFlattenedSah(MakeGrid(2u), {1u, 64u});
        Require(build.Succeeded(), "malformed traversal fixture failed to build");
        L4::FlatBvh malformed = build.bvh;
        malformed.nodes[0].links.x = 9999u;
        const L4::TraceResult malformedResult = L4::Trace(
            malformed,
            valid,
            L4::QueryMode::Closest);
        Require(malformedResult.status == L4::TraceStatus::MalformedBvh, "bad child was accepted");
        Require(malformedResult.counters.invalidHits == 1u, "invalid hit counter drifted");

        malformed = build.bvh;
        malformed.nodes[0].links.x = 0u;
        const L4::TraceResult cycleResult = L4::Trace(
            malformed,
            valid,
            L4::QueryMode::Closest);
        Require(cycleResult.status == L4::TraceStatus::MalformedBvh, "child cycle was not bounded");
        Require(cycleResult.counters.invalidHits == 1u, "cycle invalid counter drifted");
    }

    void TestBenchmarkHarnessSmoke()
    {
        const std::vector<L4::SoftwarePrimitiveRecord> primitives = MakeGrid(16u);
        const std::vector<L4::SoftwareRayRecord> rays = MakeCorpus(16u);
        const L4::BenchmarkResult benchmark =
            L4::RunCpuMirrorBenchmark(primitives, rays, 2u, {2u, 64u});
        Require(
            benchmark.flattenedSah.buildStatus == L4::BuildStatus::Success,
            "SAH benchmark build failed");
        Require(
            benchmark.lbvh.buildStatus == L4::BuildStatus::Success,
            "LBVH benchmark build failed");
        Require(benchmark.flattenedSah.memoryBytes != 0u, "SAH memory was not reported");
        Require(benchmark.lbvh.memoryBytes != 0u, "LBVH memory was not reported");
        Require(
            benchmark.flattenedSah.counters.rays == rays.size() * 2u,
            "SAH benchmark ray count drifted");
        Require(
            benchmark.lbvh.counters.rays == rays.size() * 2u,
            "LBVH benchmark ray count drifted");
        Require(benchmark.flattenedSah.traceMilliseconds >= 0.0, "SAH trace time is invalid");
        Require(benchmark.lbvh.traceMilliseconds >= 0.0, "LBVH trace time is invalid");
        Require(
            benchmark.flattenedSah.measurementDomain == L4::MeasurementDomain::CpuMirror &&
                !benchmark.flattenedSah.gpuMeasured,
            "SAH smoke timing was mislabeled as GPU evidence");
        Require(
            benchmark.lbvh.measurementDomain == L4::MeasurementDomain::CpuMirror &&
                !benchmark.lbvh.gpuMeasured,
            "LBVH smoke timing was mislabeled as GPU evidence");
    }

    template <typename Function>
    bool RunTest(const std::string_view name, Function&& function)
    {
        try
        {
            function();
            std::cout << "[PASS] " << name << '\n';
            return true;
        }
        catch (const std::exception& exception)
        {
            std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
            return false;
        }
    }
}

int main()
{
    std::uint32_t passed{};
    passed += RunTest("flattened SAH conversion and fixed-ray parity", TestFlattenedSahAndParity) ? 1u : 0u;
    passed += RunTest("canonical L3 SAH flatten adapter", TestCanonicalL3BinnedSahAdapter) ? 1u : 0u;
    passed += RunTest("bounded stack overflow", TestExplicitStackOverflow) ? 1u : 0u;
    passed += RunTest("Morton stable radix and duplicate centroids", TestMortonStableRadixAndDuplicateCentroids) ? 1u : 0u;
    passed += RunTest("Karras LBVH topology, bottom-up bounds, parity", TestKarrasLbvhTopologyAndParity) ? 1u : 0u;
    passed += RunTest("Karras cardinality and repeated-centroid sweep", TestKarrasCardinalitySweep) ? 1u : 0u;
    passed += RunTest("malformed adapters and logical keys", TestMalformedAdapterAndInputs) ? 1u : 0u;
    passed += RunTest("trace input and malformed BVH", TestTraceInputAndMalformedBvh) ? 1u : 0u;
    passed += RunTest("benchmark harness smoke", TestBenchmarkHarnessSmoke) ? 1u : 0u;
    std::cout << "L4 tests: " << passed << "/9 groups, " << gAssertions << " assertions\n";
    return passed == 9u ? 0 : 1;
}
