#include "AccelerationStructures.hpp"
#include "HardwareRtCapabilities.hpp"
#include "ParityHarness.hpp"
#include "RayQueryBackend.hpp"
#include "RtPipelineBackend.hpp"
#include "RtPipelineTraversalAdapter.hpp"
#include "SbtBuilder.hpp"

#include "contracts/AbiVersion.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace Hw = RenderingEngine::Rt::Hardware;
namespace Abi = RenderingEngine::Contracts::AbiV0;
namespace Gpu = RenderingEngine::Rt::Gpu;

namespace
{
    int failures = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    Abi::GpuHitV0 MakeMiss(const Abi::GpuRayV0& ray)
    {
        Abi::GpuHitV0 hit{};
        hit.positionT.w = ray.directionTMax.w;
        hit.ids = {Abi::kInvalidId, Abi::kInvalidId, Abi::kInvalidId, Abi::kInvalidId};
        hit.metadata = {ray.query.x, Abi::HitKindMiss, Abi::HitFlagNone, 0u};
        return hit;
    }

    Abi::GpuHitV0 MakeHit(const Abi::GpuRayV0& ray, std::uint32_t primitiveId, float t)
    {
        Abi::GpuHitV0 hit{};
        hit.positionT = {0.0f, 0.0f, 0.0f, t};
        hit.geometricNormalBaryU = {0.0f, 1.0f, 0.0f, 0.25f};
        hit.shadingNormalBaryV = {0.0f, 1.0f, 0.0f, 0.25f};
        hit.ids = {7u, primitiveId, 9u, 11u};
        hit.metadata = {ray.query.x, Abi::HitKindTriangle, Abi::HitFlagFrontFace, 0u};
        return hit;
    }

    void TestUpdatePolicy()
    {
        const Hw::SceneBuildFingerprint initial{1u, 10u, 2u, true};
        Check(Hw::ChooseSceneUpdate(nullptr, initial) == Hw::SceneUpdateDecision::RebuildBlasAndTlas,
            "first scene build rebuilds BLAS and TLAS");
        Check(Hw::ChooseSceneUpdate(&initial, initial) == Hw::SceneUpdateDecision::NoBuild,
            "identical scene does not rebuild");
        const Hw::SceneBuildFingerprint moved{1u, 11u, 2u, true};
        Check(Hw::ChooseSceneUpdate(&initial, moved) == Hw::SceneUpdateDecision::RefitTlas,
            "rigid transform change refits TLAS");
        const Hw::SceneBuildFingerprint resized{1u, 11u, 3u, true};
        Check(Hw::ChooseSceneUpdate(&initial, resized) == Hw::SceneUpdateDecision::RebuildTlas,
            "instance count change rebuilds TLAS only");
        const Hw::SceneBuildFingerprint topology{2u, 11u, 2u, true};
        Check(Hw::ChooseSceneUpdate(&initial, topology) == Hw::SceneUpdateDecision::RebuildBlasAndTlas,
            "topology change rebuilds BLAS and TLAS");
        const Hw::SceneBuildFingerprint updateDisabled{1u, 10u, 2u, false};
        Check(Hw::ChooseSceneUpdate(&updateDisabled, initial) == Hw::SceneUpdateDecision::RebuildTlas,
            "enabling TLAS refit capability rebuilds the TLAS");
        Check(Hw::ChooseSceneUpdate(&initial, updateDisabled) == Hw::SceneUpdateDecision::RebuildTlas,
            "changing the retained TLAS update capability is explicit");
        Check(Hw::ChooseTriangleGeometryFlags(false, true) == VK_GEOMETRY_OPAQUE_BIT_KHR,
            "opaque two-sided geometry may bypass candidate shaders");
        Check(Hw::ChooseTriangleGeometryFlags(true, true) == 0u &&
            Hw::ChooseTriangleGeometryFlags(false, false) == 0u,
            "alpha-mask and single-sided geometry remain non-opaque");
    }

