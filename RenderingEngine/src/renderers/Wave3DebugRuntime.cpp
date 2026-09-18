#include "renderers/Wave3DebugRuntime.hpp"

#include "DeviceBuffer.hpp"
#include "DeviceDispatch.hpp"
#include "VulkanWavefrontRecorder.hpp"
#include "WavefrontSchedule.hpp"
#include "contracts/AbiV3.hpp"
#include "contracts/ReconstructionAbiV2.hpp"
#include "reconstruction/VulkanReconstructionRecorder.hpp"
#include "renderers/ReSTIRFrameParameters.hpp"
#include "restir/VulkanProductionRuntime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RenderingEngine::Renderers
{
    namespace
    {
        namespace Hardware = Rt::Hardware;
        namespace L8 = rendering::reconstruction;
        namespace Abi3 = Contracts::AbiV3;
        namespace ReSTIRProduction = Restir;

        constexpr std::uint32_t kMaximumBounceCount = 12u;
        constexpr std::uint32_t kL8SetsPerFrame = 12u;
        constexpr std::uint32_t kMaximumAtrousIterations = 8u;
        constexpr VkBufferUsageFlags kStorageUsage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        constexpr std::uint32_t kReSTIRHistorySlotCount =
            2u * kWave3DebugFrameCount;

        void Check(const VkResult result, const std::string_view operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(
                    std::string(operation) + " failed with VkResult "
                    + std::to_string(static_cast<int>(result)));
            }
        }

        template <typename Status>
        void RequireStatus(const Status& status, const std::string_view operation)
        {
            if (!static_cast<bool>(status))
            {
                throw std::runtime_error(
                    std::string(operation) + " failed: " + status.message);
            }
        }

        [[nodiscard]] std::vector<std::uint32_t> ReadSpirv(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw std::runtime_error(
                    "Unable to open Wave 3 production shader: " + path.string());
            }
            const std::streamsize byteCount = stream.tellg();
            if (byteCount <= 0 || byteCount % 4 != 0)
            {
                throw std::runtime_error(
                    "Invalid Wave 3 SPIR-V byte count: " + path.string());
            }
            stream.seekg(0, std::ios::beg);
            std::vector<std::uint32_t> words(
                static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
            stream.read(reinterpret_cast<char*>(words.data()), byteCount);
            if (!stream)
            {
                throw std::runtime_error(
                    "Unable to read Wave 3 SPIR-V: " + path.string());
            }
            return words;
        }

        [[nodiscard]] Hardware::DeviceBuffer CreateBuffer(
            const Hardware::DeviceBufferAllocator& allocator,
            const VkDeviceSize size,
            const VkBufferUsageFlags usage,
            const bool mapped)
        {
            if (size == 0u)
            {
                throw std::invalid_argument("Wave 3 cannot allocate an empty buffer.");
            }
            Hardware::DeviceBuffer result;
            const VkMemoryPropertyFlags properties = mapped
                ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            RequireStatus(allocator.Create(
                size, usage, properties, false, mapped, result),
                "DeviceBufferAllocator::Create(Wave3)");
            if (mapped)
            {
                std::memset(result.MappedData(), 0, static_cast<std::size_t>(size));
            }
            return result;
        }

        [[nodiscard]] VkDescriptorBufferInfo Descriptor(
            const Hardware::DeviceBuffer& buffer) noexcept
        {
            return {buffer.Handle(), 0u, buffer.Size()};
        }

        struct alignas(16) Matrix4 final
        {
            std::array<float, 16u> values{};
        };
        static_assert(sizeof(Matrix4) == 64u);

        [[nodiscard]] Matrix4 IdentityMatrix() noexcept
        {
            Matrix4 result{};
            result.values[0u] = 1.0f;
            result.values[5u] = 1.0f;
            result.values[10u] = 1.0f;
            result.values[15u] = 1.0f;
            return result;
        }

        [[nodiscard]] Matrix4 Multiply(
            const Matrix4& left,
            const Matrix4& right) noexcept
        {
            Matrix4 result{};
            for (std::size_t row = 0u; row < 4u; ++row)
            {
                for (std::size_t column = 0u; column < 4u; ++column)
                {
                    float value = 0.0f;
                    for (std::size_t inner = 0u; inner < 4u; ++inner)
                    {
                        value += left.values[row * 4u + inner]
                            * right.values[inner * 4u + column];
                    }
                    result.values[row * 4u + column] = value;
                }
            }
            return result;
        }

        [[nodiscard]] Matrix4 MakeWorldToView(
            const Wave3DebugCamera& camera) noexcept
        {
            Matrix4 result = IdentityMatrix();
            result.values[0u] = camera.right.x;
            result.values[1u] = camera.right.y;
            result.values[2u] = camera.right.z;
            result.values[3u] = -RenderingEngine::Dot(
                camera.right, camera.position);
            result.values[4u] = camera.up.x;
            result.values[5u] = camera.up.y;
            result.values[6u] = camera.up.z;
            result.values[7u] = -RenderingEngine::Dot(
                camera.up, camera.position);
            result.values[8u] = -camera.forward.x;
            result.values[9u] = -camera.forward.y;
            result.values[10u] = -camera.forward.z;
            result.values[11u] = RenderingEngine::Dot(
                camera.forward, camera.position);
            return result;
        }

        [[nodiscard]] Matrix4 MakeViewProjection(
            const Wave3DebugCamera& camera,
            const VkExtent2D extent) noexcept
        {
            constexpr float nearPlane = 0.01f;
            constexpr float farPlane = 10000.0f;
            constexpr float degreesToRadians =
                std::numbers::pi_v<float> / 180.0f;
            const float tangent = std::tan(
                camera.verticalFovDegrees * 0.5f * degreesToRadians);
            const float aspect = static_cast<float>(extent.width)
                / static_cast<float>(extent.height);
            Matrix4 projection{};
            projection.values[0u] = 1.0f / (aspect * tangent);
            projection.values[5u] = 1.0f / tangent;
            projection.values[10u] = farPlane / (nearPlane - farPlane);
            projection.values[11u] = farPlane * nearPlane
                / (nearPlane - farPlane);
            projection.values[14u] = -1.0f;
            return Multiply(projection, MakeWorldToView(camera));
        }

        struct alignas(16) MotionConstants final
        {
            Matrix4 currentViewProjection{};
            Matrix4 previousViewProjection{};
            Matrix4 previousWorldToView{};
            std::array<float, 4u> jitter{};
            std::array<std::uint32_t, 4u> extentAndTransformCounts{};
        };
        static_assert(sizeof(MotionConstants) == 224u);

        struct alignas(16) PrepareConstants final
        {
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            float minimumAlbedo = 1.0e-3f;
            std::uint32_t demodulateSpecular = 1u;
        };
        static_assert(sizeof(PrepareConstants) == 16u);

        struct alignas(16) TemporalConstants final
        {
            std::array<std::uint32_t, 4u> extentHistoryReset{};
            std::uint32_t maximumHistoryLength = 32u;
            float minimumColorAlpha = 0.05f;
            float minimumMomentsAlpha = 0.10f;
            float relativeDepthThreshold = 0.02f;
            float absoluteDepthThreshold = 0.01f;
            float normalCosineThreshold = 0.9063078f;
            std::uint32_t requireMaterialId = 1u;
            std::uint32_t requireObjectId = 1u;
        };
        static_assert(sizeof(TemporalConstants) == 48u);

        struct alignas(16) VarianceConstants final
        {
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            std::uint32_t shortHistoryLength = 4u;
            std::uint32_t spatialRadius = 2u;
            float minimumVariance = 1.0e-6f;
            std::uint32_t useTemporalVariance = 1u;
            std::array<float, 2u> padding{};
        };
        static_assert(sizeof(VarianceConstants) == 32u);

        struct alignas(16) AtrousConstants final
        {
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            std::uint32_t step = 1u;
            float phiDepth = 1.0f;
            float phiNormal = 32.0f;
            float phiLuminance = 4.0f;
            float minimumVariance = 1.0e-6f;
            float padding = 0.0f;
        };
        static_assert(sizeof(AtrousConstants) == 32u);

        struct alignas(16) ComposeConstants final
        {
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            std::uint32_t outputMode = 0u;
            std::uint32_t maximumHistoryLength = 32u;
            float minimumAlbedo = 1.0e-3f;
            std::uint32_t demodulateSpecular = 1u;
            std::uint32_t filmSampleCount = 0u;
            std::uint32_t padding = 0u;
        };
        static_assert(sizeof(ComposeConstants) == 32u);

        [[nodiscard]] L8::ReconstructionOutput ToL8Output(
            const ReconstructionMode mode)
        {
            switch (mode)
            {
            case ReconstructionMode::ProgressiveMean:
            case ReconstructionMode::CurrentFrame:
                return L8::ReconstructionOutput::Raw;
            case ReconstructionMode::TemporalAccumulation:
                return L8::ReconstructionOutput::Temporal;
            case ReconstructionMode::SpatialFixedAtrous:
                return L8::ReconstructionOutput::ATrous;
            case ReconstructionMode::Svgf:
                return L8::ReconstructionOutput::Svgf;
            default:
                throw std::invalid_argument(
                    "The shared reconstruction stage received an invalid output mode.");
            }
        }

        [[nodiscard]] std::uint32_t ToComposeOutputMode(
            const RuntimeConfig& config)
        {
            switch (config.debugView)
            {
            case DebugView::BaseColor: return 10u;
            case DebugView::Normal: return 11u;
            case DebugView::Roughness: return 12u;
            case DebugView::Metallic: return 13u;
            case DebugView::Emissive: return 14u;
            case DebugView::Motion: return 4u;
            case DebugView::HistoryLength: return 5u;
            case DebugView::Moments: return 6u;
            case DebugView::Variance: return 7u;
            case DebugView::TemporalAcceptance: return 8u;
            case DebugView::TemporalRejectReasons: return 9u;
            default: break;
            }
            // Reservoir presentation runs after Compose. Never update Film
            // while a display-only debug view is selected.
            if (config.debugView != DebugView::Final) return 0u;
            switch (config.reconstruction)
            {
            case ReconstructionMode::ProgressiveMean: return 15u;
            case ReconstructionMode::TemporalAccumulation: return 1u;
            case ReconstructionMode::SpatialFixedAtrous: return 2u;
            case ReconstructionMode::Svgf: return 3u;
            default: return 0u;
            }
        }
    }

    class Wave3DebugRuntime::Impl final
        : public IVulkanReSTIRReconstructionRecorder
    {
    public:
        ~Impl() { Reset(); }

        void Create(const Wave3DebugRuntimeCreateInfo& createInfo)
        {
            if (device_ != VK_NULL_HANDLE)
            {
                throw std::logic_error(
                    "Wave3DebugRuntime::Create may only be called once.");
            }
            if (createInfo.physicalDevice == VK_NULL_HANDLE
                || createInfo.device == VK_NULL_HANDLE
                || createInfo.outputImageView == VK_NULL_HANDLE
                || createInfo.extent.width == 0u || createInfo.extent.height == 0u
                || createInfo.frameSetLayout == VK_NULL_HANDLE
                || createInfo.canonicalSceneSetLayout == VK_NULL_HANDLE
                || createInfo.softwareTraversalSetLayout == VK_NULL_HANDLE
                || createInfo.rayQueryTraversalSetLayout == VK_NULL_HANDLE
                || createInfo.maximumComputeGroupCountX == 0u
                || createInfo.maximumComputeGroupCountY == 0u)
            {
                throw std::invalid_argument(
                    "Wave3DebugRuntimeCreateInfo is incomplete.");
            }
            for (const VkImageView view : createInfo.signalImageViews)
            {
                if (view == VK_NULL_HANDLE)
                {
                    throw std::invalid_argument(
                        "Wave3DebugRuntime requires all five split-signal images.");
                }
            }

            physicalDevice_ = createInfo.physicalDevice;
            device_ = createInfo.device;
            extent_ = createInfo.extent;
            outputImageView_ = createInfo.outputImageView;
            signalImageViews_ = createInfo.signalImageViews;
            frameSetLayout_ = createInfo.frameSetLayout;
            canonicalSceneSetLayout_ = createInfo.canonicalSceneSetLayout;
            traversalSetLayouts_[0u] = createInfo.softwareTraversalSetLayout;
            traversalSetLayouts_[1u] = createInfo.rayQueryTraversalSetLayout;
            maximumGroupCountX_ = createInfo.maximumComputeGroupCountX;
            maximumGroupCountY_ = createInfo.maximumComputeGroupCountY;
            shaderDirectory_ = createInfo.shaderDirectory;

            RequireStatus(Hardware::DeviceDispatch::Load(
                device_, false, dispatch_),
                "DeviceDispatch::Load(Wave3)");
            allocator_ = std::make_unique<Hardware::DeviceBufferAllocator>(
                physicalDevice_, device_, dispatch_);

            CreateWavefrontResources();
            CreateReconstructionResources();
            CreateWavefrontPipelines();
            CreateReconstructionPipelines();
            CreateReSTIRRecorder();
            ready_ = true;
        }

        void Reset() noexcept
        {
            if (device_ == VK_NULL_HANDLE)
            {
                return;
            }
            ResetReSTIR();
            for (VkPipeline& pipeline : reconstructionPipelines_.passes)
            {
                if (pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
            }
            if (reconstructionPipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(
                    device_, reconstructionPipelineLayout_, nullptr);
                reconstructionPipelineLayout_ = VK_NULL_HANDLE;
            }
            for (auto& backendPipelines : wavefrontPipelines_)
            {
                for (VkPipeline& pipeline : backendPipelines.passes)
                {
                    if (pipeline != VK_NULL_HANDLE)
                    {
                        vkDestroyPipeline(device_, pipeline, nullptr);
                        pipeline = VK_NULL_HANDLE;
                    }
                }
            }
            for (VkPipelineLayout& layout : wavefrontPipelineLayouts_)
            {
                if (layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device_, layout, nullptr);
                    layout = VK_NULL_HANDLE;
                }
            }
            if (reconstructionDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(
                    device_, reconstructionDescriptorPool_, nullptr);
                reconstructionDescriptorPool_ = VK_NULL_HANDLE;
            }
            if (reconstructionSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(
                    device_, reconstructionSetLayout_, nullptr);
                reconstructionSetLayout_ = VK_NULL_HANDLE;
            }
            if (wavefrontDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(
                    device_, wavefrontDescriptorPool_, nullptr);
                wavefrontDescriptorPool_ = VK_NULL_HANDLE;
            }
            if (wavefrontSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(
                    device_, wavefrontSetLayout_, nullptr);
                wavefrontSetLayout_ = VK_NULL_HANDLE;
            }

            ResetBuffers();
            allocator_.reset();
            preparedWavefront_.fill({});
            preparedReconstruction_.fill({});
            preparedReconstructionSets_.fill({});
            ready_ = false;
            hasLastCamera_ = false;
            device_ = VK_NULL_HANDLE;
            physicalDevice_ = VK_NULL_HANDLE;
            extent_ = {};
            outputImageView_ = VK_NULL_HANDLE;
        }

        [[nodiscard]] bool IsReady() const noexcept { return ready_; }

        [[nodiscard]] std::uint32_t WavefrontFatalMask() const noexcept
        {
            const void* const mapped = wavefrontHeaders_.MappedData();
            if (!ready_ || mapped == nullptr)
            {
                return 0u;
            }
            const auto* const headers =
                static_cast<const Wavefront::QueueHeader*>(mapped);
            return headers[static_cast<std::size_t>(
                Wavefront::QueueId::Global)].overflowCount;
        }

        [[nodiscard]] VkDescriptorSetLayout ReconstructionSetLayout() const noexcept
        {
            return reconstructionSetLayout_;
        }

        [[nodiscard]] VkDescriptorSet ReconstructionSet(
            const std::uint32_t frameSlot) const noexcept
        {
            return ready_ && frameSlot < kWave3DebugFrameCount
                ? reconstructionDescriptorSets_[frameSlot * kL8SetsPerFrame]
                : VK_NULL_HANDLE;
        }

        void InvalidateHistories() noexcept
        {
            hasLastCamera_ = false;
            filmSampleCount_ = 0u;
            restirPublishedHistory_ = {};
            restirHasPublishedCamera_ = false;
        }

        void InvalidateProgressiveFilm() noexcept { filmSampleCount_ = 0u; }

        void SetSceneTransforms(
            const std::span<const Contracts::AbiV0::AbiMat4Rows> currentObjectToWorld,
            const std::span<const Contracts::AbiV0::AbiMat4Rows> previousObjectToWorld)
        {
            EnsureReady();
            if (currentObjectToWorld.empty()
                || currentObjectToWorld.size() != previousObjectToWorld.size())
            {
                throw std::invalid_argument(
                    "Wave 3 reconstruction requires one current/previous transform pair per instance.");
            }
            static_assert(sizeof(Contracts::AbiV0::AbiMat4Rows) == sizeof(Matrix4));
            reconstructionCurrentTransforms_.Reset();
            reconstructionPreviousTransforms_.Reset();
            reconstructionCurrentTransforms_ = CreateBuffer(*allocator_,
                currentObjectToWorld.size_bytes(), kStorageUsage, true);
            reconstructionPreviousTransforms_ = CreateBuffer(*allocator_,
                previousObjectToWorld.size_bytes(), kStorageUsage, true);
            std::memcpy(reconstructionCurrentTransforms_.MappedData(),
                currentObjectToWorld.data(), currentObjectToWorld.size_bytes());
            std::memcpy(reconstructionPreviousTransforms_.MappedData(),
                previousObjectToWorld.data(), previousObjectToWorld.size_bytes());
            reconstructionTransformCount_ = static_cast<std::uint32_t>(
                currentObjectToWorld.size());
        }

        [[nodiscard]] Wave3DebugReSTIRTraversalViews ReSTIRTraversalViews(
            const std::uint32_t frameSlot) const noexcept
        {
            if (frameSlot >= kWave3DebugFrameCount
                || !preparedReSTIRPlans_[frameSlot].IsReady()
                || !restirResourcesReady_)
            {
                return {};
            }
            return {
                Descriptor(restirFrameBuffers_[frameSlot][
                    Abi3::RestirBinding::ShadowRayQueue]),
                Descriptor(restirFrameBuffers_[frameSlot][
                    Abi3::RestirBinding::ReferenceVisibility]),
                Descriptor(restirFrameBuffers_[frameSlot][
                    Abi3::RestirBinding::VisibilityResults])};
        }

        void UpdateFrame(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugCamera& camera,
            const std::uint32_t sampleIndex,
            const Wave3DebugFrameBindings& bindings)
        {
            EnsureReady();
            ValidateFrameSlot(frameSlot);
            if (config.transportModel != TransportModel::Pbr
                || config.executionArchitecture != ExecutionArchitecture::Wavefront)
            {
                throw std::invalid_argument(
                    "Wave 3 debug runtime requires PBR transport on GPU Wavefront execution.");
            }
            if (config.backend != TraversalBackend::CanonicalLinearGpu
                && config.backend != TraversalBackend::GpuFlattenedSahBvh
                && config.backend != TraversalBackend::VulkanRayQuery)
            {
                throw std::invalid_argument(
                    "Wavefront requires canonical-linear, flattened-SAH, or Ray Query traversal.");
            }

            Wavefront::WavefrontFrameConstants frame{};
            constexpr float degreesToRadians =
                std::numbers::pi_v<float> / 180.0f;
            const float tangent = std::tan(
                camera.verticalFovDegrees * 0.5f * degreesToRadians);
            frame.cameraPositionTanHalfFov = {
                camera.position.x, camera.position.y, camera.position.z, tangent};
            frame.cameraForwardAspect = {
                camera.forward.x, camera.forward.y, camera.forward.z,
                static_cast<float>(extent_.width)
                    / static_cast<float>(extent_.height)};
            frame.cameraRightTime = {
                camera.right.x, camera.right.y, camera.right.z, 0.0f};
            frame.cameraUpExposure = {
                camera.up.x, camera.up.y, camera.up.z, config.render.exposure};
            frame.imageSample = {
                extent_.width, extent_.height, sampleIndex,
                config.render.maximumBounce};
            frame.capacityModeSeed = {
                wavefrontAllocation_.pathCapacity,
                static_cast<std::uint32_t>(Wavefront::QueueMode::AtomicAppend),
                static_cast<std::uint32_t>(config.render.baseSeed & 0xffffffffull),
                static_cast<std::uint32_t>(config.render.baseSeed >> 32u)};
            frame.dispatchLimits = {
                maximumGroupCountX_, maximumGroupCountY_,
                Wavefront::QueueThreadCount, 0x57a20001u};
            std::memcpy(
                wavefrontFrameBuffers_[frameSlot].MappedData(),
                &frame, sizeof(frame));
            preparedWavefront_[frameSlot] =
                Wavefront::BuildPreparedFramePlan(
                    frame, wavefrontAllocation_, false);
            UpdatePostIntegratorFrame(
                frameSlot, config, camera, sampleIndex, bindings);
        }

        void RecordFrame(
            const VkCommandBuffer commandBuffer,
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugFrameBindings& bindings)
        {
            EnsureReady();
            ValidateFrameSlot(frameSlot);
            if (commandBuffer == VK_NULL_HANDLE
                || bindings.frameSet == VK_NULL_HANDLE
                || bindings.canonicalSceneSet == VK_NULL_HANDLE)
            {
                throw std::invalid_argument(
                    "Wave 3 frame bindings are incomplete.");
            }
            const std::size_t backendIndex = config.backend
                    == TraversalBackend::CanonicalLinearGpu
                    || config.backend == TraversalBackend::GpuFlattenedSahBvh
                ? 0u : config.backend == TraversalBackend::VulkanRayQuery
                    ? 1u : throw std::invalid_argument(
                        "Wave 3 frame has no production traversal variant.");
            const VkDescriptorSet traversalSet = backendIndex == 0u
                ? bindings.softwareTraversalSet
                : bindings.rayQueryTraversalSet;
            if (traversalSet == VK_NULL_HANDLE)
            {
                throw std::invalid_argument(
                    "Wave 3 selected traversal descriptor set is missing.");
            }

            const VkDescriptorSet reconstructionSet =
                reconstructionDescriptorSets_[frameSlot * kL8SetsPerFrame];
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                wavefrontPipelineLayouts_[backendIndex],
                4u, 1u, &reconstructionSet, 0u, nullptr);

            const std::array descriptorSets{
                bindings.frameSet,
                bindings.canonicalSceneSet,
                traversalSet,
                wavefrontDescriptorSets_[frameSlot]};
            Wavefront::VulkanWavefrontFunctions functions{};
            functions.cmdPipelineBarrier2 = vkCmdPipelineBarrier2;
            functions.cmdWriteTimestamp2 = vkCmdWriteTimestamp2;
            functions.cmdBindPipeline = vkCmdBindPipeline;
            functions.cmdBindDescriptorSets = vkCmdBindDescriptorSets;
            functions.cmdPushConstants = vkCmdPushConstants;
            functions.cmdDispatch = vkCmdDispatch;
            functions.cmdDispatchIndirect = vkCmdDispatchIndirect;
            const Wavefront::VulkanWavefrontRecordResult recorded =
                Wavefront::RecordVulkanWavefront(functions, {
                    commandBuffer,
                    preparedWavefront_[frameSlot],
                    wavefrontPipelines_[backendIndex],
                    descriptorSets,
                    wavefrontIndirect_.Handle(),
                    0u,
                    VK_NULL_HANDLE,
                    0u,
                    false});
            if (!recorded.Succeeded())
            {
                throw std::runtime_error(
                    "Wavefront Vulkan recording failed: " + recorded.reason);
            }

            RecordPostIntegratorFrame(
                commandBuffer, frameSlot, config, bindings);

            // The caller owns the backing images and their layouts. A global
            // dependency makes the split AOV writes visible without trying to
            // recover VkImage handles from externally-owned VkImageViews.
            VkMemoryBarrier2 memoryBarrier{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                | VK_ACCESS_2_TRANSFER_READ_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.memoryBarrierCount = 1u;
            dependency.pMemoryBarriers = &memoryBarrier;
            dispatch_.cmdPipelineBarrier2(commandBuffer, &dependency);
        }

        void UpdatePostIntegratorFrame(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugCamera& camera,
            const std::uint32_t sampleIndex,
            const Wave3DebugFrameBindings& bindings)
        {
            EnsureReady();
            ValidateFrameSlot(frameSlot);
            const bool restir = config.directLightingEstimator
                == DirectLightingEstimator::RestirDirectIllumination;
            // L8 owns the final output for every reconstruction selection,
            // including Raw.  Integrators publish one ABI-v2 sample; the Raw
            // plan is a single Compose dispatch while temporal modes add their
            // history/filter stages.  Keeping one output owner prevents Raw
            // from changing meaning when the direct-lighting estimator changes.
            UpdateReconstruction(frameSlot, config, camera, sampleIndex);
            if (restir)
            {
                PrepareReSTIRFrame(
                    frameSlot, config, camera, sampleIndex, bindings);
            }
            else
            {
                preparedReSTIRPlans_[frameSlot] = {};
                preparedReSTIRRequests_[frameSlot] = {};
            }
            lastCamera_ = camera;
            hasLastCamera_ = true;
        }

        void RecordPostIntegratorFrame(
            const VkCommandBuffer commandBuffer,
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugFrameBindings& bindings)
        {
            EnsureReady();
            ValidateFrameSlot(frameSlot);
            if (config.directLightingEstimator
                == DirectLightingEstimator::RestirDirectIllumination)
            {
                RecordReSTIRFrame(
                    commandBuffer, frameSlot, config, bindings);
                return;
            }
            const ReSTIRRuntimeStatus status =
                RecordPreparedReconstruction(
                    commandBuffer, frameSlot, bindings.canonicalSceneSet);
            if (!status)
            {
                throw std::runtime_error(
                    "Shared reconstruction recording failed: " + status.reason);
            }
        }

        [[nodiscard]] Wave3ReSTIRFrameObservation CollectCompletedFrame(
            const std::uint32_t frameSlot) noexcept
        {
            if (frameSlot >= kWave3DebugFrameCount
                || !restirStatisticsReadbackPending_[frameSlot]
                || restirStatisticsReadback_[frameSlot].MappedData() == nullptr)
            {
                return {};
            }
            restirStatisticsReadbackPending_[frameSlot] = false;
            Abi3::GpuRestirStatisticsV3 statistics{};
            std::memcpy(&statistics,
                restirStatisticsReadback_[frameSlot].MappedData(),
                sizeof(statistics));
            return {
                true,
                statistics.candidateCounts.x,
                statistics.visibilityCounts.x,
                statistics.visibilityCounts.y};
        }

    private:
        void EnsureReady() const
        {
            if (!ready_)
            {
                throw std::logic_error(
                    "Wave3DebugRuntime is not ready.");
            }
        }

        static void ValidateFrameSlot(const std::uint32_t frameSlot)
        {
            if (frameSlot >= kWave3DebugFrameCount)
            {
                throw std::out_of_range("Wave 3 frame slot is out of range.");
            }
        }

        [[nodiscard]] VkPipeline CreateComputePipeline(
            const VkPipelineLayout layout,
            const std::filesystem::path& path,
            const char* const entryPoint = "main") const
        {
            const std::vector<std::uint32_t> spirv = ReadSpirv(path);
            VkShaderModule module = VK_NULL_HANDLE;
            const VkShaderModuleCreateInfo moduleInfo{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                nullptr,
                0u,
                spirv.size() * sizeof(std::uint32_t),
                spirv.data()};
            Check(vkCreateShaderModule(device_, &moduleInfo, nullptr, &module),
                "vkCreateShaderModule(Wave3)");
            VkPipeline result = VK_NULL_HANDLE;
            const VkPipelineShaderStageCreateInfo stage{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0u,
                VK_SHADER_STAGE_COMPUTE_BIT,
                module,
                entryPoint,
                nullptr};
            VkComputePipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipelineInfo.stage = stage;
            pipelineInfo.layout = layout;
            const VkResult createResult = vkCreateComputePipelines(
                device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &result);
            vkDestroyShaderModule(device_, module, nullptr);
            if (createResult != VK_SUCCESS)
            {
                throw std::runtime_error(
                    "vkCreateComputePipelines(Wave3 "
                    + path.filename().string() + "::" + entryPoint
                    + ") failed with VkResult "
                    + std::to_string(static_cast<int>(createResult)));
            }
            return result;
        }

        void CreateWavefrontResources()
        {
            wavefrontRequirements_ =
                Wavefront::MakeVulkanWavefrontResourceRequirements(
                    extent_.width, extent_.height, kMaximumBounceCount, 1u);
            wavefrontAllocation_ = wavefrontRequirements_.queues;
            for (Hardware::DeviceBuffer& frame : wavefrontFrameBuffers_)
            {
                frame = CreateBuffer(*allocator_,
                    sizeof(Wavefront::WavefrontFrameConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
            }
            wavefrontSharedPaths_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.sharedPathStateBytes, kStorageUsage, false);
            wavefrontRayA_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.rayQueueBytesEach, kStorageUsage, false);
            wavefrontMaterial_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.materialWorkBytes, kStorageUsage, false);
            wavefrontShadowQueue_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.shadowQueueBytes, kStorageUsage, false);
            wavefrontRayB_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.rayQueueBytesEach, kStorageUsage, false);
            wavefrontPaths_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.privatePathStateBytes, kStorageUsage, false);
            wavefrontDenseNext_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.denseNextBytes, kStorageUsage, false);
            wavefrontDenseShadow_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.denseShadowBytes, kStorageUsage, false);
            wavefrontNextQueue_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.nextQueueBytes, kStorageUsage, false);
            wavefrontFlags_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.compactionFlagsBytes, kStorageUsage, false);
            wavefrontPrefix_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.prefixBytes, kStorageUsage, false);
            wavefrontScan_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.scanScratchBytes, kStorageUsage, false);
            wavefrontHeaders_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.queueHeaderBytes, kStorageUsage, true);
            wavefrontIndirect_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.indirectArgumentBytes,
                kStorageUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false);
            wavefrontCounters_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.bounceCounterBytes, kStorageUsage, false);
            wavefrontShadowAov_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.shadowAovBytes, kStorageUsage, false);
            wavefrontPrimarySurfaceV2_ = CreateBuffer(*allocator_,
                wavefrontRequirements_.primarySurfaceV2Bytes,
                kStorageUsage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);

            std::array<VkDescriptorSetLayoutBinding, 25u> bindings{};
            for (std::uint32_t binding = 0u;
                binding < static_cast<std::uint32_t>(bindings.size()); ++binding)
            {
                bindings[binding].binding = binding;
                bindings[binding].descriptorCount = 1u;
                bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                bindings[binding].descriptorType = binding == 0u
                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    : binding >= 16u && binding <= 22u
                        ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                        : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
            const VkDescriptorSetLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0u,
                static_cast<std::uint32_t>(bindings.size()),
                bindings.data()};
            Check(vkCreateDescriptorSetLayout(
                device_, &layoutInfo, nullptr, &wavefrontSetLayout_),
                "vkCreateDescriptorSetLayout(Wavefront set3)");

            const std::array poolSizes{
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    kWave3DebugFrameCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    kWave3DebugFrameCount * 17u},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    kWave3DebugFrameCount * 7u}};
            const VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                nullptr,
                0u,
                kWave3DebugFrameCount,
                static_cast<std::uint32_t>(poolSizes.size()),
                poolSizes.data()};
            Check(vkCreateDescriptorPool(
                device_, &poolInfo, nullptr, &wavefrontDescriptorPool_),
                "vkCreateDescriptorPool(Wavefront set3)");
            std::array<VkDescriptorSetLayout, kWave3DebugFrameCount> layouts{};
            layouts.fill(wavefrontSetLayout_);
            const VkDescriptorSetAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                nullptr,
                wavefrontDescriptorPool_,
                kWave3DebugFrameCount,
                layouts.data()};
            Check(vkAllocateDescriptorSets(
                device_, &allocateInfo, wavefrontDescriptorSets_.data()),
                "vkAllocateDescriptorSets(Wavefront set3)");
            UpdateWavefrontDescriptors();
        }

        void UpdateWavefrontDescriptors()
        {
            const std::array commonBuffers{
                Descriptor(wavefrontSharedPaths_),
                Descriptor(wavefrontRayA_),
                Descriptor(wavefrontMaterial_),
                Descriptor(wavefrontShadowQueue_),
                Descriptor(wavefrontRayB_),
                Descriptor(wavefrontPaths_),
                Descriptor(wavefrontDenseNext_),
                Descriptor(wavefrontDenseShadow_),
                Descriptor(wavefrontNextQueue_),
                Descriptor(wavefrontFlags_),
                Descriptor(wavefrontPrefix_),
                Descriptor(wavefrontScan_),
                Descriptor(wavefrontHeaders_),
                Descriptor(wavefrontIndirect_),
                Descriptor(wavefrontCounters_),
                Descriptor(wavefrontShadowAov_),
                Descriptor(wavefrontPrimarySurfaceV2_)};
            const std::array<VkImageView, 7u> views{
                outputImageView_,
                signalImageViews_[1u],
                signalImageViews_[2u],
                signalImageViews_[3u],
                signalImageViews_[4u],
                signalImageViews_[0u],
                signalImageViews_[0u]};
            std::array<VkDescriptorImageInfo, 7u> images{};
            for (std::size_t index = 0u; index < images.size(); ++index)
            {
                images[index].imageView = views[index];
                images[index].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave3DebugFrameCount; ++frameSlot)
            {
                const VkDescriptorBufferInfo frame =
                    Descriptor(wavefrontFrameBuffers_[frameSlot]);
                std::array<VkWriteDescriptorSet, 25u> writes{};
                for (std::uint32_t binding = 0u;
                    binding < static_cast<std::uint32_t>(writes.size()); ++binding)
                {
                    VkWriteDescriptorSet& write = writes[binding];
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = wavefrontDescriptorSets_[frameSlot];
                    write.dstBinding = binding;
                    write.descriptorCount = 1u;
                    if (binding == 0u)
                    {
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        write.pBufferInfo = &frame;
                    }
                    else if (binding >= 16u && binding <= 22u)
                    {
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        write.pImageInfo = &images[binding - 16u];
                    }
                    else
                    {
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        const std::size_t bufferIndex = binding == 23u
                            ? 15u : binding == 24u
                                ? 16u
                                : static_cast<std::size_t>(binding - 1u);
                        write.pBufferInfo = &commonBuffers[bufferIndex];
                    }
                }
                vkUpdateDescriptorSets(device_,
                    static_cast<std::uint32_t>(writes.size()),
                    writes.data(), 0u, nullptr);
            }
        }

        void CreateReconstructionResources()
        {
            reconstructionRequirements_ =
                L8::MakeVulkanReconstructionResourceRequirements(
                    {extent_.width, extent_.height}, kWave3DebugFrameCount);
            reconstructionGBuffer_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.gBufferBytes, kStorageUsage, false);
            reconstructionMotionInput_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.motionInputBytes, kStorageUsage, false);
            reconstructionCurrentTransforms_ = CreateBuffer(*allocator_,
                sizeof(Matrix4), kStorageUsage, true);
            reconstructionPreviousTransforms_ = CreateBuffer(*allocator_,
                sizeof(Matrix4), kStorageUsage, true);
            const Matrix4 identity = IdentityMatrix();
            std::memcpy(reconstructionCurrentTransforms_.MappedData(),
                &identity, sizeof(identity));
            std::memcpy(reconstructionPreviousTransforms_.MappedData(),
                &identity, sizeof(identity));
            reconstructionRaw_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.rawSignalBytes, kStorageUsage, false);
            // Private post-stage resources, separate from ABI-v2 surface-lighting
            // and temporal histories. Only Compose owns the progressive film.
            reconstructionFilm_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.outputImageBytes, kStorageUsage, false);
            reconstructionTerminal_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.outputImageBytes, kStorageUsage, false);
            reconstructionDemodulated_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.workingSignalBytesEach,
                kStorageUsage, false);
            for (Hardware::DeviceBuffer& history : reconstructionHistory_)
            {
                history = CreateBuffer(*allocator_,
                    reconstructionRequirements_.historyBytesEach,
                    kStorageUsage, false);
            }
            reconstructionTemporal_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.workingSignalBytesEach,
                kStorageUsage, false);
            reconstructionTemporalDebug_ = CreateBuffer(*allocator_,
                reconstructionRequirements_.temporalDebugBytes,
                kStorageUsage, false);
            for (Hardware::DeviceBuffer& variance : reconstructionVariance_)
            {
                variance = CreateBuffer(*allocator_,
                    reconstructionRequirements_.varianceBytesEach,
                    kStorageUsage, false);
            }
            for (Hardware::DeviceBuffer& signal : reconstructionAtrous_)
            {
                signal = CreateBuffer(*allocator_,
                    reconstructionRequirements_.workingSignalBytesEach,
                    kStorageUsage, false);
            }
            for (std::uint32_t frame = 0u;
                frame < kWave3DebugFrameCount; ++frame)
            {
                reconstructionMotionConstants_[frame] = CreateBuffer(*allocator_,
                    sizeof(MotionConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
                reconstructionPrepareConstants_[frame] = CreateBuffer(*allocator_,
                    sizeof(PrepareConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
                reconstructionTemporalConstants_[frame] = CreateBuffer(*allocator_,
                    sizeof(TemporalConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
                reconstructionVarianceConstants_[frame] = CreateBuffer(*allocator_,
                    sizeof(VarianceConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
                reconstructionComposeConstants_[frame] = CreateBuffer(*allocator_,
                    sizeof(ComposeConstants),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
                for (std::uint32_t iteration = 0u;
                    iteration < kMaximumAtrousIterations; ++iteration)
                {
                    reconstructionAtrousConstants_[frame][iteration] =
                        CreateBuffer(*allocator_, sizeof(AtrousConstants),
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
                }
            }

            std::array<VkDescriptorSetLayoutBinding, 24u> bindings{};
            std::size_t next = 0u;
            for (std::uint32_t binding = 0u; binding <= 15u; ++binding)
            {
                VkDescriptorSetLayoutBinding& item = bindings[next++];
                item.binding = binding;
                item.descriptorType = binding == 13u
                    ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                item.descriptorCount = 1u;
                item.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            for (std::uint32_t binding = 16u; binding <= 21u; ++binding)
            {
                VkDescriptorSetLayoutBinding& item = bindings[next++];
                item.binding = binding;
                item.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                item.descriptorCount = 1u;
                item.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            for (std::uint32_t binding = 22u; binding <= 23u; ++binding)
            {
                VkDescriptorSetLayoutBinding& item = bindings[next++];
                item.binding = binding;
                item.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                item.descriptorCount = 1u;
                item.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            const VkDescriptorSetLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0u,
                static_cast<std::uint32_t>(bindings.size()),
                bindings.data()};
            Check(vkCreateDescriptorSetLayout(
                device_, &layoutInfo, nullptr, &reconstructionSetLayout_),
                "vkCreateDescriptorSetLayout(Reconstruction set4)");

            constexpr std::uint32_t setCount =
                kWave3DebugFrameCount * kL8SetsPerFrame;
            const std::array poolSizes{
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    setCount * 17u},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    setCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    setCount * 6u}};
            const VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                nullptr,
                0u,
                setCount,
                static_cast<std::uint32_t>(poolSizes.size()),
                poolSizes.data()};
            Check(vkCreateDescriptorPool(
                device_, &poolInfo, nullptr, &reconstructionDescriptorPool_),
                "vkCreateDescriptorPool(Reconstruction set4)");
            std::array<VkDescriptorSetLayout, setCount> layouts{};
            layouts.fill(reconstructionSetLayout_);
            const VkDescriptorSetAllocateInfo allocationInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                nullptr,
                reconstructionDescriptorPool_,
                setCount,
                layouts.data()};
            Check(vkAllocateDescriptorSets(device_, &allocationInfo,
                reconstructionDescriptorSets_.data()),
                "vkAllocateDescriptorSets(Reconstruction set4)");
            // Populate a valid base set immediately because Wavefront Shade and
            // Resolve export ABI-v2 through set4 even when reconstruction=Raw.
            for (std::uint32_t frame = 0u;
                frame < kWave3DebugFrameCount; ++frame)
            {
                UpdateReconstructionDescriptors(frame, 0u, false);
            }
        }

        void CreateWavefrontPipelines()
        {
            for (std::size_t backend = 0u; backend < 2u; ++backend)
            {
                const std::array layouts{
                    frameSetLayout_, canonicalSceneSetLayout_,
                    traversalSetLayouts_[backend], wavefrontSetLayout_,
                    reconstructionSetLayout_};
                VkPushConstantRange push{};
                push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                push.size = sizeof(Wavefront::WavefrontPassConstants);
                const VkPipelineLayoutCreateInfo layoutInfo{
                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    nullptr,
                    0u,
                    static_cast<std::uint32_t>(layouts.size()),
                    layouts.data(),
                    1u,
                    &push};
                Check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                    &wavefrontPipelineLayouts_[backend]),
                    "vkCreatePipelineLayout(Wavefront)");
                wavefrontPipelines_[backend].layout =
                    wavefrontPipelineLayouts_[backend];

                auto& pipelines = wavefrontPipelines_[backend].passes;
                const auto create = [&](const Wavefront::PassKind pass,
                                        const std::string_view file,
                                        const char* entry)
                {
                    pipelines[static_cast<std::size_t>(pass)] =
                        CreateComputePipeline(
                            wavefrontPipelineLayouts_[backend],
                            shaderDirectory_ / file, entry);
                };
                create(Wavefront::PassKind::FrameReset,
                    "wavefront_reset.spv", "ResetCS");
                create(Wavefront::PassKind::ResetShadeOutputs,
                    "wavefront_reset.spv", "ResetCS");
                create(Wavefront::PassKind::ResetNextRay,
                    "wavefront_reset.spv", "ResetCS");
                create(Wavefront::PassKind::RayGen,
                    "wavefront_raygen.spv", "RayGenCS");
                create(Wavefront::PassKind::PrepareIntersect,
                    "wavefront_prepare_dispatch.spv", "PrepareDispatchCS");
                create(Wavefront::PassKind::PrepareShadow,
                    "wavefront_prepare_dispatch.spv", "PrepareDispatchCS");
                create(Wavefront::PassKind::PrepareNext,
                    "wavefront_prepare_dispatch.spv", "PrepareDispatchCS");
                create(Wavefront::PassKind::Intersect,
                    backend == 0u ? "wavefront_intersect_software.spv"
                                  : "wavefront_intersect_rayquery.spv",
                    "IntersectCS");
                create(Wavefront::PassKind::Shade,
                    "wavefront_shade.spv", "ShadeCS");
                create(Wavefront::PassKind::ScanBlocks,
                    "wavefront_scan.spv", "ScanBlocksCS");
                create(Wavefront::PassKind::AddScanOffsets,
                    "wavefront_add_offsets.spv", "AddOffsetsCS");
                create(Wavefront::PassKind::Scatter,
                    "wavefront_scatter.spv", "ScatterCS");
                create(Wavefront::PassKind::TraceShadow,
                    backend == 0u ? "wavefront_trace_shadow_software.spv"
                                  : "wavefront_trace_shadow_rayquery.spv",
                    "TraceShadowCS");
                create(Wavefront::PassKind::NextBounce,
                    "wavefront_next_bounce.spv", "NextBounceCS");
                create(Wavefront::PassKind::Resolve,
                    "wavefront_resolve.spv", "ResolveCS");
                create(Wavefront::PassKind::VisualizeCounters,
                    "wavefront_visualize_counters.spv", "VisualizeCountersCS");
            }
        }

        void CreateReconstructionPipelines()
        {
            const std::array layouts{
                frameSetLayout_, canonicalSceneSetLayout_,
                traversalSetLayouts_[1u], wavefrontSetLayout_,
                reconstructionSetLayout_};
            const VkPipelineLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                nullptr,
                0u,
                static_cast<std::uint32_t>(layouts.size()),
                layouts.data(),
                0u,
                nullptr};
            Check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                &reconstructionPipelineLayout_),
                "vkCreatePipelineLayout(Reconstruction)");
            reconstructionPipelines_.layout = reconstructionPipelineLayout_;
            const std::array<std::string_view,
                L8::VulkanReconstructionPassKindCount> files{
                "reconstruction_motion_vectors.spv",
                "reconstruction_prepare_signal.spv",
                "reconstruction_temporal_accumulation.spv",
                "reconstruction_variance_bootstrap.spv",
                "reconstruction_atrous.spv",
                "reconstruction_compose.spv"};
            for (std::size_t index = 0u; index < files.size(); ++index)
            {
                reconstructionPipelines_.passes[index] = CreateComputePipeline(
                    reconstructionPipelineLayout_, shaderDirectory_ / files[index]);
            }
        }

        void UpdateReconstruction(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugCamera& camera,
            const std::uint32_t sampleIndex)
        {
            // Frame/sample identity is monotonic. Ordinary camera motion is
            // handled by motion vectors, not by discarding all temporal history.
            const bool resetHistory = !hasLastCamera_
                || config.reconstruction != lastReconstructionMode_;
            if (resetHistory || !SameCamera(camera, lastCamera_)
                || config.reconstruction != lastReconstructionMode_)
            {
                filmSampleCount_ = 0u;
            }
            const Wave3DebugCamera previous =
                resetHistory ? camera : lastCamera_;
            MotionConstants motion{};
            motion.currentViewProjection = MakeViewProjection(camera, extent_);
            motion.previousViewProjection = MakeViewProjection(previous, extent_);
            motion.previousWorldToView = MakeWorldToView(previous);
            motion.extentAndTransformCounts = {
                extent_.width, extent_.height,
                reconstructionTransformCount_, reconstructionTransformCount_};
            std::memcpy(reconstructionMotionConstants_[frameSlot].MappedData(),
                &motion, sizeof(motion));

            const PrepareConstants prepare{
                extent_.width, extent_.height, 1.0e-3f, 1u};
            std::memcpy(reconstructionPrepareConstants_[frameSlot].MappedData(),
                &prepare, sizeof(prepare));

            TemporalConstants temporal{};
            temporal.extentHistoryReset = {
                extent_.width, extent_.height,
                resetHistory ? 0u : 1u,
                resetHistory ? 1u : 0u};
            std::memcpy(reconstructionTemporalConstants_[frameSlot].MappedData(),
                &temporal, sizeof(temporal));

            VarianceConstants variance{};
            variance.width = extent_.width;
            variance.height = extent_.height;
            variance.useTemporalVariance =
                config.reconstruction == ReconstructionMode::Svgf ? 1u : 0u;
            std::memcpy(reconstructionVarianceConstants_[frameSlot].MappedData(),
                &variance, sizeof(variance));

            for (std::uint32_t iteration = 0u;
                iteration < kMaximumAtrousIterations; ++iteration)
            {
                AtrousConstants atrous{};
                atrous.width = extent_.width;
                atrous.height = extent_.height;
                atrous.step = 1u << iteration;
                std::memcpy(
                    reconstructionAtrousConstants_[frameSlot][iteration]
                        .MappedData(),
                    &atrous, sizeof(atrous));
            }

            ComposeConstants compose{};
            compose.width = extent_.width;
            compose.height = extent_.height;
            compose.outputMode = ToComposeOutputMode(config);
            compose.filmSampleCount = filmSampleCount_;
            if (config.reconstruction == ReconstructionMode::ProgressiveMean
                && config.debugView == DebugView::Final)
            {
                ++filmSampleCount_;
            }
            lastReconstructionMode_ = config.reconstruction;
            std::memcpy(reconstructionComposeConstants_[frameSlot].MappedData(),
                &compose, sizeof(compose));

            constexpr std::uint32_t atrousIterations = 5u;
            const bool temporalDiagnostic =
                config.debugView == DebugView::Motion
                || config.debugView == DebugView::HistoryLength
                || config.debugView == DebugView::Moments
                || config.debugView == DebugView::Variance
                || config.debugView == DebugView::TemporalAcceptance
                || config.debugView == DebugView::TemporalRejectReasons;
            preparedReconstruction_[frameSlot] =
                L8::BuildVulkanReconstructionPlan(
                    temporalDiagnostic ? L8::ReconstructionOutput::Svgf
                        : ToL8Output(config.reconstruction),
                    atrousIterations, 0u);
            UpdateReconstructionDescriptors(
                frameSlot, sampleIndex, true);
        }

        void UpdateReconstructionDescriptors(
            const std::uint32_t frameSlot,
            const std::uint32_t sampleIndex,
            const bool usePreparedPlan)
        {
            const L8::VulkanHistoryResourceIndices history =
                L8::ResolveVulkanHistoryResources(
                    sampleIndex, kWave3DebugFrameCount);
            const std::uint32_t fallbackRead =
                (history.writePhysicalIndex + 1u)
                % static_cast<std::uint32_t>(reconstructionHistory_.size());
            const std::uint32_t readIndex = history.hasPrevious
                ? history.readPhysicalIndex : fallbackRead;

            const std::span<const L8::PreparedVulkanReconstructionPass> plan =
                usePreparedPlan
                ? std::span<const L8::PreparedVulkanReconstructionPass>{
                    preparedReconstruction_[frameSlot]}
                : std::span<const L8::PreparedVulkanReconstructionPass>{};
            const std::size_t descriptorCount =
                std::max<std::size_t>(1u, plan.size());
            preparedReconstructionSets_[frameSlot].clear();
            preparedReconstructionSets_[frameSlot].reserve(plan.size());

            std::uint32_t atrousPassCount = 0u;
            bool hasTemporalPass = false;
            for (const auto& pass : plan)
            {
                if (pass.kind
                    == L8::VulkanReconstructionPassKind::AtrousIteration)
                {
                    ++atrousPassCount;
                }
                else if (pass.kind
                    == L8::VulkanReconstructionPassKind::TemporalAccumulation)
                {
                    hasTemporalPass = true;
                }
            }
            for (std::size_t passIndex = 0u;
                passIndex < descriptorCount; ++passIndex)
            {
                const VkDescriptorSet set = reconstructionDescriptorSets_[
                    frameSlot * kL8SetsPerFrame + passIndex];
                if (usePreparedPlan)
                {
                    preparedReconstructionSets_[frameSlot].push_back(set);
                }

                const VkDescriptorBufferInfo preparedSignal = hasTemporalPass
                    ? Descriptor(reconstructionTemporal_)
                    : Descriptor(reconstructionDemodulated_);
                VkDescriptorBufferInfo signalInput = preparedSignal;
                VkDescriptorBufferInfo signalOutput =
                    Descriptor(reconstructionAtrous_[0u]);
                VkDescriptorBufferInfo varianceInput =
                    Descriptor(reconstructionVariance_[0u]);
                VkDescriptorBufferInfo varianceOutput =
                    Descriptor(reconstructionVariance_[1u]);
                std::uint32_t atrousIteration = 0u;
                if (usePreparedPlan)
                {
                    const auto& pass = plan[passIndex];
                    if (pass.kind
                        == L8::VulkanReconstructionPassKind::AtrousIteration)
                    {
                        atrousIteration = pass.iteration;
                        signalInput = pass.iteration == 0u
                            ? preparedSignal
                            : Descriptor(reconstructionAtrous_[
                                (pass.iteration - 1u) & 1u]);
                        signalOutput = Descriptor(
                            reconstructionAtrous_[pass.iteration & 1u]);
                        varianceInput = Descriptor(
                            reconstructionVariance_[pass.iteration & 1u]);
                        varianceOutput = Descriptor(
                            reconstructionVariance_[(pass.iteration + 1u) & 1u]);
                    }
                    else if (pass.kind
                        == L8::VulkanReconstructionPassKind::Compose
                        && atrousPassCount != 0u)
                    {
                        signalOutput = Descriptor(reconstructionAtrous_[
                            (atrousPassCount - 1u) & 1u]);
                    }
                }

                std::array<VkDescriptorBufferInfo, 15u> buffers{
                    Descriptor(reconstructionGBuffer_),
                    Descriptor(reconstructionMotionInput_),
                    Descriptor(reconstructionCurrentTransforms_),
                    Descriptor(reconstructionPreviousTransforms_),
                    Descriptor(reconstructionRaw_),
                    Descriptor(reconstructionDemodulated_),
                    Descriptor(reconstructionHistory_[readIndex]),
                    Descriptor(reconstructionHistory_[history.writePhysicalIndex]),
                    preparedSignal,
                    Descriptor(reconstructionTemporalDebug_),
                    varianceInput,
                    signalInput,
                    signalOutput,
                    varianceOutput,
                    Descriptor(wavefrontPrimarySurfaceV2_)};
                const std::array<VkDescriptorBufferInfo, 6u> constants{
                    Descriptor(reconstructionMotionConstants_[frameSlot]),
                    Descriptor(reconstructionPrepareConstants_[frameSlot]),
                    Descriptor(reconstructionTemporalConstants_[frameSlot]),
                    Descriptor(reconstructionVarianceConstants_[frameSlot]),
                    Descriptor(reconstructionAtrousConstants_[frameSlot][
                        std::min(atrousIteration,
                            kMaximumAtrousIterations - 1u)]),
                    Descriptor(reconstructionComposeConstants_[frameSlot])};
                const VkDescriptorImageInfo output{
                    VK_NULL_HANDLE, outputImageView_, VK_IMAGE_LAYOUT_GENERAL};

                const std::array privateSignals{
                    Descriptor(reconstructionFilm_), Descriptor(reconstructionTerminal_)};
                std::array<VkWriteDescriptorSet, 24u> writes{};
                std::size_t writeIndex = 0u;
                for (std::uint32_t binding = 0u; binding <= 15u; ++binding)
                {
                    VkWriteDescriptorSet& write = writes[writeIndex++];
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = set;
                    write.dstBinding = binding;
                    write.descriptorCount = 1u;
                    if (binding == 13u)
                    {
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        write.pImageInfo = &output;
                    }
                    else
                    {
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        const std::size_t bufferIndex = binding < 13u
                            ? binding : binding - 1u;
                        write.pBufferInfo = &buffers[bufferIndex];
                    }
                }
                for (std::uint32_t binding = 16u; binding <= 21u; ++binding)
                {
                    VkWriteDescriptorSet& write = writes[writeIndex++];
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = set;
                    write.dstBinding = binding;
                    write.descriptorCount = 1u;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &constants[binding - 16u];
                }
                for (std::uint32_t binding = 22u; binding <= 23u; ++binding)
                {
                    VkWriteDescriptorSet& write = writes[writeIndex++];
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = set;
                    write.dstBinding = binding;
                    write.descriptorCount = 1u;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &privateSignals[binding - 22u];
                }
                vkUpdateDescriptorSets(device_,
                    static_cast<std::uint32_t>(writes.size()),
                    writes.data(), 0u, nullptr);
            }
        }

        [[nodiscard]] static bool SameCamera(
            const Wave3DebugCamera& left,
            const Wave3DebugCamera& right) noexcept
        {
            constexpr float epsilon = 1.0e-5f;
            const auto same = [epsilon](const float a, const float b) noexcept
            {
                return std::abs(a - b) <= epsilon;
            };
            const auto sameVector = [&same](const Vec3 a, const Vec3 b) noexcept
            {
                return same(a.x, b.x) && same(a.y, b.y)
                    && same(a.z, b.z);
            };
            return sameVector(left.position, right.position)
                && sameVector(left.forward, right.forward)
                && sameVector(left.right, right.right)
                && sameVector(left.up, right.up)
                && same(left.verticalFovDegrees, right.verticalFovDegrees);
        }

        [[nodiscard]] static bool SameReSTIRConfiguration(
            const RuntimeConfig& left,
            const RuntimeConfig& right) noexcept
        {
            return left.scene == right.scene
                && left.backend == right.backend
                && left.transportModel == right.transportModel
                && left.executionArchitecture == right.executionArchitecture
                && left.directLightingEstimator
                    == right.directLightingEstimator
                && left.lightSelection == right.lightSelection
                && left.environmentSampler == right.environmentSampler
                && left.shadowMethod == right.shadowMethod
                && left.reconstruction == right.reconstruction
                && left.debugView == right.debugView
                && left.render.width == right.render.width
                && left.render.height == right.render.height
                && left.restir.manyLightsTier
                    == right.restir.manyLightsTier
                && left.restir.reuseStage == right.restir.reuseStage
                && left.restir.biasMode == right.restir.biasMode
                && left.restir.initialCandidatesPerPixel
                    == right.restir.initialCandidatesPerPixel
                && left.restir.spatialNeighbors
                    == right.restir.spatialNeighbors
                && left.restir.maximumReservoirM
                    == right.restir.maximumReservoirM
                && left.restir.maximumHistoryAge
                    == right.restir.maximumHistoryAge;
        }

        void CreateReSTIRRecorder()
        {
            restirDebugConstants_ = CreateBuffer(
                *allocator_, 16u, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
            VkDescriptorSetLayoutBinding debugBinding{};
            debugBinding.binding = 0u;
            debugBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            debugBinding.descriptorCount = 1u;
            debugBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            const VkDescriptorSetLayoutCreateInfo debugLayoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0u,
                1u,
                &debugBinding};
            Check(vkCreateDescriptorSetLayout(
                device_, &debugLayoutInfo, nullptr, &restirDebugSetLayout_),
                "vkCreateDescriptorSetLayout(ReSTIR set6)");
            const VkDescriptorPoolSize debugPoolSize{
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u};
            const VkDescriptorPoolCreateInfo debugPoolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                nullptr,
                0u,
                1u,
                1u,
                &debugPoolSize};
            Check(vkCreateDescriptorPool(
                device_, &debugPoolInfo, nullptr, &restirDebugDescriptorPool_),
                "vkCreateDescriptorPool(ReSTIR set6)");
            const VkDescriptorSetAllocateInfo debugAllocateInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                nullptr,
                restirDebugDescriptorPool_,
                1u,
                &restirDebugSetLayout_};
            Check(vkAllocateDescriptorSets(
                device_, &debugAllocateInfo, &restirDebugDescriptorSet_),
                "vkAllocateDescriptorSets(ReSTIR set6)");
            const VkDescriptorBufferInfo debugConstants =
                Descriptor(restirDebugConstants_);
            VkWriteDescriptorSet debugWrite{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            debugWrite.dstSet = restirDebugDescriptorSet_;
            debugWrite.dstBinding = 0u;
            debugWrite.descriptorCount = 1u;
            debugWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            debugWrite.pBufferInfo = &debugConstants;
            vkUpdateDescriptorSets(device_, 1u, &debugWrite, 0u, nullptr);

            const std::array restirPoolSizes{
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    kWave3DebugFrameCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    kWave3DebugFrameCount
                        * (kVulkanReSTIRSet5BindingCount - 2u)},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    kWave3DebugFrameCount}};
            VkDescriptorPoolCreateInfo restirPoolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            restirPoolInfo.flags =
                VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            restirPoolInfo.maxSets = kWave3DebugFrameCount;
            restirPoolInfo.poolSizeCount =
                static_cast<std::uint32_t>(restirPoolSizes.size());
            restirPoolInfo.pPoolSizes = restirPoolSizes.data();
            Check(vkCreateDescriptorPool(
                device_, &restirPoolInfo, nullptr, &restirDescriptorPool_),
                "vkCreateDescriptorPool(ReSTIR set5)");

            VulkanReSTIRRecorderCreateInfo recorderInfo{};
            recorderInfo.device = device_;
            recorderInfo.descriptorPool = restirDescriptorPool_;
            recorderInfo.pipelineSetLayouts = {
                frameSetLayout_,
                canonicalSceneSetLayout_,
                traversalSetLayouts_[1u],
                wavefrontSetLayout_,
                reconstructionSetLayout_,
                VK_NULL_HANDLE,
                restirDebugSetLayout_};
            recorderInfo.framesInFlight = kWave3DebugFrameCount;
            const ReSTIRRuntimeStatus initialized =
                restirRecorder_.Initialize(recorderInfo);
            if (!initialized)
            {
                throw std::runtime_error(
                    "VulkanReSTIRRecorder::Initialize failed: "
                    + initialized.reason);
            }

            for (std::size_t index = 0u;
                index < restirPipelines_.size(); ++index)
            {
                const ReSTIRPass pass = static_cast<ReSTIRPass>(index);
                if (pass == ReSTIRPass::Reconstruction)
                {
                    continue;
                }
                const std::string_view file =
                    ReSTIRProduction::VulkanReSTIRShaderFileName(pass);
                if (file.empty())
                {
                    throw std::runtime_error(
                        "Wave 4 production pass has no shader mapping.");
                }
                restirPipelines_[index] = CreateComputePipeline(
                    restirRecorder_.PipelineLayout(),
                    shaderDirectory_ / file,
                    pass == ReSTIRPass::GenerateCandidates
                        ? "CSMain" : "main");
            }
            const ReSTIRRuntimeStatus pipelinesPublished =
                restirRecorder_.SetPipelines(restirPipelines_);
            if (!pipelinesPublished)
            {
                throw std::runtime_error(
                    "VulkanReSTIRRecorder::SetPipelines failed: "
                    + pipelinesPublished.reason);
            }
        }

        void ResetReSTIRResourceBuffers() noexcept
        {
            for (auto& frame : restirFrameBuffers_)
            {
                for (Hardware::DeviceBuffer& buffer : frame)
                {
                    buffer.Reset();
                }
            }
            for (Hardware::DeviceBuffer& buffer : restirReservoirHistory_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : restirSurfaceHistory_)
                buffer.Reset();
            restirResetReservoir_.Reset();
            restirResetSurface_.Reset();
            for (Hardware::DeviceBuffer& buffer : restirStatisticsReadback_)
                buffer.Reset();
            restirStatisticsReadbackPending_.fill(false);
            restirResourcesReady_ = false;
        }

        void EnsureReSTIRResources(
            const ReSTIRFramePlan& plan,
            const RuntimeConfig& config)
        {
            const bool sameAllocation = restirResourcesReady_
                && restirAllocatedLightCount_ == plan.currentLightCount
                && restirAllocatedCandidates_
                    == plan.initialCandidateCount
                && restirAllocatedNeighbors_
                    == plan.spatialNeighborCount
                && restirAllocatedEstimator_ == plan.estimatorMode
                && restirAllocatedShadowMethod_ == plan.shadowMethod;
            if (sameAllocation)
            {
                return;
            }
            if (restirResourcesReady_)
            {
                Check(vkDeviceWaitIdle(device_),
                    "vkDeviceWaitIdle(ReSTIR resource reconfiguration)");
                ResetReSTIRResourceBuffers();
            }

            ReSTIRProduction::VulkanReSTIRResourcePlan resourcePlan =
                ReSTIRProduction::BuildVulkanReSTIRResourcePlan(plan);
            if (!resourcePlan.IsReady())
            {
                throw std::runtime_error(
                    "Wave 4 resource plan failed: "
                    + resourcePlan.status.reason);
            }
            std::array<VkDeviceSize, kVulkanReSTIRSet5BindingCount>
                allocationBytes = resourcePlan.descriptorBufferBytes;
            const VkDeviceSize pixelCount =
                static_cast<VkDeviceSize>(extent_.width) * extent_.height;
            if (UsesRestirTemporalReuse(config.restir.reuseStage))
            {
                allocationBytes[Abi3::RestirBinding::SurfaceHistory] =
                    pixelCount
                    * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2);
                allocationBytes[Abi3::RestirBinding::HistoryAtCurrent] =
                    pixelCount
                    * sizeof(Abi3::GpuPersistentLightSampleV3);
                allocationBytes[
                    Abi3::RestirBinding::PreviousToCurrentLightIndex] =
                    static_cast<VkDeviceSize>(plan.currentLightCount)
                    * 2u * sizeof(std::uint32_t);
                allocationBytes[
                    Abi3::RestirBinding::PreviousPublishedReservoir] =
                    pixelCount * sizeof(Abi3::GpuRestirReservoirV3);
            }

            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave3DebugFrameCount; ++frameSlot)
            {
                for (std::uint32_t binding = 0u;
                    binding < kVulkanReSTIRSet5BindingCount; ++binding)
                {
                    if (binding == Abi3::RestirBinding::DebugImage
                        || binding == Abi3::RestirBinding::SurfaceHistory
                        || binding == Abi3::RestirBinding::PublishedReservoir
                        || binding
                            == Abi3::RestirBinding::PreviousPublishedReservoir)
                    {
                        continue;
                    }
                    const bool mapped =
                        binding == Abi3::RestirBinding::Parameters
                        || binding
                            == Abi3::RestirBinding::CurrentToPreviousLightIndex
                        || binding
                            == Abi3::RestirBinding::PreviousToCurrentLightIndex
                        || binding == Abi3::RestirBinding::NeighborIndices;
                    VkBufferUsageFlags usage = binding == Abi3::RestirBinding::Parameters
                        ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : kStorageUsage;
                    if (binding == Abi3::RestirBinding::Statistics)
                        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                    restirFrameBuffers_[frameSlot][binding] = CreateBuffer(
                        *allocator_,
                        std::max<VkDeviceSize>(
                            allocationBytes[binding],
                            ReSTIRProduction::kReSTIRDummyDescriptorBytes),
                        usage,
                        mapped);
                    if (binding == Abi3::RestirBinding::Statistics)
                    {
                        restirStatisticsReadback_[frameSlot] = CreateBuffer(
                            *allocator_, sizeof(Abi3::GpuRestirStatisticsV3),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
                    }
                }
            }

            const VkDeviceSize reservoirBytes = pixelCount
                * sizeof(Abi3::GpuRestirReservoirV3);
            const VkDeviceSize surfaceBytes = pixelCount
                * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2);
            for (Hardware::DeviceBuffer& buffer : restirReservoirHistory_)
            {
                buffer = CreateBuffer(
                    *allocator_, reservoirBytes, kStorageUsage, false);
            }
            for (Hardware::DeviceBuffer& buffer : restirSurfaceHistory_)
            {
                buffer = CreateBuffer(
                    *allocator_, surfaceBytes,
                    kStorageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    false);
            }
            restirResetReservoir_ = CreateBuffer(
                *allocator_, ReSTIRProduction::kReSTIRDummyDescriptorBytes,
                kStorageUsage, false);
            restirResetSurface_ = CreateBuffer(
                *allocator_, ReSTIRProduction::kReSTIRDummyDescriptorBytes,
                kStorageUsage, false);

            restirAllocatedLightCount_ = plan.currentLightCount;
            restirAllocatedCandidates_ = plan.initialCandidateCount;
            restirAllocatedNeighbors_ = plan.spatialNeighborCount;
            restirAllocatedEstimator_ = plan.estimatorMode;
            restirAllocatedShadowMethod_ = plan.shadowMethod;
            restirResourcesReady_ = true;
            restirPublishedHistory_ = {};
        }

        void UpdateReSTIRHostResources(
            const std::uint32_t frameSlot,
            const ReSTIRFrameRequest& request,
            const ReSTIRFramePlan& plan,
            const ReSTIRFrameParameterResult& parameters)
        {
            std::memcpy(
                restirFrameBuffers_[frameSlot][
                    Abi3::RestirBinding::Parameters].MappedData(),
                &parameters.parameters,
                sizeof(parameters.parameters));

            auto* const currentToPrevious = static_cast<std::uint32_t*>(
                restirFrameBuffers_[frameSlot][
                    Abi3::RestirBinding::CurrentToPreviousLightIndex]
                    .MappedData());
            auto* const previousToCurrent = static_cast<std::uint32_t*>(
                restirFrameBuffers_[frameSlot][
                    Abi3::RestirBinding::PreviousToCurrentLightIndex]
                    .MappedData());
            const bool reuse =
                plan.historyDecision == ReSTIRHistoryDecision::Reuse;
            for (std::uint32_t light = 0u;
                light < plan.currentLightCount; ++light)
            {
                currentToPrevious[light * 2u] = reuse
                    ? light : 0xffffffffu;
                currentToPrevious[light * 2u + 1u] =
                    static_cast<std::uint32_t>(request.identity.lightGeneration);
                previousToCurrent[light * 2u] = reuse
                    ? light : 0xffffffffu;
                previousToCurrent[light * 2u + 1u] =
                    static_cast<std::uint32_t>(request.identity.lightGeneration);
            }

            if (plan.spatialNeighborCount != 0u)
            {
                auto* const neighbors = static_cast<std::uint32_t*>(
                    restirFrameBuffers_[frameSlot][
                        Abi3::RestirBinding::NeighborIndices].MappedData());
                const std::int32_t width =
                    static_cast<std::int32_t>(extent_.width);
                const std::int32_t height =
                    static_cast<std::int32_t>(extent_.height);
                const std::uint32_t pixelCount =
                    static_cast<std::uint32_t>(plan.footprint.pixelCount);
                for (std::uint32_t pixel = 0u;
                    pixel < pixelCount; ++pixel)
                {
                    const std::int32_t x =
                        static_cast<std::int32_t>(pixel % extent_.width);
                    const std::int32_t y =
                        static_cast<std::int32_t>(pixel / extent_.width);
                    for (std::uint32_t neighbor = 0u;
                        neighbor < plan.spatialNeighborCount; ++neighbor)
                    {
                        const std::int32_t ring =
                            static_cast<std::int32_t>(neighbor / 8u) + 1;
                        constexpr std::array<std::array<std::int32_t, 2u>, 8u>
                            directions{{
                                {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
                                {{1, 1}}, {{-1, 1}}, {{1, -1}}, {{-1, -1}}}};
                        const auto direction = directions[neighbor % 8u];
                        const std::int32_t sampleX = std::clamp(
                            x + direction[0u] * ring, 0, width - 1);
                        const std::int32_t sampleY = std::clamp(
                            y + direction[1u] * ring, 0, height - 1);
                        neighbors[pixel * plan.spatialNeighborCount + neighbor] =
                            static_cast<std::uint32_t>(
                                sampleY * width + sampleX);
                    }
                }
            }
        }

        [[nodiscard]] VulkanReSTIRResourceTable MakeReSTIRResourceTable(
            const std::uint32_t frameSlot,
            const ReSTIRFramePlan& plan) const
        {
            VulkanReSTIRResourceTable table{};
            for (std::uint32_t binding = 0u;
                binding < kVulkanReSTIRSet5BindingCount; ++binding)
            {
                table.valid[binding] = true;
                if (binding == Abi3::RestirBinding::DebugImage)
                {
                    continue;
                }
                if (binding == Abi3::RestirBinding::SurfaceHistory)
                {
                    table.buffers[binding] = plan.historyDecision
                            == ReSTIRHistoryDecision::Reuse
                        ? Descriptor(restirSurfaceHistory_[
                            plan.historyReadPhysicalIndex])
                        : Descriptor(restirResetSurface_);
                }
                else if (binding == Abi3::RestirBinding::PublishedReservoir)
                {
                    table.buffers[binding] = Descriptor(
                        restirReservoirHistory_[
                            plan.historyWritePhysicalIndex]);
                }
                else if (binding
                    == Abi3::RestirBinding::PreviousPublishedReservoir)
                {
                    table.buffers[binding] = plan.historyDecision
                            == ReSTIRHistoryDecision::Reuse
                        ? Descriptor(restirReservoirHistory_[
                            plan.historyReadPhysicalIndex])
                        : Descriptor(restirResetReservoir_);
                }
                else
                {
                    table.buffers[binding] =
                        Descriptor(restirFrameBuffers_[frameSlot][binding]);
                }
            }
            table.debugImage = {
                VK_NULL_HANDLE,
                outputImageView_,
                VK_IMAGE_LAYOUT_GENERAL};
            table.debugImageExtent = extent_;
            table.debugImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
            return table;
        }

        void PrepareReSTIRFrame(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave3DebugCamera& camera,
            const std::uint32_t /* sampleIndex */,
            const Wave3DebugFrameBindings& bindings)
        {
            if ((config.backend != TraversalBackend::CanonicalLinearGpu
                    && config.backend != TraversalBackend::GpuFlattenedSahBvh
                    && config.backend != TraversalBackend::VulkanRayQuery)
                || config.transportModel != TransportModel::Pbr
                || (config.executionArchitecture != ExecutionArchitecture::Staged
                    && config.executionArchitecture
                        != ExecutionArchitecture::Megakernel
                    && config.executionArchitecture
                        != ExecutionArchitecture::Wavefront))
            {
                throw std::invalid_argument(
                    "ReSTIR DI requires a built GPU traversal backend and PBR GPU execution architecture.");
            }
            if (bindings.pbrLightTableL6.buffer == VK_NULL_HANDLE
                || bindings.pbrLightTableL6.range == 0u
                || bindings.pbrLightCount == 0u
                || bindings.sceneFingerprint == 0u
                || bindings.sceneGeneration == 0u
                || bindings.resourceGeneration == 0u
                || bindings.lightGeneration == 0u)
            {
                throw std::invalid_argument(
                    "Wave 4 production evidence is incomplete.");
            }

            const bool configurationChanged = !restirHasConfig_
                || !SameReSTIRConfiguration(restirLastConfig_, config);
            if (configurationChanged)
            {
                ++restirConfigGeneration_;
                if (restirConfigGeneration_ == 0u)
                    restirConfigGeneration_ = 1u;
                restirPublishedHistory_ = {};
                restirLastConfig_ = config;
                restirHasConfig_ = true;
            }
            while ((restirNextFrameIndex_ % kWave3DebugFrameCount)
                != frameSlot)
            {
                ++restirNextFrameIndex_;
            }
            const std::uint64_t frameIndex = restirNextFrameIndex_++;

            ReSTIRFrameRequest request{};
            request.config = config;
            request.providers = {
                true, true, true, true, true, true, true, true, true, true};
            request.identity = {
                frameIndex,
                restirConfigGeneration_,
                bindings.sceneGeneration,
                bindings.resourceGeneration,
                bindings.lightGeneration,
                extent_.width,
                extent_.height,
                config.shadowMethod};
            if (restirHasPublishedCamera_)
            {
                request.history = restirPublishedHistory_;
            }
            request.settings.lightCount = bindings.pbrLightCount;
            request.settings.previousLightCount =
                request.history.valid ? bindings.pbrLightCount : 0u;
            request.settings.initialCandidateCount =
                config.restir.initialCandidatesPerPixel;
            request.settings.spatialNeighborCount =
                UsesRestirSpatialReuse(config.restir.reuseStage)
                    ? config.restir.spatialNeighbors : 0u;
            request.settings.maximumReservoirM =
                config.restir.maximumReservoirM;
            request.settings.maximumHistoryAge =
                config.restir.maximumHistoryAge;
            request.settings.framesInFlight = kWave3DebugFrameCount;
            request.settings.estimatorMode =
                config.restir.biasMode == RestirBiasMode::ReferenceCorrection
                    ? ReSTIREstimatorMode::ReferenceCorrection
                    : ReSTIREstimatorMode::ExplicitlyBiased;
            request.settings.temporalReuse =
                UsesRestirTemporalReuse(config.restir.reuseStage);
            request.settings.spatialReuse =
                UsesRestirSpatialReuse(config.restir.reuseStage);
            request.settings.writeDebug =
                config.debugView >= DebugView::ReservoirM;

            ReSTIRFramePlan plan = BuildReSTIRFramePlan(request);
            if (!plan.IsReady())
            {
                throw std::runtime_error(
                    "Wave 4 frame plan failed: " + plan.status.reason);
            }
            EnsureReSTIRResources(plan, config);
            const ReSTIRFrameParameterResult parameters =
                BuildReSTIRFrameParameters(
                    request,
                    plan,
                    {bindings.pbrLightCount,
                     plan.historyDecision == ReSTIRHistoryDecision::Reuse
                        ? bindings.pbrLightCount : 0u,
                     1u,
                     {0.8f, 0.05f, 0.02f, 0.005f},
                     {camera.position.x, camera.position.y,
                        camera.position.z, 0.0f}});
            if (!parameters.IsReady())
            {
                throw std::runtime_error(
                    "Wave 4 frame parameters failed: "
                    + parameters.status.reason);
            }
            UpdateReSTIRHostResources(frameSlot, request, plan, parameters);
            preparedReSTIRRequests_[frameSlot] = request;
            preparedReSTIRPlans_[frameSlot] = std::move(plan);
            preparedReSTIRCameras_[frameSlot] = camera;
        }

        void RecordReSTIRMotionPrepass(
            const VkCommandBuffer commandBuffer,
            const std::uint32_t frameSlot)
        {
            VkMemoryBarrier2 beforeMotion{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            beforeMotion.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            beforeMotion.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT
                | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            beforeMotion.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            beforeMotion.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                | VK_ACCESS_2_SHADER_WRITE_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.memoryBarrierCount = 1u;
            dependency.pMemoryBarriers = &beforeMotion;
            vkCmdPipelineBarrier2(commandBuffer, &dependency);

            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                reconstructionPipelines_.For(
                    L8::VulkanReconstructionPassKind::MotionVectors));
            const VkDescriptorSet set =
                preparedReconstructionSets_[frameSlot].front();
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                reconstructionPipelineLayout_,
                4u, 1u, &set, 0u, nullptr);
            vkCmdDispatch(
                commandBuffer,
                (extent_.width + 7u) / 8u,
                (extent_.height + 7u) / 8u,
                1u);

            beforeMotion.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            beforeMotion.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier2(commandBuffer, &dependency);
        }

        [[nodiscard]] ReSTIRRuntimeStatus RecordPreparedReconstruction(
            const VkCommandBuffer commandBuffer,
            const std::uint32_t frameSlot,
            const VkDescriptorSet canonicalSceneSet)
        {
            if (canonicalSceneSet == VK_NULL_HANDLE)
            {
                return {
                    ReSTIRRuntimeStatusCode::InvalidRequest,
                    "Shared reconstruction requires the canonical scene set for material debug views"};
            }
            // ComposeCS consumes the real material table from set 1 for the
            // BaseColor and Emissive views. The generic L8 recorder owns set 4,
            // so bind the compatible scene set once before it records passes.
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                reconstructionPipelineLayout_,
                1u,
                1u,
                &canonicalSceneSet,
                0u,
                nullptr);
            L8::VulkanReconstructionFunctions functions{};
            functions.cmdPipelineBarrier2 = vkCmdPipelineBarrier2;
            functions.cmdWriteTimestamp2 = vkCmdWriteTimestamp2;
            functions.cmdBindPipeline = vkCmdBindPipeline;
            functions.cmdBindDescriptorSets = vkCmdBindDescriptorSets;
            functions.cmdDispatch = vkCmdDispatch;
            const L8::VulkanReconstructionRecordResult reconstruction =
                L8::RecordVulkanReconstruction(
                    functions,
                    {commandBuffer,
                     {extent_.width, extent_.height},
                     preparedReconstruction_[frameSlot],
                     reconstructionPipelines_,
                     preparedReconstructionSets_[frameSlot],
                     VK_NULL_HANDLE,
                     false,
                     true,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_WRITE_BIT});
            return reconstruction.Succeeded()
                ? ReSTIRRuntimeStatus{}
                : ReSTIRRuntimeStatus{
                    ReSTIRRuntimeStatusCode::RecordingFailed,
                    reconstruction.reason};
        }

        [[nodiscard]] ReSTIRRuntimeStatus RecordReconstruction(
            const ReSTIRFramePlan& plan,
            const VkCommandBuffer commandBuffer,
            const std::array<VkDescriptorSet, kVulkanReSTIRSetCount>&
                descriptorSets)
            override
        {
            if (activeReSTIRFrameSlot_ >= kWave3DebugFrameCount
                || plan.identity.frameIndex
                    != preparedReSTIRPlans_[activeReSTIRFrameSlot_]
                        .identity.frameIndex)
            {
                return {ReSTIRRuntimeStatusCode::InvalidRequest,
                    "Wave 4 reconstruction callback has no matching prepared frame"};
            }
            return RecordPreparedReconstruction(
                commandBuffer,
                activeReSTIRFrameSlot_,
                descriptorSets[static_cast<std::size_t>(
                    Contracts::AbiV3::DescriptorSet::Scene)]);
        }

        void RecordSurfaceHistoryCopy(
            const VkCommandBuffer commandBuffer,
            const ReSTIRFramePlan& plan)
        {
            VkMemoryBarrier2 memory{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            memory.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            memory.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT
                | VK_ACCESS_2_SHADER_READ_BIT;
            memory.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memory.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT
                | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.memoryBarrierCount = 1u;
            dependency.pMemoryBarriers = &memory;
            vkCmdPipelineBarrier2(commandBuffer, &dependency);
            const VkBufferCopy region{
                0u,
                0u,
                static_cast<VkDeviceSize>(extent_.width) * extent_.height
                    * sizeof(Contracts::AbiV2::GpuPrimarySurfaceV2)};
            vkCmdCopyBuffer(
                commandBuffer,
                wavefrontPrimarySurfaceV2_.Handle(),
                restirSurfaceHistory_[plan.historyWritePhysicalIndex].Handle(),
                1u,
                &region);
        }

        void RecordReSTIRFrame(
            const VkCommandBuffer commandBuffer,
            const std::uint32_t frameSlot,
            const RuntimeConfig&,
            const Wave3DebugFrameBindings& bindings)
        {
            if (bindings.restirReferenceTraversalSet == VK_NULL_HANDLE
                || bindings.restirWinnerTraversalSet == VK_NULL_HANDLE
                || bindings.traversalBackend == nullptr)
            {
                throw std::invalid_argument(
                    "Wave 4 requires distinct reference/winner TraceAny attachments.");
            }
            const ReSTIRFramePlan& plan = preparedReSTIRPlans_[frameSlot];
            if (!plan.IsReady())
            {
                throw std::logic_error("Wave 4 frame was not prepared.");
            }
            RecordReSTIRMotionPrepass(commandBuffer, frameSlot);

            VulkanReSTIRFrameContext context{};
            context.commandBuffer = commandBuffer;
            context.descriptorSets = {
                bindings.frameSet,
                bindings.canonicalSceneSet,
                // Set 2 is layout-only for the ReSTIR compute shaders. Keep
                // the recorder's Ray Query-compatible placeholder here; the
                // actual reference/winner TraceAny calls bind the selected
                // backend's distinct sets through IGpuTraversalBackend below.
                bindings.rayQueryTraversalSet,
                wavefrontDescriptorSets_[frameSlot],
                reconstructionDescriptorSets_[
                    frameSlot * kL8SetsPerFrame],
                restirRecorder_.DescriptorSet(frameSlot),
                restirDebugDescriptorSet_};
            context.referenceTraversalSet =
                bindings.restirReferenceTraversalSet;
            context.winnerTraversalSet =
                bindings.restirWinnerTraversalSet;
            const Wave3DebugReSTIRTraversalViews traversal =
                ReSTIRTraversalViews(frameSlot);
            context.referenceTraversalHits = traversal.referenceHits;
            context.winnerTraversalHits = traversal.winnerHits;
            context.referenceTraversalRays = traversal.shadowRays;
            context.winnerTraversalRays = traversal.shadowRays;
            context.sceneFingerprint = bindings.sceneFingerprint;
            context.sceneGeneration = bindings.sceneGeneration;
            context.frameSlot = frameSlot;
            context.historyReadPhysicalIndex =
                plan.historyReadPhysicalIndex;
            context.historyWritePhysicalIndex =
                plan.historyWritePhysicalIndex;
            context.reconstructionRecorder = this;
            context.resources = MakeReSTIRResourceTable(frameSlot, plan);

            ReSTIRProduction::VulkanReSTIRProductionEvidence evidence{};
            evidence.primarySurfaceV2 =
                Descriptor(wavefrontPrimarySurfaceV2_);
            evidence.motionVectorsV2 = Descriptor(reconstructionGBuffer_);
            evidence.pbrLightTableL6 = bindings.pbrLightTableL6;
            evidence.pbrLightCount = bindings.pbrLightCount;
            evidence.reconstructionOutput = {
                VK_NULL_HANDLE, outputImageView_, VK_IMAGE_LAYOUT_GENERAL};
            evidence.reconstructionOutputExtent = extent_;
            evidence.sceneFingerprint = bindings.sceneFingerprint;
            evidence.sceneGeneration = bindings.sceneGeneration;
            evidence.resourceGeneration = bindings.resourceGeneration;
            evidence.lightGeneration = bindings.lightGeneration;
            evidence.pipelineLayout = restirRecorder_.PipelineLayout();
            evidence.pipelines = restirPipelines_;

            activeReSTIRFrameSlot_ = frameSlot;
            const ReSTIRRuntimeStatus status =
                ReSTIRProduction::RecordVulkanReSTIRProductionFrame(
                    plan,
                    context,
                    evidence,
                    restirRecorder_,
                    *bindings.traversalBackend);
            activeReSTIRFrameSlot_ = kWave3DebugFrameCount;
            if (!status)
            {
                throw std::runtime_error(
                    "Wave 4 production recording failed: " + status.reason);
            }
            RecordSurfaceHistoryCopy(commandBuffer, plan);
            VkMemoryBarrier2 statisticsBarrier{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            statisticsBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            statisticsBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            statisticsBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            statisticsBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            VkDependencyInfo statisticsDependency{
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            statisticsDependency.memoryBarrierCount = 1u;
            statisticsDependency.pMemoryBarriers = &statisticsBarrier;
            vkCmdPipelineBarrier2(commandBuffer, &statisticsDependency);
            const VkBufferCopy statisticsCopy{
                0u, 0u, sizeof(Abi3::GpuRestirStatisticsV3)};
            vkCmdCopyBuffer(commandBuffer,
                restirFrameBuffers_[frameSlot][Abi3::RestirBinding::Statistics].Handle(),
                restirStatisticsReadback_[frameSlot].Handle(), 1u, &statisticsCopy);
            restirStatisticsReadbackPending_[frameSlot] = true;
            restirPublishedHistory_.valid = true;
            restirPublishedHistory_.published = plan.identity;
            restirPublishedHistory_.physicalIndex =
                plan.historyWritePhysicalIndex;
            restirPublishedCamera_ = preparedReSTIRCameras_[frameSlot];
            restirHasPublishedCamera_ = true;
        }

        void ResetReSTIR() noexcept
        {
            ResetReSTIRResourceBuffers();
            for (VkPipeline& pipeline : restirPipelines_)
            {
                if (pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
            }
            (void)restirRecorder_.Shutdown();
            if (restirDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(
                    device_, restirDescriptorPool_, nullptr);
                restirDescriptorPool_ = VK_NULL_HANDLE;
            }
            if (restirDebugDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(
                    device_, restirDebugDescriptorPool_, nullptr);
                restirDebugDescriptorPool_ = VK_NULL_HANDLE;
            }
            if (restirDebugSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(
                    device_, restirDebugSetLayout_, nullptr);
                restirDebugSetLayout_ = VK_NULL_HANDLE;
            }
            restirDebugDescriptorSet_ = VK_NULL_HANDLE;
            restirDebugConstants_.Reset();
            preparedReSTIRPlans_.fill({});
            preparedReSTIRRequests_.fill({});
            restirPublishedHistory_ = {};
            restirHasConfig_ = false;
            restirHasPublishedCamera_ = false;
            restirNextFrameIndex_ = 0u;
            restirConfigGeneration_ = 0u;
            activeReSTIRFrameSlot_ = kWave3DebugFrameCount;
        }

        void ResetBuffers() noexcept
        {
            for (Hardware::DeviceBuffer& buffer : wavefrontFrameBuffers_)
                buffer.Reset();
            wavefrontSharedPaths_.Reset();
            wavefrontRayA_.Reset();
            wavefrontMaterial_.Reset();
            wavefrontShadowQueue_.Reset();
            wavefrontRayB_.Reset();
            wavefrontPaths_.Reset();
            wavefrontDenseNext_.Reset();
            wavefrontDenseShadow_.Reset();
            wavefrontNextQueue_.Reset();
            wavefrontFlags_.Reset();
            wavefrontPrefix_.Reset();
            wavefrontScan_.Reset();
            wavefrontHeaders_.Reset();
            wavefrontIndirect_.Reset();
            wavefrontCounters_.Reset();
            wavefrontShadowAov_.Reset();
            wavefrontPrimarySurfaceV2_.Reset();
            reconstructionGBuffer_.Reset();
            reconstructionMotionInput_.Reset();
            reconstructionCurrentTransforms_.Reset();
            reconstructionPreviousTransforms_.Reset();
            reconstructionRaw_.Reset();
            reconstructionFilm_.Reset();
            reconstructionTerminal_.Reset();
            reconstructionDemodulated_.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionHistory_)
                buffer.Reset();
            reconstructionTemporal_.Reset();
            reconstructionTemporalDebug_.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionVariance_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionAtrous_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionMotionConstants_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionPrepareConstants_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionTemporalConstants_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionVarianceConstants_)
                buffer.Reset();
            for (Hardware::DeviceBuffer& buffer : reconstructionComposeConstants_)
                buffer.Reset();
            for (auto& frame : reconstructionAtrousConstants_)
                for (Hardware::DeviceBuffer& buffer : frame) buffer.Reset();
        }

        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkExtent2D extent_{};
        VkImageView outputImageView_ = VK_NULL_HANDLE;
        std::array<VkImageView, kWave3DebugSignalCount> signalImageViews_{};
        std::filesystem::path shaderDirectory_;
        VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout canonicalSceneSetLayout_ = VK_NULL_HANDLE;
        std::array<VkDescriptorSetLayout, 2u> traversalSetLayouts_{};
        std::uint32_t maximumGroupCountX_ = 0u;
        std::uint32_t maximumGroupCountY_ = 0u;
        Hardware::DeviceDispatch dispatch_{};
        std::unique_ptr<Hardware::DeviceBufferAllocator> allocator_;

        Wavefront::VulkanWavefrontResourceRequirements wavefrontRequirements_{};
        Wavefront::QueueAllocation wavefrontAllocation_{};
        VkDescriptorSetLayout wavefrontSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorPool wavefrontDescriptorPool_ = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kWave3DebugFrameCount>
            wavefrontDescriptorSets_{};
        std::array<VkPipelineLayout, 2u> wavefrontPipelineLayouts_{};
        std::array<Wavefront::VulkanWavefrontPipelines, 2u>
            wavefrontPipelines_{};
        std::array<std::vector<Wavefront::PreparedPass>, kWave3DebugFrameCount>
            preparedWavefront_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            wavefrontFrameBuffers_{};
        Hardware::DeviceBuffer wavefrontSharedPaths_{};
        Hardware::DeviceBuffer wavefrontRayA_{};
        Hardware::DeviceBuffer wavefrontMaterial_{};
        Hardware::DeviceBuffer wavefrontShadowQueue_{};
        Hardware::DeviceBuffer wavefrontRayB_{};
        Hardware::DeviceBuffer wavefrontPaths_{};
        Hardware::DeviceBuffer wavefrontDenseNext_{};
        Hardware::DeviceBuffer wavefrontDenseShadow_{};
        Hardware::DeviceBuffer wavefrontNextQueue_{};
        Hardware::DeviceBuffer wavefrontFlags_{};
        Hardware::DeviceBuffer wavefrontPrefix_{};
        Hardware::DeviceBuffer wavefrontScan_{};
        Hardware::DeviceBuffer wavefrontHeaders_{};
        Hardware::DeviceBuffer wavefrontIndirect_{};
        Hardware::DeviceBuffer wavefrontCounters_{};
        Hardware::DeviceBuffer wavefrontShadowAov_{};
        Hardware::DeviceBuffer wavefrontPrimarySurfaceV2_{};

        L8::VulkanReconstructionResourceRequirements
            reconstructionRequirements_{};
        VkDescriptorSetLayout reconstructionSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorPool reconstructionDescriptorPool_ = VK_NULL_HANDLE;
        std::array<VkDescriptorSet,
            kWave3DebugFrameCount * kL8SetsPerFrame>
            reconstructionDescriptorSets_{};
        VkPipelineLayout reconstructionPipelineLayout_ = VK_NULL_HANDLE;
        L8::VulkanReconstructionPipelines reconstructionPipelines_{};
        std::array<std::vector<L8::PreparedVulkanReconstructionPass>,
            kWave3DebugFrameCount> preparedReconstruction_{};
        std::array<std::vector<VkDescriptorSet>, kWave3DebugFrameCount>
            preparedReconstructionSets_{};
        Hardware::DeviceBuffer reconstructionGBuffer_{};
        Hardware::DeviceBuffer reconstructionMotionInput_{};
        Hardware::DeviceBuffer reconstructionCurrentTransforms_{};
        Hardware::DeviceBuffer reconstructionPreviousTransforms_{};
        std::uint32_t reconstructionTransformCount_ = 1u;
        Hardware::DeviceBuffer reconstructionRaw_{};
        Hardware::DeviceBuffer reconstructionFilm_{};
        Hardware::DeviceBuffer reconstructionTerminal_{};
        Hardware::DeviceBuffer reconstructionDemodulated_{};
        std::array<Hardware::DeviceBuffer, 4u> reconstructionHistory_{};
        Hardware::DeviceBuffer reconstructionTemporal_{};
        Hardware::DeviceBuffer reconstructionTemporalDebug_{};
        std::array<Hardware::DeviceBuffer, 2u> reconstructionVariance_{};
        std::array<Hardware::DeviceBuffer, 2u> reconstructionAtrous_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            reconstructionMotionConstants_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            reconstructionPrepareConstants_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            reconstructionTemporalConstants_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            reconstructionVarianceConstants_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            reconstructionComposeConstants_{};
        std::array<std::array<Hardware::DeviceBuffer,
            kMaximumAtrousIterations>, kWave3DebugFrameCount>
            reconstructionAtrousConstants_{};

        VkDescriptorPool restirDescriptorPool_ = VK_NULL_HANDLE;
        VulkanReSTIRRecorder restirRecorder_{};
        std::array<VkPipeline, kVulkanReSTIRPassCount> restirPipelines_{};
        VkDescriptorSetLayout restirDebugSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorPool restirDebugDescriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet restirDebugDescriptorSet_ = VK_NULL_HANDLE;
        Hardware::DeviceBuffer restirDebugConstants_{};
        std::array<std::array<Hardware::DeviceBuffer,
            kVulkanReSTIRSet5BindingCount>, kWave3DebugFrameCount>
            restirFrameBuffers_{};
        std::array<Hardware::DeviceBuffer, kWave3DebugFrameCount>
            restirStatisticsReadback_{};
        std::array<bool, kWave3DebugFrameCount>
            restirStatisticsReadbackPending_{};
        std::array<Hardware::DeviceBuffer, kReSTIRHistorySlotCount>
            restirReservoirHistory_{};
        std::array<Hardware::DeviceBuffer, kReSTIRHistorySlotCount>
            restirSurfaceHistory_{};
        Hardware::DeviceBuffer restirResetReservoir_{};
        Hardware::DeviceBuffer restirResetSurface_{};
        std::array<ReSTIRFramePlan, kWave3DebugFrameCount>
            preparedReSTIRPlans_{};
        std::array<ReSTIRFrameRequest, kWave3DebugFrameCount>
            preparedReSTIRRequests_{};
        std::array<Wave3DebugCamera, kWave3DebugFrameCount>
            preparedReSTIRCameras_{};
        ReSTIRHistoryIdentity restirPublishedHistory_{};
        Wave3DebugCamera restirPublishedCamera_{};
        RuntimeConfig restirLastConfig_{};
        std::uint64_t restirNextFrameIndex_ = 0u;
        std::uint64_t restirConfigGeneration_ = 0u;
        std::uint32_t restirAllocatedLightCount_ = 0u;
        std::uint32_t restirAllocatedCandidates_ = 0u;
        std::uint32_t restirAllocatedNeighbors_ = 0u;
        ReSTIREstimatorMode restirAllocatedEstimator_ =
            ReSTIREstimatorMode::ExplicitlyBiased;
        ShadowMethod restirAllocatedShadowMethod_ = ShadowMethod::Physical;
        std::uint32_t activeReSTIRFrameSlot_ = kWave3DebugFrameCount;
        bool restirResourcesReady_ = false;
        bool restirHasConfig_ = false;
        bool restirHasPublishedCamera_ = false;

        Wave3DebugCamera lastCamera_{};
        ReconstructionMode lastReconstructionMode_ = ReconstructionMode::ProgressiveMean;
        std::uint32_t filmSampleCount_ = 0u;
        bool hasLastCamera_ = false;
        bool ready_ = false;
    };

    Wave3DebugRuntime::Wave3DebugRuntime()
        : impl_(std::make_unique<Impl>()) {}
    Wave3DebugRuntime::~Wave3DebugRuntime() = default;
    Wave3DebugRuntime::Wave3DebugRuntime(Wave3DebugRuntime&&) noexcept = default;
    Wave3DebugRuntime& Wave3DebugRuntime::operator=(
        Wave3DebugRuntime&&) noexcept = default;

    void Wave3DebugRuntime::Create(
        const Wave3DebugRuntimeCreateInfo& createInfo)
    {
        impl_->Create(createInfo);
    }

    void Wave3DebugRuntime::Reset() noexcept { impl_->Reset(); }
    void Wave3DebugRuntime::InvalidateHistories() noexcept
    {
        impl_->InvalidateHistories();
    }
    void Wave3DebugRuntime::InvalidateProgressiveFilm() noexcept
    {
        impl_->InvalidateProgressiveFilm();
    }
    void Wave3DebugRuntime::SetSceneTransforms(
        const std::span<const Contracts::AbiV0::AbiMat4Rows> currentObjectToWorld,
        const std::span<const Contracts::AbiV0::AbiMat4Rows> previousObjectToWorld)
    {
        impl_->SetSceneTransforms(currentObjectToWorld, previousObjectToWorld);
    }
    bool Wave3DebugRuntime::IsReady() const noexcept { return impl_->IsReady(); }
    std::uint32_t Wave3DebugRuntime::WavefrontFatalMask() const noexcept
    {
        return impl_->WavefrontFatalMask();
    }

    Wave3ReSTIRFrameObservation Wave3DebugRuntime::CollectCompletedFrame(
        const std::uint32_t frameSlot) noexcept
    {
        return impl_->CollectCompletedFrame(frameSlot);
    }

    VkDescriptorSetLayout Wave3DebugRuntime::ReconstructionSetLayout() const noexcept
    {
        return impl_->ReconstructionSetLayout();
    }

    VkDescriptorSet Wave3DebugRuntime::ReconstructionSet(
        const std::uint32_t frameSlot) const noexcept
    {
        return impl_->ReconstructionSet(frameSlot);
    }

    void Wave3DebugRuntime::UpdateFrame(
        const std::uint32_t frameSlot,
        const RuntimeConfig& config,
        const Wave3DebugCamera& camera,
        const std::uint32_t sampleIndex,
        const Wave3DebugFrameBindings& bindings)
    {
        impl_->UpdateFrame(frameSlot, config, camera, sampleIndex, bindings);
    }

    Wave3DebugReSTIRTraversalViews Wave3DebugRuntime::ReSTIRTraversalViews(
        const std::uint32_t frameSlot) const noexcept
    {
        return impl_->ReSTIRTraversalViews(frameSlot);
    }

    void Wave3DebugRuntime::RecordFrame(
        const VkCommandBuffer commandBuffer,
        const std::uint32_t frameSlot,
        const RuntimeConfig& config,
        const Wave3DebugFrameBindings& bindings)
    {
        impl_->RecordFrame(commandBuffer, frameSlot, config, bindings);
    }

    void Wave3DebugRuntime::UpdatePostIntegratorFrame(
        const std::uint32_t frameSlot,
        const RuntimeConfig& config,
        const Wave3DebugCamera& camera,
        const std::uint32_t sampleIndex,
        const Wave3DebugFrameBindings& bindings)
    {
        impl_->UpdatePostIntegratorFrame(
            frameSlot, config, camera, sampleIndex, bindings);
    }

    void Wave3DebugRuntime::RecordPostIntegratorFrame(
        const VkCommandBuffer commandBuffer,
        const std::uint32_t frameSlot,
        const RuntimeConfig& config,
        const Wave3DebugFrameBindings& bindings)
    {
        impl_->RecordPostIntegratorFrame(
            commandBuffer, frameSlot, config, bindings);
    }
}
