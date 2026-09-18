#include "demos/ManyLightsWave4.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace RenderingEngine::Demos
{
    namespace
    {
        const std::array<ManyLightsWave4Preset, kManyLightsWave4PresetCount>
            kPresets = {
                ManyLightsWave4Preset{
                    ManyLightsWave4PresetId::Lights100,
                    "many-lights-100",
                    "Many Lights (100)",
                    100u
                },
                ManyLightsWave4Preset{
                    ManyLightsWave4PresetId::Lights1000,
                    "many-lights-1000",
                    "Many Lights (1k)",
                    1'000u
                },
                ManyLightsWave4Preset{
                    ManyLightsWave4PresetId::Lights10000,
                    "many-lights-10000",
                    "Many Lights (10k)",
                    10'000u
                }
            };

        const std::array<ManyLightsWave4LegDefinition, kManyLightsWave4LegCount>
            kBiasedLegCatalog = {
                ManyLightsWave4LegDefinition{
                    ManyLightsWave4Technique::Uniform,
                    ManyLightsWave4BiasMode::NotApplicable,
                    "uniform-one-light",
                    "Uniform one-light",
                    false
                },
                ManyLightsWave4LegDefinition{
                    ManyLightsWave4Technique::PowerWeighted,
                    ManyLightsWave4BiasMode::NotApplicable,
                    "power-weighted-one-light",
                    "Power-weighted one-light",
                    false
                },
                ManyLightsWave4LegDefinition{
                    ManyLightsWave4Technique::RestirDi,
                    ManyLightsWave4BiasMode::Biased,
                    "restir-di-biased",
                    "ReSTIR DI (biased)",
                    false
                },
                ManyLightsWave4LegDefinition{
                    ManyLightsWave4Technique::HighSppReference,
                    ManyLightsWave4BiasMode::NotApplicable,
                    "cpu-reference-high-spp",
                    "CPU reference (high SPP)",
                    true
                }
            };

        const std::array<ManyLightsWave4LegDefinition, kManyLightsWave4LegCount>
            kUnbiasedLegCatalog = {
                kBiasedLegCatalog[0],
                kBiasedLegCatalog[1],
                ManyLightsWave4LegDefinition{
                    ManyLightsWave4Technique::RestirDi,
                    ManyLightsWave4BiasMode::ReferenceCorrection,
                    "restir-di-reference-correction",
                    "ReSTIR DI (reference correction)",
                    false
                },
                kBiasedLegCatalog[3]
            };

        [[nodiscard]] bool IsValid(
            const ManyLightsWave4SnapshotState state) noexcept
        {
            switch (state)
            {
            case ManyLightsWave4SnapshotState::Unavailable:
            case ManyLightsWave4SnapshotState::Pending:
            case ManyLightsWave4SnapshotState::Fresh:
            case ManyLightsWave4SnapshotState::Stale:
            case ManyLightsWave4SnapshotState::Invalid:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsValid(
            const ManyLightsWave4ProvenanceSource source) noexcept
        {
            switch (source)
            {
            case ManyLightsWave4ProvenanceSource::LiveRuntime:
            case ManyLightsWave4ProvenanceSource::ImportedArtifact:
            case ManyLightsWave4ProvenanceSource::SyntheticTest:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsValidComparisonBias(
            const ManyLightsWave4BiasMode bias) noexcept
        {
            return bias == ManyLightsWave4BiasMode::Biased
                || bias == ManyLightsWave4BiasMode::ReferenceCorrection;
        }

        [[nodiscard]] bool IsHighSppReference(
            const ManyLightsWave4LegDefinition& leg) noexcept
        {
            return leg.technique == ManyLightsWave4Technique::HighSppReference;
        }

        [[nodiscard]] std::span<const ManyLightsWave4LegDefinition> LegCatalog(
            const ManyLightsWave4BiasMode bias) noexcept
        {
            if (bias == ManyLightsWave4BiasMode::Biased)
            {
                return kBiasedLegCatalog;
            }
            if (bias == ManyLightsWave4BiasMode::ReferenceCorrection)
            {
                return kUnbiasedLegCatalog;
            }
            return {};
        }

        [[nodiscard]] bool IsFiniteNonNegative(const double value) noexcept
        {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus Fail(
            const ManyLightsWave4ValidationCode code,
            std::string reason)
        {
            return { code, std::move(reason) };
        }

        [[nodiscard]] bool SameGeneration(
            const ManyLightsWave4GenerationTuple& left,
            const ManyLightsWave4GenerationTuple& right) noexcept
        {
            return left.frameGeneration == right.frameGeneration
                && left.configGeneration == right.configGeneration
                && left.sceneGeneration == right.sceneGeneration
                && left.resourceGeneration == right.resourceGeneration
                && left.lightGeneration == right.lightGeneration;
        }

        [[nodiscard]] bool SameFixed(
            const ManyLightsWave4FixedConditions& left,
            const ManyLightsWave4FixedConditions& right) noexcept
        {
            return left.sceneFingerprint == right.sceneFingerprint
                && left.assetFingerprint == right.assetFingerprint
                && left.configFingerprint == right.configFingerprint
                && left.machineFingerprint == right.machineFingerprint
                && left.driverFingerprint == right.driverFingerprint
                && left.powerProfileToken == right.powerProfileToken
                && left.cameraPresetToken == right.cameraPresetToken
                && left.width == right.width
                && left.height == right.height
                && left.baseSeed == right.baseSeed
                && left.frameIndex == right.frameIndex
                && SameGeneration(left.generation, right.generation);
        }

        [[nodiscard]] bool SameBudget(
            const ManyLightsWave4Budget& left,
            const ManyLightsWave4Budget& right) noexcept
        {
            return left.identityToken == right.identityToken
                && left.candidateCount == right.candidateCount
                && left.visibilityRayCount == right.visibilityRayCount;
        }

        [[nodiscard]] const ManyLightsWave4Preset* FindPreset(
            const ManyLightsWave4Preset& requested) noexcept
        {
            const auto found = std::find_if(
                kPresets.begin(),
                kPresets.end(),
                [&requested](const ManyLightsWave4Preset& preset)
                {
                    return preset.id == requested.id
                        && preset.stableToken == requested.stableToken
                        && preset.label == requested.label
                        && preset.lightCount == requested.lightCount;
                });
            return found == kPresets.end() ? nullptr : &*found;
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateFixed(
            const ManyLightsWave4FixedConditions& fixed)
        {
            if (fixed.sceneFingerprint.empty()
                || fixed.assetFingerprint.empty()
                || fixed.configFingerprint.empty()
                || fixed.machineFingerprint.empty()
                || fixed.driverFingerprint.empty()
                || fixed.powerProfileToken.empty()
                || fixed.cameraPresetToken.empty()
                || fixed.width == 0u
                || fixed.height == 0u
                || !fixed.baseSeed.has_value()
                || !fixed.frameIndex.has_value()
                || !fixed.generation.frameGeneration.has_value()
                || !fixed.generation.configGeneration.has_value()
                || !fixed.generation.sceneGeneration.has_value()
                || !fixed.generation.resourceGeneration.has_value()
                || !fixed.generation.lightGeneration.has_value())
            {
                return Fail(
                    ManyLightsWave4ValidationCode::MissingField,
                    "fixed scene/asset/config/machine/driver/power identity, camera, extent, seed, frame, and all generations are required");
            }
            return {};
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateBudget(
            const ManyLightsWave4Budget& budget)
        {
            if (budget.identityToken.empty()
                || !budget.candidateCount.has_value()
                || !budget.visibilityRayCount.has_value())
            {
                return Fail(
                    ManyLightsWave4ValidationCode::MissingField,
                    "candidate and visibility budgets require an identity and both counts");
            }
            if (*budget.candidateCount == 0u || *budget.visibilityRayCount == 0u)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "candidate and visibility budgets must be greater than zero");
            }
            return {};
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateRequest(
            const ManyLightsWave4ComparisonRequest& request)
        {
            if (FindPreset(request.preset) == nullptr)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidPreset,
                    "request preset is not one of the immutable 100/1000/10000 presets");
            }
            const ManyLightsWave4ValidationStatus fixed = ValidateFixed(request.fixed);
            if (!fixed.Accepted())
            {
                return fixed;
            }
            const ManyLightsWave4ValidationStatus budget = ValidateBudget(request.budget);
            if (!budget.Accepted())
            {
                return budget;
            }
            const ManyLightsWave4ValidationStatus referenceBudget =
                ValidateBudget(request.referenceBudget);
            if (!referenceBudget.Accepted())
            {
                return referenceBudget;
            }
            if (!IsValidComparisonBias(request.restirBias)
                || SameBudget(request.budget, request.referenceBudget))
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "ReSTIR bias must be explicit and the high-SPP reference budget must be independent");
            }

            const std::span<const ManyLightsWave4LegDefinition> catalog =
                LegCatalog(request.restirBias);
            std::set<std::string_view> tokens;
            for (std::size_t index = 0u; index < request.legs.size(); ++index)
            {
                const ManyLightsWave4LegDefinition& leg = request.legs[index];
                if (!tokens.insert(leg.stableToken).second)
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::DuplicateLeg,
                        "comparison leg stable tokens must be unique");
                }
                if (catalog.size() != request.legs.size()
                    || leg.stableToken != catalog[index].stableToken
                    || leg.technique != catalog[index].technique
                    || leg.bias != catalog[index].bias
                    || leg.label != catalog[index].label
                    || leg.highSppReference != catalog[index].highSppReference)
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::InvalidLegCatalog,
                        "comparison legs must use the complete stable Wave 4 catalog");
                }
            }
            return {};
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateReservoir(
            const ManyLightsWave4ReservoirObservation& reservoir)
        {
            if (!reservoir.m.has_value()
                || !reservoir.weight.has_value()
                || !reservoir.lightId.has_value()
                || reservoir.source.empty()
                || !reservoir.reuseSource.has_value()
                || reservoir.reuseSource->empty()
                || !reservoir.rejectionReason.has_value())
            {
                return Fail(
                    ManyLightsWave4ValidationCode::MissingField,
                    "reservoir M, weight, light ID, source, reuse source, and rejection reason are required");
            }
            if (*reservoir.m == 0u || !IsFiniteNonNegative(*reservoir.weight))
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "reservoir M must be non-zero and weight must be finite and non-negative");
            }
            return {};
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateMetrics(
            const ManyLightsWave4LegSnapshot& leg)
        {
            if (leg.leg.technique == ManyLightsWave4Technique::RestirDi)
            {
                const ManyLightsWave4ValidationStatus reservoir =
                    ValidateReservoir(leg.reservoir);
                if (!reservoir.Accepted())
                {
                    return reservoir;
                }
            }
            else if (leg.reservoir.m.has_value()
                || leg.reservoir.weight.has_value()
                || leg.reservoir.lightId.has_value()
                || !leg.reservoir.source.empty()
                || leg.reservoir.reuseSource.has_value()
                || leg.reservoir.rejectionReason.has_value())
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "non-ReSTIR comparison legs must not fabricate reservoir telemetry");
            }
            const bool highSppReference = IsHighSppReference(leg.leg);
            const bool hasAnyQuality = leg.quality.mae.has_value()
                || leg.quality.rmse.has_value()
                || leg.quality.psnr.has_value();
            if (highSppReference && hasAnyQuality)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "the high-SPP oracle must not publish quality metrics against itself");
            }
            if (!highSppReference
                && (!leg.quality.mae.has_value()
                    || !leg.quality.rmse.has_value()
                    || !leg.quality.psnr.has_value()))
            {
                return Fail(
                    ManyLightsWave4ValidationCode::MissingField,
                    "real-time comparison legs require MAE, RMSE, and PSNR against the high-SPP oracle");
            }
            if (!leg.performance.elapsedMilliseconds.has_value()
                || !leg.performance.timingSource.has_value()
                || !leg.performance.observedCandidates.has_value()
                || !leg.performance.visibilityRays.has_value()
                || !leg.performance.memoryBytes.has_value()
                || !leg.benchmark.warmupFrameCount.has_value()
                || !leg.benchmark.measurementFrameCount.has_value()
                || !leg.benchmark.repeatCount.has_value()
                || !leg.benchmark.medianMilliseconds.has_value()
                || !leg.benchmark.p95Milliseconds.has_value()
                || !leg.benchmark.timingSource.has_value())
            {
                return Fail(
                    ManyLightsWave4ValidationCode::MissingField,
                    "performance, quality, and warm-up/median/P95 measurements are required");
            }
            const ManyLightsWave4TimingSource expectedTiming =
                highSppReference
                    ? ManyLightsWave4TimingSource::CpuWallClock
                    : ManyLightsWave4TimingSource::VulkanGpuTimestamp;
            if (!IsFiniteNonNegative(*leg.performance.elapsedMilliseconds)
                || (!highSppReference
                    && (!IsFiniteNonNegative(*leg.quality.mae)
                        || !IsFiniteNonNegative(*leg.quality.rmse)
                        || !std::isfinite(*leg.quality.psnr)))
                || !IsFiniteNonNegative(*leg.benchmark.medianMilliseconds)
                || !IsFiniteNonNegative(*leg.benchmark.p95Milliseconds)
                || *leg.benchmark.p95Milliseconds
                    < *leg.benchmark.medianMilliseconds
                || *leg.performance.memoryBytes == 0u
                || *leg.benchmark.warmupFrameCount == 0u
                || *leg.benchmark.measurementFrameCount == 0u
                || *leg.benchmark.repeatCount == 0u
                || (!highSppReference
                    && (*leg.benchmark.warmupFrameCount
                            != kManyLightsWave4RealtimeWarmupFrames
                        || *leg.benchmark.measurementFrameCount
                            != kManyLightsWave4RealtimeMeasurementFrames
                        || *leg.benchmark.repeatCount
                            < kManyLightsWave4RealtimeMinimumRepeats))
                || *leg.performance.timingSource != expectedTiming
                || *leg.benchmark.timingSource != expectedTiming
                || *leg.performance.observedCandidates
                    != *leg.budget.candidateCount
                || *leg.performance.visibilityRays
                    > *leg.budget.visibilityRayCount)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "measurements must be finite/non-zero where required, consume the declared candidate count, remain within the ray budget, use the correct GPU/CPU timing provenance, keep P95 at or above median, and use the canonical realtime cadence");
            }
            return {};
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateSnapshotShape(
            const ManyLightsWave4ProviderSnapshot& snapshot)
        {
            if (snapshot.providerId.empty()
                || snapshot.provenance.providerId.empty()
                || snapshot.provenance.detail.empty()
                || snapshot.presetToken.empty()
                || snapshot.lightCount == 0u)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::MissingField,
                    "provider, provenance, preset, and light-count identity are required");
            }
            if (snapshot.providerId != snapshot.provenance.providerId)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidProvenance,
                    "snapshot and provenance provider identities must match exactly");
            }
            if (!IsValid(snapshot.state)
                || !IsValid(snapshot.provenance.source))
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidProvenance,
                    "provider snapshot state or provenance is invalid");
            }
            const ManyLightsWave4ValidationStatus fixed = ValidateFixed(snapshot.fixed);
            if (!fixed.Accepted())
            {
                return fixed;
            }
            const ManyLightsWave4ValidationStatus budget = ValidateBudget(snapshot.budget);
            if (!budget.Accepted())
            {
                return budget;
            }
            const ManyLightsWave4ValidationStatus referenceBudget =
                ValidateBudget(snapshot.referenceBudget);
            if (!referenceBudget.Accepted())
            {
                return referenceBudget;
            }
            if (!IsValidComparisonBias(snapshot.restirBias)
                || SameBudget(snapshot.budget, snapshot.referenceBudget))
            {
                return Fail(
                    ManyLightsWave4ValidationCode::InvalidMeasurement,
                    "provider snapshot requires an explicit ReSTIR bias and an independent reference budget");
            }
            const auto preset = std::find_if(
                kPresets.begin(),
                kPresets.end(),
                [&snapshot](const ManyLightsWave4Preset& candidate)
                {
                    return candidate.stableToken == snapshot.presetToken
                        && candidate.lightCount == snapshot.lightCount;
                });
            if (preset == kPresets.end())
            {
                return Fail(
                    ManyLightsWave4ValidationCode::LightCountMismatch,
                    "provider snapshot light count does not match an immutable preset");
            }
            if (snapshot.provenance.source == ManyLightsWave4ProvenanceSource::SyntheticTest)
            {
                return Fail(
                    ManyLightsWave4ValidationCode::SyntheticProvider,
                    "synthetic provider snapshots cannot be promoted to Wave 4 evidence");
            }
            return {};
        }

        [[nodiscard]] ManyLightsWave4ValidationStatus ValidateFreshSnapshot(
            const ManyLightsWave4ProviderSnapshot& snapshot)
        {
            if (snapshot.state != ManyLightsWave4SnapshotState::Fresh)
            {
                return {};
            }
            const std::span<const ManyLightsWave4LegDefinition> catalog =
                LegCatalog(snapshot.restirBias);
            for (std::size_t index = 0u; index < snapshot.legs.size(); ++index)
            {
                const ManyLightsWave4LegSnapshot& leg = snapshot.legs[index];
                if (leg.state != ManyLightsWave4SnapshotState::Fresh)
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::UnavailableProvider,
                        "a fresh provider snapshot cannot contain a pending or unavailable leg");
                }
                if (catalog.size() != snapshot.legs.size()
                    || leg.leg.stableToken != catalog[index].stableToken
                    || leg.leg.technique != catalog[index].technique
                    || leg.leg.bias != catalog[index].bias
                    || leg.leg.label != catalog[index].label
                    || leg.leg.highSppReference
                        != catalog[index].highSppReference)
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::InvalidLegCatalog,
                        "provider leg identity does not match the stable Wave 4 catalog");
                }
                if (leg.lightCount != snapshot.lightCount)
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::LightCountMismatch,
                        "provider leg light count differs from the frame snapshot");
                }
                if (!SameFixed(leg.fixed, snapshot.fixed))
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::GenerationMismatch,
                        "provider leg fixed camera/seed/frame/generation differs from the frame snapshot");
                }
                const ManyLightsWave4Budget& expectedBudget =
                    IsHighSppReference(leg.leg)
                        ? snapshot.referenceBudget : snapshot.budget;
                if (!SameBudget(leg.budget, expectedBudget))
                {
                    return Fail(
                        ManyLightsWave4ValidationCode::BudgetMismatch,
                        "provider leg candidate/visibility budget differs from the frame snapshot");
                }
                const ManyLightsWave4ValidationStatus metrics = ValidateMetrics(leg);
                if (!metrics.Accepted())
                {
                    return metrics;
                }
            }
            return {};
        }

        [[nodiscard]] std::string JsonEscape(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size() + 2u);
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': result.append("\\\""); break;
                case '\\': result.append("\\\\"); break;
                case '\b': result.append("\\b"); break;
                case '\f': result.append("\\f"); break;
                case '\n': result.append("\\n"); break;
                case '\r': result.append("\\r"); break;
                case '\t': result.append("\\t"); break;
                default:
                    if (character < 0x20u)
                    {
                        std::ostringstream escaped;
                        escaped << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << static_cast<unsigned int>(character);
                        result.append(escaped.str());
                    }
                    else
                    {
                        result.push_back(static_cast<char>(character));
                    }
                    break;
                }
            }
            return result;
        }

        [[nodiscard]] std::string FormatDouble(const double value)
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(17) << value;
            return output.str();
        }

        void AppendCsvField(std::string& output, const std::string_view value)
        {
            bool quote = false;
            for (const char character : value)
            {
                quote = quote || character == ',' || character == '"'
                    || character == '\r' || character == '\n';
            }
            if (!quote)
            {
                output.append(value);
                return;
            }
            output.push_back('"');
            for (const char character : value)
            {
                if (character == '"')
                {
                    output.push_back('"');
                }
                output.push_back(character);
            }
            output.push_back('"');
        }

        void AppendCsvField(std::string& output, const std::uint64_t value)
        {
            AppendCsvField(output, std::to_string(value));
        }

        void AppendCsvField(std::string& output, const std::uint32_t value)
        {
            AppendCsvField(output, std::to_string(value));
        }

        void AppendCsvField(std::string& output, const double value)
        {
            AppendCsvField(output, FormatDouble(value));
        }

        template <typename Value>
        void AppendCsvValue(std::string& output, const Value& value, bool& first)
        {
            if (!first)
            {
                output.push_back(',');
            }
            first = false;
            AppendCsvField(output, value);
        }

        void AppendJsonString(std::string& output, const std::string_view value)
        {
            output.push_back('"');
            output.append(JsonEscape(value));
            output.push_back('"');
        }

        void AppendJsonKey(std::string& output, const std::string_view key)
        {
            AppendJsonString(output, key);
            output.push_back(':');
        }

        void AppendJsonGeneration(
            std::string& output,
            const ManyLightsWave4GenerationTuple& generation)
        {
            output.push_back('{');
            AppendJsonKey(output, "frame_generation");
            output.append(std::to_string(*generation.frameGeneration));
            output.push_back(',');
            AppendJsonKey(output, "config_generation");
            output.append(std::to_string(*generation.configGeneration));
            output.push_back(',');
            AppendJsonKey(output, "scene_generation");
            output.append(std::to_string(*generation.sceneGeneration));
            output.push_back(',');
            AppendJsonKey(output, "resource_generation");
            output.append(std::to_string(*generation.resourceGeneration));
            output.push_back(',');
            AppendJsonKey(output, "light_generation");
            output.append(std::to_string(*generation.lightGeneration));
            output.push_back('}');
        }

        void AppendJsonLeg(
            std::string& output,
            const ManyLightsWave4LegSnapshot& leg)
        {
            output.push_back('{');
            AppendJsonKey(output, "stable_token");
            AppendJsonString(output, leg.leg.stableToken);
            output.push_back(',');
            AppendJsonKey(output, "label");
            AppendJsonString(output, leg.leg.label);
            output.push_back(',');
            AppendJsonKey(output, "technique");
            AppendJsonString(output, ManyLightsWave4TechniqueToken(leg.leg.technique));
            output.push_back(',');
            AppendJsonKey(output, "bias");
            AppendJsonString(output, ManyLightsWave4BiasToken(leg.leg.bias));
            output.push_back(',');
            AppendJsonKey(output, "state");
            AppendJsonString(output, ManyLightsWave4SnapshotStateToken(leg.state));
            output.push_back(',');
            AppendJsonKey(output, "reservoir");
            if (leg.leg.technique == ManyLightsWave4Technique::RestirDi)
            {
                output.push_back('{');
                AppendJsonKey(output, "m");
                output.append(std::to_string(*leg.reservoir.m));
                output.push_back(',');
                AppendJsonKey(output, "weight");
                output.append(FormatDouble(*leg.reservoir.weight));
                output.push_back(',');
                AppendJsonKey(output, "light_id");
                output.append(std::to_string(*leg.reservoir.lightId));
                output.push_back(',');
                AppendJsonKey(output, "source");
                AppendJsonString(output, leg.reservoir.source);
                output.push_back(',');
                AppendJsonKey(output, "reuse_source");
                AppendJsonString(output, *leg.reservoir.reuseSource);
                output.push_back(',');
                AppendJsonKey(output, "rejection_reason");
                AppendJsonString(output, *leg.reservoir.rejectionReason);
                output.push_back('}');
            }
            else
            {
                output.append("null");
            }
            output.push_back(',');
            AppendJsonKey(output, "performance");
            output.push_back('{');
            AppendJsonKey(output, "timing_source");
            AppendJsonString(output, ManyLightsWave4TimingSourceToken(
                *leg.performance.timingSource));
            output.push_back(',');
            AppendJsonKey(output, "elapsed_ms");
            output.append(FormatDouble(*leg.performance.elapsedMilliseconds));
            output.push_back(',');
            AppendJsonKey(output, "observed_candidates");
            output.append(std::to_string(*leg.performance.observedCandidates));
            output.push_back(',');
            AppendJsonKey(output, "visibility_rays");
            output.append(std::to_string(*leg.performance.visibilityRays));
            output.push_back(',');
            AppendJsonKey(output, "memory_bytes");
            output.append(std::to_string(*leg.performance.memoryBytes));
            output.push_back('}');
            output.push_back(',');
            AppendJsonKey(output, "quality");
            if (IsHighSppReference(leg.leg))
            {
                output.append("null");
            }
            else
            {
                output.push_back('{');
                AppendJsonKey(output, "mae");
                output.append(FormatDouble(*leg.quality.mae));
                output.push_back(',');
                AppendJsonKey(output, "rmse");
                output.append(FormatDouble(*leg.quality.rmse));
                output.push_back(',');
                AppendJsonKey(output, "psnr");
                output.append(FormatDouble(*leg.quality.psnr));
                output.push_back('}');
            }
            output.push_back(',');
            AppendJsonKey(output, "benchmark");
            output.push_back('{');
            AppendJsonKey(output, "warmup_frames");
            output.append(std::to_string(*leg.benchmark.warmupFrameCount));
            output.push_back(',');
            AppendJsonKey(output, "measurement_frames");
            output.append(std::to_string(*leg.benchmark.measurementFrameCount));
            output.push_back(',');
            AppendJsonKey(output, "repeat_count");
            output.append(std::to_string(*leg.benchmark.repeatCount));
            output.push_back(',');
            AppendJsonKey(output, "timing_source");
            AppendJsonString(output, ManyLightsWave4TimingSourceToken(
                *leg.benchmark.timingSource));
            output.push_back(',');
            AppendJsonKey(output, "median_ms");
            output.append(FormatDouble(*leg.benchmark.medianMilliseconds));
            output.push_back(',');
            AppendJsonKey(output, "p95_ms");
            output.append(FormatDouble(*leg.benchmark.p95Milliseconds));
            output.push_back('}');
            output.push_back('}');
        }
    }

    std::span<const ManyLightsWave4Preset> GetManyLightsWave4Presets() noexcept
    {
        return kPresets;
    }

    std::span<const ManyLightsWave4LegDefinition> GetManyLightsWave4LegCatalog(
        const ManyLightsWave4BiasMode restirBias) noexcept
    {
        return LegCatalog(restirBias);
    }

    ManyLightsWave4RequestResult BuildManyLightsWave4ComparisonRequest(
        const ManyLightsWave4Preset& preset,
        ManyLightsWave4FixedConditions fixed,
        const ManyLightsWave4BiasMode restirBias,
        ManyLightsWave4Budget budget,
        ManyLightsWave4Budget referenceBudget)
    {
        ManyLightsWave4RequestResult result;
        result.request.preset = preset;
        result.request.fixed = std::move(fixed);
        result.request.restirBias = restirBias;
        result.request.budget = std::move(budget);
        result.request.referenceBudget = std::move(referenceBudget);
        const std::span<const ManyLightsWave4LegDefinition> catalog =
            LegCatalog(restirBias);
        if (catalog.size() == result.request.legs.size())
        {
            std::copy(catalog.begin(), catalog.end(), result.request.legs.begin());
        }
        const ManyLightsWave4ValidationStatus status = ValidateRequest(result.request);
        result.code = status.code;
        result.reason = status.reason;
        return result;
    }

    ManyLightsWave4ValidationStatus ValidateManyLightsWave4ProviderSnapshot(
        const ManyLightsWave4ProviderSnapshot& snapshot)
    {
        const ManyLightsWave4ValidationStatus shape = ValidateSnapshotShape(snapshot);
        if (!shape.Accepted())
        {
            return shape;
        }
        if (snapshot.state == ManyLightsWave4SnapshotState::Invalid)
        {
            return Fail(
                ManyLightsWave4ValidationCode::UnavailableProvider,
                snapshot.reason.empty()
                    ? "provider marked the Many Lights snapshot invalid"
                    : snapshot.reason);
        }
        if (snapshot.state == ManyLightsWave4SnapshotState::Stale
            || snapshot.state == ManyLightsWave4SnapshotState::Unavailable
            || snapshot.state == ManyLightsWave4SnapshotState::Pending)
        {
            // A pending/stale/unavailable frame is safe to retain for UI
            // diagnostics, but is never a valid comparison result.  Keeping
            // this status accepted lets the adapter publish an explicit
            // fail-closed state without fabricating numeric measurements.
            ManyLightsWave4ValidationStatus result;
            result.reason = snapshot.reason.empty()
                ? (snapshot.state == ManyLightsWave4SnapshotState::Stale
                    ? "provider snapshot is stale after a generation change"
                    : "provider has not published a fresh Many Lights snapshot")
                : snapshot.reason;
            return result;
        }
        return ValidateFreshSnapshot(snapshot);
    }

    ManyLightsWave4ComparisonResult BuildManyLightsWave4Comparison(
        const ManyLightsWave4ComparisonRequest& request,
        const ManyLightsWave4ProviderSnapshot& snapshot)
    {
        ManyLightsWave4ComparisonResult result;
        result.request = request;
        result.snapshot = snapshot;
        const ManyLightsWave4ValidationStatus requestStatus = ValidateRequest(request);
        if (!requestStatus.Accepted())
        {
            result.code = requestStatus.code;
            result.reason = requestStatus.reason;
            return result;
        }
        const ManyLightsWave4ValidationStatus snapshotStatus =
            ValidateManyLightsWave4ProviderSnapshot(snapshot);
        if (!snapshotStatus.Accepted())
        {
            result.code = snapshotStatus.code;
            result.reason = snapshotStatus.reason;
            return result;
        }
        if (snapshot.state != ManyLightsWave4SnapshotState::Fresh)
        {
            result.code = snapshot.state == ManyLightsWave4SnapshotState::Stale
                ? ManyLightsWave4ValidationCode::StaleProvider
                : ManyLightsWave4ValidationCode::UnavailableProvider;
            result.reason = snapshotStatus.reason.empty()
                ? "comparison requires a fresh provider snapshot"
                : snapshotStatus.reason;
            return result;
        }
        if (snapshot.presetToken != request.preset.stableToken
            || snapshot.lightCount != request.preset.lightCount)
        {
            result.code = ManyLightsWave4ValidationCode::LightCountMismatch;
            result.reason =
                "provider preset/light count does not match the requested immutable preset";
            return result;
        }
        if (!SameFixed(snapshot.fixed, request.fixed))
        {
            result.code = ManyLightsWave4ValidationCode::GenerationMismatch;
            result.reason =
                "provider fixed camera/seed/frame/generation does not match the request";
            return result;
        }
        if (snapshot.restirBias != request.restirBias)
        {
            result.code = ManyLightsWave4ValidationCode::InvalidLegCatalog;
            result.reason = "provider ReSTIR bias mode does not match the request";
            return result;
        }
        if (!SameBudget(snapshot.budget, request.budget)
            || !SameBudget(snapshot.referenceBudget, request.referenceBudget))
        {
            result.code = ManyLightsWave4ValidationCode::BudgetMismatch;
            result.reason =
                "provider real-time or high-SPP reference budget does not match the request";
            return result;
        }
        for (std::size_t index = 0u; index < request.legs.size(); ++index)
        {
            const ManyLightsWave4LegSnapshot& leg = snapshot.legs[index];
            const ManyLightsWave4LegDefinition& expected = request.legs[index];
            if (leg.leg.stableToken != expected.stableToken
                || leg.leg.technique != expected.technique
                || leg.leg.bias != expected.bias
                || leg.leg.label != expected.label
                || leg.leg.highSppReference != expected.highSppReference)
            {
                result.code = ManyLightsWave4ValidationCode::InvalidLegCatalog;
                result.reason = "provider leg order or identity does not match the request";
                return result;
            }
        }
        result.code = ManyLightsWave4ValidationCode::Accepted;
        result.reason.clear();
        return result;
    }

    ManyLightsWave4SerializedResult BuildManyLightsWave4Json(
        const ManyLightsWave4ComparisonResult& comparison)
    {
        ManyLightsWave4SerializedResult result;
        if (!comparison.Accepted())
        {
            result.code = comparison.code;
            result.reason = comparison.reason.empty()
                ? "cannot serialize a rejected Many Lights comparison"
                : comparison.reason;
            return result;
        }
        const ManyLightsWave4ComparisonResult validated =
            BuildManyLightsWave4Comparison(
                comparison.request, comparison.snapshot);
        if (!validated.Accepted())
        {
            result.code = validated.code;
            result.reason = validated.reason.empty()
                ? "comparison changed after validation"
                : validated.reason;
            return result;
        }

        const ManyLightsWave4ProviderSnapshot& snapshot = comparison.snapshot;
        result.text = "{";
        AppendJsonKey(result.text, "schema");
        AppendJsonString(result.text, "l10-many-lights-wave4-v1");
        result.text.push_back(',');
        AppendJsonKey(result.text, "provider_id");
        AppendJsonString(result.text, snapshot.providerId);
        result.text.push_back(',');
        AppendJsonKey(result.text, "provenance");
        AppendJsonString(result.text, ManyLightsWave4ProvenanceToken(
            snapshot.provenance.source));
        result.text.push_back(',');
        AppendJsonKey(result.text, "provenance_provider_id");
        AppendJsonString(result.text, snapshot.provenance.providerId);
        result.text.push_back(',');
        AppendJsonKey(result.text, "provenance_detail");
        AppendJsonString(result.text, snapshot.provenance.detail);
        result.text.push_back(',');
        AppendJsonKey(result.text, "preset");
        AppendJsonString(result.text, snapshot.presetToken);
        result.text.push_back(',');
        AppendJsonKey(result.text, "light_count");
        result.text.append(std::to_string(snapshot.lightCount));
        result.text.push_back(',');
        AppendJsonKey(result.text, "scene_fingerprint");
        AppendJsonString(result.text, snapshot.fixed.sceneFingerprint);
        result.text.push_back(',');
        AppendJsonKey(result.text, "asset_fingerprint");
        AppendJsonString(result.text, snapshot.fixed.assetFingerprint);
        result.text.push_back(',');
        AppendJsonKey(result.text, "config_fingerprint");
        AppendJsonString(result.text, snapshot.fixed.configFingerprint);
        result.text.push_back(',');
        AppendJsonKey(result.text, "machine_fingerprint");
        AppendJsonString(result.text, snapshot.fixed.machineFingerprint);
        result.text.push_back(',');
        AppendJsonKey(result.text, "driver_fingerprint");
        AppendJsonString(result.text, snapshot.fixed.driverFingerprint);
        result.text.push_back(',');
        AppendJsonKey(result.text, "power_profile");
        AppendJsonString(result.text, snapshot.fixed.powerProfileToken);
        result.text.push_back(',');
        AppendJsonKey(result.text, "camera_preset");
        AppendJsonString(result.text, snapshot.fixed.cameraPresetToken);
        result.text.push_back(',');
        AppendJsonKey(result.text, "width");
        result.text.append(std::to_string(snapshot.fixed.width));
        result.text.push_back(',');
        AppendJsonKey(result.text, "height");
        result.text.append(std::to_string(snapshot.fixed.height));
        result.text.push_back(',');
        AppendJsonKey(result.text, "base_seed");
        result.text.append(std::to_string(*snapshot.fixed.baseSeed));
        result.text.push_back(',');
        AppendJsonKey(result.text, "frame_index");
        result.text.append(std::to_string(*snapshot.fixed.frameIndex));
        result.text.push_back(',');
        AppendJsonKey(result.text, "generation");
        AppendJsonGeneration(result.text, snapshot.fixed.generation);
        result.text.push_back(',');
        AppendJsonKey(result.text, "restir_bias");
        AppendJsonString(result.text, ManyLightsWave4BiasToken(snapshot.restirBias));
        result.text.push_back(',');
        AppendJsonKey(result.text, "realtime_budget");
        result.text.push_back('{');
        AppendJsonKey(result.text, "identity");
        AppendJsonString(result.text, snapshot.budget.identityToken);
        result.text.push_back(',');
        AppendJsonKey(result.text, "candidate_count");
        result.text.append(std::to_string(*snapshot.budget.candidateCount));
        result.text.push_back(',');
        AppendJsonKey(result.text, "visibility_ray_count");
        result.text.append(std::to_string(*snapshot.budget.visibilityRayCount));
        result.text.push_back('}');
        result.text.push_back(',');
        AppendJsonKey(result.text, "reference_budget");
        result.text.push_back('{');
        AppendJsonKey(result.text, "identity");
        AppendJsonString(result.text, snapshot.referenceBudget.identityToken);
        result.text.push_back(',');
        AppendJsonKey(result.text, "sample_count");
        result.text.append(std::to_string(*snapshot.referenceBudget.candidateCount));
        result.text.push_back(',');
        AppendJsonKey(result.text, "visibility_ray_count");
        result.text.append(std::to_string(*snapshot.referenceBudget.visibilityRayCount));
        result.text.push_back('}');
        result.text.push_back(',');
        AppendJsonKey(result.text, "legs");
        result.text.push_back('[');
        for (std::size_t index = 0u; index < snapshot.legs.size(); ++index)
        {
            if (index != 0u)
            {
                result.text.push_back(',');
            }
            AppendJsonLeg(result.text, snapshot.legs[index]);
        }
        result.text.append("]}");
        return result;
    }

    ManyLightsWave4SerializedResult BuildManyLightsWave4Csv(
        const ManyLightsWave4ComparisonResult& comparison)
    {
        ManyLightsWave4SerializedResult result;
        if (!comparison.Accepted())
        {
            result.code = comparison.code;
            result.reason = comparison.reason.empty()
                ? "cannot serialize a rejected Many Lights comparison"
                : comparison.reason;
            return result;
        }
        const ManyLightsWave4ComparisonResult validated =
            BuildManyLightsWave4Comparison(
                comparison.request, comparison.snapshot);
        if (!validated.Accepted())
        {
            result.code = validated.code;
            result.reason = validated.reason.empty()
                ? "comparison changed after validation"
                : validated.reason;
            return result;
        }

        result.text =
            "schema,provider_id,provenance,provenance_provider_id,provenance_detail,"
            "preset,light_count,scene_fingerprint,asset_fingerprint,config_fingerprint,"
            "machine_fingerprint,driver_fingerprint,power_profile,"
            "camera_preset,width,height,base_seed,"
            "frame_index,frame_generation,config_generation,scene_generation,"
            "resource_generation,light_generation,restir_bias,realtime_budget_identity,"
            "reference_budget_identity,budget_identity,candidate_count,"
            "visibility_ray_count,leg_token,label,technique,bias,state,m,weight,light_id,"
            "source,reuse_source,rejection_reason,timing_source,elapsed_ms,observed_candidates,"
            "visibility_rays,memory_bytes,"
            "mae,rmse,psnr,warmup_frames,measurement_frames,repeat_count,benchmark_timing_source,"
            "median_ms,p95_ms\r\n";
        const ManyLightsWave4ProviderSnapshot& snapshot = comparison.snapshot;
        for (const ManyLightsWave4LegSnapshot& leg : snapshot.legs)
        {
            bool first = true;
            AppendCsvValue(result.text, "l10-many-lights-wave4-v1", first);
            AppendCsvValue(result.text, snapshot.providerId, first);
            AppendCsvValue(result.text, ManyLightsWave4ProvenanceToken(
                snapshot.provenance.source), first);
            AppendCsvValue(result.text, snapshot.provenance.providerId, first);
            AppendCsvValue(result.text, snapshot.provenance.detail, first);
            AppendCsvValue(result.text, snapshot.presetToken, first);
            AppendCsvValue(result.text, snapshot.lightCount, first);
            AppendCsvValue(result.text, snapshot.fixed.sceneFingerprint, first);
            AppendCsvValue(result.text, snapshot.fixed.assetFingerprint, first);
            AppendCsvValue(result.text, snapshot.fixed.configFingerprint, first);
            AppendCsvValue(result.text, snapshot.fixed.machineFingerprint, first);
            AppendCsvValue(result.text, snapshot.fixed.driverFingerprint, first);
            AppendCsvValue(result.text, snapshot.fixed.powerProfileToken, first);
            AppendCsvValue(result.text, snapshot.fixed.cameraPresetToken, first);
            AppendCsvValue(result.text, snapshot.fixed.width, first);
            AppendCsvValue(result.text, snapshot.fixed.height, first);
            AppendCsvValue(result.text, *snapshot.fixed.baseSeed, first);
            AppendCsvValue(result.text, *snapshot.fixed.frameIndex, first);
            AppendCsvValue(result.text, *snapshot.fixed.generation.frameGeneration, first);
            AppendCsvValue(result.text, *snapshot.fixed.generation.configGeneration, first);
            AppendCsvValue(result.text, *snapshot.fixed.generation.sceneGeneration, first);
            AppendCsvValue(result.text, *snapshot.fixed.generation.resourceGeneration, first);
            AppendCsvValue(result.text, *snapshot.fixed.generation.lightGeneration, first);
            AppendCsvValue(result.text, ManyLightsWave4BiasToken(snapshot.restirBias), first);
            AppendCsvValue(result.text, snapshot.budget.identityToken, first);
            AppendCsvValue(result.text, snapshot.referenceBudget.identityToken, first);
            AppendCsvValue(result.text, leg.budget.identityToken, first);
            AppendCsvValue(result.text, *leg.budget.candidateCount, first);
            AppendCsvValue(result.text, *leg.budget.visibilityRayCount, first);
            AppendCsvValue(result.text, leg.leg.stableToken, first);
            AppendCsvValue(result.text, leg.leg.label, first);
            AppendCsvValue(result.text, ManyLightsWave4TechniqueToken(leg.leg.technique), first);
            AppendCsvValue(result.text, ManyLightsWave4BiasToken(leg.leg.bias), first);
            AppendCsvValue(result.text, ManyLightsWave4SnapshotStateToken(leg.state), first);
            if (leg.leg.technique == ManyLightsWave4Technique::RestirDi)
            {
                AppendCsvValue(result.text, *leg.reservoir.m, first);
                AppendCsvValue(result.text, *leg.reservoir.weight, first);
                AppendCsvValue(result.text, *leg.reservoir.lightId, first);
                AppendCsvValue(result.text, leg.reservoir.source, first);
                AppendCsvValue(result.text, *leg.reservoir.reuseSource, first);
                AppendCsvValue(result.text, *leg.reservoir.rejectionReason, first);
            }
            else
            {
                for (std::uint32_t field = 0u; field < 6u; ++field)
                {
                    AppendCsvValue(result.text, std::string_view{}, first);
                }
            }
            AppendCsvValue(result.text, ManyLightsWave4TimingSourceToken(
                *leg.performance.timingSource), first);
            AppendCsvValue(result.text, *leg.performance.elapsedMilliseconds, first);
            AppendCsvValue(result.text, *leg.performance.observedCandidates, first);
            AppendCsvValue(result.text, *leg.performance.visibilityRays, first);
            AppendCsvValue(result.text, *leg.performance.memoryBytes, first);
            if (IsHighSppReference(leg.leg))
            {
                for (std::uint32_t field = 0u; field < 3u; ++field)
                {
                    AppendCsvValue(result.text, std::string_view{}, first);
                }
            }
            else
            {
                AppendCsvValue(result.text, *leg.quality.mae, first);
                AppendCsvValue(result.text, *leg.quality.rmse, first);
                AppendCsvValue(result.text, *leg.quality.psnr, first);
            }
            AppendCsvValue(result.text, *leg.benchmark.warmupFrameCount, first);
            AppendCsvValue(result.text, *leg.benchmark.measurementFrameCount, first);
            AppendCsvValue(result.text, *leg.benchmark.repeatCount, first);
            AppendCsvValue(result.text, ManyLightsWave4TimingSourceToken(
                *leg.benchmark.timingSource), first);
            AppendCsvValue(result.text, *leg.benchmark.medianMilliseconds, first);
            AppendCsvValue(result.text, *leg.benchmark.p95Milliseconds, first);
            result.text.append("\r\n");
        }
        return result;
    }

    std::string_view ManyLightsWave4TechniqueToken(
        const ManyLightsWave4Technique technique) noexcept
    {
        switch (technique)
        {
        case ManyLightsWave4Technique::Uniform: return "uniform";
        case ManyLightsWave4Technique::PowerWeighted: return "power";
        case ManyLightsWave4Technique::RestirDi: return "restir-di";
        case ManyLightsWave4Technique::HighSppReference: return "cpu-reference-high-spp";
        }
        return "invalid";
    }

    std::string_view ManyLightsWave4BiasToken(
        const ManyLightsWave4BiasMode bias) noexcept
    {
        switch (bias)
        {
        case ManyLightsWave4BiasMode::Biased: return "biased";
        case ManyLightsWave4BiasMode::ReferenceCorrection: return "reference-correction";
        case ManyLightsWave4BiasMode::NotApplicable: return "not-applicable";
        }
        return "invalid";
    }

    std::string_view ManyLightsWave4SnapshotStateToken(
        const ManyLightsWave4SnapshotState state) noexcept
    {
        switch (state)
        {
        case ManyLightsWave4SnapshotState::Unavailable: return "unavailable";
        case ManyLightsWave4SnapshotState::Pending: return "pending";
        case ManyLightsWave4SnapshotState::Fresh: return "fresh";
        case ManyLightsWave4SnapshotState::Stale: return "stale";
        case ManyLightsWave4SnapshotState::Invalid: return "invalid";
        }
        return "invalid";
    }

    std::string_view ManyLightsWave4ProvenanceToken(
        const ManyLightsWave4ProvenanceSource source) noexcept
    {
        switch (source)
        {
        case ManyLightsWave4ProvenanceSource::LiveRuntime: return "live-runtime";
        case ManyLightsWave4ProvenanceSource::ImportedArtifact: return "imported-artifact";
        case ManyLightsWave4ProvenanceSource::SyntheticTest: return "synthetic-test";
        }
        return "invalid";
    }

    std::string_view ManyLightsWave4TimingSourceToken(
        const ManyLightsWave4TimingSource source) noexcept
    {
        switch (source)
        {
        case ManyLightsWave4TimingSource::VulkanGpuTimestamp:
            return "vulkan-gpu-timestamp";
        case ManyLightsWave4TimingSource::CpuWallClock:
            return "cpu-wall-clock";
        }
        return "invalid";
    }
}