    void TestImmutableBuildSignatures()
    {
        Hw::TriangleGeometryInput geometry{};
        geometry.vertexAddress = 0x1000u;
        geometry.vertexStride = 64u;
        geometry.maxVertex = 7u;
        geometry.indexAddress = 0x2000u;
        geometry.indexType = VK_INDEX_TYPE_UINT32;
        geometry.transformAddress = 0x3000u;
        geometry.primitiveCount = 4u;
        geometry.primitiveOffset = 12u;
        geometry.firstVertex = 2u;
        geometry.transformOffset = 0u;
        geometry.geometryFlags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        Check(Hw::IsTriangleGeometryInputValid(geometry),
            "a complete indexed-triangle geometry is accepted");
        Hw::TriangleGeometryInput invalid = geometry;
        invalid.vertexAddress = 0u;
        Check(!Hw::IsTriangleGeometryInputValid(invalid),
            "BLAS recording rejects a zero vertex address");
        invalid = geometry;
        invalid.indexAddress = 0u;
        Check(!Hw::IsTriangleGeometryInputValid(invalid),
            "BLAS recording rejects a zero index address");
        invalid = geometry;
        invalid.vertexStride = 0u;
        Check(!Hw::IsTriangleGeometryInputValid(invalid),
            "BLAS recording rejects a zero vertex stride");

        const Hw::TriangleGeometryBuildSignature signature =
            Hw::CaptureTriangleGeometryBuildSignature(geometry);
        Check(Hw::IsBottomLevelUpdateCompatible(
                  std::span<const Hw::TriangleGeometryBuildSignature>(&signature, 1u),
                  std::span<const Hw::TriangleGeometryInput>(&geometry, 1u)),
            "an exact BLAS build description is UPDATE compatible");

        Hw::TriangleGeometryInput changed = geometry;
        changed.primitiveCount -= 1u;
        Check(!Hw::IsBottomLevelUpdateCompatible(
                  std::span<const Hw::TriangleGeometryBuildSignature>(&signature, 1u),
                  std::span<const Hw::TriangleGeometryInput>(&changed, 1u)),
            "BLAS UPDATE cannot change primitive count");
        changed = geometry;
        changed.geometryFlags = 0u;
        Check(!Hw::IsBottomLevelUpdateCompatible(
                  std::span<const Hw::TriangleGeometryBuildSignature>(&signature, 1u),
                  std::span<const Hw::TriangleGeometryInput>(&changed, 1u)),
            "BLAS UPDATE cannot change geometry flags");
        changed = geometry;
        changed.indexAddress += 4u;
        Check(!Hw::IsBottomLevelUpdateCompatible(
                  std::span<const Hw::TriangleGeometryBuildSignature>(&signature, 1u),
                  std::span<const Hw::TriangleGeometryInput>(&changed, 1u)),
            "BLAS UPDATE cannot change immutable index input");

        Check(Hw::AreCompactionStatesValid(
                  Hw::AccelerationStructureState::Ready,
                  Hw::AccelerationStructureState::Allocated),
            "compaction requires a completed BUILD source and fresh destination");
        Check(!Hw::AreCompactionStatesValid(
                  Hw::AccelerationStructureState::BuildRecorded,
                  Hw::AccelerationStructureState::Allocated) &&
                !Hw::AreCompactionStatesValid(
                  Hw::AccelerationStructureState::Ready,
                  Hw::AccelerationStructureState::Ready),
            "compaction rejects incomplete sources and reused destinations");
    }

