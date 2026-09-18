#include "renderers/Wave2Runtime.hpp"
#include "renderers/Wave3DebugRuntime.hpp"

#include "AccelerationStructures.hpp"
#include "DeviceBuffer.hpp"
#include "DeviceDispatch.hpp"
#include "HardwareRtCapabilities.hpp"
#include "MegakernelBridge.hpp"
#include "RayQueryBackend.hpp"
#include "contracts/AbiV1.hpp"
#include "rt/cpu/Bvh.hpp"
#include "rt/gpu/CanonicalTraversalScene.hpp"
#include "rt/software_gpu/SoftwareGpuBackend.hpp"
#include "scene/CanonicalScene.hpp"
#include "scene/ExperimentScenes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace RenderingEngine::Renderers
{
    namespace
    {
        namespace Abi0 = Contracts::AbiV0;
        namespace Abi1 = Contracts::AbiV1;
        namespace Gpu = Rt::Gpu;
        namespace Hardware = Rt::Hardware;
        namespace L4 = Rt::SoftwareGpu;
        namespace Mega = Integrators::Megakernel;

        constexpr VkBufferUsageFlags kStorageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        constexpr VkBufferUsageFlags kAsInputUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        constexpr std::size_t kWave2SignalImageCount = kWave2RuntimeSignalCount;
        constexpr std::size_t kWave2PrimaryPipelineCount = 4u;
        constexpr std::size_t kWave2IndirectSignalCount = 2u;
        constexpr std::size_t kWave2SignalPipelineCount =
            2u * kWave2SignalImageCount;
        constexpr std::size_t kWave2IndirectSeedPipelineBase =
            kWave2PrimaryPipelineCount + kWave2SignalPipelineCount;
        constexpr std::size_t kWave2IndirectShadePipelineBase =
            kWave2IndirectSeedPipelineBase
            + 2u * kWave2IndirectSignalCount;
        constexpr std::size_t kWave2IndirectResolvePipelineBase =
            kWave2IndirectShadePipelineBase
            + 2u * kWave2IndirectSignalCount;
        constexpr std::size_t kWave2RawSeedPipelineBase =
            kWave2IndirectResolvePipelineBase
            + 2u * kWave2IndirectSignalCount;
        constexpr std::size_t kWave2RawTracePipelineBase =
            kWave2RawSeedPipelineBase + 2u;
        constexpr std::size_t kWave2RawShadePipelineBase =
            kWave2RawTracePipelineBase + 2u;
        constexpr std::size_t kWave2RawResolvePipelineBase =
            kWave2RawShadePipelineBase + 2u;
        constexpr std::size_t kWave2PipelineCount =
            kWave2RawResolvePipelineBase + 2u;
        // Staged Raw carries five ABI-v2 split-signal accumulators after the
        // original seven float4 path-state records. The same backing buffer is
        // intentionally oversized for the smaller single-AOV variants.
        constexpr VkDeviceSize kWave2IndirectSeedStride = 12u * 4u
            * sizeof(float);
        constexpr std::array<std::string_view, kWave2SignalImageCount>
            kWave2SignalShaderSuffixes{
                "camera_emission",
                "direct_diffuse",
                "direct_specular",
                "indirect_diffuse",
                "indirect_specular"};
        constexpr VkFormat kWave2SignalImageFormat =
            VK_FORMAT_R32G32B32A32_SFLOAT;

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

        void Require(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string(message));
            }
        }

        [[nodiscard]] std::vector<std::uint32_t> ReadSpirv(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw std::runtime_error("Unable to open Wave 2 shader: " + path.string());
            }
            const std::streamsize byteCount = stream.tellg();
            if (byteCount <= 0 || byteCount % 4 != 0)
            {
                throw std::runtime_error("Invalid Wave 2 SPIR-V byte count: " + path.string());
            }
            stream.seekg(0, std::ios::beg);
            std::vector<std::uint32_t> words(
                static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
            stream.read(reinterpret_cast<char*>(words.data()), byteCount);
            if (!stream)
            {
                throw std::runtime_error("Unable to read Wave 2 shader: " + path.string());
            }
            return words;
        }

        template <typename Value, std::size_t Extent>
        [[nodiscard]] Hardware::DeviceBuffer UploadBuffer(
            const Hardware::DeviceBufferAllocator& allocator,
            const std::span<Value, Extent> values,
            const VkBufferUsageFlags usage,
            const bool requireDeviceAddress = false)
        {
            Require(!values.empty(), "Wave 2 cannot upload an empty buffer.");
            Hardware::DeviceBuffer buffer;
            RequireStatus(allocator.Create(
                values.size_bytes(), usage,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                requireDeviceAddress, true, buffer),
                "DeviceBufferAllocator::Create(upload)");
            RequireStatus(allocator.Upload(buffer, std::as_bytes(values)),
                "DeviceBufferAllocator::Upload");
            return buffer;
        }

        [[nodiscard]] Hardware::DeviceBuffer CreateMappedBuffer(
            const Hardware::DeviceBufferAllocator& allocator,
            const VkDeviceSize byteCount,
            const VkBufferUsageFlags usage)
        {
            Hardware::DeviceBuffer buffer;
            RequireStatus(allocator.Create(
                byteCount, usage,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                false, true, buffer),
                "DeviceBufferAllocator::Create(mapped)");
            std::memset(buffer.MappedData(), 0, static_cast<std::size_t>(byteCount));
            return buffer;
        }

        [[nodiscard]] Hardware::DeviceBuffer CreateDeviceLocalBuffer(
            const Hardware::DeviceBufferAllocator& allocator,
            const VkDeviceSize byteCount,
            const VkBufferUsageFlags usage)
        {
            Hardware::DeviceBuffer buffer;
            RequireStatus(allocator.Create(
                byteCount,
                usage,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                false,
                false,
                buffer),
                "DeviceBufferAllocator::Create(device-local)");
            return buffer;
        }

        [[nodiscard]] VkDescriptorBufferInfo Descriptor(
            const Hardware::DeviceBuffer& buffer) noexcept
        {
            return {buffer.Handle(), 0u, buffer.Size()};
        }

        [[nodiscard]] Mega::Float4 ToMega(const Abi0::AbiFloat4 value) noexcept
        {
            return {value.x, value.y, value.z, value.w};
        }

        [[nodiscard]] VkTransformMatrixKHR ToVkTransform(
            const Abi0::AbiMat4Rows& transform) noexcept
        {
            VkTransformMatrixKHR result{};
            const std::array rows{transform.row0, transform.row1, transform.row2};
            for (std::size_t row = 0u; row < rows.size(); ++row)
            {
                result.matrix[row][0] = rows[row].x;
                result.matrix[row][1] = rows[row].y;
                result.matrix[row][2] = rows[row].z;
                result.matrix[row][3] = rows[row].w;
            }
            return result;
        }

        [[nodiscard]] Mega::DirectLightingEstimator ToMegakernelEstimator(
            const DirectLightingEstimator estimator)
        {
            switch (estimator)
            {
            case DirectLightingEstimator::BsdfOnly:
                return Mega::DirectLightingEstimator::BsdfOnly;
            case DirectLightingEstimator::NextEventEstimation:
                return Mega::DirectLightingEstimator::NextEventEstimation;
            case DirectLightingEstimator::MultipleImportanceSampling:
                return Mega::DirectLightingEstimator::Mis;
            case DirectLightingEstimator::RestirDirectIllumination:
                return Mega::DirectLightingEstimator::RestirPrimary;
            default:
                throw std::invalid_argument(
                    "Wave 2 Megakernel received an unsupported direct-lighting estimator.");
            }
        }

        [[nodiscard]] Mega::LightSelection ToMegakernelLightSelection(
            const LightSelectionStrategy selection)
        {
            switch (selection)
            {
            case LightSelectionStrategy::Uniform:
                return Mega::LightSelection::Uniform;
            case LightSelectionStrategy::PowerWeighted:
                return Mega::LightSelection::PowerWeighted;
            default:
                throw std::invalid_argument(
                    "Wave 2 Megakernel received an unsupported light-selection strategy.");
            }
        }

        [[nodiscard]] Mega::EnvironmentSampler ToMegakernelEnvironmentSampler(
            const EnvironmentDirectionSampler sampler)
        {
            switch (sampler)
            {
            case EnvironmentDirectionSampler::UniformSphere:
                return Mega::EnvironmentSampler::UniformSphere;
            case EnvironmentDirectionSampler::ImportanceMap:
                return Mega::EnvironmentSampler::ImportanceMap;
            default:
                throw std::invalid_argument(
                    "Wave 2 Megakernel received an unsupported environment sampler.");
            }
        }

        class DummySampledResources final
        {
        public:
            void Create(const VkPhysicalDevice physicalDevice, const VkDevice device)
            {
                device_ = device;
                VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                imageInfo.extent = {1u, 1u, 1u};
                imageInfo.mipLevels = 1u;
                imageInfo.arrayLayers = 2u;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                Check(vkCreateImage(device_, &imageInfo, nullptr, &image_),
                    "vkCreateImage(Wave2 dummy sampled)");

                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements(device_, image_, &requirements);
                VkPhysicalDeviceMemoryProperties memoryProperties{};
                vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
                std::uint32_t memoryType = std::numeric_limits<std::uint32_t>::max();
                for (std::uint32_t index = 0u; index < memoryProperties.memoryTypeCount; ++index)
                {
                    if ((requirements.memoryTypeBits & (1u << index)) != 0u
                        && (memoryProperties.memoryTypes[index].propertyFlags
                            & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                            == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                    {
                        memoryType = index;
                        break;
                    }
                }
                Require(memoryType != std::numeric_limits<std::uint32_t>::max(),
                    "Wave 2 dummy sampled image has no device-local memory type.");
                VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocationInfo.allocationSize = requirements.size;
                allocationInfo.memoryTypeIndex = memoryType;
                Check(vkAllocateMemory(device_, &allocationInfo, nullptr, &memory_),
                    "vkAllocateMemory(Wave2 dummy sampled)");
                Check(vkBindImageMemory(device_, image_, memory_, 0u),
                    "vkBindImageMemory(Wave2 dummy sampled)");

                VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = image_;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.levelCount = 1u;
                viewInfo.subresourceRange.layerCount = 2u;
                Check(vkCreateImageView(device_, &viewInfo, nullptr, &arrayView_),
                    "vkCreateImageView(Wave2 alpha atlas)");
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.subresourceRange.layerCount = 1u;
                Check(vkCreateImageView(device_, &viewInfo, nullptr, &environmentView_),
                    "vkCreateImageView(Wave2 environment)");

                VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                samplerInfo.magFilter = VK_FILTER_NEAREST;
                samplerInfo.minFilter = VK_FILTER_NEAREST;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.maxLod = 0.0f;
                Check(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_),
                    "vkCreateSampler(Wave2 dummy sampled)");
            }

            void Destroy() noexcept
            {
                if (device_ != VK_NULL_HANDLE && sampler_ != VK_NULL_HANDLE)
                {
                    vkDestroySampler(device_, sampler_, nullptr);
                }
                if (device_ != VK_NULL_HANDLE && environmentView_ != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device_, environmentView_, nullptr);
                }
                if (device_ != VK_NULL_HANDLE && arrayView_ != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device_, arrayView_, nullptr);
                }
                if (device_ != VK_NULL_HANDLE && image_ != VK_NULL_HANDLE)
                {
                    vkDestroyImage(device_, image_, nullptr);
                }
                if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE)
                {
                    vkFreeMemory(device_, memory_, nullptr);
                }
                device_ = VK_NULL_HANDLE;
                sampler_ = VK_NULL_HANDLE;
                environmentView_ = VK_NULL_HANDLE;
                arrayView_ = VK_NULL_HANDLE;
                image_ = VK_NULL_HANDLE;
                memory_ = VK_NULL_HANDLE;
                initialized_ = false;
            }

            void RecordInitialize(
                const Hardware::DeviceDispatch& dispatch,
                const VkCommandBuffer commandBuffer)
            {
                if (initialized_)
                {
                    return;
                }
                VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image_;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.layerCount = 2u;
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.imageMemoryBarrierCount = 1u;
                dependency.pImageMemoryBarriers = &barrier;
                dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);

                VkImageSubresourceRange layer{};
                layer.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                layer.levelCount = 1u;
                layer.layerCount = 1u;
                VkClearColorValue transparent{};
                transparent.float32[0] = 1.0f;
                transparent.float32[1] = 1.0f;
                transparent.float32[2] = 1.0f;
                transparent.float32[3] = 0.0f;
                layer.baseArrayLayer = 0u;
                vkCmdClearColorImage(commandBuffer, image_,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent, 1u, &layer);
                VkClearColorValue opaque = transparent;
                opaque.float32[3] = 1.0f;
                layer.baseArrayLayer = 1u;
                vkCmdClearColorImage(commandBuffer, image_,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &opaque, 1u, &layer);

                barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                dispatch.cmdPipelineBarrier2(commandBuffer, &dependency);
                initialized_ = true;
            }

            [[nodiscard]] VkDescriptorImageInfo AlphaImage() const noexcept
            {
                return {VK_NULL_HANDLE, arrayView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
            [[nodiscard]] VkDescriptorImageInfo EnvironmentImage() const noexcept
            {
                return {VK_NULL_HANDLE, environmentView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
            [[nodiscard]] VkDescriptorImageInfo Sampler() const noexcept
            {
                return {sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            }

        private:
            VkDevice device_ = VK_NULL_HANDLE;
            VkImage image_ = VK_NULL_HANDLE;
            VkDeviceMemory memory_ = VK_NULL_HANDLE;
            VkImageView arrayView_ = VK_NULL_HANDLE;
            VkImageView environmentView_ = VK_NULL_HANDLE;
            VkSampler sampler_ = VK_NULL_HANDLE;
            bool initialized_ = false;
        };

        struct MegakernelHostScene final
        {
            std::vector<Mega::PbrMaterialGpu> materials;
            std::vector<Mega::PbrLightGpu> lights;
            Mega::TopLevelLightDistribution uniformDistribution;
            Mega::TopLevelLightDistribution powerDistribution;
            std::vector<Mega::AliasEntryGpu> environmentRows{{1.0f, 1.0f, 0u, 0u}};
            std::vector<Mega::AliasEntryGpu> environmentColumns{{1.0f, 1.0f, 0u, 0u}};
            std::uint32_t environmentLightIndex = Mega::kInvalidIndex;
            std::uint32_t environmentWidth = 0u;
            std::uint32_t environmentHeight = 0u;
            double environmentIntegratedLuminance = 0.0;
            std::vector<Mega::EmitterMapEntryGpu> emitterMap;
            std::uint32_t emitterMapCount = 0u;
            std::vector<Mega::FixtureTriangleGpu> fixtureTriangles;
            std::vector<Mega::FixtureSphereGpu> fixtureSpheres{1u};
            Mega::Float4 sceneCenterRadius{};
        };

        [[nodiscard]] MegakernelHostScene BuildMegakernelHostScene(
            const Scene::CanonicalScene& canonical,
            const Gpu::CanonicalTraversalScene& traversal,
            const Scene::ExperimentEnvironment* const environment,
            const bool environmentEnabled)
        {
            constexpr std::uint32_t diffuseLobe = 1u << 0u;
            constexpr std::uint32_t glossyLobe = 1u << 1u;
            constexpr std::uint32_t specularLobe = 1u << 2u;
            constexpr std::uint32_t reflectionLobe = 1u << 3u;
            constexpr std::uint32_t transmissionLobe = 1u << 4u;
            constexpr std::uint32_t metallicRoughnessModel = 5u;
            constexpr std::uint32_t smoothGlassModel = 3u;
            constexpr std::uint32_t roughDielectricModel = 4u;

            MegakernelHostScene result;
            result.materials.reserve(canonical.materials.size());
            for (const Abi0::GpuMaterialV0& source : canonical.materials)
            {
                Mega::PbrMaterialGpu material{};
                material.baseColorMetallic = {
                    source.baseColorFactor.x, source.baseColorFactor.y,
                    source.baseColorFactor.z, source.surfaceParams.x};
                material.emissiveRoughness = {
                    source.emissiveFactorStrength.x * source.emissiveFactorStrength.w,
                    source.emissiveFactorStrength.y * source.emissiveFactorStrength.w,
                    source.emissiveFactorStrength.z * source.emissiveFactorStrength.w,
                    source.surfaceParams.y};
                material.transmissionIor = {
                    source.transmissionParams.x, source.transmissionParams.y, 0.0f, 0.0f};
                material.attenuationColorDistance = ToMega(source.attenuationColorDistance);
                material.f0 = {0.04f, 0.04f, 0.04f, 0.0f};
                material.conductorEta = {1.0f, 1.0f, 1.0f, 0.0f};
                material.conductorK = {};
                const bool smoothDielectric =
                    source.metadata.x == Abi0::MaterialModelSmoothDielectric;
                // Scene ABI v0 already carries transmission and IOR on its
                // metallic-roughness record. Preserve that public contract and
                // route a transmissive record to L6's private rough-dielectric
                // model instead of publishing a new shared material enum.
                const bool roughDielectric = !smoothDielectric
                    && source.transmissionParams.x > 0.0f;
                std::uint32_t privateFlags = Mega::MaterialFlagNone;
                const float baseMaximum = std::max({
                    source.baseColorFactor.x, source.baseColorFactor.y,
                    source.baseColorFactor.z});
                const float emissionMaximum = std::max({
                    material.emissiveRoughness.x, material.emissiveRoughness.y,
                    material.emissiveRoughness.z});
                if (emissionMaximum > 0.0f && baseMaximum == 0.0f
                    && source.transmissionParams.x == 0.0f)
                {
                    privateFlags |= Mega::MaterialFlagPureEmitter;
                }
                if ((source.metadata.y & Abi0::MaterialFlagThinWalled) != 0u)
                {
                    privateFlags |= Mega::MaterialFlagThinWalled;
                }
                material.metadata = {
                    smoothDielectric
                        ? smoothGlassModel
                        : (roughDielectric ? roughDielectricModel : metallicRoughnessModel),
                    smoothDielectric
                        ? specularLobe | reflectionLobe | transmissionLobe
                        : (roughDielectric
                            ? glossyLobe | reflectionLobe | transmissionLobe
                            : diffuseLobe | glossyLobe | reflectionLobe),
                    0u, privateFlags};
                result.materials.push_back(material);
            }

            result.lights.reserve(canonical.lights.size());
            for (const Abi0::GpuLightV0& source : canonical.lights)
            {
                Mega::PbrLightGpu light{};
                light.positionRange = ToMega(source.positionRange);
                light.directionCosOuter = ToMega(source.directionCosOuter);
                light.radianceScale = ToMega(source.radianceScale);
                light.shapeParams = ToMega(source.shapeParams);
                light.identity = {
                    source.identity.x, source.extra.y, source.identity.y, source.identity.w};
                light.payload = {
                    source.extra.x, source.extra.z, source.extra.w, source.identity.z};
                if (source.identity.x == Abi0::LightTypeEmissiveTriangle)
                {
                    const auto found = std::find_if(
                        traversal.triangles.begin(), traversal.triangles.end(),
                        [&source](const Gpu::CanonicalTraversalTriangle& triangle)
                        {
                            return triangle.identity.x == source.identity.z
                                && triangle.identity.y == source.identity.w;
                        });
                    Require(found != traversal.triangles.end(),
                        "Canonical emissive light references no traversal triangle.");
                    light.payload.x = static_cast<std::uint32_t>(
                        std::distance(traversal.triangles.begin(), found));
                }
                result.lights.push_back(light);
            }
            Require(!result.materials.empty() && !result.lights.empty(),
                "Wave 2 production scenes require materials and at least one light.");

            for (std::size_t index = 0u; index < result.lights.size(); ++index)
            {
                const Mega::PbrLightGpu& light = result.lights[index];
                if (light.identity.x != static_cast<std::uint32_t>(Mega::LightType::Environment)
                    || (light.identity.y & Mega::LightFlagEnabled) == 0u)
                {
                    continue;
                }
                Require(result.environmentLightIndex == Mega::kInvalidIndex,
                    "Wave 2 production supports one active environment light per scene variant.");
                Require(index <= std::numeric_limits<std::uint32_t>::max(),
                    "Wave 2 environment light index exceeds uint32.");
                result.environmentLightIndex = static_cast<std::uint32_t>(index);
            }
            if (environmentEnabled)
            {
                Require(environment != nullptr && !environment->Empty(),
                    "Wave 2 environment variant requires non-empty linear environment data.");
                Require(result.environmentLightIndex != Mega::kInvalidIndex,
                    "Wave 2 environment variant has no enabled environment light.");
                const std::uint64_t texelCount =
                    static_cast<std::uint64_t>(environment->width) * environment->height;
                Require(texelCount == environment->linearRgba.size()
                        && texelCount <= std::numeric_limits<std::uint32_t>::max(),
                    "Wave 2 environment dimensions do not match the linear texel payload.");
                std::vector<float> luminance;
                luminance.reserve(environment->linearRgba.size());
                for (const Abi0::AbiFloat4 texel : environment->linearRgba)
                {
                    Require(std::isfinite(texel.x) && std::isfinite(texel.y)
                            && std::isfinite(texel.z) && std::isfinite(texel.w)
                            && texel.x >= 0.0f && texel.y >= 0.0f
                            && texel.z >= 0.0f,
                        "Wave 2 environment contains invalid linear radiance.");
                    luminance.push_back(
                        0.2126f * texel.x + 0.7152f * texel.y + 0.0722f * texel.z);
                }
                Mega::EnvironmentDistribution distribution =
                    Mega::BuildEnvironmentDistribution(
                        environment->width, environment->height, luminance);
                result.environmentRows = std::move(distribution.rowAlias);
                result.environmentColumns = std::move(distribution.columnAlias);
                result.environmentWidth = distribution.width;
                result.environmentHeight = distribution.height;
                result.environmentIntegratedLuminance =
                    distribution.integratedLuminance;
            }
            else
            {
                Require(result.environmentLightIndex == Mega::kInvalidIndex,
                    "An enabled environment light requires an active environment variant.");
            }

            result.emitterMap = Mega::BuildEmitterMap(result.lights);
            Require(result.emitterMap.size()
                    <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                "Wave 2 emitter map exceeds uint32 capacity.");
            result.emitterMapCount = static_cast<std::uint32_t>(result.emitterMap.size());
            if (result.emitterMap.empty())
            {
                result.emitterMap.push_back({});
            }

            result.fixtureTriangles.reserve(traversal.triangles.size());
            for (const Gpu::CanonicalTraversalTriangle& source : traversal.triangles)
            {
                Mega::FixtureTriangleGpu triangle{};
                triangle.p0 = ToMega(source.positions[0]);
                triangle.p1 = ToMega(source.positions[1]);
                triangle.p2 = ToMega(source.positions[2]);
                triangle.metadata = {
                    source.identity.w, source.identity.y, source.identity.x, source.metadata.x};
                result.fixtureTriangles.push_back(triangle);
            }

            const float centerX = (canonical.constants.sceneBoundsMin.x
                + canonical.constants.sceneBoundsMax.x) * 0.5f;
            const float centerY = (canonical.constants.sceneBoundsMin.y
                + canonical.constants.sceneBoundsMax.y) * 0.5f;
            const float centerZ = (canonical.constants.sceneBoundsMin.z
                + canonical.constants.sceneBoundsMax.z) * 0.5f;
            const float halfX = (canonical.constants.sceneBoundsMax.x
                - canonical.constants.sceneBoundsMin.x) * 0.5f;
            const float halfY = (canonical.constants.sceneBoundsMax.y
                - canonical.constants.sceneBoundsMin.y) * 0.5f;
            const float halfZ = (canonical.constants.sceneBoundsMax.z
                - canonical.constants.sceneBoundsMin.z) * 0.5f;
            const float radius = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
            Require(std::isfinite(radius) && radius > 0.0f,
                "Wave 2 scene radius must be finite and positive.");
            result.sceneCenterRadius = {centerX, centerY, centerZ, radius};

            result.uniformDistribution = Mega::BuildTopLevelLightDistribution(
                result.lights, Mega::LightSelection::Uniform, radius,
                result.environmentIntegratedLuminance);
            result.powerDistribution = Mega::BuildTopLevelLightDistribution(
                result.lights, Mega::LightSelection::PowerWeighted, radius,
                result.environmentIntegratedLuminance);
            Require(!result.uniformDistribution.alias.empty()
                    && !result.powerDistribution.alias.empty(),
                "Wave 2 light distributions must not be empty.");
            return result;
        }
    }

    class Wave2Runtime::Impl final
    {
    public:
        ~Impl()
        {
            Reset();
        }

        void Create(const Wave2RuntimeCreateInfo& createInfo)
        {
            if (device_ != VK_NULL_HANDLE)
            {
                throw std::logic_error("Wave2Runtime::Create may only be called once.");
            }
            if (createInfo.physicalDevice == VK_NULL_HANDLE
                || createInfo.device == VK_NULL_HANDLE
                || createInfo.queue == VK_NULL_HANDLE
                || createInfo.commandPool == VK_NULL_HANDLE
                || createInfo.outputImageView == VK_NULL_HANDLE
                || createInfo.outputExtent.width == 0u
                || createInfo.outputExtent.height == 0u)
            {
                throw std::invalid_argument("Wave2RuntimeCreateInfo is incomplete.");
            }

            physicalDevice_ = createInfo.physicalDevice;
            device_ = createInfo.device;
            queue_ = createInfo.queue;
            commandPool_ = createInfo.commandPool;
            outputImageView_ = createInfo.outputImageView;
            outputExtent_ = createInfo.outputExtent;
            shaderDirectory_ = createInfo.shaderDirectory;

            const Hardware::HardwareRtCapabilityReport capabilities =
                Hardware::QueryHardwareRtCapabilities(physicalDevice_);
            if (!capabilities.SupportsRayQuery())
            {
                std::string message = "Wave 2 requires Vulkan Ray Query:";
                for (const std::string& missing : capabilities.missingRayQueryRequirements)
                {
                    message += " ";
                    message += missing;
                    message += ";";
                }
                throw std::runtime_error(message);
            }
            maximumComputeGroupCountX_ = capabilities.limits.maxComputeWorkGroupCountX;
            scratchAlignment_ = capabilities.limits.minAccelerationStructureScratchOffsetAlignment;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
            maximumComputeGroupCountY_ =
                properties.limits.maxComputeWorkGroupCount[1u];
            timestampPeriodNanoseconds_ = properties.limits.timestampPeriod;

            RequireStatus(Hardware::DeviceDispatch::Load(device_, false, dispatch_),
                "DeviceDispatch::Load(Wave2)");
            allocator_ = std::make_unique<Hardware::DeviceBufferAllocator>(
                physicalDevice_, device_, dispatch_);

            VkDescriptorSetLayoutCreateInfo emptyInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            Check(vkCreateDescriptorSetLayout(device_, &emptyInfo, nullptr, &emptyLayout_),
                "vkCreateDescriptorSetLayout(Wave2 empty)");

            const std::vector<std::uint32_t> softwareTraversal = ReadSpirv(
                createInfo.shaderDirectory / "software_trace_v1.spv");
            const std::vector<std::uint32_t> rayQueryTraversal = ReadSpirv(
                createInfo.shaderDirectory / "hardware_ray_query_v1.spv");
            const std::vector<std::uint32_t> softwareMegakernel = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_software.spv");
            const std::vector<std::uint32_t> rayQueryMegakernel = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_rayquery.spv");
            const std::vector<std::uint32_t> softwareMegakernelNoProfile = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_software_noprofile.spv");
            const std::vector<std::uint32_t> rayQueryMegakernelNoProfile = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_rayquery_noprofile.spv");
            const std::vector<std::uint32_t> rawSeed = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_raw_seed.spv");
            const std::vector<std::uint32_t> softwareRawTrace = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_software_raw_trace.spv");
            const std::vector<std::uint32_t> rayQueryRawTrace = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_rayquery_raw_trace.spv");
            const std::vector<std::uint32_t> softwareRawShade = ReadSpirv(
                createInfo.shaderDirectory /
                    "pbr_megakernel_software_raw_shade.spv");
            const std::vector<std::uint32_t> rayQueryRawShade = ReadSpirv(
                createInfo.shaderDirectory /
                    "pbr_megakernel_rayquery_raw_shade.spv");
            const std::vector<std::uint32_t> rawResolve = ReadSpirv(
                createInfo.shaderDirectory / "pbr_megakernel_raw_resolve.spv");
            std::array<std::vector<std::uint32_t>, kWave2SignalImageCount>
                softwareSignalMegakernels;
            std::array<std::vector<std::uint32_t>, kWave2SignalImageCount>
                rayQuerySignalMegakernels;
            for (std::size_t index = 0u;
                index < kWave2SignalImageCount; ++index)
            {
                softwareSignalMegakernels[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    ("pbr_megakernel_software_"
                        + std::string(kWave2SignalShaderSuffixes[index]) + ".spv"));
                rayQuerySignalMegakernels[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    ("pbr_megakernel_rayquery_"
                        + std::string(kWave2SignalShaderSuffixes[index]) + ".spv"));
            }
            std::array<std::vector<std::uint32_t>, kWave2IndirectSignalCount>
                softwareIndirectSeeds;
            std::array<std::vector<std::uint32_t>, kWave2IndirectSignalCount>
                rayQueryIndirectSeeds;
            std::array<std::vector<std::uint32_t>, kWave2IndirectSignalCount>
                softwareIndirectShades;
            std::array<std::vector<std::uint32_t>, kWave2IndirectSignalCount>
                rayQueryIndirectShades;
            std::array<std::vector<std::uint32_t>, kWave2IndirectSignalCount>
                indirectResolves;
            for (std::size_t index = 0u;
                index < kWave2IndirectSignalCount; ++index)
            {
                const std::string suffix = index == 0u
                    ? "indirect_diffuse_seed"
                    : "indirect_specular_seed";
                softwareIndirectSeeds[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    ("pbr_megakernel_software_" + suffix + ".spv"));
                rayQueryIndirectSeeds[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    ("pbr_megakernel_rayquery_" + suffix + ".spv"));
                const std::string shadeSuffix = index == 0u
                    ? "indirect_diffuse_shade"
                    : "indirect_specular_shade";
                softwareIndirectShades[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    ("pbr_megakernel_software_" + shadeSuffix + ".spv"));
                rayQueryIndirectShades[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    ("pbr_megakernel_rayquery_" + shadeSuffix + ".spv"));
                indirectResolves[index] = ReadSpirv(
                    createInfo.shaderDirectory /
                    (index == 0u
                        ? "pbr_megakernel_indirect_diffuse_resolve.spv"
                        : "pbr_megakernel_indirect_specular_resolve.spv"));
            }

            RequireStatus(rayQueryBackend_.Create(
                device_, rayQueryTraversal, maximumComputeGroupCountX_),
                "RayQueryBackend::Create(production)");
            RequireStatus(softwareBackend_.Create(L4::SoftwareGpuBackendCreateInfo{
                device_, emptyLayout_, rayQueryBackend_.SceneAdapterLayout(),
                softwareTraversal, maximumComputeGroupCountX_}),
                "SoftwareGpuTraversalBackend::Create(production)");
            RequireStatus(canonicalLinearBackend_.Create(
                L4::SoftwareGpuBackendCreateInfo{
                    device_, emptyLayout_, rayQueryBackend_.SceneAdapterLayout(),
                    softwareTraversal, maximumComputeGroupCountX_, true}),
                "SoftwareGpuTraversalBackend::Create(canonical linear)");
            rayQueryAdapter_ = std::make_unique<Hardware::RayQueryTraversalAdapter>(
                rayQueryBackend_, 2u, 0u);

            sampledResources_.Create(physicalDevice_, device_);
            CreateSignalImages();
            CreateMegakernelDescriptors();
            // L8 owns the shared set-4 layout consumed by every integrator.
            // Create it before the Megakernel pipeline layouts so changing the
            // reconstruction mode never changes the renderer route.
            EnsureWave3DebugRuntime();
            CreateMegakernelPipeline(
                0u, softwareMegakernel, softwareBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                1u, rayQueryMegakernel, rayQueryBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                2u, softwareMegakernelNoProfile, softwareBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                3u, rayQueryMegakernelNoProfile, rayQueryBackend_.TraversalLayout());
            for (std::size_t index = 0u;
                index < kWave2SignalImageCount; ++index)
            {
                CreateMegakernelPipeline(
                    kWave2PrimaryPipelineCount + index,
                    softwareSignalMegakernels[index],
                    softwareBackend_.TraversalLayout());
                CreateMegakernelPipeline(
                    kWave2PrimaryPipelineCount + kWave2SignalImageCount + index,
                    rayQuerySignalMegakernels[index],
                    rayQueryBackend_.TraversalLayout());
            }
            CreateMegakernelPipeline(
                kWave2RawSeedPipelineBase,
                rawSeed,
                softwareBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawSeedPipelineBase + 1u,
                rawSeed,
                rayQueryBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawTracePipelineBase,
                softwareRawTrace,
                softwareBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawTracePipelineBase + 1u,
                rayQueryRawTrace,
                rayQueryBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawShadePipelineBase,
                softwareRawShade,
                softwareBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawShadePipelineBase + 1u,
                rayQueryRawShade,
                rayQueryBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawResolvePipelineBase,
                rawResolve,
                softwareBackend_.TraversalLayout());
            CreateMegakernelPipeline(
                kWave2RawResolvePipelineBase + 1u,
                rawResolve,
                rayQueryBackend_.TraversalLayout());
            for (std::size_t index = 0u;
                index < kWave2IndirectSignalCount; ++index)
            {
                CreateMegakernelPipeline(
                    kWave2IndirectSeedPipelineBase + index,
                    softwareIndirectSeeds[index],
                    softwareBackend_.TraversalLayout());
                CreateMegakernelPipeline(
                    kWave2IndirectSeedPipelineBase
                        + kWave2IndirectSignalCount + index,
                    rayQueryIndirectSeeds[index],
                    rayQueryBackend_.TraversalLayout());
                CreateMegakernelPipeline(
                    kWave2IndirectShadePipelineBase + index,
                    softwareIndirectShades[index],
                    softwareBackend_.TraversalLayout());
                CreateMegakernelPipeline(
                    kWave2IndirectShadePipelineBase
                        + kWave2IndirectSignalCount + index,
                    rayQueryIndirectShades[index],
                    rayQueryBackend_.TraversalLayout());
                CreateMegakernelPipeline(
                    kWave2IndirectResolvePipelineBase + index,
                    indirectResolves[index],
                    softwareBackend_.TraversalLayout());
                CreateMegakernelPipeline(
                    kWave2IndirectResolvePipelineBase
                        + kWave2IndirectSignalCount + index,
                    indirectResolves[index],
                    rayQueryBackend_.TraversalLayout());
            }
        }

        void SetScene(
            const Scene::CanonicalScene& canonical,
            const Scene::ExperimentEnvironment* const environment,
            const bool environmentEnabled)
        {
            EnsureCreated();
            const Scene::CanonicalSceneValidation validation =
                Scene::ValidateCanonicalScene(canonical);
            if (!validation)
            {
                throw std::invalid_argument(
                    "Wave 2 canonical scene validation failed: " + validation.reason);
            }
            DestroyScene();
            canonicalScene_ = canonical;
            const Scene::CanonicalSceneView view =
                Scene::MakeCanonicalSceneView(canonicalScene_);
            Gpu::CanonicalTraversalSceneBuild traversalBuild =
                Gpu::BuildCanonicalTraversalScene(view);
            if (!traversalBuild)
            {
                throw std::runtime_error(
                    "Wave 2 traversal expansion failed: " + traversalBuild.error);
            }
            traversal_ = std::move(traversalBuild.scene);

            const auto sahStart = std::chrono::steady_clock::now();
            Rt::Cpu::Bvh<float> cpuBvh(
                traversal_.cpuTriangles, Rt::Cpu::BvhBuildMethod::BinnedSah);
            L4::FlatBuildResult flattened = L4::FlattenCanonicalL3BinnedSah(
                cpuBvh, traversal_.softwareBuildPrimitives);
            if (!flattened.Succeeded())
            {
                throw std::runtime_error("Wave 2 L3-to-L4 flatten failed: " + flattened.message);
            }
            flattenedSahBuildMilliseconds_ =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - sahStart).count();
            flattened_ = std::move(flattened.bvh);
            Require(flattened_.primitives.size() == traversal_.triangles.size(),
                "Wave 2 flattened primitive count differs from canonical traversal.");

            std::vector<Gpu::CanonicalTraversalTriangle> reorderedTriangles;
            reorderedTriangles.reserve(flattened_.primitives.size());
            for (const L4::SoftwarePrimitiveRecord& primitive : flattened_.primitives)
            {
                Require(primitive.identity.x < traversal_.triangles.size(),
                    "Wave 2 flattened primitive has an invalid traversal ID.");
                reorderedTriangles.push_back(traversal_.triangles[primitive.identity.x]);
            }

            const MegakernelHostScene megaScene =
                BuildMegakernelHostScene(
                    canonicalScene_, traversal_, environment, environmentEnabled);
            sceneCenterRadius_ = {
                megaScene.sceneCenterRadius.x,
                megaScene.sceneCenterRadius.y,
                megaScene.sceneCenterRadius.z,
                megaScene.sceneCenterRadius.w};
            materialCount_ = static_cast<std::uint32_t>(megaScene.materials.size());
            lightCount_ = static_cast<std::uint32_t>(megaScene.lights.size());
            fixtureTriangleCount_ =
                static_cast<std::uint32_t>(megaScene.fixtureTriangles.size());
            emitterMapCount_ = megaScene.emitterMapCount;
            uniformAliasCount_ =
                static_cast<std::uint32_t>(megaScene.uniformDistribution.alias.size());
            powerAliasCount_ =
                static_cast<std::uint32_t>(megaScene.powerDistribution.alias.size());
            environmentLightIndex_ = megaScene.environmentLightIndex;
            environmentWidth_ = megaScene.environmentWidth;
            environmentHeight_ = megaScene.environmentHeight;

            UploadCanonicalSceneBuffers();
            softwareNodeBuffer_ = UploadBuffer(*allocator_,
                std::span<const L4::SoftwareNodeRecord>{flattened_.nodes}, kStorageUsage);
            softwareTriangleBuffer_ = UploadBuffer(*allocator_,
                std::span<const Gpu::CanonicalTraversalTriangle>{reorderedTriangles},
                kStorageUsage);
            canonicalLinearTriangleBuffer_ = UploadBuffer(*allocator_,
                std::span<const Gpu::CanonicalTraversalTriangle>{
                    traversal_.triangles},
                kStorageUsage);

            materialBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::PbrMaterialGpu>{megaScene.materials}, kStorageUsage);
            lightBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::PbrLightGpu>{megaScene.lights}, kStorageUsage);
            uniformAliasBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::AliasEntryGpu>{megaScene.uniformDistribution.alias},
                kStorageUsage);
            powerAliasBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::AliasEntryGpu>{megaScene.powerDistribution.alias},
                kStorageUsage);
            environmentRowBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::AliasEntryGpu>{megaScene.environmentRows}, kStorageUsage);
            environmentColumnBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::AliasEntryGpu>{megaScene.environmentColumns}, kStorageUsage);
            uniformPmfBuffer_ = UploadBuffer(*allocator_,
                std::span<const float>{megaScene.uniformDistribution.selectionPmfByLight},
                kStorageUsage);
            powerPmfBuffer_ = UploadBuffer(*allocator_,
                std::span<const float>{megaScene.powerDistribution.selectionPmfByLight},
                kStorageUsage);
            emitterMapBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::EmitterMapEntryGpu>{megaScene.emitterMap}, kStorageUsage);
            fixtureTriangleBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::FixtureTriangleGpu>{megaScene.fixtureTriangles},
                kStorageUsage);
            fixtureSphereBuffer_ = UploadBuffer(*allocator_,
                std::span<const Mega::FixtureSphereGpu>{megaScene.fixtureSpheres},
                kStorageUsage);

            if (environmentEnabled)
            {
                CreateEnvironmentImage(*environment);
            }

            const std::array<Abi1::GpuRayQueueRecordV1, 1u> dummyRays{};
            const std::array<Abi1::GpuHitQueueRecordV1, 1u> dummyHits{};
            dummyRayBuffer_ = UploadBuffer(*allocator_, std::span{dummyRays}, kStorageUsage);
            dummyHitBuffer_ = UploadBuffer(*allocator_, std::span{dummyHits}, kStorageUsage);
            for (Hardware::DeviceBuffer& counterBuffer : softwareCounterBuffers_)
            {
                counterBuffer = CreateMappedBuffer(*allocator_,
                    sizeof(L4::GpuTraversalCounterReadback),
                    kStorageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            }

            BuildHardwareAccelerationStructures();
            CreateSceneDescriptorSets();
            SubmitSceneBuild();
            UpdateAllFrameDescriptors();

            sceneReady_ = true;
            ++resourceGeneration_;
            sceneGeneration_ = canonicalScene_.generation;
            SynchronizeWave3SceneTransforms();
        }

        void SetOutput(const VkImageView outputImageView, const VkExtent2D outputExtent)
        {
            EnsureCreated();
            if (outputImageView == VK_NULL_HANDLE
                || outputExtent.width == 0u || outputExtent.height == 0u)
            {
                throw std::invalid_argument("Wave2Runtime output is invalid.");
            }
            // The Wave 3 attachment bakes the output view and extent into its
            // set-3/set-4 descriptors. Tear it down before any externally-owned
            // image view or split-signal image is replaced, then recreate it
            // lazily on the next Wavefront frame.
            wave3DebugRuntime_.reset();
            const bool extentChanged = outputExtent.width != outputExtent_.width
                || outputExtent.height != outputExtent_.height;
            outputImageView_ = outputImageView;
            outputExtent_ = outputExtent;
            if (extentChanged)
            {
                CreateSignalImages();
                CreateIndirectSeedBuffers();
            }
            if (sceneReady_)
            {
                UpdateAllFrameDescriptors();
            }
            ++resourceGeneration_;
        }

        void UpdateRigidTransforms(const Scene::CanonicalScene& canonical)
        {
            EnsureReady();
            if (canonical.stableId != canonicalScene_.stableId
                || canonical.instances.size() != canonicalScene_.instances.size()
                || canonical.vertices.size() != canonicalScene_.vertices.size()
                || canonical.indices.size() != canonicalScene_.indices.size()
                || canonical.geometries.size() != canonicalScene_.geometries.size()
                || canonical.materials.size() != canonicalScene_.materials.size()
                || canonical.lights.size() != canonicalScene_.lights.size())
            {
                throw std::invalid_argument(
                    "Rigid transform update cannot change canonical scene topology or payload counts.");
            }
            for (std::size_t index = 0u; index < canonical.instances.size(); ++index)
            {
                if (std::memcmp(&canonical.instances[index].metadata,
                        &canonicalScene_.instances[index].metadata,
                        sizeof(canonical.instances[index].metadata)) != 0)
                {
                    throw std::invalid_argument(
                        "Rigid transform update cannot change instance identity or geometry ownership.");
                }
            }

            // Scene buffers and TLAS are shared by the two frame slots. This
            // explicit debug-scene synchronization prevents host writes from
            // racing an earlier in-flight frame while preserving history IDs.
            Check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(rigid transform update)");

            Gpu::CanonicalTraversalSceneBuild traversalBuild =
                Gpu::BuildCanonicalTraversalScene(Scene::MakeCanonicalSceneView(canonical));
            if (!traversalBuild)
            {
                throw std::runtime_error(
                    "Rigid transform traversal expansion failed: " + traversalBuild.error);
            }
            Gpu::CanonicalTraversalScene nextTraversal =
                std::move(traversalBuild.scene);
            Rt::Cpu::Bvh<float> cpuBvh(
                nextTraversal.cpuTriangles, Rt::Cpu::BvhBuildMethod::BinnedSah);
            L4::FlatBuildResult nextFlat = L4::FlattenCanonicalL3BinnedSah(
                cpuBvh, nextTraversal.softwareBuildPrimitives);
            if (!nextFlat.Succeeded())
            {
                throw std::runtime_error(
                    "Rigid transform flattened-SAH rebuild failed: " + nextFlat.message);
            }
            std::vector<Gpu::CanonicalTraversalTriangle> reordered;
            reordered.reserve(nextFlat.bvh.primitives.size());
            for (const L4::SoftwarePrimitiveRecord& primitive : nextFlat.bvh.primitives)
            {
                reordered.push_back(nextTraversal.triangles.at(primitive.identity.x));
            }
            if (nextFlat.bvh.nodes.size() * sizeof(nextFlat.bvh.nodes.front())
                    != softwareNodeBuffer_.Size()
                || reordered.size() * sizeof(reordered.front())
                    != softwareTriangleBuffer_.Size()
                || nextTraversal.triangles.size() * sizeof(nextTraversal.triangles.front())
                    != canonicalLinearTriangleBuffer_.Size())
            {
                throw std::runtime_error(
                    "Rigid transform update changed traversal buffer sizes; a full scene rebuild is required.");
            }

            canonicalScene_.instances = canonical.instances;
            RequireStatus(allocator_->Upload(instanceBuffer_,
                std::as_bytes(std::span<const Abi0::GpuInstanceV0>{
                    canonicalScene_.instances})),
                "DeviceBufferAllocator::Upload(dynamic instances)");
            RequireStatus(allocator_->Upload(softwareNodeBuffer_,
                std::as_bytes(std::span<const L4::SoftwareNodeRecord>{
                    nextFlat.bvh.nodes})),
                "DeviceBufferAllocator::Upload(dynamic SAH nodes)");
            RequireStatus(allocator_->Upload(softwareTriangleBuffer_,
                std::as_bytes(std::span<const Gpu::CanonicalTraversalTriangle>{reordered})),
                "DeviceBufferAllocator::Upload(dynamic SAH triangles)");
            RequireStatus(allocator_->Upload(canonicalLinearTriangleBuffer_,
                std::as_bytes(std::span<const Gpu::CanonicalTraversalTriangle>{
                    nextTraversal.triangles})),
                "DeviceBufferAllocator::Upload(dynamic linear triangles)");

            std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
            vkInstances.reserve(canonicalScene_.instances.size());
            for (std::size_t index = 0u; index < canonicalScene_.instances.size(); ++index)
            {
                const Abi0::GpuInstanceV0& source = canonicalScene_.instances[index];
                VkAccelerationStructureInstanceKHR instance{};
                instance.transform = ToVkTransform(source.objectToWorld);
                instance.instanceCustomIndex = static_cast<std::uint32_t>(index);
                instance.mask = (source.metadata.w & Abi0::InstanceFlagVisible) != 0u
                    ? 0xffu : 0u;
                instance.flags =
                    VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                instance.accelerationStructureReference = blases_[index].Address();
                vkInstances.push_back(instance);
            }
            RequireStatus(allocator_->Upload(asInstanceBuffer_,
                std::as_bytes(std::span<const VkAccelerationStructureInstanceKHR>{
                    vkInstances})),
                "DeviceBufferAllocator::Upload(dynamic TLAS instances)");
            SubmitAndWait([&](const VkCommandBuffer commandBuffer)
            {
                RequireStatus(asBuilder_->RecordTopLevelBuild(
                    commandBuffer, asInstanceBuffer_.Address(),
                    static_cast<std::uint32_t>(vkInstances.size()),
                    asScratchBuffer_, tlas_,
                    VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR),
                    "AccelerationStructureBuilder::RecordTopLevelBuild(dynamic update)");
                asBuilder_->RecordBuildToTraceBarrier(commandBuffer);
            });
            tlas_.MarkReady();
            traversal_ = std::move(nextTraversal);
            flattened_ = std::move(nextFlat.bvh);
            SynchronizeWave3SceneTransforms();
        }

        void InvalidateWave3Histories() noexcept
        {
            if (wave3DebugRuntime_ != nullptr)
            {
                wave3DebugRuntime_->InvalidateHistories();
            }
        }

        void InvalidateProgressiveFilm() noexcept
        {
            if (wave3DebugRuntime_ != nullptr)
            {
                wave3DebugRuntime_->InvalidateProgressiveFilm();
            }
        }

        [[nodiscard]] Wave3DebugFrameBindings MakeWave3Bindings(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config) noexcept
        {
            const bool rayQuery =
                config.backend == TraversalBackend::VulkanRayQuery;
            const bool canonicalLinear =
                config.backend == TraversalBackend::CanonicalLinearGpu;
            return {
                frameDescriptorSets_[frameSlot],
                canonicalSceneSet_,
                canonicalLinear
                    ? canonicalLinearTraversalSets_[frameSlot]
                    : softwareTraversalSets_[frameSlot],
                rayQueryTraversalSet_,
                rayQuery
                    ? rayQueryReferenceTraversalSets_[frameSlot]
                    : canonicalLinear
                        ? canonicalLinearReferenceTraversalSets_[frameSlot]
                        : softwareReferenceTraversalSets_[frameSlot],
                rayQuery
                    ? rayQueryWinnerTraversalSets_[frameSlot]
                    : canonicalLinear
                        ? canonicalLinearWinnerTraversalSets_[frameSlot]
                        : softwareWinnerTraversalSets_[frameSlot],
                rayQuery
                    ? static_cast<Rt::Gpu::IGpuTraversalBackend*>(
                        rayQueryAdapter_.get())
                    : canonicalLinear
                        ? static_cast<Rt::Gpu::IGpuTraversalBackend*>(
                            &canonicalLinearBackend_)
                    : static_cast<Rt::Gpu::IGpuTraversalBackend*>(
                        &softwareBackend_),
                Descriptor(lightBuffer_),
                lightCount_,
                traversal_.fingerprint,
                static_cast<std::uint32_t>(traversal_.generation),
                static_cast<std::uint32_t>(resourceGeneration_),
                static_cast<std::uint32_t>(traversal_.generation)};
        }

        void UpdateReSTIRTraversalDescriptors(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config)
        {
            if (config.directLightingEstimator
                != DirectLightingEstimator::RestirDirectIllumination)
            {
                return;
            }
            const Wave3DebugReSTIRTraversalViews views =
                wave3DebugRuntime_->ReSTIRTraversalViews(frameSlot);
            Require(views.IsReady(),
                "ReSTIR did not publish its TraceAny resource views.");
            if (config.backend == TraversalBackend::VulkanRayQuery)
            {
                RequireStatus(rayQueryBackend_.UpdateTraversalDescriptors(
                    rayQueryReferenceTraversalSets_[frameSlot],
                    Hardware::RayQueryTraversalBindings{
                        tlas_.Handle(), views.shadowRays,
                        views.referenceHits,
                        sampledResources_.AlphaImage(),
                        sampledResources_.Sampler()}),
                    "RayQueryBackend::UpdateTraversalDescriptors(ReSTIR reference)");
                RequireStatus(rayQueryBackend_.UpdateTraversalDescriptors(
                    rayQueryWinnerTraversalSets_[frameSlot],
                    Hardware::RayQueryTraversalBindings{
                        tlas_.Handle(), views.shadowRays,
                        views.winnerHits,
                        sampledResources_.AlphaImage(),
                        sampledResources_.Sampler()}),
                    "RayQueryBackend::UpdateTraversalDescriptors(ReSTIR winner)");
                return;
            }

            const bool canonicalLinear =
                config.backend == TraversalBackend::CanonicalLinearGpu;
            L4::SoftwareGpuTraversalBackend& backend = canonicalLinear
                ? canonicalLinearBackend_ : softwareBackend_;
            const VkDescriptorBufferInfo nodes = Descriptor(canonicalLinear
                ? canonicalLinearTriangleBuffer_ : softwareNodeBuffer_);
            const VkDescriptorBufferInfo triangles = Descriptor(canonicalLinear
                ? canonicalLinearTriangleBuffer_ : softwareTriangleBuffer_);
            const std::uint32_t nodeCount = canonicalLinear
                ? 0u : static_cast<std::uint32_t>(flattened_.nodes.size());
            const std::uint32_t triangleCount = canonicalLinear
                ? static_cast<std::uint32_t>(traversal_.triangles.size())
                : static_cast<std::uint32_t>(flattened_.primitives.size());
            const std::uint32_t stackCapacity = canonicalLinear
                ? 0u
                : std::min<std::uint32_t>(
                    64u,
                    static_cast<std::uint32_t>(flattened_.maximumDepth + 1u));
            const auto updateSoftware = [this, frameSlot, &views, &backend,
                                            nodes, triangles, nodeCount,
                                            triangleCount, stackCapacity](
                const VkDescriptorSet set,
                const VkDescriptorBufferInfo hits,
                const char* operation)
            {
                RequireStatus(backend.UpdateTraversalSet(
                    set,
                    L4::SoftwareGpuTraversalBindings{
                        nodes, views.shadowRays, hits, triangles,
                        Descriptor(softwareCounterBuffers_[frameSlot]),
                        sampledResources_.AlphaImage(),
                        sampledResources_.Sampler(),
                        nodeCount, triangleCount, stackCapacity,
                        2u, 0u}),
                    operation);
            };
            updateSoftware(
                canonicalLinear
                    ? canonicalLinearReferenceTraversalSets_[frameSlot]
                    : softwareReferenceTraversalSets_[frameSlot],
                views.referenceHits,
                "SoftwareGpuTraversalBackend::UpdateTraversalSet(ReSTIR reference)");
            updateSoftware(
                canonicalLinear
                    ? canonicalLinearWinnerTraversalSets_[frameSlot]
                    : softwareWinnerTraversalSets_[frameSlot],
                views.winnerHits,
                "SoftwareGpuTraversalBackend::UpdateTraversalSet(ReSTIR winner)");
        }

        void UpdateFrame(
            const std::uint32_t frameSlot,
            const RuntimeConfig& config,
            const Wave2RuntimeCamera& camera,
            const std::uint32_t sampleIndex,
            const bool enableProfilerCounters,
            const std::optional<std::size_t> signalAovIndex)
        {
            EnsureReady();
            ValidateFrameSlot(frameSlot);
            if (signalAovIndex.has_value()
                && *signalAovIndex >= kWave2SignalImageCount)
            {
                throw std::out_of_range(
                    "Wave 2 signal AOV index is outside the production signal set.");
            }
            const Mega::TraversalBackend backend = config.backend
                    == TraversalBackend::CanonicalLinearGpu
                ? Mega::TraversalBackend::CanonicalLinear
                : config.backend == TraversalBackend::GpuFlattenedSahBvh
                    ? Mega::TraversalBackend::FlattenedSah
                    : config.backend == TraversalBackend::VulkanRayQuery
                        ? Mega::TraversalBackend::HardwareRayQuery
                        : throw std::invalid_argument(
                            "Configured renderer requires canonical-linear, flattened-SAH, or Ray Query traversal.");
            const Mega::LightSelection lightSelection =
                ToMegakernelLightSelection(config.lightSelection);
            const Mega::EnvironmentSampler environmentSampler =
                ToMegakernelEnvironmentSampler(config.environmentSampler);
            const Mega::DirectLightingEstimator estimator =
                ToMegakernelEstimator(config.directLightingEstimator);

            constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
            Mega::MegakernelFrameConstantsGpu frame{};
            const float tangent = std::tan(
                camera.verticalFovDegrees * 0.5f * degreesToRadians);
            frame.cameraPositionTanHalfFov = {
                camera.position.x, camera.position.y, camera.position.z, tangent};
            frame.cameraForwardAspect = {
                camera.forward.x, camera.forward.y, camera.forward.z,
                static_cast<float>(outputExtent_.width)
                    / static_cast<float>(outputExtent_.height)};
            frame.cameraRightLensRadius = {
                camera.right.x, camera.right.y, camera.right.z, 0.0f};
            frame.cameraUpExposure = {
                camera.up.x, camera.up.y, camera.up.z, config.render.exposure};
            frame.image = {
                outputExtent_.width, outputExtent_.height, sampleIndex,
                config.render.maximumBounce};
            frame.trace = {
                fixtureTriangleCount_, 0u, materialCount_, lightCount_};
            frame.sampling = {
                static_cast<std::uint32_t>(config.render.baseSeed & 0xffffffffull),
                static_cast<std::uint32_t>(config.render.baseSeed >> 32u),
                static_cast<std::uint32_t>(lightSelection),
                static_cast<std::uint32_t>(estimator)};
            frame.environment = {
                environmentLightIndex_, environmentWidth_, environmentHeight_,
                static_cast<std::uint32_t>(environmentSampler)};
            frame.distribution = {
                lightSelection == Mega::LightSelection::PowerWeighted
                    ? powerAliasCount_ : uniformAliasCount_,
                environmentHeight_, environmentWidth_ * environmentHeight_,
                emitterMapCount_};
            frame.russianRoulette = {3.0f, 0.05f, 0.95f, 1.0e-4f};
            frame.sceneCenterRadius = {
                sceneCenterRadius_[0u],
                sceneCenterRadius_[1u],
                sceneCenterRadius_[2u],
                sceneCenterRadius_[3u]};
            frame.environmentToWorld0 = {1.0f, 0.0f, 0.0f, 0.0f};
            frame.environmentToWorld1 = {0.0f, 1.0f, 0.0f, 0.0f};
            frame.environmentToWorld2 = {0.0f, 0.0f, 1.0f, 0.0f};
            frame.worldToEnvironment0 = frame.environmentToWorld0;
            frame.worldToEnvironment1 = frame.environmentToWorld1;
            frame.worldToEnvironment2 = frame.environmentToWorld2;
            frame.traversal = {
                static_cast<std::uint32_t>(backend),
                backend == Mega::TraversalBackend::FlattenedSah
                    ? static_cast<std::uint32_t>(flattened_.nodes.size()) : 0u,
                backend == Mega::TraversalBackend::FlattenedSah
                        || backend == Mega::TraversalBackend::CanonicalLinear
                    ? static_cast<std::uint32_t>(flattened_.primitives.size()) : 0u,
                2u};
            frame.output = {
                0x57a20001u,
                0u,
                static_cast<std::uint32_t>(config.transportModel),
                static_cast<std::uint32_t>(config.shadowMethod)};
            Require(Mega::ValidateMegakernelFrameConstants(frame),
                "Wave 2 host generated invalid Megakernel frame constants.");
            std::memcpy(frameBuffers_[frameSlot].MappedData(), &frame, sizeof(frame));
            // Only the full monolithic shader is forced onto the driver-safe
            // no-profile variant. The staged PBR/Whitted route keeps its
            // requested counters; Wavefront reports its own telemetry path.
            // Global per-ray atomics in the complete-path kernel can otherwise
            // cross the Windows GPU timeout on the supported NVIDIA setup.
            preparedProfilerCounters_[frameSlot] = enableProfilerCounters
                && config.executionArchitecture != ExecutionArchitecture::Megakernel;
            preparedSignalAovIndices_[frameSlot] = signalAovIndex;
            preparedMaximumBounces_[frameSlot] = config.render.maximumBounce;
            preparedConfigs_[frameSlot] = config;
            UpdateFrameDescriptors(frameSlot, lightSelection);
            if (config.executionArchitecture == ExecutionArchitecture::Wavefront)
            {
                EnsureWave3DebugRuntime();
                const Wave3DebugFrameBindings wave3Bindings =
                    MakeWave3Bindings(frameSlot, config);
                wave3DebugRuntime_->UpdateFrame(
                    frameSlot,
                    config,
                    Wave3DebugCamera{
                        camera.position,
                        camera.forward,
                        camera.right,
                        camera.up,
                        camera.verticalFovDegrees},
                    sampleIndex,
                    wave3Bindings);
            }
            else
            {
                EnsureWave3DebugRuntime();
                wave3DebugRuntime_->UpdatePostIntegratorFrame(
                    frameSlot,
                    config,
                    Wave3DebugCamera{
                        camera.position,
                        camera.forward,
                        camera.right,
                        camera.up,
                        camera.verticalFovDegrees},
                    sampleIndex,
                    MakeWave3Bindings(frameSlot, config));
            }
            UpdateReSTIRTraversalDescriptors(frameSlot, config);
        }

        void RecordFrame(
            const VkCommandBuffer commandBuffer,
            const std::uint32_t frameSlot,
            const TraversalBackend backend)
        {
            EnsureReady();
            ValidateFrameSlot(frameSlot);
            const std::size_t backendIndex = backend
                    == TraversalBackend::CanonicalLinearGpu
                    || backend == TraversalBackend::GpuFlattenedSahBvh
                ? 0u
                : backend == TraversalBackend::VulkanRayQuery
                    ? 1u
                    : throw std::invalid_argument(
                        "Configured renderer record requires canonical-linear, flattened-SAH, or Ray Query.");
            const VkDescriptorSet traversalSet =
                backend == TraversalBackend::CanonicalLinearGpu
                    ? canonicalLinearTraversalSets_[frameSlot]
                    : backend == TraversalBackend::GpuFlattenedSahBvh
                        ? softwareTraversalSets_[frameSlot]
                        : rayQueryTraversalSet_;
            const bool enableCounters =
                preparedProfilerCounters_[frameSlot];
            const std::optional<std::size_t> signalAovIndex =
                preparedSignalAovIndices_[frameSlot];
            const std::uint32_t firstQuery = frameSlot * 2u;
            vkCmdResetQueryPool(commandBuffer, traceQueryPool_, firstQuery, 2u);

            const RuntimeConfig& preparedConfig = preparedConfigs_[frameSlot];
            const bool useMonolithicMegakernel =
                preparedConfig.executionArchitecture
                    == ExecutionArchitecture::Megakernel;
            if (preparedConfig.executionArchitecture
                == ExecutionArchitecture::Wavefront)
            {
                if (preparedConfig.backend != backend
                    || wave3DebugRuntime_ == nullptr
                    || !wave3DebugRuntime_->IsReady())
                {
                    throw std::logic_error(
                        "Wave 3 frame was not prepared for the selected backend.");
                }

                std::array<VkImageMemoryBarrier2, kWave2SignalImageCount>
                    signalAcquireBarriers{};
                for (std::size_t index = 0u;
                    index < signalAcquireBarriers.size(); ++index)
                {
                    VkImageMemoryBarrier2& barrier = signalAcquireBarriers[index];
                    barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                        | VK_PIPELINE_STAGE_2_TRANSFER_BIT
                        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                        | VK_ACCESS_2_TRANSFER_READ_BIT
                        | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                        | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = signalImages_[index];
                    barrier.subresourceRange.aspectMask =
                        VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.levelCount = 1u;
                    barrier.subresourceRange.layerCount = 1u;
                }
                VkDependencyInfo signalAcquire{
                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                signalAcquire.imageMemoryBarrierCount =
                    static_cast<std::uint32_t>(signalAcquireBarriers.size());
                signalAcquire.pImageMemoryBarriers =
                    signalAcquireBarriers.data();
                dispatch_.cmdPipelineBarrier2(commandBuffer, &signalAcquire);

                vkCmdWriteTimestamp2(
                    commandBuffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    traceQueryPool_,
                    firstQuery);
                wave3DebugRuntime_->RecordFrame(
                    commandBuffer,
                    frameSlot,
                    preparedConfig,
                    MakeWave3Bindings(frameSlot, preparedConfig));
                vkCmdWriteTimestamp2(
                    commandBuffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    traceQueryPool_,
                    firstQuery + 1u);

                PendingFrame& pending = pendingFrames_[frameSlot];
                pending.pending = true;
                pending.backend = backend;
                pending.frameGeneration = ++frameGeneration_;
                pending.megakernelCountersAvailable = false;
                pending.softwareTraversalCountersAvailable = false;
                pending.signalAovIndex = signalAovIndex;
                return;
            }

            if (enableCounters)
            {
                vkCmdFillBuffer(commandBuffer, counterBuffers_[frameSlot].Handle(),
                    0u, VK_WHOLE_SIZE, 0u);
                if (backendIndex == 0u)
                {
                    vkCmdFillBuffer(
                        commandBuffer,
                        softwareCounterBuffers_[frameSlot].Handle(),
                        0u,
                        VK_WHOLE_SIZE,
                        0u);
                }
            }

            std::array<VkBufferMemoryBarrier2, 2u> clearBarriers{};
            const std::uint32_t clearBarrierCount = !enableCounters
                ? 0u : backendIndex == 0u ? 2u : 1u;
            const std::array<VkBuffer, 2u> clearedBuffers{
                counterBuffers_[frameSlot].Handle(),
                softwareCounterBuffers_[frameSlot].Handle()};
            for (std::uint32_t index = 0u; index < clearBarrierCount; ++index)
            {
                VkBufferMemoryBarrier2& clearBarrier = clearBarriers[index];
                clearBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                clearBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                clearBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                clearBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                clearBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                    | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                clearBarrier.buffer = clearedBuffers[index];
                clearBarrier.offset = 0u;
                clearBarrier.size = VK_WHOLE_SIZE;
            }
            if (clearBarrierCount != 0u)
            {
                VkDependencyInfo clearDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                clearDependency.bufferMemoryBarrierCount = clearBarrierCount;
                clearDependency.pBufferMemoryBarriers = clearBarriers.data();
                dispatch_.cmdPipelineBarrier2(commandBuffer, &clearDependency);
            }

            std::array<VkImageMemoryBarrier2, kWave2SignalImageCount>
                signalBarriers{};
            for (std::size_t index = 0u;
                index < signalBarriers.size(); ++index)
            {
                VkImageMemoryBarrier2& barrier = signalBarriers[index];
                barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                // The previous dispatch leaves auxiliary signals available for
                // a provider-owned transfer/readback consumer. Reacquire them
                // for the next Megakernel write without changing layout.
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                    | VK_PIPELINE_STAGE_2_TRANSFER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                    | VK_ACCESS_2_TRANSFER_READ_BIT
                    | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                    | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = signalImages_[index];
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.layerCount = 1u;
            }
            VkDependencyInfo signalDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            signalDependency.imageMemoryBarrierCount =
                static_cast<std::uint32_t>(signalBarriers.size());
            signalDependency.pImageMemoryBarriers = signalBarriers.data();
            dispatch_.cmdPipelineBarrier2(commandBuffer, &signalDependency);

            vkCmdWriteTimestamp2(commandBuffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, traceQueryPool_, firstQuery);
            const std::array descriptorSets{
                frameDescriptorSets_[frameSlot], canonicalSceneSet_, traversalSet};
            if (wave3DebugRuntime_ == nullptr
                || !wave3DebugRuntime_->IsReady())
            {
                throw std::logic_error(
                    "The shared reconstruction descriptor owner is not ready.");
            }
            const VkDescriptorSet reconstructionSet =
                wave3DebugRuntime_->ReconstructionSet(frameSlot);
            if (reconstructionSet == VK_NULL_HANDLE)
            {
                throw std::logic_error(
                    "The shared reconstruction descriptor set is missing.");
            }
            const std::size_t monolithicPipelineIndex = enableCounters
                ? backendIndex
                : 2u + backendIndex;
            const std::size_t reconstructionProducerPipelineIndex =
                useMonolithicMegakernel
                    ? monolithicPipelineIndex
                    : kWave2RawSeedPipelineBase + backendIndex;
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                megakernelPipelineLayouts_[reconstructionProducerPipelineIndex],
                4u,
                1u,
                &reconstructionSet,
                0u,
                nullptr);
            const auto recordDispatch = [&descriptorSets, commandBuffer, this](
                const std::size_t index,
                const std::optional<std::uint32_t> depth)
            {
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    megakernelPipelines_[index]);
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    megakernelPipelineLayouts_[index],
                    0u,
                    static_cast<std::uint32_t>(descriptorSets.size()),
                    descriptorSets.data(),
                    0u,
                    nullptr);
                if (depth.has_value())
                {
                    const std::array<std::uint32_t, 4u> constants{
                        *depth, 0u, 0u, 0u};
                    vkCmdPushConstants(
                        commandBuffer,
                        megakernelPipelineLayouts_[index],
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0u,
                        static_cast<std::uint32_t>(sizeof(constants)),
                        constants.data());
                }
                const std::uint32_t groupSize =
                    index < kWave2PrimaryPipelineCount ? 1u : 8u;
                vkCmdDispatch(commandBuffer,
                    (outputExtent_.width + groupSize - 1u) / groupSize,
                    (outputExtent_.height + groupSize - 1u) / groupSize,
                    1u);
            };
            VkMemoryBarrier2 stagedBarrier{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            stagedBarrier.srcStageMask =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            stagedBarrier.srcAccessMask =
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            stagedBarrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            stagedBarrier.dstAccessMask =
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            VkDependencyInfo stagedDependency{
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            stagedDependency.memoryBarrierCount = 1u;
            stagedDependency.pMemoryBarriers = &stagedBarrier;

            if (useMonolithicMegakernel)
            {
                // This is the defining P1 route: one dispatch owns camera-ray
                // generation and the complete bounce loop. The split Raw
                // stages below remain exclusive to PBR and Whitted.
                recordDispatch(monolithicPipelineIndex, std::nullopt);
            }
            else
            {
                recordDispatch(
                    kWave2RawSeedPipelineBase + backendIndex,
                    std::nullopt);
                for (std::uint32_t depth = 0u;
                    depth < preparedMaximumBounces_[frameSlot]; ++depth)
                {
                    dispatch_.cmdPipelineBarrier2(
                        commandBuffer, &stagedDependency);
                    recordDispatch(
                        kWave2RawTracePipelineBase + backendIndex,
                        depth);
                    dispatch_.cmdPipelineBarrier2(
                        commandBuffer, &stagedDependency);
                    recordDispatch(
                        kWave2RawShadePipelineBase + backendIndex,
                        depth);
                }
                dispatch_.cmdPipelineBarrier2(
                    commandBuffer, &stagedDependency);
                recordDispatch(
                    kWave2RawResolvePipelineBase + backendIndex,
                    std::nullopt);
            }
            dispatch_.cmdPipelineBarrier2(
                commandBuffer, &stagedDependency);
            wave3DebugRuntime_->RecordPostIntegratorFrame(
                commandBuffer,
                frameSlot,
                preparedConfig,
                MakeWave3Bindings(frameSlot, preparedConfig));
            dispatch_.cmdPipelineBarrier2(
                commandBuffer, &stagedDependency);
            if (!useMonolithicMegakernel && signalAovIndex.has_value())
            {
                const std::size_t signalPipelineBase = kWave2PrimaryPipelineCount
                    + backendIndex * kWave2SignalImageCount;
                const std::size_t signalPipelineIndex =
                    signalPipelineBase + *signalAovIndex;
                if (*signalAovIndex < 3u)
                {
                    vkCmdBindPipeline(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        megakernelPipelines_[signalPipelineIndex]);
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        megakernelPipelineLayouts_[signalPipelineIndex],
                        0u,
                        static_cast<std::uint32_t>(descriptorSets.size()),
                        descriptorSets.data(),
                        0u,
                        nullptr);
                    vkCmdDispatch(commandBuffer,
                        (outputExtent_.width + 7u) / 8u,
                        (outputExtent_.height + 7u) / 8u,
                        1u);
                }
                else
                {
                    VkBufferMemoryBarrier2 stateBarrier{
                        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                    stateBarrier.srcStageMask =
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    stateBarrier.srcAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                        | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                    stateBarrier.dstStageMask =
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    stateBarrier.dstAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                    stateBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    stateBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    stateBarrier.buffer = indirectSeedBuffers_[frameSlot].Handle();
                    stateBarrier.offset = 0u;
                    stateBarrier.size = VK_WHOLE_SIZE;
                    VkDependencyInfo stateDependency{
                        VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    stateDependency.bufferMemoryBarrierCount = 1u;
                    stateDependency.pBufferMemoryBarriers = &stateBarrier;
                    dispatch_.cmdPipelineBarrier2(commandBuffer, &stateDependency);

                    const std::size_t indirectIndex = *signalAovIndex - 3u;
                    const std::size_t seedPipelineIndex =
                        kWave2IndirectSeedPipelineBase
                        + backendIndex * kWave2IndirectSignalCount
                        + indirectIndex;
                    vkCmdBindPipeline(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        megakernelPipelines_[seedPipelineIndex]);
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        megakernelPipelineLayouts_[seedPipelineIndex],
                        0u,
                        static_cast<std::uint32_t>(descriptorSets.size()),
                        descriptorSets.data(),
                        0u,
                        nullptr);
                    vkCmdDispatch(commandBuffer,
                        (outputExtent_.width + 7u) / 8u,
                        (outputExtent_.height + 7u) / 8u,
                        1u);

                    const std::size_t shadePipelineIndex =
                        kWave2IndirectShadePipelineBase
                        + backendIndex * kWave2IndirectSignalCount
                        + indirectIndex;
                    VkMemoryBarrier2 scratchBarrier{
                        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
                    scratchBarrier.srcStageMask =
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    scratchBarrier.srcAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                    scratchBarrier.dstStageMask =
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    scratchBarrier.dstAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                    VkDependencyInfo scratchDependency{
                        VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    scratchDependency.memoryBarrierCount = 1u;
                    scratchDependency.pMemoryBarriers = &scratchBarrier;
                    for (std::uint32_t depth = 1u;
                        depth < preparedMaximumBounces_[frameSlot]; ++depth)
                    {
                        dispatch_.cmdPipelineBarrier2(
                            commandBuffer, &scratchDependency);

                        vkCmdBindPipeline(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            megakernelPipelines_[signalPipelineIndex]);
                        vkCmdBindDescriptorSets(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            megakernelPipelineLayouts_[signalPipelineIndex],
                            0u,
                            static_cast<std::uint32_t>(descriptorSets.size()),
                            descriptorSets.data(),
                            0u,
                            nullptr);
                        const std::array<std::uint32_t, 4u> stepConstants{
                            depth, 0u, 0u, 0u};
                        vkCmdPushConstants(
                            commandBuffer,
                            megakernelPipelineLayouts_[signalPipelineIndex],
                            VK_SHADER_STAGE_COMPUTE_BIT,
                            0u,
                            static_cast<std::uint32_t>(sizeof(stepConstants)),
                            stepConstants.data());
                        vkCmdDispatch(commandBuffer,
                            (outputExtent_.width + 7u) / 8u,
                            (outputExtent_.height + 7u) / 8u,
                            1u);

                        dispatch_.cmdPipelineBarrier2(
                            commandBuffer, &scratchDependency);

                        vkCmdBindPipeline(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            megakernelPipelines_[shadePipelineIndex]);
                        vkCmdBindDescriptorSets(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            megakernelPipelineLayouts_[shadePipelineIndex],
                            0u,
                            static_cast<std::uint32_t>(descriptorSets.size()),
                            descriptorSets.data(),
                            0u,
                            nullptr);
                        vkCmdPushConstants(
                            commandBuffer,
                            megakernelPipelineLayouts_[shadePipelineIndex],
                            VK_SHADER_STAGE_COMPUTE_BIT,
                            0u,
                            static_cast<std::uint32_t>(sizeof(stepConstants)),
                            stepConstants.data());
                        vkCmdDispatch(commandBuffer,
                            (outputExtent_.width + 7u) / 8u,
                            (outputExtent_.height + 7u) / 8u,
                            1u);
                    }

                    stateBarrier.srcAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                        | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                    stateBarrier.dstAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                    dispatch_.cmdPipelineBarrier2(commandBuffer, &stateDependency);

                    const std::size_t resolvePipelineIndex =
                        kWave2IndirectResolvePipelineBase
                        + backendIndex * kWave2IndirectSignalCount
                        + indirectIndex;
                    vkCmdBindPipeline(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        megakernelPipelines_[resolvePipelineIndex]);
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        megakernelPipelineLayouts_[resolvePipelineIndex],
                        0u,
                        static_cast<std::uint32_t>(descriptorSets.size()),
                        descriptorSets.data(),
                        0u,
                        nullptr);
                    vkCmdDispatch(commandBuffer,
                        (outputExtent_.width + 7u) / 8u,
                        (outputExtent_.height + 7u) / 8u,
                        1u);
                }
            }
            vkCmdWriteTimestamp2(commandBuffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, traceQueryPool_, firstQuery + 1u);

            std::array<VkImageMemoryBarrier2, kWave2SignalImageCount>
                signalConsumerBarriers{};
            for (std::size_t index = 0u;
                index < signalConsumerBarriers.size(); ++index)
            {
                VkImageMemoryBarrier2& barrier = signalConsumerBarriers[index];
                barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT
                    | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = signalImages_[index];
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.layerCount = 1u;
            }
            VkDependencyInfo signalConsumerDependency{
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            signalConsumerDependency.imageMemoryBarrierCount =
                static_cast<std::uint32_t>(signalConsumerBarriers.size());
            signalConsumerDependency.pImageMemoryBarriers =
                signalConsumerBarriers.data();
            dispatch_.cmdPipelineBarrier2(commandBuffer, &signalConsumerDependency);

            if (enableCounters)
            {
                VkBufferMemoryBarrier2 hostBarrier{
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                hostBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
                hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
                hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                hostBarrier.buffer = counterBuffers_[frameSlot].Handle();
                hostBarrier.offset = 0u;
                hostBarrier.size = VK_WHOLE_SIZE;
                VkDependencyInfo hostDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                hostDependency.bufferMemoryBarrierCount = 1u;
                hostDependency.pBufferMemoryBarriers = &hostBarrier;
                dispatch_.cmdPipelineBarrier2(commandBuffer, &hostDependency);
                if (backendIndex == 0u)
                {
                    hostBarrier.buffer = softwareCounterBuffers_[frameSlot].Handle();
                    dispatch_.cmdPipelineBarrier2(commandBuffer, &hostDependency);
                }
            }

            PendingFrame& pending = pendingFrames_[frameSlot];
            pending.pending = true;
            pending.backend = backend;
            pending.frameGeneration = ++frameGeneration_;
            pending.megakernelCountersAvailable = enableCounters;
            pending.softwareTraversalCountersAvailable =
                backendIndex == 0u
                && enableCounters;
            pending.signalAovIndex = signalAovIndex;
        }

        [[nodiscard]] Wave2RuntimeTelemetry CollectCompletedFrame(
            const std::uint32_t frameSlot)
        {
            ValidateFrameSlot(frameSlot);
            Wave2RuntimeTelemetry result;
            const PendingFrame& pending = pendingFrames_[frameSlot];
            if (!pending.pending)
            {
                result.reason = "frame slot has no completed Wave 2 dispatch";
                return result;
            }

            std::array<std::uint64_t, 2u> timestamps{};
            const VkResult queryResult = vkGetQueryPoolResults(
                device_, traceQueryPool_, frameSlot * 2u, 2u,
                sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (queryResult != VK_SUCCESS)
            {
                result.reason = queryResult == VK_NOT_READY
                    ? "Wave 2 timestamp is not ready after frame fence"
                    : "Wave 2 timestamp query failed";
                pendingFrames_[frameSlot].pending = false;
                return result;
            }
            if (timestamps[1] < timestamps[0])
            {
                result.reason = "Wave 2 timestamp interval is reversed";
                pendingFrames_[frameSlot].pending = false;
                return result;
            }

            result.available = true;
            result.backend = pending.backend;
            result.frameGeneration = pending.frameGeneration;
            result.sceneGeneration = sceneGeneration_;
            result.resourceGeneration = resourceGeneration_;
            result.flattenedSahBuildMilliseconds = flattenedSahBuildMilliseconds_;
            result.accelerationStructureBuildMilliseconds =
                accelerationStructureBuildMilliseconds_;
            result.traceMilliseconds =
                static_cast<double>(timestamps[1] - timestamps[0])
                * static_cast<double>(timestampPeriodNanoseconds_) * 1.0e-6;
            result.flattenedNodeCount =
                static_cast<std::uint32_t>(flattened_.nodes.size());
            result.flattenedTriangleCount =
                static_cast<std::uint32_t>(flattened_.primitives.size());
            if (pending.backend == TraversalBackend::CanonicalLinearGpu
                || pending.backend == TraversalBackend::GpuFlattenedSahBvh)
            {
                result.softwareTraversalCountersAvailable =
                    pending.softwareTraversalCountersAvailable;
                std::memcpy(
                    result.softwareTraversalCounters.data(),
                    softwareCounterBuffers_[frameSlot].MappedData(),
                    sizeof(result.softwareTraversalCounters));
            }
            result.megakernelCountersAvailable =
                pending.megakernelCountersAvailable;
            if (wave3DebugRuntime_ != nullptr)
            {
                const Wave3ReSTIRFrameObservation observation =
                    wave3DebugRuntime_->CollectCompletedFrame(frameSlot);
                result.restirStatisticsAvailable = observation.available;
                result.restirGeneratedCandidates =
                    observation.generatedCandidates;
                result.restirSubmittedVisibilityRays =
                    observation.submittedVisibilityRays;
                result.restirEvaluatedVisibilityRays =
                    observation.evaluatedVisibilityRays;
            }
            if (wave3DebugRuntime_ != nullptr
                && preparedConfigs_[frameSlot].executionArchitecture
                    == ExecutionArchitecture::Wavefront)
            {
                result.wavefrontFatalMask =
                    wave3DebugRuntime_->WavefrontFatalMask();
            }
            result.signalAovIndex = pending.signalAovIndex;
            if (pending.megakernelCountersAvailable)
            {
                std::memcpy(result.megakernelCounters.data(),
                    counterBuffers_[frameSlot].MappedData(),
                    sizeof(result.megakernelCounters));
            }
            pendingFrames_[frameSlot].pending = false;
            return result;
        }

        void Reset() noexcept
        {
            if (device_ == VK_NULL_HANDLE)
            {
                return;
            }
            // Wave 3 pipeline layouts reference the main set layouts and its
            // descriptors reference the split-signal image views. Destroy it
            // while all of those parent Vulkan objects are still alive.
            wave3DebugRuntime_.reset();
            DestroyScene();
            for (VkPipeline& pipeline : megakernelPipelines_)
            {
                if (pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device_, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
            }
            for (VkPipelineLayout& layout : megakernelPipelineLayouts_)
            {
                if (layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device_, layout, nullptr);
                    layout = VK_NULL_HANDLE;
                }
            }
            for (VkShaderModule& module : megakernelShaderModules_)
            {
                if (module != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(device_, module, nullptr);
                    module = VK_NULL_HANDLE;
                }
            }
            for (Hardware::DeviceBuffer& buffer : counterBuffers_)
            {
                buffer.Reset();
            }
            for (Hardware::DeviceBuffer& buffer : indirectSeedBuffers_)
            {
                buffer.Reset();
            }
            for (Hardware::DeviceBuffer& buffer : frameBuffers_)
            {
                buffer.Reset();
            }
            if (traceQueryPool_ != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device_, traceQueryPool_, nullptr);
                traceQueryPool_ = VK_NULL_HANDLE;
            }
            if (frameDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_, frameDescriptorPool_, nullptr);
                frameDescriptorPool_ = VK_NULL_HANDLE;
            }
            if (frameDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, frameDescriptorSetLayout_, nullptr);
                frameDescriptorSetLayout_ = VK_NULL_HANDLE;
            }
            DestroySignalImages();
            sampledResources_.Destroy();
            rayQueryAdapter_.reset();
            canonicalLinearBackend_.Reset();
            softwareBackend_.Reset();
            rayQueryBackend_.Reset();
            if (emptyLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, emptyLayout_, nullptr);
                emptyLayout_ = VK_NULL_HANDLE;
            }
            allocator_.reset();
            device_ = VK_NULL_HANDLE;
            physicalDevice_ = VK_NULL_HANDLE;
            queue_ = VK_NULL_HANDLE;
            commandPool_ = VK_NULL_HANDLE;
            outputImageView_ = VK_NULL_HANDLE;
            outputExtent_ = {};
        }

        [[nodiscard]] bool IsReady() const noexcept { return sceneReady_; }
        [[nodiscard]] std::uint64_t ResourceGeneration() const noexcept
        {
            return resourceGeneration_;
        }
        [[nodiscard]] std::uint64_t SceneGeneration() const noexcept
        {
            return sceneGeneration_;
        }
        [[nodiscard]] bool UsesStagedRaw(
            const ExecutionArchitecture architecture) const noexcept
        {
            return architecture == ExecutionArchitecture::Staged;
        }
        [[nodiscard]] VkImage SignalImage(
            const std::size_t index) const noexcept
        {
            return index < signalImages_.size()
                ? signalImages_[index]
                : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkImageView SignalImageView(
            const std::size_t index) const noexcept
        {
            return index < signalImageViews_.size()
                ? signalImageViews_[index]
                : VK_NULL_HANDLE;
        }

    private:
        struct PendingFrame final
        {
            bool pending = false;
            TraversalBackend backend = TraversalBackend::GpuFlattenedSahBvh;
            std::uint64_t frameGeneration = 0u;
            bool megakernelCountersAvailable = false;
            bool softwareTraversalCountersAvailable = false;
            std::optional<std::size_t> signalAovIndex;
        };

        void EnsureCreated() const
        {
            if (device_ == VK_NULL_HANDLE)
            {
                throw std::logic_error("Wave2Runtime has not been created.");
            }
        }

        void EnsureReady() const
        {
            EnsureCreated();
            if (!sceneReady_)
            {
                throw std::logic_error("Wave2Runtime has no active scene.");
            }
        }

        void EnsureWave3DebugRuntime()
        {
            if (wave3DebugRuntime_ != nullptr)
            {
                return;
            }
            auto runtime = std::make_unique<Wave3DebugRuntime>();
            runtime->Create(Wave3DebugRuntimeCreateInfo{
                physicalDevice_,
                device_,
                outputExtent_,
                outputImageView_,
                signalImageViews_,
                shaderDirectory_,
                frameDescriptorSetLayout_,
                rayQueryBackend_.SceneAdapterLayout(),
                softwareBackend_.TraversalLayout(),
                rayQueryBackend_.TraversalLayout(),
                maximumComputeGroupCountX_,
                maximumComputeGroupCountY_});
            wave3DebugRuntime_ = std::move(runtime);
            SynchronizeWave3SceneTransforms();
        }

        void SynchronizeWave3SceneTransforms()
        {
            if (wave3DebugRuntime_ == nullptr || canonicalScene_.instances.empty())
            {
                return;
            }
            std::vector<Abi0::AbiMat4Rows> current;
            std::vector<Abi0::AbiMat4Rows> previous;
            current.reserve(canonicalScene_.instances.size());
            previous.reserve(canonicalScene_.instances.size());
            for (const Abi0::GpuInstanceV0& instance : canonicalScene_.instances)
            {
                current.push_back(instance.objectToWorld);
                previous.push_back(instance.previousObjectToWorld);
            }
            wave3DebugRuntime_->SetSceneTransforms(current, previous);
        }

        static void ValidateFrameSlot(const std::uint32_t frameSlot)
        {
            if (frameSlot >= kWave2RuntimeFrameCount)
            {
                throw std::out_of_range("Wave 2 frame slot is invalid.");
            }
        }

        [[nodiscard]] std::uint32_t FindDeviceLocalMemoryType(
            const std::uint32_t allowedTypes) const
        {
            VkPhysicalDeviceMemoryProperties properties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &properties);
            for (std::uint32_t index = 0u;
                index < properties.memoryTypeCount; ++index)
            {
                if ((allowedTypes & (1u << index)) != 0u
                    && (properties.memoryTypes[index].propertyFlags
                        & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                        == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                {
                    return index;
                }
            }
            throw std::runtime_error(
                "Wave 2 found no device-local memory type for a signal image.");
        }

        void DestroySignalImages() noexcept
        {
            if (device_ == VK_NULL_HANDLE)
            {
                return;
            }
            for (std::size_t index = 0u;
                index < kWave2SignalImageCount; ++index)
            {
                if (signalImageViews_[index] != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device_, signalImageViews_[index], nullptr);
                    signalImageViews_[index] = VK_NULL_HANDLE;
                }
                if (signalImages_[index] != VK_NULL_HANDLE)
                {
                    vkDestroyImage(device_, signalImages_[index], nullptr);
                    signalImages_[index] = VK_NULL_HANDLE;
                }
                if (signalImageMemories_[index] != VK_NULL_HANDLE)
                {
                    vkFreeMemory(device_, signalImageMemories_[index], nullptr);
                    signalImageMemories_[index] = VK_NULL_HANDLE;
                }
            }
        }

        void CreateSignalImages()
        {
            EnsureCreated();
            DestroySignalImages();
            try
            {
                for (std::size_t index = 0u;
                    index < kWave2SignalImageCount; ++index)
                {
                    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                    imageInfo.imageType = VK_IMAGE_TYPE_2D;
                    imageInfo.format = kWave2SignalImageFormat;
                    imageInfo.extent = {
                        outputExtent_.width, outputExtent_.height, 1u};
                    imageInfo.mipLevels = 1u;
                    imageInfo.arrayLayers = 1u;
                    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT
                        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                        | VK_IMAGE_USAGE_SAMPLED_BIT;
                    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    Check(vkCreateImage(
                        device_, &imageInfo, nullptr, &signalImages_[index]),
                        "vkCreateImage(Wave2 signal)");

                    VkMemoryRequirements requirements{};
                    vkGetImageMemoryRequirements(
                        device_, signalImages_[index], &requirements);
                    VkMemoryAllocateInfo allocation{
                        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    allocation.allocationSize = requirements.size;
                    allocation.memoryTypeIndex = FindDeviceLocalMemoryType(
                        requirements.memoryTypeBits);
                    Check(vkAllocateMemory(
                        device_, &allocation, nullptr, &signalImageMemories_[index]),
                        "vkAllocateMemory(Wave2 signal)");
                    Check(vkBindImageMemory(
                        device_, signalImages_[index],
                        signalImageMemories_[index], 0u),
                        "vkBindImageMemory(Wave2 signal)");

                    VkImageViewCreateInfo viewInfo{
                        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    viewInfo.image = signalImages_[index];
                    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = kWave2SignalImageFormat;
                    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    viewInfo.subresourceRange.levelCount = 1u;
                    viewInfo.subresourceRange.layerCount = 1u;
                    Check(vkCreateImageView(
                        device_, &viewInfo, nullptr, &signalImageViews_[index]),
                        "vkCreateImageView(Wave2 signal)");
                }

                SubmitAndWait([this](const VkCommandBuffer commandBuffer)
                {
                    std::array<VkImageMemoryBarrier2, kWave2SignalImageCount>
                        barriers{};
                    for (std::size_t index = 0u;
                        index < barriers.size(); ++index)
                    {
                        VkImageMemoryBarrier2& barrier = barriers[index];
                        barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                        barrier.srcAccessMask = VK_ACCESS_2_NONE;
                        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                            | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                            | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.image = signalImages_[index];
                        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        barrier.subresourceRange.levelCount = 1u;
                        barrier.subresourceRange.layerCount = 1u;
                    }
                    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    dependency.imageMemoryBarrierCount =
                        static_cast<std::uint32_t>(barriers.size());
                    dependency.pImageMemoryBarriers = barriers.data();
                    dispatch_.cmdPipelineBarrier2(commandBuffer, &dependency);
                });
            }
            catch (...)
            {
                DestroySignalImages();
                throw;
            }
        }

        void CreateIndirectSeedBuffers()
        {
            EnsureCreated();
            for (Hardware::DeviceBuffer& buffer : indirectSeedBuffers_)
            {
                buffer.Reset();
            }
            const std::uint64_t pixelCount =
                static_cast<std::uint64_t>(outputExtent_.width)
                * static_cast<std::uint64_t>(outputExtent_.height);
            const std::uint64_t byteCount = pixelCount
                * static_cast<std::uint64_t>(kWave2IndirectSeedStride);
            if (pixelCount == 0u
                || byteCount > std::numeric_limits<VkDeviceSize>::max())
            {
                throw std::overflow_error(
                    "Wave 2 indirect seed buffer extent is invalid.");
            }
            for (Hardware::DeviceBuffer& buffer : indirectSeedBuffers_)
            {
                buffer = CreateDeviceLocalBuffer(
                    *allocator_,
                    static_cast<VkDeviceSize>(byteCount),
                    kStorageUsage);
            }
        }

        void DestroyEnvironmentImage() noexcept
        {
            if (device_ == VK_NULL_HANDLE)
            {
                return;
            }
            if (environmentSampler_ != VK_NULL_HANDLE)
            {
                vkDestroySampler(device_, environmentSampler_, nullptr);
                environmentSampler_ = VK_NULL_HANDLE;
            }
            if (environmentImageView_ != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_, environmentImageView_, nullptr);
                environmentImageView_ = VK_NULL_HANDLE;
            }
            if (environmentImage_ != VK_NULL_HANDLE)
            {
                vkDestroyImage(device_, environmentImage_, nullptr);
                environmentImage_ = VK_NULL_HANDLE;
            }
            if (environmentImageMemory_ != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_, environmentImageMemory_, nullptr);
                environmentImageMemory_ = VK_NULL_HANDLE;
            }
        }

        void CreateEnvironmentImage(const Scene::ExperimentEnvironment& environment)
        {
            Require(!environment.Empty(),
                "Wave 2 cannot upload an empty environment image.");
            DestroyEnvironmentImage();
            try
            {
                VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                imageInfo.extent = {environment.width, environment.height, 1u};
                imageInfo.mipLevels = 1u;
                imageInfo.arrayLayers = 1u;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                Check(vkCreateImage(device_, &imageInfo, nullptr, &environmentImage_),
                    "vkCreateImage(Wave2 environment)");

                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements(device_, environmentImage_, &requirements);
                VkMemoryAllocateInfo allocation{
                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocation.allocationSize = requirements.size;
                allocation.memoryTypeIndex = FindDeviceLocalMemoryType(
                    requirements.memoryTypeBits);
                Check(vkAllocateMemory(
                    device_, &allocation, nullptr, &environmentImageMemory_),
                    "vkAllocateMemory(Wave2 environment)");
                Check(vkBindImageMemory(
                    device_, environmentImage_, environmentImageMemory_, 0u),
                    "vkBindImageMemory(Wave2 environment)");

                VkImageViewCreateInfo viewInfo{
                    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = environmentImage_;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.levelCount = 1u;
                viewInfo.subresourceRange.layerCount = 1u;
                Check(vkCreateImageView(
                    device_, &viewInfo, nullptr, &environmentImageView_),
                    "vkCreateImageView(Wave2 environment)");

                // Environment longitude wraps while latitude remains clamped.
                // Nearest filtering keeps the sampled radiance exactly aligned
                // with the per-texel importance table on all supported devices.
                VkSamplerCreateInfo samplerInfo{
                    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                samplerInfo.magFilter = VK_FILTER_NEAREST;
                samplerInfo.minFilter = VK_FILTER_NEAREST;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.maxLod = 0.0f;
                Check(vkCreateSampler(
                    device_, &samplerInfo, nullptr, &environmentSampler_),
                    "vkCreateSampler(Wave2 environment)");

                Hardware::DeviceBuffer staging = UploadBuffer(
                    *allocator_,
                    std::span<const Abi0::AbiFloat4>{environment.linearRgba},
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                SubmitAndWait([&](const VkCommandBuffer commandBuffer)
                {
                    VkImageMemoryBarrier2 barrier{
                        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                    barrier.srcAccessMask = VK_ACCESS_2_NONE;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = environmentImage_;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.levelCount = 1u;
                    barrier.subresourceRange.layerCount = 1u;
                    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    dependency.imageMemoryBarrierCount = 1u;
                    dependency.pImageMemoryBarriers = &barrier;
                    dispatch_.cmdPipelineBarrier2(commandBuffer, &dependency);

                    VkBufferImageCopy copy{};
                    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copy.imageSubresource.layerCount = 1u;
                    copy.imageExtent = {environment.width, environment.height, 1u};
                    vkCmdCopyBufferToImage(
                        commandBuffer, staging.Handle(), environmentImage_,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);

                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    dispatch_.cmdPipelineBarrier2(commandBuffer, &dependency);
                });
            }
            catch (...)
            {
                DestroyEnvironmentImage();
                throw;
            }
        }

        void CreateMegakernelDescriptors()
        {
            std::array<VkDescriptorSetLayoutBinding, 20u> bindings{};
            for (std::uint32_t binding = 0u; binding < bindings.size(); ++binding)
            {
                bindings[binding].binding = binding;
                bindings[binding].descriptorCount = 1u;
                bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                if (binding == 0u)
                {
                    bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                }
                else if (binding == 10u)
                {
                    bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                }
                else if (binding == 11u)
                {
                    bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                }
                else if (binding >= 12u && binding <= 17u)
                {
                    bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                }
                else
                {
                    bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
            }
            VkDescriptorSetLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            Check(vkCreateDescriptorSetLayout(
                device_, &layoutInfo, nullptr, &frameDescriptorSetLayout_),
                "vkCreateDescriptorSetLayout(Wave2 Megakernel)");

            constexpr std::uint32_t setCount = kWave2RuntimeFrameCount;
            const std::array<VkDescriptorPoolSize, 5u> poolSizes{
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11u * setCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, setCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, setCount},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6u * setCount}};
            VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets = setCount;
            poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            Check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &frameDescriptorPool_),
                "vkCreateDescriptorPool(Wave2 Megakernel)");

            const std::array layouts{frameDescriptorSetLayout_, frameDescriptorSetLayout_};
            VkDescriptorSetAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocateInfo.descriptorPool = frameDescriptorPool_;
            allocateInfo.descriptorSetCount = setCount;
            allocateInfo.pSetLayouts = layouts.data();
            Check(vkAllocateDescriptorSets(
                device_, &allocateInfo, frameDescriptorSets_.data()),
                "vkAllocateDescriptorSets(Wave2 Megakernel)");

            for (std::uint32_t index = 0u; index < setCount; ++index)
            {
                frameBuffers_[index] = CreateMappedBuffer(*allocator_,
                    sizeof(Mega::MegakernelFrameConstantsGpu),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
                counterBuffers_[index] = CreateMappedBuffer(*allocator_,
                    kWave2RuntimeCounterCount * sizeof(std::uint32_t),
                    kStorageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            }
            CreateIndirectSeedBuffers();

            VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = setCount * 2u;
            Check(vkCreateQueryPool(device_, &queryInfo, nullptr, &traceQueryPool_),
                "vkCreateQueryPool(Wave2 trace)");
        }

        void CreateMegakernelPipeline(
            const std::size_t index,
            const std::span<const std::uint32_t> spirv,
            const VkDescriptorSetLayout traversalLayout)
        {
            VkShaderModuleCreateInfo moduleInfo{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = spirv.size_bytes();
            moduleInfo.pCode = spirv.data();
            Check(vkCreateShaderModule(
                device_, &moduleInfo, nullptr, &megakernelShaderModules_[index]),
                "vkCreateShaderModule(Wave2 Megakernel)");

            if (wave3DebugRuntime_ == nullptr
                || wave3DebugRuntime_->ReconstructionSetLayout()
                    == VK_NULL_HANDLE)
            {
                throw std::logic_error(
                    "Wave 2 pipeline creation requires the shared L8 set layout.");
            }
            const std::array layouts{
                frameDescriptorSetLayout_,
                rayQueryBackend_.SceneAdapterLayout(),
                traversalLayout,
                emptyLayout_,
                wave3DebugRuntime_->ReconstructionSetLayout()};
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushRange.offset = 0u;
            pushRange.size = 4u * sizeof(std::uint32_t);
            VkPipelineLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
            layoutInfo.pSetLayouts = layouts.data();
            layoutInfo.pushConstantRangeCount = 1u;
            layoutInfo.pPushConstantRanges = &pushRange;
            Check(vkCreatePipelineLayout(
                device_, &layoutInfo, nullptr, &megakernelPipelineLayouts_[index]),
                "vkCreatePipelineLayout(Wave2 Megakernel)");

            VkPipelineShaderStageCreateInfo stageInfo{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = megakernelShaderModules_[index];
            stageInfo.pName = "CSMain";
            VkComputePipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = megakernelPipelineLayouts_[index];
            Check(vkCreateComputePipelines(
                device_, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr,
                &megakernelPipelines_[index]),
                "vkCreateComputePipelines(Wave2 Megakernel)");
        }

        void UploadCanonicalSceneBuffers()
        {
            constantsBuffer_ = UploadBuffer(*allocator_,
                std::span{&canonicalScene_.constants, 1u},
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            vertexBuffer_ = UploadBuffer(*allocator_,
                std::span<const Abi0::GpuVertexV0>{canonicalScene_.vertices},
                kAsInputUsage, true);
            indexBuffer_ = UploadBuffer(*allocator_,
                std::span<const std::uint32_t>{canonicalScene_.indices},
                kAsInputUsage, true);
            geometryBuffer_ = UploadBuffer(*allocator_,
                std::span<const Abi0::GpuGeometryV0>{canonicalScene_.geometries},
                kStorageUsage);
            instanceBuffer_ = UploadBuffer(*allocator_,
                std::span<const Abi0::GpuInstanceV0>{canonicalScene_.instances},
                kStorageUsage);
            canonicalMaterialBuffer_ = UploadBuffer(*allocator_,
                std::span<const Abi0::GpuMaterialV0>{canonicalScene_.materials},
                kStorageUsage);
            canonicalLightBuffer_ = UploadBuffer(*allocator_,
                std::span<const Abi0::GpuLightV0>{canonicalScene_.lights},
                kStorageUsage);
        }

        void BuildHardwareAccelerationStructures()
        {
            std::vector<Hardware::TriangleGeometryInput> canonicalGeometryInputs;
            canonicalGeometryInputs.reserve(canonicalScene_.geometries.size());
            for (const Abi0::GpuGeometryV0& geometry : canonicalScene_.geometries)
            {
                Require(geometry.identity.z < canonicalScene_.materials.size(),
                    "Wave 2 geometry references an invalid material.");
                const bool alphaMasked =
                    (geometry.identity.w & Abi0::GeometryFlagAlphaMask) != 0u
                    || (canonicalScene_.materials[geometry.identity.z].metadata.y
                        & Abi0::MaterialFlagAlphaMask) != 0u;
                const bool doubleSided =
                    (geometry.identity.w & Abi0::GeometryFlagDoubleSided) != 0u
                    || (canonicalScene_.materials[geometry.identity.z].metadata.y
                        & Abi0::MaterialFlagDoubleSided) != 0u;
                Hardware::TriangleGeometryInput input{};
                input.vertexAddress = vertexBuffer_.Address();
                input.vertexStride = sizeof(Abi0::GpuVertexV0);
                input.maxVertex =
                    static_cast<std::uint32_t>(canonicalScene_.vertices.size() - 1u);
                input.indexAddress = indexBuffer_.Address();
                input.indexType = VK_INDEX_TYPE_UINT32;
                input.primitiveCount = geometry.indexRange.y / 3u;
                input.primitiveOffset = geometry.indexRange.x * sizeof(std::uint32_t);
                input.firstVertex = geometry.indexRange.z;
                input.geometryFlags = Hardware::ChooseTriangleGeometryFlags(
                    alphaMasked, doubleSided);
                canonicalGeometryInputs.push_back(input);
            }

            asBuilder_ = std::make_unique<Hardware::AccelerationStructureBuilder>(
                *allocator_, scratchAlignment_);
            blasGeometryInputs_.clear();
            blases_.clear();
            blasGeometryInputs_.reserve(canonicalScene_.instances.size());
            blases_.resize(canonicalScene_.instances.size());
            VkDeviceSize maximumScratchSize = 0u;
            for (std::size_t instanceIndex = 0u;
                instanceIndex < canonicalScene_.instances.size(); ++instanceIndex)
            {
                const Abi0::GpuInstanceV0& instance =
                    canonicalScene_.instances[instanceIndex];
                const std::uint64_t geometryEnd =
                    static_cast<std::uint64_t>(instance.metadata.x)
                    + static_cast<std::uint64_t>(instance.metadata.y);
                Require(instance.metadata.y > 0u
                        && geometryEnd <= canonicalGeometryInputs.size(),
                    "Wave 2 canonical instance has an invalid geometry range.");
                const auto begin = canonicalGeometryInputs.begin()
                    + instance.metadata.x;
                const auto end = begin + instance.metadata.y;
                blasGeometryInputs_.emplace_back(begin, end);
                RequireStatus(asBuilder_->CreateBottomLevel(
                    blasGeometryInputs_.back(),
                    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                    blases_[instanceIndex]),
                    "AccelerationStructureBuilder::CreateBottomLevel(production instance)");
                maximumScratchSize = std::max(
                    maximumScratchSize,
                    blases_[instanceIndex].BuildScratchSize());
            }
            RequireStatus(asBuilder_->CreateTopLevel(
                static_cast<std::uint32_t>(canonicalScene_.instances.size()),
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
                tlas_), "AccelerationStructureBuilder::CreateTopLevel(production)");

            std::vector<VkAccelerationStructureInstanceKHR> instances;
            instances.reserve(canonicalScene_.instances.size());
            for (std::size_t instanceIndex = 0u;
                instanceIndex < canonicalScene_.instances.size(); ++instanceIndex)
            {
                const Abi0::GpuInstanceV0& source =
                    canonicalScene_.instances[instanceIndex];
                VkAccelerationStructureInstanceKHR instance{};
                instance.transform = ToVkTransform(source.objectToWorld);
                instance.instanceCustomIndex = static_cast<std::uint32_t>(instanceIndex);
                instance.mask = (source.metadata.w & Abi0::InstanceFlagVisible) != 0u
                    ? 0xffu : 0u;
                instance.instanceShaderBindingTableRecordOffset = 0u;
                instance.flags =
                    VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                instance.accelerationStructureReference = blases_[instanceIndex].Address();
                instances.push_back(instance);
            }
            RequireStatus(asBuilder_->CreateInstanceBuffer(instances, asInstanceBuffer_),
                "AccelerationStructureBuilder::CreateInstanceBuffer(production)");
            maximumScratchSize = std::max(
                maximumScratchSize, tlas_.BuildScratchSize());
            maximumScratchSize = std::max(
                maximumScratchSize, tlas_.UpdateScratchSize());
            RequireStatus(asBuilder_->CreateScratchBuffer(
                maximumScratchSize, asScratchBuffer_),
                "AccelerationStructureBuilder::CreateScratchBuffer(production)");
        }

        void CreateSceneDescriptorSets()
        {
            RequireStatus(rayQueryBackend_.CreateDescriptorPool(
                1u + 2u * kWave2RuntimeFrameCount,
                rayQueryDescriptorPool_),
                "RayQueryBackend::CreateDescriptorPool(production)");
            RequireStatus(rayQueryBackend_.AllocateDescriptorSets(
                rayQueryDescriptorPool_, canonicalSceneSet_, rayQueryTraversalSet_),
                "RayQueryBackend::AllocateDescriptorSets(production)");
            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave2RuntimeFrameCount; ++frameSlot)
            {
                RequireStatus(rayQueryBackend_.AllocateDescriptorSets(
                    rayQueryDescriptorPool_,
                    rayQueryReferenceSceneSets_[frameSlot],
                    rayQueryReferenceTraversalSets_[frameSlot]),
                    "RayQueryBackend::AllocateDescriptorSets(ReSTIR reference)");
                RequireStatus(rayQueryBackend_.AllocateDescriptorSets(
                    rayQueryDescriptorPool_,
                    rayQueryWinnerSceneSets_[frameSlot],
                    rayQueryWinnerTraversalSets_[frameSlot]),
                    "RayQueryBackend::AllocateDescriptorSets(ReSTIR winner)");
            }
            const Hardware::HardwareSceneBufferBindings sceneBindings{
                    Descriptor(constantsBuffer_), Descriptor(vertexBuffer_),
                    Descriptor(indexBuffer_), Descriptor(geometryBuffer_),
                    Descriptor(instanceBuffer_), Descriptor(canonicalMaterialBuffer_),
                    Descriptor(canonicalLightBuffer_)};
            RequireStatus(rayQueryBackend_.UpdateSceneDescriptors(
                canonicalSceneSet_, sceneBindings),
                "RayQueryBackend::UpdateSceneDescriptors(production)");
            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave2RuntimeFrameCount; ++frameSlot)
            {
                RequireStatus(rayQueryBackend_.UpdateSceneDescriptors(
                    rayQueryReferenceSceneSets_[frameSlot], sceneBindings),
                    "RayQueryBackend::UpdateSceneDescriptors(ReSTIR reference)");
                RequireStatus(rayQueryBackend_.UpdateSceneDescriptors(
                    rayQueryWinnerSceneSets_[frameSlot], sceneBindings),
                    "RayQueryBackend::UpdateSceneDescriptors(ReSTIR winner)");
            }
            RequireStatus(rayQueryBackend_.UpdateTraversalDescriptors(
                rayQueryTraversalSet_, Hardware::RayQueryTraversalBindings{
                    tlas_.Handle(), Descriptor(dummyRayBuffer_), Descriptor(dummyHitBuffer_),
                    sampledResources_.AlphaImage(), sampledResources_.Sampler()}),
                "RayQueryBackend::UpdateTraversalDescriptors(production)");
            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave2RuntimeFrameCount; ++frameSlot)
            {
                RequireStatus(rayQueryBackend_.UpdateTraversalDescriptors(
                    rayQueryReferenceTraversalSets_[frameSlot],
                    Hardware::RayQueryTraversalBindings{
                        tlas_.Handle(), Descriptor(dummyRayBuffer_),
                        Descriptor(dummyHitBuffer_),
                        sampledResources_.AlphaImage(),
                        sampledResources_.Sampler()}),
                    "RayQueryBackend::UpdateTraversalDescriptors(ReSTIR reference dummy)");
                RequireStatus(rayQueryBackend_.UpdateTraversalDescriptors(
                    rayQueryWinnerTraversalSets_[frameSlot],
                    Hardware::RayQueryTraversalBindings{
                        tlas_.Handle(), Descriptor(dummyRayBuffer_),
                        Descriptor(dummyHitBuffer_),
                        sampledResources_.AlphaImage(),
                        sampledResources_.Sampler()}),
                    "RayQueryBackend::UpdateTraversalDescriptors(ReSTIR winner dummy)");
            }

            RequireStatus(softwareBackend_.CreateDescriptorPool(
                3u * kWave2RuntimeFrameCount, softwareDescriptorPool_),
                "SoftwareGpuTraversalBackend::CreateDescriptorPool(production)");
            RequireStatus(canonicalLinearBackend_.CreateDescriptorPool(
                3u * kWave2RuntimeFrameCount, canonicalLinearDescriptorPool_),
                "SoftwareGpuTraversalBackend::CreateDescriptorPool(canonical linear)");
            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave2RuntimeFrameCount; ++frameSlot)
            {
                RequireStatus(softwareBackend_.AllocateTraversalSet(
                    softwareDescriptorPool_, softwareTraversalSets_[frameSlot]),
                    "SoftwareGpuTraversalBackend::AllocateTraversalSet(production)");
                RequireStatus(softwareBackend_.AllocateTraversalSet(
                    softwareDescriptorPool_, softwareReferenceTraversalSets_[frameSlot]),
                    "SoftwareGpuTraversalBackend::AllocateTraversalSet(ReSTIR reference)");
                RequireStatus(softwareBackend_.AllocateTraversalSet(
                    softwareDescriptorPool_, softwareWinnerTraversalSets_[frameSlot]),
                    "SoftwareGpuTraversalBackend::AllocateTraversalSet(ReSTIR winner)");
                RequireStatus(softwareBackend_.UpdateTraversalSet(
                    softwareTraversalSets_[frameSlot],
                    L4::SoftwareGpuTraversalBindings{
                        Descriptor(softwareNodeBuffer_), Descriptor(dummyRayBuffer_),
                        Descriptor(dummyHitBuffer_), Descriptor(softwareTriangleBuffer_),
                        Descriptor(softwareCounterBuffers_[frameSlot]),
                        sampledResources_.AlphaImage(), sampledResources_.Sampler(),
                        static_cast<std::uint32_t>(flattened_.nodes.size()),
                        static_cast<std::uint32_t>(flattened_.primitives.size()),
                        std::min<std::uint32_t>(
                            64u,
                            static_cast<std::uint32_t>(flattened_.maximumDepth + 1u)),
                        2u, 0u}),
                    "SoftwareGpuTraversalBackend::UpdateTraversalSet(production)");
                const auto updateDummy = [this, frameSlot](
                    const VkDescriptorSet set,
                    const char* operation)
                {
                    RequireStatus(softwareBackend_.UpdateTraversalSet(
                        set,
                        L4::SoftwareGpuTraversalBindings{
                            Descriptor(softwareNodeBuffer_),
                            Descriptor(dummyRayBuffer_),
                            Descriptor(dummyHitBuffer_),
                            Descriptor(softwareTriangleBuffer_),
                            Descriptor(softwareCounterBuffers_[frameSlot]),
                            sampledResources_.AlphaImage(),
                            sampledResources_.Sampler(),
                            static_cast<std::uint32_t>(flattened_.nodes.size()),
                            static_cast<std::uint32_t>(flattened_.primitives.size()),
                            std::min<std::uint32_t>(
                                64u,
                                static_cast<std::uint32_t>(
                                    flattened_.maximumDepth + 1u)),
                            2u, 0u}),
                        operation);
                };
                updateDummy(
                    softwareReferenceTraversalSets_[frameSlot],
                    "SoftwareGpuTraversalBackend::UpdateTraversalSet(ReSTIR reference dummy)");
                updateDummy(
                    softwareWinnerTraversalSets_[frameSlot],
                    "SoftwareGpuTraversalBackend::UpdateTraversalSet(ReSTIR winner dummy)");

                RequireStatus(canonicalLinearBackend_.AllocateTraversalSet(
                    canonicalLinearDescriptorPool_,
                    canonicalLinearTraversalSets_[frameSlot]),
                    "SoftwareGpuTraversalBackend::AllocateTraversalSet(canonical linear production)");
                RequireStatus(canonicalLinearBackend_.AllocateTraversalSet(
                    canonicalLinearDescriptorPool_,
                    canonicalLinearReferenceTraversalSets_[frameSlot]),
                    "SoftwareGpuTraversalBackend::AllocateTraversalSet(canonical linear ReSTIR reference)");
                RequireStatus(canonicalLinearBackend_.AllocateTraversalSet(
                    canonicalLinearDescriptorPool_,
                    canonicalLinearWinnerTraversalSets_[frameSlot]),
                    "SoftwareGpuTraversalBackend::AllocateTraversalSet(canonical linear ReSTIR winner)");
                const auto updateCanonicalLinear = [this, frameSlot](
                    const VkDescriptorSet set,
                    const char* operation)
                {
                    const VkDescriptorBufferInfo canonicalTriangles =
                        Descriptor(canonicalLinearTriangleBuffer_);
                    RequireStatus(canonicalLinearBackend_.UpdateTraversalSet(
                        set,
                        L4::SoftwareGpuTraversalBindings{
                            canonicalTriangles,
                            Descriptor(dummyRayBuffer_),
                            Descriptor(dummyHitBuffer_),
                            canonicalTriangles,
                            Descriptor(softwareCounterBuffers_[frameSlot]),
                            sampledResources_.AlphaImage(),
                            sampledResources_.Sampler(),
                            0u,
                            static_cast<std::uint32_t>(
                                traversal_.triangles.size()),
                            0u,
                            2u, 0u}),
                        operation);
                };
                updateCanonicalLinear(
                    canonicalLinearTraversalSets_[frameSlot],
                    "SoftwareGpuTraversalBackend::UpdateTraversalSet(canonical linear production)");
                updateCanonicalLinear(
                    canonicalLinearReferenceTraversalSets_[frameSlot],
                    "SoftwareGpuTraversalBackend::UpdateTraversalSet(canonical linear ReSTIR reference dummy)");
                updateCanonicalLinear(
                    canonicalLinearWinnerTraversalSets_[frameSlot],
                    "SoftwareGpuTraversalBackend::UpdateTraversalSet(canonical linear ReSTIR winner dummy)");
            }
        }

        void SubmitSceneBuild()
        {
            VkQueryPool buildQueryPool = VK_NULL_HANDLE;
            VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = 2u;
            Check(vkCreateQueryPool(device_, &queryInfo, nullptr, &buildQueryPool),
                "vkCreateQueryPool(Wave2 AS build)");
            try
            {
                SubmitAndWait([&](const VkCommandBuffer commandBuffer)
                {
                    sampledResources_.RecordInitialize(dispatch_, commandBuffer);
                    vkCmdResetQueryPool(commandBuffer, buildQueryPool, 0u, 2u);
                    vkCmdWriteTimestamp2(commandBuffer,
                        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        buildQueryPool, 0u);
                    for (std::size_t instanceIndex = 0u;
                        instanceIndex < blases_.size(); ++instanceIndex)
                    {
                        RequireStatus(asBuilder_->RecordBottomLevelBuild(
                            commandBuffer,
                            blasGeometryInputs_[instanceIndex],
                            asScratchBuffer_,
                            blases_[instanceIndex]),
                            "AccelerationStructureBuilder::RecordBottomLevelBuild(production instance)");
                        asBuilder_->RecordBuildToBuildBarrier(commandBuffer);
                    }
                    RequireStatus(asBuilder_->RecordTopLevelBuild(
                        commandBuffer, asInstanceBuffer_.Address(),
                        static_cast<std::uint32_t>(canonicalScene_.instances.size()),
                        asScratchBuffer_, tlas_),
                        "AccelerationStructureBuilder::RecordTopLevelBuild(production)");
                    vkCmdWriteTimestamp2(commandBuffer,
                        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        buildQueryPool, 1u);
                    asBuilder_->RecordBuildToTraceBarrier(commandBuffer);

                    const Gpu::GpuSceneBuildRequest request{
                        commandBuffer, canonicalSceneSet_, traversal_.fingerprint,
                        traversal_.generation, true};
                    RequireStatus(softwareBackend_.BuildOrUpdateScene(request),
                        "SoftwareGpuTraversalBackend::BuildOrUpdateScene(production)");
                    RequireStatus(canonicalLinearBackend_.BuildOrUpdateScene(request),
                        "SoftwareGpuTraversalBackend::BuildOrUpdateScene(canonical linear)");
                    RequireStatus(rayQueryAdapter_->BuildOrUpdateScene(request),
                        "RayQueryTraversalAdapter::BuildOrUpdateScene(production)");
                });
                for (Hardware::AccelerationStructureResource& blas : blases_)
                {
                    blas.MarkReady();
                }
                tlas_.MarkReady();
                Require(std::all_of(
                            blases_.begin(), blases_.end(),
                            [](const Hardware::AccelerationStructureResource& blas)
                            {
                                return blas.State()
                                    == Hardware::AccelerationStructureState::Ready;
                            })
                        && tlas_.State() == Hardware::AccelerationStructureState::Ready,
                    "Wave 2 acceleration structures did not reach Ready state.");

                std::array<std::uint64_t, 2u> timestamps{};
                Check(vkGetQueryPoolResults(device_, buildQueryPool, 0u, 2u,
                    sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t),
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                    "vkGetQueryPoolResults(Wave2 AS build)");
                Require(timestamps[1] >= timestamps[0],
                    "Wave 2 AS build timestamp interval is reversed.");
                accelerationStructureBuildMilliseconds_ =
                    static_cast<double>(timestamps[1] - timestamps[0])
                    * static_cast<double>(timestampPeriodNanoseconds_) * 1.0e-6;
            }
            catch (...)
            {
                vkDestroyQueryPool(device_, buildQueryPool, nullptr);
                throw;
            }
            vkDestroyQueryPool(device_, buildQueryPool, nullptr);
        }

        void SubmitAndWait(const std::function<void(VkCommandBuffer)>& record)
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocateInfo.commandPool = commandPool_;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            Check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
                "vkAllocateCommandBuffers(Wave2 one-time)");
            try
            {
                VkCommandBufferBeginInfo beginInfo{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                    "vkBeginCommandBuffer(Wave2 one-time)");
                record(commandBuffer);
                Check(vkEndCommandBuffer(commandBuffer),
                    "vkEndCommandBuffer(Wave2 one-time)");
                VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                Check(vkCreateFence(device_, &fenceInfo, nullptr, &fence),
                    "vkCreateFence(Wave2 one-time)");
                VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submitInfo.commandBufferCount = 1u;
                submitInfo.pCommandBuffers = &commandBuffer;
                Check(vkQueueSubmit(queue_, 1u, &submitInfo, fence),
                    "vkQueueSubmit(Wave2 one-time)");
                Check(vkWaitForFences(device_, 1u, &fence, VK_TRUE, UINT64_MAX),
                    "vkWaitForFences(Wave2 one-time)");
            }
            catch (...)
            {
                if (fence != VK_NULL_HANDLE)
                {
                    vkDestroyFence(device_, fence, nullptr);
                }
                vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
                throw;
            }
            vkDestroyFence(device_, fence, nullptr);
            vkFreeCommandBuffers(device_, commandPool_, 1u, &commandBuffer);
        }

        void UpdateAllFrameDescriptors()
        {
            for (std::uint32_t frameSlot = 0u;
                frameSlot < kWave2RuntimeFrameCount; ++frameSlot)
            {
                UpdateFrameDescriptors(
                    frameSlot, Mega::LightSelection::PowerWeighted);
            }
        }

        void UpdateFrameDescriptors(
            const std::uint32_t frameSlot,
            const Mega::LightSelection selection)
        {
            const Hardware::DeviceBuffer& alias = selection
                    == Mega::LightSelection::PowerWeighted
                ? powerAliasBuffer_ : uniformAliasBuffer_;
            const Hardware::DeviceBuffer& pmf = selection
                    == Mega::LightSelection::PowerWeighted
                ? powerPmfBuffer_ : uniformPmfBuffer_;
            const std::array<VkDescriptorBufferInfo, 9u> storageInputs{
                Descriptor(materialBuffer_), Descriptor(lightBuffer_), Descriptor(alias),
                Descriptor(environmentRowBuffer_), Descriptor(environmentColumnBuffer_),
                Descriptor(pmf), Descriptor(emitterMapBuffer_),
                Descriptor(fixtureTriangleBuffer_), Descriptor(fixtureSphereBuffer_)};
            const VkDescriptorBufferInfo frame = Descriptor(frameBuffers_[frameSlot]);
            const VkDescriptorImageInfo environment = environmentImageView_ != VK_NULL_HANDLE
                ? VkDescriptorImageInfo{
                    VK_NULL_HANDLE,
                    environmentImageView_,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                : sampledResources_.EnvironmentImage();
            const VkDescriptorImageInfo sampler = environmentSampler_ != VK_NULL_HANDLE
                ? VkDescriptorImageInfo{
                    environmentSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}
                : sampledResources_.Sampler();
            std::array<VkDescriptorImageInfo, 6u> outputs{};
            outputs[0u] = {
                VK_NULL_HANDLE, outputImageView_, VK_IMAGE_LAYOUT_GENERAL};
            for (std::size_t index = 0u;
                index < signalImageViews_.size(); ++index)
            {
                outputs[index + 1u] = {
                    VK_NULL_HANDLE,
                    signalImageViews_[index],
                    VK_IMAGE_LAYOUT_GENERAL};
            }
            const VkDescriptorBufferInfo counters = Descriptor(counterBuffers_[frameSlot]);
            const VkDescriptorBufferInfo indirectSeeds =
                Descriptor(indirectSeedBuffers_[frameSlot]);

            std::array<VkWriteDescriptorSet, 20u> writes{};
            for (std::uint32_t binding = 0u; binding < writes.size(); ++binding)
            {
                VkWriteDescriptorSet& write = writes[binding];
                write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = frameDescriptorSets_[frameSlot];
                write.dstBinding = binding;
                write.descriptorCount = 1u;
                if (binding == 0u)
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &frame;
                }
                else if (binding >= 1u && binding <= 9u)
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &storageInputs[binding - 1u];
                }
                else if (binding == 10u)
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    write.pImageInfo = &environment;
                }
                else if (binding == 11u)
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    write.pImageInfo = &sampler;
                }
                else if (binding >= 12u && binding <= 17u)
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    write.pImageInfo = &outputs[binding - 12u];
                }
                else if (binding == 18u)
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &counters;
                }
                else
                {
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &indirectSeeds;
                }
            }
            vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
                writes.data(), 0u, nullptr);
        }

        void DestroyScene() noexcept
        {
            sceneReady_ = false;
            for (PendingFrame& pending : pendingFrames_)
            {
                pending = {};
            }
            if (device_ != VK_NULL_HANDLE && softwareDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_, softwareDescriptorPool_, nullptr);
            }
            if (device_ != VK_NULL_HANDLE
                && canonicalLinearDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(
                    device_, canonicalLinearDescriptorPool_, nullptr);
            }
            if (device_ != VK_NULL_HANDLE && rayQueryDescriptorPool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_, rayQueryDescriptorPool_, nullptr);
            }
            softwareDescriptorPool_ = VK_NULL_HANDLE;
            canonicalLinearDescriptorPool_ = VK_NULL_HANDLE;
            rayQueryDescriptorPool_ = VK_NULL_HANDLE;
            softwareTraversalSets_.fill(VK_NULL_HANDLE);
            softwareReferenceTraversalSets_.fill(VK_NULL_HANDLE);
            softwareWinnerTraversalSets_.fill(VK_NULL_HANDLE);
            canonicalLinearTraversalSets_.fill(VK_NULL_HANDLE);
            canonicalLinearReferenceTraversalSets_.fill(VK_NULL_HANDLE);
            canonicalLinearWinnerTraversalSets_.fill(VK_NULL_HANDLE);
            rayQueryTraversalSet_ = VK_NULL_HANDLE;
            canonicalSceneSet_ = VK_NULL_HANDLE;
            rayQueryReferenceSceneSets_.fill(VK_NULL_HANDLE);
            rayQueryReferenceTraversalSets_.fill(VK_NULL_HANDLE);
            rayQueryWinnerSceneSets_.fill(VK_NULL_HANDLE);
            rayQueryWinnerTraversalSets_.fill(VK_NULL_HANDLE);
            DestroyEnvironmentImage();
            tlas_.Reset();
            blases_.clear();
            asBuilder_.reset();
            asScratchBuffer_.Reset();
            asInstanceBuffer_.Reset();
            blasGeometryInputs_.clear();
            fixtureSphereBuffer_.Reset();
            fixtureTriangleBuffer_.Reset();
            emitterMapBuffer_.Reset();
            powerPmfBuffer_.Reset();
            uniformPmfBuffer_.Reset();
            environmentColumnBuffer_.Reset();
            environmentRowBuffer_.Reset();
            powerAliasBuffer_.Reset();
            uniformAliasBuffer_.Reset();
            lightBuffer_.Reset();
            materialBuffer_.Reset();
            for (Hardware::DeviceBuffer& counterBuffer : softwareCounterBuffers_)
            {
                counterBuffer.Reset();
            }
            dummyHitBuffer_.Reset();
            dummyRayBuffer_.Reset();
            canonicalLinearTriangleBuffer_.Reset();
            softwareTriangleBuffer_.Reset();
            softwareNodeBuffer_.Reset();
            canonicalLightBuffer_.Reset();
            canonicalMaterialBuffer_.Reset();
            instanceBuffer_.Reset();
            geometryBuffer_.Reset();
            indexBuffer_.Reset();
            vertexBuffer_.Reset();
            constantsBuffer_.Reset();
            canonicalScene_ = {};
            traversal_ = {};
            flattened_ = {};
            environmentLightIndex_ = Mega::kInvalidIndex;
            environmentWidth_ = 0u;
            environmentHeight_ = 0u;
            sceneGeneration_ = 0u;
        }

        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue queue_ = VK_NULL_HANDLE;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        VkImageView outputImageView_ = VK_NULL_HANDLE;
        VkExtent2D outputExtent_{};
        std::uint32_t maximumComputeGroupCountX_ = 0u;
        std::uint32_t maximumComputeGroupCountY_ = 0u;
        std::filesystem::path shaderDirectory_{};
        VkDeviceSize scratchAlignment_ = 1u;
        float timestampPeriodNanoseconds_ = 0.0f;
        Hardware::DeviceDispatch dispatch_{};
        std::unique_ptr<Hardware::DeviceBufferAllocator> allocator_;
        VkDescriptorSetLayout emptyLayout_ = VK_NULL_HANDLE;
        Hardware::RayQueryBackend rayQueryBackend_{};
        L4::SoftwareGpuTraversalBackend softwareBackend_{};
        L4::SoftwareGpuTraversalBackend canonicalLinearBackend_{};
        std::unique_ptr<Hardware::RayQueryTraversalAdapter> rayQueryAdapter_;
        DummySampledResources sampledResources_{};
        std::array<VkImage, kWave2SignalImageCount> signalImages_{};
        std::array<VkDeviceMemory, kWave2SignalImageCount> signalImageMemories_{};
        std::array<VkImageView, kWave2SignalImageCount> signalImageViews_{};
        VkImage environmentImage_ = VK_NULL_HANDLE;
        VkDeviceMemory environmentImageMemory_ = VK_NULL_HANDLE;
        VkImageView environmentImageView_ = VK_NULL_HANDLE;
        VkSampler environmentSampler_ = VK_NULL_HANDLE;

        VkDescriptorSetLayout frameDescriptorSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorPool frameDescriptorPool_ = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount> frameDescriptorSets_{};
        std::array<Hardware::DeviceBuffer, kWave2RuntimeFrameCount> frameBuffers_{};
        std::array<Hardware::DeviceBuffer, kWave2RuntimeFrameCount> counterBuffers_{};
        std::array<Hardware::DeviceBuffer, kWave2RuntimeFrameCount>
            indirectSeedBuffers_{};
        VkQueryPool traceQueryPool_ = VK_NULL_HANDLE;
        std::array<VkShaderModule, kWave2PipelineCount> megakernelShaderModules_{};
        std::array<VkPipelineLayout, kWave2PipelineCount> megakernelPipelineLayouts_{};
        std::array<VkPipeline, kWave2PipelineCount> megakernelPipelines_{};
        std::array<PendingFrame, kWave2RuntimeFrameCount> pendingFrames_{};
        std::array<bool, kWave2RuntimeFrameCount>
            preparedProfilerCounters_{};
        std::array<std::optional<std::size_t>, kWave2RuntimeFrameCount>
            preparedSignalAovIndices_{};
        std::array<std::uint32_t, kWave2RuntimeFrameCount>
            preparedMaximumBounces_{};
        std::array<RuntimeConfig, kWave2RuntimeFrameCount> preparedConfigs_{};
        std::unique_ptr<Wave3DebugRuntime> wave3DebugRuntime_{};

        Scene::CanonicalScene canonicalScene_{};
        Gpu::CanonicalTraversalScene traversal_{};
        L4::FlatBvh flattened_{};
        Hardware::DeviceBuffer constantsBuffer_{};
        Hardware::DeviceBuffer vertexBuffer_{};
        Hardware::DeviceBuffer indexBuffer_{};
        Hardware::DeviceBuffer geometryBuffer_{};
        Hardware::DeviceBuffer instanceBuffer_{};
        Hardware::DeviceBuffer canonicalMaterialBuffer_{};
        Hardware::DeviceBuffer canonicalLightBuffer_{};
        Hardware::DeviceBuffer softwareNodeBuffer_{};
        Hardware::DeviceBuffer softwareTriangleBuffer_{};
        Hardware::DeviceBuffer canonicalLinearTriangleBuffer_{};
        Hardware::DeviceBuffer dummyRayBuffer_{};
        Hardware::DeviceBuffer dummyHitBuffer_{};
        std::array<Hardware::DeviceBuffer, kWave2RuntimeFrameCount>
            softwareCounterBuffers_{};
        Hardware::DeviceBuffer materialBuffer_{};
        Hardware::DeviceBuffer lightBuffer_{};
        Hardware::DeviceBuffer uniformAliasBuffer_{};
        Hardware::DeviceBuffer powerAliasBuffer_{};
        Hardware::DeviceBuffer environmentRowBuffer_{};
        Hardware::DeviceBuffer environmentColumnBuffer_{};
        Hardware::DeviceBuffer uniformPmfBuffer_{};
        Hardware::DeviceBuffer powerPmfBuffer_{};
        Hardware::DeviceBuffer emitterMapBuffer_{};
        Hardware::DeviceBuffer fixtureTriangleBuffer_{};
        Hardware::DeviceBuffer fixtureSphereBuffer_{};

        std::vector<std::vector<Hardware::TriangleGeometryInput>>
            blasGeometryInputs_{};
        std::unique_ptr<Hardware::AccelerationStructureBuilder> asBuilder_;
        std::vector<Hardware::AccelerationStructureResource> blases_{};
        Hardware::AccelerationStructureResource tlas_{};
        Hardware::DeviceBuffer asInstanceBuffer_{};
        Hardware::DeviceBuffer asScratchBuffer_{};
        VkDescriptorPool rayQueryDescriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorPool softwareDescriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorPool canonicalLinearDescriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet canonicalSceneSet_ = VK_NULL_HANDLE;
        VkDescriptorSet rayQueryTraversalSet_ = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            rayQueryReferenceSceneSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            rayQueryReferenceTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            rayQueryWinnerSceneSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            rayQueryWinnerTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            softwareTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            softwareReferenceTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            softwareWinnerTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            canonicalLinearTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            canonicalLinearReferenceTraversalSets_{};
        std::array<VkDescriptorSet, kWave2RuntimeFrameCount>
            canonicalLinearWinnerTraversalSets_{};

        std::array<float, 4u> sceneCenterRadius_{};
        std::uint32_t materialCount_ = 0u;
        std::uint32_t lightCount_ = 0u;
        std::uint32_t fixtureTriangleCount_ = 0u;
        std::uint32_t emitterMapCount_ = 0u;
        std::uint32_t uniformAliasCount_ = 0u;
        std::uint32_t powerAliasCount_ = 0u;
        std::uint32_t environmentLightIndex_ = Mega::kInvalidIndex;
        std::uint32_t environmentWidth_ = 0u;
        std::uint32_t environmentHeight_ = 0u;
        double flattenedSahBuildMilliseconds_ = 0.0;
        double accelerationStructureBuildMilliseconds_ = 0.0;
        std::uint64_t frameGeneration_ = 0u;
        std::uint64_t sceneGeneration_ = 0u;
        std::uint64_t resourceGeneration_ = 0u;
        bool sceneReady_ = false;
    };

    Wave2Runtime::Wave2Runtime() : impl_(std::make_unique<Impl>()) {}
    Wave2Runtime::~Wave2Runtime() = default;
    Wave2Runtime::Wave2Runtime(Wave2Runtime&&) noexcept = default;
    Wave2Runtime& Wave2Runtime::operator=(Wave2Runtime&&) noexcept = default;

    void Wave2Runtime::Create(const Wave2RuntimeCreateInfo& createInfo)
    {
        impl_->Create(createInfo);
    }

    void Wave2Runtime::SetScene(
        const Scene::CanonicalScene& scene,
        const Scene::ExperimentEnvironment* const environment,
        const bool environmentEnabled)
    {
        impl_->SetScene(scene, environment, environmentEnabled);
    }

    void Wave2Runtime::UpdateRigidTransforms(const Scene::CanonicalScene& scene)
    {
        impl_->UpdateRigidTransforms(scene);
    }

    void Wave2Runtime::SetOutput(
        const VkImageView outputImageView,
        const VkExtent2D outputExtent)
    {
        impl_->SetOutput(outputImageView, outputExtent);
    }

    void Wave2Runtime::Reset() noexcept { impl_->Reset(); }
    void Wave2Runtime::InvalidateWave3Histories() noexcept
    {
        impl_->InvalidateWave3Histories();
    }
    void Wave2Runtime::InvalidateProgressiveFilm() noexcept
    {
        impl_->InvalidateProgressiveFilm();
    }
    bool Wave2Runtime::IsReady() const noexcept { return impl_->IsReady(); }
    std::uint64_t Wave2Runtime::ResourceGeneration() const noexcept
    {
        return impl_->ResourceGeneration();
    }
    std::uint64_t Wave2Runtime::SceneGeneration() const noexcept
    {
        return impl_->SceneGeneration();
    }
    bool Wave2Runtime::UsesStagedRaw(
        const ExecutionArchitecture architecture) const noexcept
    {
        return impl_->UsesStagedRaw(architecture);
    }
    VkImage Wave2Runtime::SignalImage(const std::size_t index) const noexcept
    {
        return impl_->SignalImage(index);
    }
    VkImageView Wave2Runtime::SignalImageView(const std::size_t index) const noexcept
    {
        return impl_->SignalImageView(index);
    }

    void Wave2Runtime::UpdateFrame(
        const std::uint32_t frameSlot,
        const RuntimeConfig& config,
        const Wave2RuntimeCamera& camera,
        const std::uint32_t sampleIndex,
        const bool enableProfilerCounters,
        const std::optional<std::size_t> signalAovIndex)
    {
        impl_->UpdateFrame(
            frameSlot,
            config,
            camera,
            sampleIndex,
            enableProfilerCounters,
            signalAovIndex);
    }

    void Wave2Runtime::RecordFrame(
        const VkCommandBuffer commandBuffer,
        const std::uint32_t frameSlot,
        const TraversalBackend backend)
    {
        impl_->RecordFrame(commandBuffer, frameSlot, backend);
    }

    Wave2RuntimeTelemetry Wave2Runtime::CollectCompletedFrame(
        const std::uint32_t frameSlot)
    {
        return impl_->CollectCompletedFrame(frameSlot);
    }
}
