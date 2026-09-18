#include "demos/ManyLightsWave4.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <locale>
#include <ostream>
#include <string>
#include <string_view>

namespace
{
    using namespace RenderingEngine::Demos;

    class Checks final
    {
    public:
        explicit Checks(std::ostream& output) noexcept
            : output_(output)
        {
        }

        void Expect(const bool condition, const std::string_view message)
        {
            if (!condition)
            {
                output_ << "L10 ManyLightsWave4 test failed: " << message << '\n';
                ++failures_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failures_ == 0u;
        }

    private:
        std::ostream& output_;
        std::size_t failures_ = 0u;
    };

    class CommaDecimalPoint final : public std::numpunct<char>
    {
    protected:
        [[nodiscard]] char do_decimal_point() const override
        {
            return ',';
        }
    };

    [[nodiscard]] std::size_t CsvColumnCount(const std::string_view row)
    {
        std::size_t columns = 1u;
        bool quoted = false;
        for (std::size_t index = 0u; index < row.size(); ++index)
        {
            const char value = row[index];
            if (value == '"')
            {
                if (quoted && index + 1u < row.size() && row[index + 1u] == '"')
                {
                    ++index;
                }
                else
                {
                    quoted = !quoted;
                }
            }
            else if (value == ',' && !quoted)
            {
                ++columns;
            }
        }
        return columns;
    }

    [[nodiscard]] bool CsvHasStableShape(const std::string& csv)
    {
        const std::size_t headerEnd = csv.find("\r\n");
        if (headerEnd == std::string::npos)
        {
            return false;
        }
        const std::size_t expectedColumns = CsvColumnCount(
            std::string_view(csv).substr(0u, headerEnd));
        std::size_t rowStart = headerEnd + 2u;
        std::size_t rowCount = 0u;
        while (rowStart < csv.size())
        {
            const std::size_t rowEnd = csv.find("\r\n", rowStart);
            if (rowEnd == std::string::npos
                || CsvColumnCount(std::string_view(csv).substr(
                    rowStart, rowEnd - rowStart)) != expectedColumns)
            {
                return false;
            }
            ++rowCount;
            rowStart = rowEnd + 2u;
        }
        return rowCount == kManyLightsWave4LegCount;
    }

    [[nodiscard]] ManyLightsWave4FixedConditions MakeFixed(
        const std::uint64_t lightGeneration = 9u)
    {
        ManyLightsWave4FixedConditions fixed;
        fixed.sceneFingerprint = "scene-sha256-many-lights";
        fixed.assetFingerprint = "asset-sha256-many-lights";
        fixed.configFingerprint = "config-sha256-wave4";
        fixed.machineFingerprint = "machine-sha256-test-host";
        fixed.driverFingerprint = "driver-sha256-test-driver";
        fixed.powerProfileToken = "fixed-performance-test";
        fixed.cameraPresetToken = "many-lights-hero";
        fixed.width = 1'920u;
        fixed.height = 1'080u;
        fixed.baseSeed = 1'337u;
        fixed.frameIndex = 42u;
        fixed.generation = { 42u, 7u, 3u, 5u, lightGeneration };
        return fixed;
    }

    [[nodiscard]] ManyLightsWave4Budget MakeBudget()
    {
        ManyLightsWave4Budget budget;
        budget.identityToken = "equal-candidates-64-visibility-32";
        budget.candidateCount = 64u;
        budget.visibilityRayCount = 32u;
        return budget;
    }

    [[nodiscard]] ManyLightsWave4Budget MakeReferenceBudget()
    {
        ManyLightsWave4Budget budget;
        budget.identityToken = "high-spp-4096-reference";
        budget.candidateCount = 4'096u;
        budget.visibilityRayCount = 1'048'576u;
        return budget;
    }