    void TestCapabilityFeatureChain()
    {
        Hw::HardwareRtDeviceFeatureChain rayQueryOnly(false);
        Check(rayQueryOnly.synchronization2.synchronization2 == VK_TRUE &&
                rayQueryOnly.rayQuery.pNext == &rayQueryOnly.synchronization2,
            "Ray Query device chain explicitly enables synchronization2");
        Hw::HardwareRtDeviceFeatureChain withPipeline(true);
        Check(withPipeline.rayQuery.pNext == &withPipeline.rtPipeline &&
                withPipeline.rtPipeline.pNext == &withPipeline.synchronization2 &&
                withPipeline.synchronization2.synchronization2 == VK_TRUE,
            "RT Pipeline device chain retains synchronization2");

        const auto vulkan13 = Hw::RequiredHardwareRtDeviceExtensions(false, VK_API_VERSION_1_3);
        const auto vulkan12 = Hw::RequiredHardwareRtDeviceExtensions(false, VK_API_VERSION_1_2);
        const auto contains = [](const std::vector<const char*>& extensions, const std::string_view target)
        {
            return std::any_of(extensions.begin(), extensions.end(), [target](const char* extension)
            {
                return std::string_view(extension) == target;
            });
        };
        Check(!contains(vulkan13, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
            "Vulkan 1.3 uses core synchronization2");
        Check(contains(vulkan12, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
            "pre-1.3 device creation requests VK_KHR_synchronization2");
    }

    void TestRayQueryAbiV1Adapter()
    {
        Check(sizeof(Hw::RayQueryPushConstants) == 24u &&
                sizeof(Hw::RayBatchPushConstants) == 24u,
            "Ray Query and RT Pipeline push constants carry independent v1 ray/hit offsets");
        Check(Hw::kCanonicalSceneConstantsBinding == 0u &&
                Hw::kCanonicalSceneVerticesBinding == 1u &&
                Hw::kCanonicalSceneIndicesBinding == 2u &&
                Hw::kCanonicalSceneGeometriesBinding == 3u &&
                Hw::kCanonicalSceneInstancesBinding == 4u &&
                Hw::kCanonicalSceneMaterialsBinding == 5u &&
                Hw::kCanonicalSceneLightsBinding == 6u,
            "Ray Query scene layout retains canonical bindings 0..6");

        Hw::RayQueryBackend backend;
        Hw::RayQueryTraversalAdapter adapter(backend, 4u, 2u);
        const Gpu::GpuTraversalBackendDescriptor descriptor = adapter.Descriptor();
        Check(descriptor.stableToken == "hardware-ray-query" && descriptor.supportsClosest &&
                descriptor.supportsAny && descriptor.requiresAccelerationStructure,
            "Ray Query adapter advertises the shared hardware backend contract");

        const auto fakeCommandBuffer = reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(1u));
        const auto fakeSceneSet = reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(2u));
        const auto fakeTraversalSet = reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(3u));

        Gpu::GpuTraceBatch trace{};
        trace.commandBuffer = fakeCommandBuffer;
        trace.canonicalSceneSet = fakeSceneSet;
        trace.traversalSet = fakeTraversalSet;
        trace.sceneFingerprint = 17u;
        trace.sceneGeneration = 3u;
        trace.rayOffset = 5u;
        trace.hitOffset = 11u;
        trace.rayCount = 2u;
        const Gpu::GpuTraversalStatus beforeBuild = adapter.RecordTraceClosestBatch(trace);
        Check(!beforeBuild && beforeBuild.code == Gpu::GpuTraversalStatusCode::MissingScene,
            "Ray Query adapter rejects trace before scene identity is accepted");

        Gpu::GpuSceneBuildRequest scene{};
        scene.commandBuffer = fakeCommandBuffer;
        scene.canonicalSceneSet = fakeSceneSet;
        scene.sceneFingerprint = trace.sceneFingerprint;
        scene.sceneGeneration = trace.sceneGeneration;
        const Gpu::GpuTraversalStatus sceneStatus = adapter.BuildOrUpdateScene(scene);
        Check(static_cast<bool>(sceneStatus),
            "Ray Query adapter accepts and remembers a non-zero scene fingerprint/generation");

        Gpu::GpuTraceBatch wrongScene = trace;
        wrongScene.sceneGeneration += 1u;
        const Gpu::GpuTraversalStatus wrongIdentity = adapter.RecordTraceAnyBatch(wrongScene);
        Check(!wrongIdentity && wrongIdentity.code == Gpu::GpuTraversalStatusCode::MissingScene,
            "Ray Query adapter rejects a trace with a stale scene generation");

