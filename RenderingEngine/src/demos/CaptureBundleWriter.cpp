#ifdef _MSC_VER
// stb_image_write 1.16 uses sprintf internally. Keep the CRT opt-out local to
// this implementation translation unit instead of weakening project warnings.
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "app/CapabilityTable.hpp"
#include "demos/CaptureBundleWriter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <tinyexr.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace RenderingEngine::Demos
{
    namespace
    {
        constexpr std::string_view kLinearImageRelativePath = "captures/image.exr";
        constexpr std::string_view kPreviewImageRelativePath = "captures/preview.png";
        constexpr std::string_view kMetadataRelativePath = "metadata.json";

        struct PngStreamContext
        {
            std::ofstream* stream = nullptr;
            bool writeFailed = false;
        };

        void WritePngBytes(void* contextPointer, void* bytes, int byteCount)
        {
            auto& context = *static_cast<PngStreamContext*>(contextPointer);
            if (context.writeFailed || context.stream == nullptr || byteCount <= 0)
            {
                return;
            }

            context.stream->write(
                static_cast<const char*>(bytes),
                static_cast<std::streamsize>(byteCount));
            context.writeFailed = !*context.stream;
        }

        [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path)
        {
            const std::u8string value = path.generic_u8string();
            return std::string(
                reinterpret_cast<const char*>(value.data()),
                value.size());
        }

        [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view value)
        {
            std::u8string utf8Path;
            utf8Path.reserve(value.size());
            for (const unsigned char byte : value)
            {
                utf8Path.push_back(static_cast<char8_t>(byte));
            }
            return std::filesystem::path(utf8Path);
        }

        [[nodiscard]] bool IsValidUtf8(std::string_view value) noexcept
        {
            std::size_t offset = 0;
            while (offset < value.size())
            {
                const auto lead = static_cast<unsigned char>(value[offset]);
                if (lead <= 0x7fu)
                {
                    ++offset;
                    continue;
                }

                std::size_t sequenceLength = 0;
                std::uint32_t codePoint = 0;
                std::uint32_t minimumCodePoint = 0;
                if (lead >= 0xc2u && lead <= 0xdfu)
                {
                    sequenceLength = 2u;
                    codePoint = lead & 0x1fu;
                    minimumCodePoint = 0x80u;
                }
                else if (lead >= 0xe0u && lead <= 0xefu)
                {
                    sequenceLength = 3u;
                    codePoint = lead & 0x0fu;
                    minimumCodePoint = 0x800u;
                }
                else if (lead >= 0xf0u && lead <= 0xf4u)
                {
                    sequenceLength = 4u;
                    codePoint = lead & 0x07u;
                    minimumCodePoint = 0x10000u;
                }
                else
                {
                    return false;
                }

                if (sequenceLength > value.size() - offset)
                {
                    return false;
                }
                for (std::size_t index = 1u; index < sequenceLength; ++index)
                {
                    const auto continuation = static_cast<unsigned char>(value[offset + index]);
                    if ((continuation & 0xc0u) != 0x80u)
                    {
                        return false;
                    }
                    codePoint = (codePoint << 6u) | (continuation & 0x3fu);
                }
                if (codePoint < minimumCodePoint
                    || codePoint > 0x10ffffu
                    || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
                {
                    return false;
                }
                offset += sequenceLength;
            }
            return true;
        }

        void AppendJsonString(std::string& output, std::string_view value)
        {
            constexpr char hexDigits[] = "0123456789abcdef";
            output.push_back('"');
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': output.append("\\\""); break;
                case '\\': output.append("\\\\"); break;
                case '\b': output.append("\\b"); break;
                case '\f': output.append("\\f"); break;
                case '\n': output.append("\\n"); break;
                case '\r': output.append("\\r"); break;
                case '\t': output.append("\\t"); break;
                default:
                    if (character < 0x20u)
                    {
                        output.append("\\u00");
                        output.push_back(hexDigits[character >> 4u]);
                        output.push_back(hexDigits[character & 0x0fu]);
                    }
                    else
                    {
                        output.push_back(static_cast<char>(character));
                    }
                    break;
                }
            }
            output.push_back('"');
        }

        template <typename Integer>
        void AppendInteger(std::string& output, Integer value)
        {
            std::array<char, 32> buffer{};
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            output.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
        }

        void AppendFloat(std::string& output, float value)
        {
            std::array<char, 64> buffer{};
            const auto result = std::to_chars(
                buffer.data(),
                buffer.data() + buffer.size(),
                value,
                std::chars_format::general,
                std::numeric_limits<float>::max_digits10);
            output.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
        }

        [[nodiscard]] const char* Token(ScenePreset value) noexcept
        {
            switch (value)
            {
            case ScenePreset::BaselineGallery: return "baseline";
            case ScenePreset::IntersectionBvhLab: return "intersection-bvh";
            case ScenePreset::WhittedOpticsRoom: return "whitted-optics";
            case ScenePreset::CornellBox: return "cornell";
            case ScenePreset::GgxMisMaterialLab: return "ggx-mis";
            case ScenePreset::EnvironmentSamplingDome: return "environment-dome";
            case ScenePreset::SponzaTraversalHall: return "sponza";
            case ScenePreset::BackendParityBenchmark: return "backend-parity";
            case ScenePreset::TemporalStabilityCorridor: return "temporal-stability";
            case ScenePreset::ManyLightsRestirArena: return "many-lights";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(TraversalBackend value) noexcept
        {
            switch (value)
            {
            case TraversalBackend::LegacyAnalyticGpu: return "legacy-analytic-gpu";
            case TraversalBackend::CpuBruteForce: return "cpu-brute-force";
            case TraversalBackend::CpuSahBvh: return "cpu-sah";
            case TraversalBackend::GpuFlattenedSahBvh: return "gpu-flattened-sah";
            case TraversalBackend::GpuLbvh: return "gpu-lbvh";
            case TraversalBackend::VulkanRayQuery: return "ray-query";
            case TraversalBackend::VulkanRayTracingPipeline: return "rt-pipeline";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(Integrator value) noexcept
        {
            switch (value)
            {
            case Integrator::Pbr: return "pbr";
            case Integrator::Whitted: return "whitted";
            case Integrator::CpuReferencePathTracer: return "cpu-reference";
            case Integrator::GpuMegakernelPathTracer: return "megakernel";
            case Integrator::GpuWavefrontPathTracer: return "wavefront";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(DirectLightingEstimator value) noexcept
        {
            switch (value)
            {
            case DirectLightingEstimator::LegacyAnalyticDirect: return "legacy-analytic-direct";
            case DirectLightingEstimator::BsdfOnly: return "bsdf-only";
            case DirectLightingEstimator::NextEventEstimation: return "nee";
            case DirectLightingEstimator::MultipleImportanceSampling: return "mis";
            case DirectLightingEstimator::RestirDirectIllumination: return "restir-di";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(LightProposalDistribution value) noexcept
        {
            switch (value)
            {
            case LightProposalDistribution::LegacyAnalyticLights: return "legacy-analytic";
            case LightProposalDistribution::UniformLights: return "uniform";
            case LightProposalDistribution::PowerWeightedLights: return "power";
            case LightProposalDistribution::EnvironmentImportance: return "environment";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(ReconstructionMode value) noexcept
        {
            switch (value)
            {
            case ReconstructionMode::Raw: return "raw";
            case ReconstructionMode::TemporalAccumulation: return "temporal";
            case ReconstructionMode::TemporalFixedAtrous: return "temporal-atrous";
            case ReconstructionMode::Svgf: return "svgf";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(ShadowMethod value) noexcept
        {
            switch (value)
            {
            case ShadowMethod::Pcf: return "pcf";
            case ShadowMethod::Pcss: return "pcss";
            case ShadowMethod::Physical: return "physical";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(DebugView value) noexcept
        {
            switch (value)
            {
            case DebugView::Final: return "final";
            case DebugView::BaseColor: return "base-color";
            case DebugView::Normal: return "normal";
            case DebugView::Roughness: return "roughness";
            case DebugView::Metallic: return "metallic";
            case DebugView::Emissive: return "emissive";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(RuntimeToggle value) noexcept
        {
            switch (value)
            {
            case RuntimeToggle::RendererDefault: return "renderer-default";
            case RuntimeToggle::Enabled: return "on";
            case RuntimeToggle::Disabled: return "off";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(CaptureEvidenceOrigin value) noexcept
        {
            switch (value)
            {
            case CaptureEvidenceOrigin::LiveRuntime: return "live-runtime";
            case CaptureEvidenceOrigin::ImportedArtifact: return "imported-artifact";
            case CaptureEvidenceOrigin::SyntheticTest: return "synthetic-test";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(CaptureEvidenceAvailability value) noexcept
        {
            switch (value)
            {
            case CaptureEvidenceAvailability::Unavailable: return "unavailable";
            case CaptureEvidenceAvailability::Pending: return "pending";
            case CaptureEvidenceAvailability::Fresh: return "fresh";
            case CaptureEvidenceAvailability::Stale: return "stale";
            case CaptureEvidenceAvailability::Invalid: return "invalid";
            }
            return nullptr;
        }

        [[nodiscard]] const char* Token(CapabilityStatus value) noexcept
        {
            switch (value)
            {
            case CapabilityStatus::Supported: return "supported";
            case CapabilityStatus::Unsupported: return "unsupported";
            case CapabilityStatus::InvalidConfiguration: return "invalid-configuration";
            }
            return nullptr;
        }

        [[nodiscard]] bool ValidateRuntimeConfig(const RuntimeConfig& config, std::string& reason)
        {
            if (Token(config.scene) == nullptr
                || Token(config.backend) == nullptr
                || Token(config.integrator) == nullptr
                || Token(config.directLightingEstimator) == nullptr
                || Token(config.lightProposalDistribution) == nullptr
                || Token(config.reconstruction) == nullptr
                || Token(config.debugView) == nullptr
                || Token(config.shadowMethod) == nullptr
                || Token(config.render.vsync) == nullptr
                || Token(config.run.validation) == nullptr)
            {
                reason = "RuntimeConfig contains an enum value outside the frozen contract.";
                return false;
            }

            if (!std::isfinite(config.render.renderScale)
                || !std::isfinite(config.render.exposure)
                || !std::isfinite(config.render.verticalFovDegrees))
            {
                reason = "RuntimeConfig contains a non-finite JSON numeric value.";
                return false;
            }

            const CapabilityDecision decision = CapabilityTable::Evaluate(config);
            if (decision.status == CapabilityStatus::InvalidConfiguration)
            {
                reason = "RuntimeConfig is invalid under runtime-config-v0: ";
                reason.append(decision.reason);
                reason.push_back('.');
                return false;
            }

            if (!IsValidUtf8(config.run.runIdentifier)
                || (config.run.benchmarkPreset.has_value()
                    && !IsValidUtf8(*config.run.benchmarkPreset))
                || !IsValidUtf8(PathToUtf8(config.run.artifactRoot))
                || (config.run.captureDirectory.has_value()
                    && !IsValidUtf8(PathToUtf8(*config.run.captureDirectory)))
                || (config.run.referenceImage.has_value()
                    && !IsValidUtf8(PathToUtf8(*config.run.referenceImage))))
            {
                reason = "RuntimeConfig contains text that is not valid UTF-8.";
                return false;
            }
            return true;
        }

        void AppendOptionalPath(std::string& output, const std::optional<std::filesystem::path>& value)
        {
            if (!value.has_value())
            {
                output.append("null");
                return;
            }
            AppendJsonString(output, PathToUtf8(*value));
        }

        void AppendOptionalString(std::string& output, const std::optional<std::string>& value)
        {
            if (!value.has_value())
            {
                output.append("null");
                return;
            }
            AppendJsonString(output, *value);
        }

        void AppendRuntimeConfig(std::string& output, const RuntimeConfig& config, std::string_view indent)
        {
            const std::string childIndent = std::string(indent) + "  ";
            const std::string grandchildIndent = childIndent + "  ";

            output.append("{\n");
            output.append(childIndent).append("\"version\": ");
            AppendInteger(output, config.version);
            output.append(",\n");
            output.append(childIndent).append("\"scene\": ");
            AppendJsonString(output, Token(config.scene));
            output.append(",\n");
            output.append(childIndent).append("\"backend\": ");
            AppendJsonString(output, Token(config.backend));
            output.append(",\n");
            output.append(childIndent).append("\"integrator\": ");
            AppendJsonString(output, Token(config.integrator));
            output.append(",\n");
            output.append(childIndent).append("\"direct_lighting_estimator\": ");
            AppendJsonString(output, Token(config.directLightingEstimator));
            output.append(",\n");
            output.append(childIndent).append("\"light_proposal_distribution\": ");
            AppendJsonString(output, Token(config.lightProposalDistribution));
            output.append(",\n");
            output.append(childIndent).append("\"reconstruction\": ");
            AppendJsonString(output, Token(config.reconstruction));
            output.append(",\n");
            output.append(childIndent).append("\"debug_view\": ");
            AppendJsonString(output, Token(config.debugView));
            output.append(",\n");
            output.append(childIndent).append("\"shadow_method\": ");
            AppendJsonString(output, Token(config.shadowMethod));
            output.append(",\n");

            output.append(childIndent).append("\"render\": {\n");
            output.append(grandchildIndent).append("\"width\": ");
            AppendInteger(output, config.render.width);
            output.append(",\n");
            output.append(grandchildIndent).append("\"height\": ");
            AppendInteger(output, config.render.height);
            output.append(",\n");
            output.append(grandchildIndent).append("\"render_scale\": ");
            AppendFloat(output, config.render.renderScale);
            output.append(",\n");
            output.append(grandchildIndent).append("\"samples_per_frame\": ");
            AppendInteger(output, config.render.samplesPerFrame);
            output.append(",\n");
            output.append(grandchildIndent).append("\"target_samples_per_pixel\": ");
            AppendInteger(output, config.render.targetSamplesPerPixel);
            output.append(",\n");
            output.append(grandchildIndent).append("\"maximum_bounce\": ");
            AppendInteger(output, config.render.maximumBounce);
            output.append(",\n");
            output.append(grandchildIndent).append("\"base_seed\": ");
            AppendInteger(output, config.render.baseSeed);
            output.append(",\n");
            output.append(grandchildIndent).append("\"exposure\": ");
            AppendFloat(output, config.render.exposure);
            output.append(",\n");
            output.append(grandchildIndent).append("\"vertical_fov_degrees\": ");
            AppendFloat(output, config.render.verticalFovDegrees);
            output.append(",\n");
            output.append(grandchildIndent).append("\"vsync\": ");
            AppendJsonString(output, Token(config.render.vsync));
            output.append("\n").append(childIndent).append("},\n");

            output.append(childIndent).append("\"run\": {\n");
            output.append(grandchildIndent).append("\"frame_limit\": ");
            AppendInteger(output, config.run.frameLimit);
            output.append(",\n");
            output.append(grandchildIndent).append("\"resize_test\": ")
                .append(config.run.resizeTest ? "true" : "false").append(",\n");
            output.append(grandchildIndent).append("\"headless\": ")
                .append(config.run.headless ? "true" : "false").append(",\n");
            output.append(grandchildIndent).append("\"validation\": ");
            AppendJsonString(output, Token(config.run.validation));
            output.append(",\n");
            output.append(grandchildIndent).append("\"capture_directory\": ");
            AppendOptionalPath(output, config.run.captureDirectory);
            output.append(",\n");
            output.append(grandchildIndent).append("\"benchmark_preset\": ");
            AppendOptionalString(output, config.run.benchmarkPreset);
            output.append(",\n");
            output.append(grandchildIndent).append("\"reference_image\": ");
            AppendOptionalPath(output, config.run.referenceImage);
            output.append(",\n");
            output.append(grandchildIndent).append("\"artifact_root\": ");
            AppendJsonString(output, PathToUtf8(config.run.artifactRoot));
            output.append(",\n");
            output.append(grandchildIndent).append("\"run_identifier\": ");
            AppendJsonString(output, config.run.runIdentifier);
            output.append("\n").append(childIndent).append("}\n");
            output.append(indent).append("}");
        }

        void AppendNamedHashes(
            std::string& output,
            const std::vector<CaptureNamedHash>& hashes,
            std::string_view indent)
        {
            output.append("[");
            if (!hashes.empty())
            {
                output.push_back('\n');
                for (std::size_t index = 0; index < hashes.size(); ++index)
                {
                    output.append(indent).append("  {\"name\": ");
                    AppendJsonString(output, hashes[index].name);
                    output.append(", \"hash\": ");
                    AppendJsonString(output, hashes[index].hash);
                    output.push_back('}');
                    output.append(index + 1u == hashes.size() ? "\n" : ",\n");
                }
                output.append(indent);
            }
            output.push_back(']');
        }

        void AppendStringArray(
            std::string& output,
            const std::vector<std::string>& values,
            std::string_view indent)
        {
            output.append("[");
            if (!values.empty())
            {
                output.push_back('\n');
                for (std::size_t index = 0; index < values.size(); ++index)
                {
                    output.append(indent).append("  ");
                    AppendJsonString(output, values[index]);
                    output.append(index + 1u == values.size() ? "\n" : ",\n");
                }
                output.append(indent);
            }
            output.push_back(']');
        }

        [[nodiscard]] std::string BuildMetadataJson(
            const CaptureMetadata& metadata,
            const CaptureImageView& image)
        {
            const CapabilityDecision requestedCapability =
                CapabilityTable::Evaluate(metadata.requestedRuntimeConfig);
            const CapabilityDecision effectiveCapability =
                CapabilityTable::Evaluate(metadata.effectiveRuntimeConfig);
            std::string output;
            output.reserve(4096u);
            output.append("{\n  \"schema_version\": ");
            AppendJsonString(output, metadata.schemaVersion);
            output.append(",\n  \"contract_versions\": [\n");
            for (std::size_t index = 0; index < metadata.contractVersions.size(); ++index)
            {
                const CaptureContractVersion& contract = metadata.contractVersions[index];
                output.append("    {\"name\": ");
                AppendJsonString(output, contract.name);
                output.append(", \"version\": ");
                AppendJsonString(output, contract.version);
                output.push_back('}');
                output.append(index + 1u == metadata.contractVersions.size() ? "\n" : ",\n");
            }
            output.append("  ],\n  \"evidence_identity\": {\n    \"provider_id\": ");
            AppendJsonString(output, metadata.evidenceIdentity.providerId);
            output.append(",\n    \"origin\": ");
            AppendJsonString(output, Token(metadata.evidenceIdentity.origin));
            output.append(",\n    \"availability\": ");
            AppendJsonString(output, Token(metadata.evidenceIdentity.availability));
            output.append(",\n    \"frame_index\": ");
            AppendInteger(output, metadata.evidenceIdentity.frameIndex);
            output.append(",\n    \"sample_index\": ");
            AppendInteger(output, metadata.evidenceIdentity.sampleIndex);
            output.append(",\n    \"config_generation\": ");
            AppendInteger(output, metadata.evidenceIdentity.configGeneration);
            output.append(",\n    \"scene_generation\": ");
            AppendInteger(output, metadata.evidenceIdentity.sceneGeneration);
            output.append(",\n    \"resource_generation\": ");
            AppendInteger(output, metadata.evidenceIdentity.resourceGeneration);
            output.append(",\n    \"reason\": ");
            AppendJsonString(output, metadata.evidenceIdentity.reason);
            output.append("\n  },\n  \"source\": {\n    \"git_commit\": ");
            AppendJsonString(output, metadata.gitCommit);
            output.append(",\n    \"dirty_worktree\": ")
                .append(metadata.dirtyWorktree ? "true" : "false")
                .append("\n  },\n  \"build\": {\n    \"configuration\": ");
            AppendJsonString(output, metadata.executableConfiguration);
            output.append(",\n    \"identity\": ");
            AppendJsonString(output, metadata.buildIdentity);
            output.append("\n  },\n  \"device\": {\n    \"gpu\": ");
            AppendJsonString(output, metadata.gpuName);
            output.append(",\n    \"driver\": ");
            AppendJsonString(output, metadata.driverVersion);
            output.append(",\n    \"vulkan_api\": ");
            AppendJsonString(output, metadata.vulkanApiVersion);
            output.append(",\n    \"vulkan_sdk\": ");
            AppendJsonString(output, metadata.vulkanSdkVersion);
            output.append("\n  },\n  \"scene\": {\n    \"id\": ");
            AppendJsonString(output, metadata.sceneId);
            output.append(",\n    \"generation\": ");
            AppendJsonString(output, metadata.sceneGeneration);
            output.append(",\n    \"hash\": ");
            AppendJsonString(output, metadata.sceneHash);
            output.append(",\n    \"asset_hashes\": ");
            AppendNamedHashes(output, metadata.assetHashes, "    ");
            output.append(",\n    \"camera_preset\": ");
            AppendJsonString(output, metadata.cameraPreset);
            output.append("\n  },\n  \"runtime_config\": {\n    \"requested\": ");
            AppendRuntimeConfig(output, metadata.requestedRuntimeConfig, "    ");
            output.append(",\n    \"effective\": ");
            AppendRuntimeConfig(output, metadata.effectiveRuntimeConfig, "    ");
            output.append("\n  },\n  \"capability\": {\n    \"requested\": {\"status\": ");
            AppendJsonString(output, Token(requestedCapability.status));
            output.append(", \"reason\": ");
            AppendJsonString(output, requestedCapability.reason);
            output.append("},\n    \"effective\": {\"status\": ");
            AppendJsonString(output, Token(effectiveCapability.status));
            output.append(", \"reason\": ");
            AppendJsonString(output, effectiveCapability.reason);
            output.append("}\n  },\n  \"capture\": {\n    \"width\": ");
            AppendInteger(output, image.width);
            output.append(",\n    \"height\": ");
            AppendInteger(output, image.height);
            output.append(",\n    \"linear_format\": \"rgba32f\",\n    \"linear_color_space\": ");
            AppendJsonString(output, metadata.linearColorSpace);
            output.append(",\n    \"preview_format\": \"rgba8\",\n    \"preview_display_transform\": ");
            AppendJsonString(output, metadata.previewDisplayTransform);
            output.append("\n  },\n  \"shader_hashes\": ");
            AppendNamedHashes(output, metadata.shaderHashes, "  ");
            output.append(",\n  \"timestamps\": {\n    \"render_started_utc\": ");
            AppendJsonString(output, metadata.renderStartedAtUtc);
            output.append(",\n    \"render_completed_utc\": ");
            AppendJsonString(output, metadata.renderCompletedAtUtc);
            output.append("\n  },\n  \"artifacts\": {\n    \"requested\": ");
            AppendStringArray(output, metadata.requestedArtifacts, "    ");
            output.append(",\n    \"produced\": ");
            AppendStringArray(output, metadata.requestedArtifacts, "    ");
            output.append("\n  },\n  \"completion\": {\n    \"state\": \"complete\"\n  }\n}\n");
            return output;
        }

        [[nodiscard]] bool IsRequiredStringMissing(const CaptureMetadata& metadata)
        {
            return metadata.schemaVersion.empty()
                || metadata.contractVersions.empty()
                || metadata.gitCommit.empty()
                || metadata.executableConfiguration.empty()
                || metadata.buildIdentity.empty()
                || metadata.gpuName.empty()
                || metadata.driverVersion.empty()
                || metadata.vulkanApiVersion.empty()
                || metadata.vulkanSdkVersion.empty()
                || metadata.sceneId.empty()
                || metadata.sceneGeneration.empty()
                || metadata.sceneHash.empty()
                || metadata.cameraPreset.empty()
                || metadata.renderStartedAtUtc.empty()
                || metadata.renderCompletedAtUtc.empty()
                || metadata.linearColorSpace.empty()
                || metadata.previewDisplayTransform.empty();
        }

        [[nodiscard]] bool IsReservedWindowsPathSegment(std::string_view segment)
        {
            if (segment.empty() || segment.back() == ' ' || segment.back() == '.')
            {
                return true;
            }

            const std::size_t extension = segment.find('.');
            std::string base(segment.substr(0u, extension));
            while (!base.empty() && (base.back() == ' ' || base.back() == '.'))
            {
                base.pop_back();
            }
            std::transform(base.begin(), base.end(), base.begin(), [](unsigned char character)
            {
                return character >= 'a' && character <= 'z'
                    ? static_cast<char>(character - ('a' - 'A'))
                    : static_cast<char>(character);
            });

            if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
                || base == "CONIN$" || base == "CONOUT$")
            {
                return true;
            }
            const bool asciiNumberedDevice = base.size() == 4u
                && (base.starts_with("COM") || base.starts_with("LPT"))
                && base[3] >= '1' && base[3] <= '9';
            const bool superscriptNumberedDevice = base.size() == 5u
                && (base.starts_with("COM") || base.starts_with("LPT"))
                && static_cast<unsigned char>(base[3]) == 0xc2u
                && (static_cast<unsigned char>(base[4]) == 0xb9u
                    || static_cast<unsigned char>(base[4]) == 0xb2u
                    || static_cast<unsigned char>(base[4]) == 0xb3u);
            return asciiNumberedDevice || superscriptNumberedDevice;
        }

        [[nodiscard]] bool AsciiCaseInsensitiveEqual(
            std::string_view left,
            std::string_view right) noexcept
        {
            return left.size() == right.size()
                && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b)
                {
                    const auto fold = [](unsigned char character) noexcept
                    {
                        return character >= 'A' && character <= 'Z'
                            ? static_cast<unsigned char>(character + ('a' - 'A'))
                            : character;
                    };
                    return fold(static_cast<unsigned char>(a))
                        == fold(static_cast<unsigned char>(b));
                });
        }

        [[nodiscard]] bool ParseCanonicalUint64(
            std::string_view text,
            std::uint64_t& value) noexcept
        {
            if (text.empty())
            {
                return false;
            }
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            {
                return false;
            }
            std::array<char, 32> canonical{};
            const auto encoded = std::to_chars(
                canonical.data(), canonical.data() + canonical.size(), value);
            return encoded.ec == std::errc{}
                && std::string_view(canonical.data(), encoded.ptr) == text;
        }

        [[nodiscard]] bool IsValidRelativeArtifactPath(std::string_view value)
        {
            if (value.empty()
                || !IsValidUtf8(value)
                || value.find('\\') != std::string_view::npos
                || value.find(':') != std::string_view::npos
                || std::any_of(value.begin(), value.end(), [](unsigned char character)
                {
                    return character >= 0x80u;
                }))
            {
                return false;
            }

            std::size_t segmentStart = 0u;
            while (segmentStart < value.size())
            {
                const std::size_t separator = value.find('/', segmentStart);
                const std::size_t segmentEnd = separator == std::string_view::npos
                    ? value.size()
                    : separator;
                const std::string_view segment =
                    value.substr(segmentStart, segmentEnd - segmentStart);
                if (IsReservedWindowsPathSegment(segment)
                    || std::any_of(segment.begin(), segment.end(), [](unsigned char character)
                    {
                        return character < 0x20u || character == 0x7fu;
                    }))
                {
                    return false;
                }
                if (separator == std::string_view::npos)
                {
                    break;
                }
                segmentStart = separator + 1u;
            }

            const std::filesystem::path path = PathFromUtf8(value);
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
            {
                return false;
            }
            for (const std::filesystem::path& component : path)
            {
                if (component == "..")
                {
                    return false;
                }
            }
            return PathToUtf8(path.lexically_normal()) == value;
        }

        [[nodiscard]] bool IsAllowedAdditionalArtifactPath(std::string_view value) noexcept
        {
            return value.back() != '/'
                && (value.starts_with("benchmarks/")
                    || value.starts_with("references/")
                    || value.starts_with("logs/")
                    || value.starts_with("reports/"));
        }

        [[nodiscard]] bool ValidateMetadata(
            const CaptureMetadata& metadata,
            std::span<const CaptureAdditionalArtifact> additionalArtifacts,
            const ArtifactLayout& layout,
            const CaptureImageView& image,
            std::string& reason)
        {
            if (IsRequiredStringMissing(metadata))
            {
                reason = "CaptureMetadata is missing a required caller-supplied truth field.";
                return false;
            }
            if (metadata.evidenceIdentity.providerId.empty()
                || !IsValidUtf8(metadata.evidenceIdentity.providerId)
                || !IsValidUtf8(metadata.evidenceIdentity.reason)
                || Token(metadata.evidenceIdentity.origin) == nullptr
                || Token(metadata.evidenceIdentity.availability) == nullptr)
            {
                reason = "Capture evidence identity has invalid provider, origin, availability, or UTF-8 text.";
                return false;
            }
            if (metadata.evidenceIdentity.availability
                    != CaptureEvidenceAvailability::Fresh
                || !metadata.evidenceIdentity.reason.empty())
            {
                reason = "A complete capture bundle requires Fresh evidence with no unavailable/stale reason.";
                return false;
            }
            std::uint64_t sceneGeneration = 0u;
            if (!ParseCanonicalUint64(metadata.sceneGeneration, sceneGeneration)
                || sceneGeneration != metadata.evidenceIdentity.sceneGeneration)
            {
                reason = "Capture metadata scene generation must be the canonical decimal form of the Fresh evidence generation.";
                return false;
            }

            const std::array<std::string_view, 16> requiredUtf8Strings = {
                metadata.schemaVersion,
                metadata.gitCommit,
                metadata.executableConfiguration,
                metadata.buildIdentity,
                metadata.gpuName,
                metadata.driverVersion,
                metadata.vulkanApiVersion,
                metadata.vulkanSdkVersion,
                metadata.sceneId,
                metadata.sceneGeneration,
                metadata.sceneHash,
                metadata.cameraPreset,
                metadata.renderStartedAtUtc,
                metadata.renderCompletedAtUtc,
                metadata.linearColorSpace,
                metadata.previewDisplayTransform
            };
            if (std::any_of(
                    requiredUtf8Strings.begin(),
                    requiredUtf8Strings.end(),
                    [](std::string_view value) { return !IsValidUtf8(value); }))
            {
                reason = "CaptureMetadata contains text that is not valid UTF-8.";
                return false;
            }

            for (const CaptureContractVersion& contract : metadata.contractVersions)
            {
                if (contract.name.empty()
                    || contract.version.empty()
                    || !IsValidUtf8(contract.name)
                    || !IsValidUtf8(contract.version))
                {
                    reason = "Every contract version requires a non-empty UTF-8 name and version.";
                    return false;
                }
            }
            if (metadata.assetHashes.empty())
            {
                reason = "CaptureMetadata requires at least one caller-supplied asset hash.";
                return false;
            }
            for (const CaptureNamedHash& hash : metadata.assetHashes)
            {
                if (hash.name.empty()
                    || hash.hash.empty()
                    || !IsValidUtf8(hash.name)
                    || !IsValidUtf8(hash.hash))
                {
                    reason = "Every asset hash requires a non-empty UTF-8 name and hash.";
                    return false;
                }
            }
            if (metadata.shaderHashes.empty())
            {
                reason = "CaptureMetadata requires at least one caller-supplied shader hash.";
                return false;
            }
            for (const CaptureNamedHash& hash : metadata.shaderHashes)
            {
                if (hash.name.empty()
                    || hash.hash.empty()
                    || !IsValidUtf8(hash.name)
                    || !IsValidUtf8(hash.hash))
                {
                    reason = "Every shader hash requires a non-empty UTF-8 name and hash.";
                    return false;
                }
            }

            const auto hasDuplicateNames = [](const auto& entries)
            {
                std::vector<std::string_view> names;
                names.reserve(entries.size());
                for (const auto& entry : entries)
                {
                    if (std::find(names.begin(), names.end(), entry.name) != names.end())
                    {
                        return true;
                    }
                    names.push_back(entry.name);
                }
                return false;
            };
            if (hasDuplicateNames(metadata.contractVersions)
                || hasDuplicateNames(metadata.assetHashes)
                || hasDuplicateNames(metadata.shaderHashes))
            {
                reason = "Contract, asset, and shader identities must be unique within their category.";
                return false;
            }

            std::vector<std::string_view> requiredArtifacts = {
                kLinearImageRelativePath,
                kPreviewImageRelativePath,
                kMetadataRelativePath
            };
            requiredArtifacts.reserve(requiredArtifacts.size() + additionalArtifacts.size());
            for (std::size_t index = 0; index < additionalArtifacts.size(); ++index)
            {
                const CaptureAdditionalArtifact& artifact = additionalArtifacts[index];
                if (!IsValidRelativeArtifactPath(artifact.relativePath)
                    || !IsAllowedAdditionalArtifactPath(artifact.relativePath))
                {
                    reason = "Additional evidence paths must be normalized ASCII files under benchmarks/, references/, logs/, or reports/.";
                    return false;
                }
                if (std::any_of(
                        requiredArtifacts.begin(),
                        requiredArtifacts.end(),
                        [&artifact](std::string_view existing)
                        {
                            return AsciiCaseInsensitiveEqual(
                                existing,
                                artifact.relativePath);
                        }))
                {
                    reason = "Every produced artifact path must be unique.";
                    return false;
                }
                requiredArtifacts.push_back(artifact.relativePath);
            }
            if (metadata.requestedArtifacts.size() != requiredArtifacts.size())
            {
                reason = "The requested artifact set does not match the files supplied to the showcase writer.";
                return false;
            }
            for (const std::string& artifact : metadata.requestedArtifacts)
            {
                if (!IsValidRelativeArtifactPath(artifact))
                {
                    reason = "Requested artifact paths must be normalized relative ASCII paths using '/'.";
                    return false;
                }
                const auto found = std::find(requiredArtifacts.begin(), requiredArtifacts.end(), artifact);
                if (found == requiredArtifacts.end())
                {
                    reason = "The showcase bundle cannot mark an artifact it does not produce as requested.";
                    return false;
                }
                *found = std::string_view{};
            }

            if (!ValidateRuntimeConfig(metadata.requestedRuntimeConfig, reason)
                || !ValidateRuntimeConfig(metadata.effectiveRuntimeConfig, reason))
            {
                return false;
            }
            const RuntimeConfig& effective = metadata.effectiveRuntimeConfig;
            if (metadata.sceneId != Token(effective.scene))
            {
                reason = "Capture scene identity must match the effective RuntimeConfig scene.";
                return false;
            }
            if (effective.render.width != image.width
                || effective.render.height != image.height)
            {
                reason = "Capture image extent must match the effective RuntimeConfig resolution.";
                return false;
            }
            const std::filesystem::path effectiveRoot =
                effective.run.artifactRoot.lexically_normal();
            const std::string layoutRunIdentifier =
                PathToUtf8(layout.runDirectory.filename());
            if (effectiveRoot != layout.artifactRoot
                || effective.run.runIdentifier != layoutRunIdentifier
                || (effective.run.captureDirectory.has_value()
                    && effective.run.captureDirectory->lexically_normal()
                        != layout.artifactRoot))
            {
                reason = "Effective RuntimeConfig artifact root/run ID/capture root must match the output ArtifactLayout.";
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidateLayout(const ArtifactLayout& layout, std::string& reason)
        {
            if (layout.artifactRoot.empty() || layout.runDirectory.empty())
            {
                reason = "ArtifactLayout artifactRoot or runDirectory is empty.";
                return false;
            }

            const std::filesystem::path normalizedRoot = layout.artifactRoot.lexically_normal();
            const std::filesystem::path normalizedRun = layout.runDirectory.lexically_normal();
            if (layout.artifactRoot != normalizedRoot
                || layout.runDirectory != normalizedRun
                || layout.capturesDirectory != layout.capturesDirectory.lexically_normal()
                || layout.benchmarksDirectory != layout.benchmarksDirectory.lexically_normal()
                || layout.referencesDirectory != layout.referencesDirectory.lexically_normal()
                || layout.logsDirectory != layout.logsDirectory.lexically_normal()
                || layout.metadataFile != layout.metadataFile.lexically_normal())
            {
                reason = "ArtifactLayout paths must already be lexically normalized.";
                return false;
            }
            if (normalizedRun == normalizedRun.root_path()
                || normalizedRun.filename().empty()
                || normalizedRun.filename() == "."
                || normalizedRun.filename() == "..")
            {
                reason = "ArtifactLayout runDirectory is not a safe run-specific directory.";
                return false;
            }
            if (normalizedRun.parent_path() != normalizedRoot)
            {
                reason = "ArtifactLayout runDirectory is not an immediate child of artifactRoot.";
                return false;
            }
            const std::string runIdentifier = PathToUtf8(normalizedRun.filename());
            if (!IsValidUtf8(runIdentifier)
                || NormalizeRunIdentifier(runIdentifier) != runIdentifier
                || IsReservedWindowsPathSegment(runIdentifier))
            {
                reason = "ArtifactLayout runDirectory leaf is not a normalized artifact-layout-v0 run ID.";
                return false;
            }
            if (layout.capturesDirectory.lexically_normal()
                != (normalizedRun / "captures").lexically_normal())
            {
                reason = "ArtifactLayout capturesDirectory does not match artifact-layout-v0.";
                return false;
            }
            if (layout.metadataFile.lexically_normal()
                != (normalizedRun / "metadata.json").lexically_normal())
            {
                reason = "ArtifactLayout metadataFile does not match artifact-layout-v0.";
                return false;
            }
            if (layout.benchmarksDirectory != normalizedRun / "benchmarks"
                || layout.referencesDirectory != normalizedRun / "references"
                || layout.logsDirectory != normalizedRun / "logs")
            {
                reason = "ArtifactLayout auxiliary directories do not match artifact-layout-v0.";
                return false;
            }
            return true;
        }

        [[nodiscard]] CaptureBundleResult MakeResult(
            CaptureBundleError error,
            std::string message,
            const ArtifactLayout& layout)
        {
            CaptureBundleResult result;
            result.error = error;
            result.message = std::move(message);
            result.runDirectory = layout.runDirectory;
            result.linearImageFile = layout.capturesDirectory / "image.exr";
            result.previewImageFile = layout.capturesDirectory / "preview.png";
            result.metadataFile = layout.metadataFile;
            return result;
        }

        [[nodiscard]] CaptureBundleResult MakeFailure(
            CaptureBundleError error,
            std::string message,
            const ArtifactLayout& layout,
            bool ownsRunDirectory)
        {
            CaptureBundleResult result = MakeResult(error, std::move(message), layout);
            if (!ownsRunDirectory)
            {
                return result;
            }

            result.rollbackAttempted = true;
            std::error_code cleanupError;
            std::filesystem::remove_all(layout.runDirectory, cleanupError);
            std::error_code existsError;
            const bool stillExists = std::filesystem::exists(layout.runDirectory, existsError);
            result.rollbackSucceeded = !cleanupError && !existsError && !stillExists;
            if (!result.rollbackSucceeded)
            {
                result.message.append(" Rollback of the newly created run directory failed");
                if (cleanupError)
                {
                    result.message.append(": ").append(cleanupError.message());
                }
                else if (existsError)
                {
                    result.message.append(": ").append(existsError.message());
                }
                else
                {
                    result.message.append(": directory still exists");
                }
                result.message.push_back('.');
            }
            return result;
        }

        [[nodiscard]] bool WriteBytes(
            const std::filesystem::path& file,
            const unsigned char* bytes,
            std::size_t byteCount,
            std::string& reason)
        {
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                reason = "Could not open '" + PathToUtf8(file) + "' for writing.";
                return false;
            }

            std::size_t offset = 0;
            constexpr std::size_t maximumChunk =
                static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());
            while (offset < byteCount)
            {
                const std::size_t chunk = std::min(byteCount - offset, maximumChunk);
                stream.write(
                    reinterpret_cast<const char*>(bytes + offset),
                    static_cast<std::streamsize>(chunk));
                if (!stream)
                {
                    reason = "Writing '" + PathToUtf8(file) + "' failed.";
                    return false;
                }
                offset += chunk;
            }
            stream.close();
            if (!stream)
            {
                reason = "Closing '" + PathToUtf8(file) + "' failed.";
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WriteExr(
            const std::filesystem::path& file,
            const CaptureImageView& source,
            std::string& reason)
        {
            const std::size_t pixelCount =
                static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height);
            std::array<std::vector<float>, 4> channels;
            for (std::vector<float>& channel : channels)
            {
                channel.resize(pixelCount);
            }
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                channels[0][pixel] = source.linearRgba[pixel * 4u + 0u];
                channels[1][pixel] = source.linearRgba[pixel * 4u + 1u];
                channels[2][pixel] = source.linearRgba[pixel * 4u + 2u];
                channels[3][pixel] = source.linearRgba[pixel * 4u + 3u];
            }

            EXRImage image;
            InitEXRImage(&image);
            image.num_channels = 4;
            image.width = static_cast<int>(source.width);
            image.height = static_cast<int>(source.height);

            // TinyEXR expects channels in alphabetical channel-name order.
            std::array<unsigned char*, 4> imagePointers = {
                reinterpret_cast<unsigned char*>(channels[3].data()),
                reinterpret_cast<unsigned char*>(channels[2].data()),
                reinterpret_cast<unsigned char*>(channels[1].data()),
                reinterpret_cast<unsigned char*>(channels[0].data())
            };
            image.images = imagePointers.data();

            EXRHeader header;
            InitEXRHeader(&header);
            header.num_channels = 4;
            header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;

            std::array<EXRChannelInfo, 4> channelInformation{};
            std::memcpy(channelInformation[0].name, "A", 2u);
            std::memcpy(channelInformation[1].name, "B", 2u);
            std::memcpy(channelInformation[2].name, "G", 2u);
            std::memcpy(channelInformation[3].name, "R", 2u);
            header.channels = channelInformation.data();

            std::array<int, 4> pixelTypes{};
            std::array<int, 4> requestedPixelTypes{};
            pixelTypes.fill(TINYEXR_PIXELTYPE_FLOAT);
            requestedPixelTypes.fill(TINYEXR_PIXELTYPE_FLOAT);
            header.pixel_types = pixelTypes.data();
            header.requested_pixel_types = requestedPixelTypes.data();

            unsigned char* encodedBytes = nullptr;
            const char* tinyExrError = nullptr;
            const std::size_t encodedSize = SaveEXRImageToMemory(
                &image,
                &header,
                &encodedBytes,
                &tinyExrError);
            if (encodedSize == 0u || encodedBytes == nullptr)
            {
                reason = "TinyEXR could not encode the linear RGBA32F image";
                if (tinyExrError != nullptr)
                {
                    reason.append(": ").append(tinyExrError);
                    FreeEXRErrorMessage(tinyExrError);
                }
                reason.push_back('.');
                std::free(encodedBytes);
                return false;
            }
            if (tinyExrError != nullptr)
            {
                FreeEXRErrorMessage(tinyExrError);
            }

            const bool writeSucceeded = WriteBytes(file, encodedBytes, encodedSize, reason);
            std::free(encodedBytes);
            return writeSucceeded;
        }

        [[nodiscard]] CaptureBundleError WritePng(
            const std::filesystem::path& file,
            const CaptureImageView& source,
            std::string& reason)
        {
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                reason = "Could not open '" + PathToUtf8(file) + "' for writing.";
                return CaptureBundleError::FileWriteFailed;
            }

            PngStreamContext context{ &stream, false };
            const int encoded = stbi_write_png_to_func(
                &WritePngBytes,
                &context,
                static_cast<int>(source.width),
                static_cast<int>(source.height),
                4,
                source.previewRgba8.data(),
                static_cast<int>(source.width * 4u));
            stream.close();

            if (context.writeFailed || !stream)
            {
                reason = "Writing '" + PathToUtf8(file) + "' failed.";
                return CaptureBundleError::FileWriteFailed;
            }
            if (encoded == 0)
            {
                reason = "stb_image_write could not encode the caller-provided RGBA8 preview.";
                return CaptureBundleError::PngEncodingFailed;
            }
            return CaptureBundleError::None;
        }
    }

    CaptureBundleResult WriteCaptureBundle(
        const ArtifactLayout& layout,
        const CaptureImageView& image,
        const CaptureMetadata& metadata)
    {
        return WriteShowcaseBundle(layout, image, metadata, {});
    }

    CaptureBundleResult WriteShowcaseBundle(
        const ArtifactLayout& layout,
        const CaptureImageView& image,
        const CaptureMetadata& metadata,
        std::span<const CaptureAdditionalArtifact> additionalArtifacts)
    {
        bool ownsRunDirectory = false;
        try
        {
            if (image.width == 0u || image.height == 0u
                || image.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4)
                || image.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            {
                return MakeFailure(
                    CaptureBundleError::InvalidImage,
                    "Capture dimensions must be non-zero and fit TinyEXR/stb integer limits.",
                    layout,
                    false);
            }
            const std::size_t width = static_cast<std::size_t>(image.width);
            const std::size_t height = static_cast<std::size_t>(image.height);
            if (height > std::numeric_limits<std::size_t>::max() / width
                || width * height > std::numeric_limits<std::size_t>::max() / 4u)
            {
                return MakeFailure(
                    CaptureBundleError::InvalidImage,
                    "Capture dimensions overflow the RGBA component count.",
                    layout,
                    false);
            }
            const std::size_t componentCount = width * height * 4u;
            if (image.linearRgba.size() != componentCount
                || image.previewRgba8.size() != componentCount)
            {
                return MakeFailure(
                    CaptureBundleError::InvalidImage,
                    "Both capture buffers must contain exactly width * height * 4 components.",
                    layout,
                    false);
            }

            std::string reason;
            if (!ValidateLayout(layout, reason))
            {
                return MakeFailure(
                    CaptureBundleError::InvalidLayout,
                    std::move(reason),
                    layout,
                    false);
            }
            if (!ValidateMetadata(metadata, additionalArtifacts, layout, image, reason))
            {
                return MakeFailure(
                    CaptureBundleError::InvalidMetadata,
                    std::move(reason),
                    layout,
                    false);
            }

            std::error_code filesystemError;
            const bool runAlreadyExists = std::filesystem::exists(layout.runDirectory, filesystemError);
            if (filesystemError)
            {
                return MakeFailure(
                    CaptureBundleError::DirectoryCreationFailed,
                    "Could not inspect the run directory: " + filesystemError.message() + ".",
                    layout,
                    false);
            }
            if (runAlreadyExists)
            {
                return MakeFailure(
                    CaptureBundleError::RunDirectoryCollision,
                    "The run directory already exists; artifact-layout-v0 forbids overwrite.",
                    layout,
                    false);
            }

            if (!layout.artifactRoot.empty())
            {
                std::filesystem::create_directories(layout.artifactRoot, filesystemError);
                if (filesystemError)
                {
                    return MakeFailure(
                        CaptureBundleError::DirectoryCreationFailed,
                        "Could not create the artifact root: " + filesystemError.message() + ".",
                        layout,
                        false);
                }
            }

            filesystemError.clear();
            const bool createdRunDirectory =
                std::filesystem::create_directory(layout.runDirectory, filesystemError);
            if (!createdRunDirectory)
            {
                if (!filesystemError)
                {
                    return MakeFailure(
                        CaptureBundleError::RunDirectoryCollision,
                        "The run directory was created concurrently; overwrite is forbidden.",
                        layout,
                        false);
                }
                return MakeFailure(
                    CaptureBundleError::DirectoryCreationFailed,
                    "Could not create the run directory: " + filesystemError.message() + ".",
                    layout,
                    false);
            }
            ownsRunDirectory = true;

            filesystemError.clear();
            if (!std::filesystem::create_directory(layout.capturesDirectory, filesystemError)
                || filesystemError)
            {
                const std::string detail = filesystemError
                    ? filesystemError.message()
                    : "the captures path already exists";
                return MakeFailure(
                    CaptureBundleError::DirectoryCreationFailed,
                    "Could not create the captures directory: " + detail + ".",
                    layout,
                    ownsRunDirectory);
            }

            const std::filesystem::path linearImageFile = layout.capturesDirectory / "image.exr";
            const std::filesystem::path previewImageFile = layout.capturesDirectory / "preview.png";
            if (!WriteExr(linearImageFile, image, reason))
            {
                return MakeFailure(
                    reason.starts_with("TinyEXR")
                        ? CaptureBundleError::ExrEncodingFailed
                        : CaptureBundleError::FileWriteFailed,
                    std::move(reason),
                    layout,
                    ownsRunDirectory);
            }

            const CaptureBundleError pngError = WritePng(previewImageFile, image, reason);
            if (pngError != CaptureBundleError::None)
            {
                return MakeFailure(
                    pngError,
                    std::move(reason),
                    layout,
                    ownsRunDirectory);
            }

            std::vector<std::filesystem::path> additionalFiles;
            additionalFiles.reserve(additionalArtifacts.size());
            for (const CaptureAdditionalArtifact& artifact : additionalArtifacts)
            {
                const std::filesystem::path outputFile =
                    layout.runDirectory / PathFromUtf8(artifact.relativePath);
                filesystemError.clear();
                std::filesystem::create_directories(outputFile.parent_path(), filesystemError);
                if (filesystemError)
                {
                    return MakeFailure(
                        CaptureBundleError::DirectoryCreationFailed,
                        "Could not create an evidence directory: "
                            + filesystemError.message() + ".",
                        layout,
                        ownsRunDirectory);
                }
                if (!WriteBytes(outputFile, artifact.bytes.data(), artifact.bytes.size(), reason))
                {
                    return MakeFailure(
                        CaptureBundleError::FileWriteFailed,
                        std::move(reason),
                        layout,
                        ownsRunDirectory);
                }
                additionalFiles.push_back(outputFile);
            }

            // Metadata is deliberately staged only after every requested data file exists.
            const std::string metadataJson = BuildMetadataJson(metadata, image);
            const std::filesystem::path temporaryMetadata =
                layout.runDirectory / ".metadata.json.tmp";
            if (!WriteBytes(
                temporaryMetadata,
                reinterpret_cast<const unsigned char*>(metadataJson.data()),
                metadataJson.size(),
                reason))
            {
                return MakeFailure(
                    CaptureBundleError::FileWriteFailed,
                    std::move(reason),
                    layout,
                    ownsRunDirectory);
            }

            CaptureBundleResult success = MakeResult(CaptureBundleError::None, {}, layout);
            success.additionalFiles = std::move(additionalFiles);
            filesystemError.clear();
            std::filesystem::rename(temporaryMetadata, layout.metadataFile, filesystemError);
            if (filesystemError)
            {
                return MakeFailure(
                    CaptureBundleError::MetadataCommitFailed,
                    "Could not publish metadata.json as the completion marker: "
                        + filesystemError.message() + ".",
                    layout,
                    ownsRunDirectory);
            }

            // No fallible work follows publishing the complete metadata marker.
            return success;
        }
        catch (const std::exception& exception)
        {
            return MakeFailure(
                CaptureBundleError::UnexpectedFailure,
                std::string("Capture bundle writer failed unexpectedly: ") + exception.what(),
                layout,
                ownsRunDirectory);
        }
    }
}