    [[nodiscard]] ManyLightsWave4ProviderSnapshot MakeFreshSnapshot(
        const std::size_t presetIndex = 1u,
        const ManyLightsWave4BiasMode bias = ManyLightsWave4BiasMode::Biased)
    {
        const auto presets = GetManyLightsWave4Presets();
        const auto requestResult = BuildManyLightsWave4ComparisonRequest(
            presets[presetIndex], MakeFixed(), bias,
            MakeBudget(), MakeReferenceBudget());
        ManyLightsWave4ProviderSnapshot snapshot;
        snapshot.providerId = "l9.many-lights.runtime";
        snapshot.provenance = {
            ManyLightsWave4ProvenanceSource::LiveRuntime,
            "l9.many-lights.runtime",
            "provider-owned immutable runtime readback"
        };
        snapshot.state = ManyLightsWave4SnapshotState::Fresh;
        snapshot.presetToken = requestResult.request.preset.stableToken;
        snapshot.lightCount = requestResult.request.preset.lightCount;
        snapshot.fixed = requestResult.request.fixed;
        snapshot.restirBias = requestResult.request.restirBias;
        snapshot.budget = requestResult.request.budget;
        snapshot.referenceBudget = requestResult.request.referenceBudget;
        for (std::size_t index = 0u; index < snapshot.legs.size(); ++index)
        {
            ManyLightsWave4LegSnapshot& leg = snapshot.legs[index];
            leg.leg = requestResult.request.legs[index];
            leg.state = ManyLightsWave4SnapshotState::Fresh;
            leg.lightCount = snapshot.lightCount;
            leg.fixed = snapshot.fixed;
            leg.budget = leg.leg.highSppReference
                ? snapshot.referenceBudget : snapshot.budget;
            if (leg.leg.technique == ManyLightsWave4Technique::RestirDi)
            {
                leg.reservoir.m = 1u + static_cast<std::uint64_t>(index);
                leg.reservoir.weight = 0.5 + static_cast<double>(index);
                leg.reservoir.lightId = 100u + static_cast<std::uint64_t>(index);
                leg.reservoir.source = "l9.reservoir.readback";
                leg.reservoir.reuseSource = "provider:restir.temporal-spatial";
                leg.reservoir.rejectionReason = std::string();
            }
            leg.performance.elapsedMilliseconds = 1.0 + static_cast<double>(index);
            leg.performance.timingSource = leg.leg.highSppReference
                ? ManyLightsWave4TimingSource::CpuWallClock
                : ManyLightsWave4TimingSource::VulkanGpuTimestamp;
            leg.performance.observedCandidates = *leg.budget.candidateCount;
            leg.performance.visibilityRays = *leg.budget.visibilityRayCount;
            leg.performance.memoryBytes = 4096u + index * 64u;
            if (!leg.leg.highSppReference)
            {
                leg.quality.mae = 0.01 + static_cast<double>(index) * 0.001;
                leg.quality.rmse = 0.02 + static_cast<double>(index) * 0.001;
                leg.quality.psnr = 20.0 + static_cast<double>(index);
            }
            leg.benchmark.warmupFrameCount = 120u;
            leg.benchmark.measurementFrameCount = 1'000u;
            leg.benchmark.repeatCount = 3u;
            leg.benchmark.medianMilliseconds = 1.5 + static_cast<double>(index);
            leg.benchmark.p95Milliseconds = 2.0 + static_cast<double>(index);
            leg.benchmark.timingSource = *leg.performance.timingSource;
        }
        return snapshot;
    }