        const Gpu::GpuTraversalStatus forwarded = adapter.RecordTraceClosestBatch(trace);
        Check(!forwarded && forwarded.code == Gpu::GpuTraversalStatusCode::RecordingFailed,
            "Ray Query adapter propagates an uninitialized command-recording failure");

        Gpu::GpuTraceBatch overflow = trace;
        overflow.rayOffset = std::numeric_limits<std::uint32_t>::max();
        const Gpu::GpuTraversalStatus overflowStatus = adapter.RecordTraceAnyBatch(overflow);
        Check(!overflowStatus && overflowStatus.code == Gpu::GpuTraversalStatusCode::InvalidArgument,
            "Ray Query adapter rejects overflowing independent queue offsets");

        const Hw::Status emptyBatch = backend.RecordTraceClosestBatch(
            fakeCommandBuffer, fakeSceneSet, fakeTraversalSet, 0u, 0u, 0u, 0u);
        Check(!emptyBatch,
            "Ray Query host record interface reports invalid input instead of silently returning");
    }

    void TestRtPipelineAbiV1Adapter()
    {
        Hw::RtPipelineBackend backend;
        Hw::RtPipelineTraversalAdapter adapter(backend, 7u, 3u);
        const Gpu::GpuTraversalBackendDescriptor descriptor = adapter.Descriptor();
        Check(descriptor.stableToken == "hardware-rt-pipeline" &&
                descriptor.supportsClosest && descriptor.supportsAny &&
                descriptor.requiresAccelerationStructure,
            "RT Pipeline adapter advertises the shared hardware backend contract");

        const auto fakeCommandBuffer =
            reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(11u));
        const auto fakeSceneSet =
            reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(12u));
        const auto fakeTraversalSet =
            reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(13u));
        Gpu::GpuTraceBatch trace{};
        trace.commandBuffer = fakeCommandBuffer;
        trace.canonicalSceneSet = fakeSceneSet;
        trace.traversalSet = fakeTraversalSet;
        trace.sceneFingerprint = 31u;
        trace.sceneGeneration = 9u;
        trace.rayOffset = 23u;
        trace.hitOffset = 41u;
        trace.rayCount = 4u;
        const Gpu::GpuTraversalStatus beforeBuild =
            adapter.RecordTraceAnyBatch(trace);
        Check(!beforeBuild && beforeBuild.code == Gpu::GpuTraversalStatusCode::MissingScene,
            "RT Pipeline adapter rejects trace before scene identity is accepted");

        Gpu::GpuSceneBuildRequest scene{};
        scene.commandBuffer = fakeCommandBuffer;
        scene.canonicalSceneSet = fakeSceneSet;
        scene.sceneFingerprint = trace.sceneFingerprint;
        scene.sceneGeneration = trace.sceneGeneration;
        Check(static_cast<bool>(adapter.BuildOrUpdateScene(scene)),
            "RT Pipeline adapter accepts canonical scene identity");

        Gpu::GpuTraceBatch stale = trace;
        ++stale.sceneGeneration;
        const Gpu::GpuTraversalStatus staleStatus =
            adapter.RecordTraceClosestBatch(stale);
        Check(!staleStatus && staleStatus.code == Gpu::GpuTraversalStatusCode::MissingScene,
            "RT Pipeline adapter rejects a stale scene generation");

        Gpu::GpuTraceBatch overflow = trace;
        overflow.hitOffset = (std::numeric_limits<std::uint32_t>::max)();
        const Gpu::GpuTraversalStatus overflowStatus =
            adapter.RecordTraceAnyBatch(overflow);
        Check(!overflowStatus &&
                overflowStatus.code == Gpu::GpuTraversalStatusCode::InvalidArgument,
            "RT Pipeline adapter rejects overflowing independent hit offsets");

        const Gpu::GpuTraversalStatus forwarded =
            adapter.RecordTraceClosestBatch(trace);
        Check(!forwarded && forwarded.code == Gpu::GpuTraversalStatusCode::RecordingFailed,
            "RT Pipeline adapter forwards closest batches through the Status-returning offset API");
        const Hw::Status emptyBatch = backend.RecordTraceAnyBatch(
            fakeCommandBuffer, fakeSceneSet, fakeTraversalSet,
            5u, 8u, 0u, 0u);
        Check(!emptyBatch && emptyBatch.result == VK_ERROR_VALIDATION_FAILED_EXT,
            "RT Pipeline Status API rejects an empty offset batch");
    }

    void TestSbtLayout()
    {
        Hw::HardwareRtLimits limits{};
        limits.shaderGroupHandleSize = 32u;
        limits.shaderGroupHandleAlignment = 32u;
        limits.shaderGroupBaseAlignment = 64u;
        limits.maxShaderGroupStride = 4096u;
        const std::array<Hw::SbtRecordData, 3> records{};
        Hw::SbtComputedLayout layout{};
        const Hw::Status status = Hw::ComputeSbtLayout(limits, Hw::SbtGroupCounts{}, records, layout);
        Check(status.Succeeded(), "32/32/64 SBT layout is accepted");
        Check(layout.recordStride == 32u, "SBT record stride uses handle alignment");
        Check(layout.rayGenerationOffset == 0u, "raygen begins at zero");
        Check(layout.missOffset == 64u && layout.hitOffset == 128u,
            "miss and hit regions use 64-byte base alignment");
        Check(layout.totalSize == 192u, "SBT keeps aligned region boundaries");
        Check(Hw::kRtPipelinePayloadSize == 32u && Hw::kRtPipelineTriangleAttributeSize == 8u,
            "RT payload and triangle attributes remain bounded");
        Check(Hw::ComputeRtDispatchChunkCount(0u, 1024u) == 0u &&
                Hw::ComputeRtDispatchChunkCount(1024u, 1024u) == 1u &&
                Hw::ComputeRtDispatchChunkCount(1025u, 1024u) == 2u &&
                Hw::ComputeRtDispatchChunkCount(1025u, 0u) == 0u,
            "RT dispatch chunks respect maxRayDispatchInvocationCount");
        Check(Hw::ComputeRayQueryDispatchChunkCount(0u, 1u) == 0u &&
                Hw::ComputeRayQueryDispatchChunkCount(64u, 1u) == 1u &&
                Hw::ComputeRayQueryDispatchChunkCount(65u, 1u) == 2u &&
                Hw::ComputeRayQueryDispatchChunkCount(65u, 0u) == 0u,
            "Ray Query dispatch chunks respect maxComputeWorkGroupCountX");
    }

    void TestCorpusAndParity()
    {
        const std::vector<Abi::GpuRayV0> rays = Hw::BuildFixedHardwareRayCorpus();
        Check(rays.size() == 16u, "fixed hardware corpus has 16 rays");
        for (std::size_t index = 0u; index < rays.size(); ++index)
        {
            const auto& direction = rays[index].directionTMax;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                direction.z * direction.z);
            Check(std::abs(length - 1.0f) < 1.0e-6f, "corpus directions are normalized");
            Check(rays[index].query.x == index, "corpus ray IDs are stable");
            Check(rays[index].reserved0.x == 0u && rays[index].reserved0.y == 0u &&
                rays[index].reserved0.z == 0u && rays[index].reserved0.w == 0u,
                "corpus reserved fields are zero");
        }

        std::vector<Abi::GpuHitV0> reference;
        reference.reserve(rays.size());
        for (std::size_t index = 0u; index < rays.size(); ++index)
        {
            reference.push_back(index == rays.size() - 1u
                ? MakeMiss(rays[index])
                : MakeHit(rays[index], static_cast<std::uint32_t>(index), 2.0f));
        }
        std::vector<Abi::GpuHitV0> candidate = reference;
        candidate[2].positionT.w *= 1.0f + 5.0e-5f;
        const Hw::ParityReport pass = Hw::CompareHitCorpus(rays, reference, candidate);
        Check(pass.Passed(), "relative t error inside 1e-4 passes");
        candidate[2].positionT.w = 2.01f;
        const Hw::ParityReport fail = Hw::CompareHitCorpus(rays, reference, candidate);
        Check(!fail.Passed() && !fail.mismatches.empty() &&
            fail.mismatches.front().kind == Hw::HitMismatchKind::Distance,
            "relative t error outside threshold fails");

        candidate = reference;
        candidate[4].ids.y = 99u;
        const Hw::EquivalentHitSet equivalent{rays[4].query.x, {{7u, 99u, 9u, 11u}}};
        const Hw::ParityReport equivalentPass = Hw::CompareHitCorpus(
            rays, reference, candidate, std::span<const Hw::EquivalentHitSet>(&equivalent, 1u));
        Check(equivalentPass.Passed() && equivalentPass.equivalentIdentityCount == 1u,
            "declared ambiguous hit identity is accepted");

        candidate = reference;
        candidate.back().ids.x = 0u;
        const Hw::ParityReport malformedMiss = Hw::CompareHitCorpus(rays, reference, candidate);
        Check(!malformedMiss.Passed() && malformedMiss.mismatches.front().kind ==
            Hw::HitMismatchKind::MissEncoding,
            "miss parity requires the exact canonical miss encoding");

        candidate = reference;
        candidate[3].geometricNormalBaryU.x = std::numeric_limits<float>::quiet_NaN();
        const Hw::ParityReport nonFinite = Hw::CompareHitCorpus(rays, reference, candidate);
        Check(!nonFinite.Passed() && nonFinite.mismatches.front().kind == Hw::HitMismatchKind::Invalid,
            "non-finite hit fields cannot pass parity");

        candidate = reference;
        candidate[5].metadata.z |= 1u << 31u;
        const Hw::ParityReport unknownFlags = Hw::CompareHitCorpus(rays, reference, candidate);
        Check(!unknownFlags.Passed() && unknownFlags.mismatches.front().kind == Hw::HitMismatchKind::Invalid,
            "unknown hit flags cannot pass parity");
    }

    void ProbePhysicalDevices()
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "RenderingEngine.HardwareRT.Tests";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        VkInstance instance = VK_NULL_HANDLE;
        const VkResult createResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if (createResult != VK_SUCCESS)
        {
            std::cout << "PROBE: vkCreateInstance unavailable (" << createResult << ")\n";
            return;
        }
        std::uint32_t count = 0u;
        if (vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count != 0u)
        {
            std::vector<VkPhysicalDevice> devices(count);
            if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) == VK_SUCCESS)
            {
                for (VkPhysicalDevice device : devices)
                {
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(device, &properties);
                    const Hw::HardwareRtCapabilityReport report = Hw::QueryHardwareRtCapabilities(device);
                    std::cout << "PROBE: " << properties.deviceName
                              << " rayQuery=" << report.SupportsRayQuery()
                              << " rtPipeline=" << report.SupportsRtPipeline()
                              << " scratchAlign=" << report.limits.minAccelerationStructureScratchOffsetAlignment
                              << " computeGroupsX=" << report.limits.maxComputeWorkGroupCountX
                              << " sbt=" << report.limits.shaderGroupHandleSize << '/'
                              << report.limits.shaderGroupHandleAlignment << '/'
                              << report.limits.shaderGroupBaseAlignment << '\n';
                }
            }
        }
        vkDestroyInstance(instance, nullptr);
    }
}

int main(int argc, char** argv)
{
    TestUpdatePolicy();
    TestImmutableBuildSignatures();
    TestCapabilityFeatureChain();
    TestRayQueryAbiV1Adapter();
    TestRtPipelineAbiV1Adapter();
    TestSbtLayout();
    TestCorpusAndParity();
    if (argc > 1 && std::string_view(argv[1]) == "--probe")
    {
        ProbePhysicalDevices();
    }
    if (failures == 0)
    {
        std::cout << "Hardware RT module tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