    void TestImmutableCatalog(Checks& checks)
    {
        const auto presets = GetManyLightsWave4Presets();
        checks.Expect(presets.size() == 3u,
            "Many Lights must expose exactly 100/1000/10000 presets");
        checks.Expect(presets[0].lightCount == 100u
                && presets[1].lightCount == 1'000u
                && presets[2].lightCount == 10'000u,
            "preset light counts must remain stable");

        const auto legs = GetManyLightsWave4LegCatalog(
            ManyLightsWave4BiasMode::Biased);
        checks.Expect(legs.size() == 4u,
            "comparison catalog must contain exactly four technique families");
        checks.Expect(legs[0].technique == ManyLightsWave4Technique::Uniform
                && legs[1].technique == ManyLightsWave4Technique::PowerWeighted
                && legs[2].technique == ManyLightsWave4Technique::RestirDi,
            "uniform, power, and ReSTIR DI real-time families must be first");
        checks.Expect(legs[3].highSppReference
                && legs[3].bias == ManyLightsWave4BiasMode::NotApplicable,
            "high-SPP CPU reference must be explicit and not claim a bias mode");
        const auto unbiased = GetManyLightsWave4LegCatalog(
            ManyLightsWave4BiasMode::ReferenceCorrection);
        checks.Expect(unbiased.size() == 4u
                && unbiased[2].stableToken == "restir-di-reference-correction"
                && unbiased[2].bias == ManyLightsWave4BiasMode::ReferenceCorrection,
            "reference-correction validation must be a separately named ReSTIR comparison");
    }

    void TestAllTiersBiasModesAndSerialization(Checks& checks)
    {
        constexpr std::array biases{
            ManyLightsWave4BiasMode::Biased,
            ManyLightsWave4BiasMode::ReferenceCorrection };
        const auto presets = GetManyLightsWave4Presets();
        for (std::size_t presetIndex = 0u;
            presetIndex < presets.size(); ++presetIndex)
        {
            for (const ManyLightsWave4BiasMode bias : biases)
            {
                const ManyLightsWave4ProviderSnapshot snapshot =
                    MakeFreshSnapshot(presetIndex, bias);
                const auto request = BuildManyLightsWave4ComparisonRequest(
                    presets[presetIndex], snapshot.fixed, snapshot.restirBias,
                    snapshot.budget, snapshot.referenceBudget);
                checks.Expect(request.Accepted(),
                    "each tier and ReSTIR bias mode must build a fixed request");
                const ManyLightsWave4ComparisonResult comparison =
                    BuildManyLightsWave4Comparison(request.request, snapshot);
                checks.Expect(comparison.Accepted(),
                    "each tier and ReSTIR bias mode must accept a fresh four-leg snapshot");

                const auto json = BuildManyLightsWave4Json(comparison);
                const auto csv = BuildManyLightsWave4Csv(comparison);
                checks.Expect(json.Accepted() && csv.Accepted(),
                    "accepted comparisons must serialize without disk I/O");
                const std::string expectedRestir = bias
                    == ManyLightsWave4BiasMode::Biased
                        ? "restir-di-biased"
                        : "restir-di-reference-correction";
                checks.Expect(!json.text.empty() && json.text.front() == '{'
                        && json.text.back() == '}'
                        && json.text.find("l10-many-lights-wave4-v1") != std::string::npos
                        && json.text.find("scene_fingerprint") != std::string::npos
                        && json.text.find("machine_fingerprint") != std::string::npos
                        && json.text.find("observed_candidates") != std::string::npos
                        && json.text.find(expectedRestir) != std::string::npos
                        && json.text.find("\"quality\":null") != std::string::npos,
                    "JSON must retain fixed identity, observed budgets, selected bias, and a null reference quality");
                checks.Expect(CsvHasStableShape(csv.text)
                        && csv.text.find("observed_candidates") != std::string::npos
                        && csv.text.find("scene_fingerprint") != std::string::npos,
                    "CSV must contain four rows with one stable column schema");
                checks.Expect(json.text.find(":nan") == std::string::npos
                        && json.text.find(":inf") == std::string::npos
                        && json.text.find(":-inf") == std::string::npos,
                    "serialized measurements must never contain NaN or infinity");
            }
        }

        ManyLightsWave4ProviderSnapshot localeSnapshot = MakeFreshSnapshot();
        const auto localeRequest = BuildManyLightsWave4ComparisonRequest(
            presets[1], localeSnapshot.fixed, localeSnapshot.restirBias,
            localeSnapshot.budget, localeSnapshot.referenceBudget);
        const auto localeComparison = BuildManyLightsWave4Comparison(
            localeRequest.request, localeSnapshot);
        const std::locale previousLocale = std::locale();
        std::locale::global(std::locale(previousLocale, new CommaDecimalPoint));
        const auto localeJson = BuildManyLightsWave4Json(localeComparison);
        std::locale::global(previousLocale);
        checks.Expect(localeJson.Accepted()
                && localeJson.text.find("\"median_ms\":1.5") != std::string::npos,
            "JSON numeric formatting must remain locale-independent");
    }

    void TestFailClosedValidation(Checks& checks)
    {
        ManyLightsWave4ProviderSnapshot missing = MakeFreshSnapshot();
        missing.legs[0].quality.mae.reset();
        checks.Expect(
            BuildManyLightsWave4Comparison(
                BuildManyLightsWave4ComparisonRequest(
                    GetManyLightsWave4Presets()[1], missing.fixed,
                    missing.restirBias, missing.budget,
                    missing.referenceBudget).request,
                missing).code == ManyLightsWave4ValidationCode::MissingField,
            "missing fresh measurements must reject the comparison");

        ManyLightsWave4ProviderSnapshot budgetMismatch = MakeFreshSnapshot();
        budgetMismatch.legs[0].budget.candidateCount = 65u;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(budgetMismatch).code
                == ManyLightsWave4ValidationCode::BudgetMismatch,
            "a leg with a different budget must be rejected");

        ManyLightsWave4ProviderSnapshot observedCandidateMismatch = MakeFreshSnapshot();
        ++*observedCandidateMismatch.legs[0].performance.observedCandidates;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(observedCandidateMismatch).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "observed candidate work must consume the declared budget exactly");

        ManyLightsWave4ProviderSnapshot observedRayMismatch = MakeFreshSnapshot();
        ++*observedRayMismatch.legs[1].performance.visibilityRays;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(observedRayMismatch).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "observed visibility work above the declared ray budget must be rejected");

        ManyLightsWave4ProviderSnapshot observedRayUnderBudget = MakeFreshSnapshot();
        --*observedRayUnderBudget.legs[1].performance.visibilityRays;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(observedRayUnderBudget).Accepted(),
            "a missing winner may leave observed visibility below the declared upper bound");

        ManyLightsWave4ProviderSnapshot referenceIdentity = MakeFreshSnapshot();
        referenceIdentity.legs[3].leg.highSppReference = false;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(referenceIdentity).code
                == ManyLightsWave4ValidationCode::InvalidLegCatalog,
            "the high-SPP reference bit must not override its catalog technique");

        ManyLightsWave4ProviderSnapshot referenceQuality = MakeFreshSnapshot();
        referenceQuality.legs[3].quality.mae = 0.0;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(referenceQuality).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "the high-SPP oracle must retain null quality instead of self-comparison metrics");

        ManyLightsWave4ProviderSnapshot timingMismatch = MakeFreshSnapshot();
        timingMismatch.legs[3].performance.timingSource =
            ManyLightsWave4TimingSource::VulkanGpuTimestamp;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(timingMismatch).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "the high-SPP reference must use CPU wall-clock provenance");

        ManyLightsWave4ProviderSnapshot realtimeCadence = MakeFreshSnapshot();
        realtimeCadence.legs[0].benchmark.warmupFrameCount = 1u;
        realtimeCadence.legs[0].benchmark.measurementFrameCount = 1u;
        realtimeCadence.legs[0].benchmark.repeatCount = 1u;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(realtimeCadence).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "realtime 1/1/1 cadence must not masquerade as accepted performance evidence");

        ManyLightsWave4ProviderSnapshot referenceCadence = MakeFreshSnapshot();
        referenceCadence.legs[3].benchmark.warmupFrameCount = 1u;
        referenceCadence.legs[3].benchmark.measurementFrameCount = 1u;
        referenceCadence.legs[3].benchmark.repeatCount = 1u;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(referenceCadence).Accepted(),
            "the CPU high-SPP oracle may retain a separately explicit non-zero cadence");

        ManyLightsWave4ProviderSnapshot zeroMemory = MakeFreshSnapshot();
        zeroMemory.legs[2].performance.memoryBytes = 0u;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(zeroMemory).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "fresh performance evidence must not claim a measured zero-byte footprint");

        ManyLightsWave4ProviderSnapshot missingIdentity = MakeFreshSnapshot();
        missingIdentity.fixed.sceneFingerprint.clear();
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(missingIdentity).code
                == ManyLightsWave4ValidationCode::MissingField,
            "scene/asset/config/machine/driver/power/extent identity must fail closed when incomplete");

        ManyLightsWave4ProviderSnapshot missingMachine = MakeFreshSnapshot();
        missingMachine.fixed.machineFingerprint.clear();
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(missingMachine).code
                == ManyLightsWave4ValidationCode::MissingField,
            "performance acceptance requires a fixed machine fingerprint");

        ManyLightsWave4ProviderSnapshot providerMismatch = MakeFreshSnapshot();
        providerMismatch.provenance.providerId = "different.provider";
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(providerMismatch).code
                == ManyLightsWave4ValidationCode::InvalidProvenance,
            "snapshot and provenance provider identities must match");

        ManyLightsWave4ProviderSnapshot synthetic = MakeFreshSnapshot();
        synthetic.provenance.source = ManyLightsWave4ProvenanceSource::SyntheticTest;
        checks.Expect(
            ValidateManyLightsWave4ProviderSnapshot(synthetic).code
                == ManyLightsWave4ValidationCode::SyntheticProvider,
            "synthetic snapshots must not be promoted as live evidence");

        ManyLightsWave4ProviderSnapshot stale = MakeFreshSnapshot();
        stale.state = ManyLightsWave4SnapshotState::Stale;
        const auto staleRequest = BuildManyLightsWave4ComparisonRequest(
            GetManyLightsWave4Presets()[1], stale.fixed, stale.restirBias,
            stale.budget, stale.referenceBudget);
        checks.Expect(
            BuildManyLightsWave4Comparison(staleRequest.request, stale).code
                == ManyLightsWave4ValidationCode::StaleProvider,
            "stale snapshots may be displayed but must not become comparison evidence");

        ManyLightsWave4ProviderSnapshot mutatedSnapshot = MakeFreshSnapshot();
        const auto mutationRequest = BuildManyLightsWave4ComparisonRequest(
            GetManyLightsWave4Presets()[1], mutatedSnapshot.fixed,
            mutatedSnapshot.restirBias, mutatedSnapshot.budget,
            mutatedSnapshot.referenceBudget);
        ManyLightsWave4ComparisonResult mutated = BuildManyLightsWave4Comparison(
            mutationRequest.request, mutatedSnapshot);
        ++*mutated.snapshot.legs[0].performance.observedCandidates;
        checks.Expect(
            BuildManyLightsWave4Json(mutated).code
                == ManyLightsWave4ValidationCode::InvalidMeasurement
                && BuildManyLightsWave4Csv(mutated).code
                    == ManyLightsWave4ValidationCode::InvalidMeasurement,
            "serializers must revalidate a comparison that changed after acceptance");

        const auto malformedRequest = BuildManyLightsWave4ComparisonRequest(
            GetManyLightsWave4Presets()[1], MakeFixed(),
            ManyLightsWave4BiasMode::Biased, ManyLightsWave4Budget{},
            MakeReferenceBudget());
        checks.Expect(malformedRequest.code == ManyLightsWave4ValidationCode::MissingField,
            "missing common budget fields must fail closed");
    }
}

bool RunManyLightsWave4Tests(std::ostream& output)
{
    Checks checks(output);
    TestImmutableCatalog(checks);
    TestAllTiersBiasModesAndSerialization(checks);
    TestFailClosedValidation(checks);
    if (checks.Passed())
    {
        output << "L10 Many Lights Wave 4 snapshot/comparison checks passed.\n";
    }
    return checks.Passed();
}
