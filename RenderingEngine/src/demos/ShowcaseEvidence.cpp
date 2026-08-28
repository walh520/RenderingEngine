#include "demos/ShowcaseEvidence.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace RenderingEngine::Demos
{
    namespace
    {
        using CanonicalConditions = std::vector<std::pair<std::string, std::string>>;

        struct MetricKey
        {
            EvidenceSampleKind kind = EvidenceSampleKind::Timing;
            std::string scope;
            std::string unit;

            [[nodiscard]] auto AsTuple() const noexcept
            {
                return std::tie(kind, scope, unit);
            }

            [[nodiscard]] bool operator<(const MetricKey& other) const noexcept
            {
                return AsTuple() < other.AsTuple();
            }
        };

        [[nodiscard]] bool IsKnownKind(EvidenceSampleKind kind) noexcept
        {
            switch (kind)
            {
            case EvidenceSampleKind::Timing:
            case EvidenceSampleKind::Counter:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnownSource(EvidenceSource source) noexcept
        {
            return !EvidenceSourceToken(source).empty();
        }

        [[nodiscard]] bool IsKnownMeasurementMethod(
            EvidenceMeasurementMethod method) noexcept
        {
            return method != EvidenceMeasurementMethod::Unspecified
                && !EvidenceMeasurementMethodToken(method).empty();
        }

        [[nodiscard]] EvidenceStatus ValidateMeasurementRequirement(
            const EvidenceMetricRequirement& metric)
        {
            if (!IsKnownMeasurementMethod(metric.measurementMethod))
            {
                return { EvidenceError::InvalidPlan,
                    "Every required metric must declare its measurement method." };
            }
            if (metric.kind == EvidenceSampleKind::Timing
                && metric.measurementMethod != EvidenceMeasurementMethod::CpuWallClock
                && metric.measurementMethod
                    != EvidenceMeasurementMethod::GpuTimestampQuery)
            {
                return { EvidenceError::InvalidPlan,
                    "Timing metrics require cpu-wall-clock or gpu-timestamp-query." };
            }
            if (metric.kind == EvidenceSampleKind::Counter
                && metric.measurementMethod
                    != EvidenceMeasurementMethod::ProviderCounter)
            {
                return { EvidenceError::InvalidPlan,
                    "Counter metrics require the provider-counter measurement method." };
            }
            return {};
        }

        [[nodiscard]] CanonicalConditions CanonicalizeConditions(
            std::span<const EvidenceCondition> conditions)
        {
            CanonicalConditions canonical;
            canonical.reserve(conditions.size());
            for (const EvidenceCondition& condition : conditions)
            {
                canonical.emplace_back(condition.name, condition.value);
            }
            std::sort(canonical.begin(), canonical.end());
            return canonical;
        }

        [[nodiscard]] EvidenceStatus ValidateConditions(
            std::span<const EvidenceCondition> conditions)
        {
            if (conditions.empty())
            {
                return { EvidenceError::InvalidPlan,
                    "Benchmark conditions must contain at least one named value." };
            }

            std::set<std::string> names;
            for (const EvidenceCondition& condition : conditions)
            {
                if (condition.name.empty() || condition.value.empty())
                {
                    return { EvidenceError::InvalidPlan,
                        "Benchmark condition names and values must not be empty." };
                }
                if (!names.insert(condition.name).second)
                {
                    return { EvidenceError::InvalidPlan,
                        "Benchmark condition names must be unique." };
                }
            }
            return {};
        }

        [[nodiscard]] EvidenceStatus ValidatePlan(const BenchmarkPlan& plan)
        {
            if (plan.measurementFrameCount == 0u)
            {
                return { EvidenceError::InvalidPlan,
                    "measurementFrameCount must be greater than zero." };
            }
            if (plan.repeatCount == 0u)
            {
                return { EvidenceError::InvalidPlan,
                    "repeatCount must be greater than zero." };
            }
            if (plan.requiredMetrics.empty())
            {
                return { EvidenceError::InvalidPlan,
                    "At least one required metric must be declared." };
            }

            const EvidenceStatus conditionStatus = ValidateConditions(plan.conditions);
            if (!conditionStatus)
            {
                return conditionStatus;
            }

            std::set<MetricKey> metricKeys;
            std::map<std::pair<EvidenceSampleKind, std::string>, std::string> unitsByScope;
            for (const EvidenceMetricRequirement& metric : plan.requiredMetrics)
            {
                if (!IsKnownKind(metric.kind) || metric.scope.empty() || metric.unit.empty())
                {
                    return { EvidenceError::InvalidPlan,
                        "Required metrics need a known kind plus non-empty scope and unit." };
                }

                const EvidenceStatus measurementStatus =
                    ValidateMeasurementRequirement(metric);
                if (!measurementStatus)
                {
                    return measurementStatus;
                }

                const MetricKey key = { metric.kind, metric.scope, metric.unit };
                if (!metricKeys.insert(key).second)
                {
                    return { EvidenceError::InvalidPlan,
                        "Required metric keys must be unique." };
                }

                const auto scopeKey = std::make_pair(metric.kind, metric.scope);
                const auto [iterator, inserted] = unitsByScope.emplace(scopeKey, metric.unit);
                if (!inserted && iterator->second != metric.unit)
                {
                    return { EvidenceError::InvalidPlan,
                        "A metric scope cannot use multiple units in one benchmark." };
                }
            }

            const std::size_t measurements = plan.measurementFrameCount;
            if (measurements > std::numeric_limits<std::size_t>::max() / plan.repeatCount)
            {
                return { EvidenceError::InvalidPlan,
                    "The requested measurement count exceeds addressable size." };
            }
            const std::size_t samplesPerMetric = measurements * plan.repeatCount;
            if (samplesPerMetric > std::numeric_limits<std::size_t>::max()
                    / plan.requiredMetrics.size())
            {
                return { EvidenceError::InvalidPlan,
                    "The requested sample count exceeds addressable size." };
            }
            return {};
        }

        [[nodiscard]] EvidenceStatus ValidateSample(
            const EvidenceSample& sample,
            const BenchmarkPlan& plan)
        {
            if (!IsKnownKind(sample.kind) || sample.scope.empty() || sample.unit.empty())
            {
                return { EvidenceError::InvalidSample,
                    "Samples need a known kind plus non-empty scope and unit." };
            }
            if (!std::isfinite(sample.value))
            {
                return { EvidenceError::NonFiniteValue,
                    "Sample values must be finite." };
            }
            if (sample.kind == EvidenceSampleKind::Timing && sample.value < 0.0)
            {
                return { EvidenceError::InvalidSample,
                    "Timing samples must not be negative." };
            }
            if (!IsKnownSource(sample.provenance.source)
                || sample.provenance.provider.empty())
            {
                return { EvidenceError::InvalidProvenance,
                    "Every sample needs a known source and a non-empty provider name." };
            }
            if (sample.configGeneration != plan.configGeneration)
            {
                return { EvidenceError::GenerationMismatch,
                    "Sample config generation does not match the benchmark plan." };
            }
            if (sample.sceneGeneration != plan.sceneGeneration)
            {
                return { EvidenceError::GenerationMismatch,
                    "Sample scene generation does not match the benchmark plan." };
            }
            if (sample.resourceGeneration != plan.resourceGeneration)
            {
                return { EvidenceError::GenerationMismatch,
                    "Sample resource generation does not match the benchmark plan." };
            }
            if (CanonicalizeConditions(sample.conditions)
                != CanonicalizeConditions(plan.conditions))
            {
                return { EvidenceError::ConditionsMismatch,
                    "Sample conditions do not match the benchmark plan." };
            }
            if (sample.repeatIndex >= plan.repeatCount)
            {
                return { EvidenceError::InvalidSample,
                    "Sample repeat index is outside the benchmark plan." };
            }
            return {};
        }

        [[nodiscard]] MetricKey MakeMetricKey(const EvidenceSample& sample)
        {
            return { sample.kind, sample.scope, sample.unit };
        }

        [[nodiscard]] MetricKey MakeMetricKey(const EvidenceMetricRequirement& metric)
        {
            return { metric.kind, metric.scope, metric.unit };
        }

        [[nodiscard]] const EvidenceMetricRequirement* FindRequirement(
            const BenchmarkPlan& plan,
            const MetricKey& key)
        {
            const auto iterator = std::find_if(
                plan.requiredMetrics.begin(),
                plan.requiredMetrics.end(),
                [&key](const EvidenceMetricRequirement& metric)
                {
                    return MakeMetricKey(metric).AsTuple() == key.AsTuple();
                });
            return iterator == plan.requiredMetrics.end() ? nullptr : &*iterator;
        }

        [[nodiscard]] EvidenceStatus ValidateSampleMeasurementSource(
            const EvidenceSample& sample,
            const EvidenceMetricRequirement& requirement)
        {
            if (requirement.measurementMethod
                    == EvidenceMeasurementMethod::GpuTimestampQuery
                && sample.provenance.source == EvidenceSource::CpuWallClock)
            {
                return { EvidenceError::InvalidProvenance,
                    "A gpu-timestamp-query timing cannot use CPU wall-clock provenance." };
            }
            if (requirement.measurementMethod
                    == EvidenceMeasurementMethod::ProviderCounter
                && sample.provenance.source == EvidenceSource::CpuWallClock)
            {
                return { EvidenceError::InvalidProvenance,
                    "A provider counter cannot use CPU wall-clock provenance." };
            }
            return {};
        }

        [[nodiscard]] std::string EscapeConditionComponent(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '\\': escaped.append("\\\\"); break;
                case '=': escaped.append("\\="); break;
                case ';': escaped.append("\\;"); break;
                case '\r': escaped.append("\\r"); break;
                case '\n': escaped.append("\\n"); break;
                default: escaped.push_back(character); break;
                }
            }
            return escaped;
        }

        [[nodiscard]] std::string SerializeConditions(
            std::span<const EvidenceCondition> conditions)
        {
            const CanonicalConditions canonical = CanonicalizeConditions(conditions);
            std::string text;
            for (std::size_t index = 0; index < canonical.size(); ++index)
            {
                if (index != 0u)
                {
                    text.push_back(';');
                }
                text.append(EscapeConditionComponent(canonical[index].first));
                text.push_back('=');
                text.append(EscapeConditionComponent(canonical[index].second));
            }
            return text;
        }

        void AppendCsvField(std::string& output, std::string_view value)
        {
            const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string_view::npos;
            if (!needsQuotes)
            {
                output.append(value);
                return;
            }

            output.push_back('"');
            for (const char character : value)
            {
                if (character == '"')
                {
                    output.append("\"\"");
                }
                else
                {
                    output.push_back(character);
                }
            }
            output.push_back('"');
        }

        template <typename Integer>
        void AppendInteger(std::string& output, Integer value)
        {
            char buffer[32]{};
            const auto conversion = std::to_chars(
                std::begin(buffer), std::end(buffer), value);
            output.append(buffer, conversion.ptr);
        }

        void AppendDouble(std::string& output, double value)
        {
            if (value == 0.0)
            {
                output.push_back('0');
                return;
            }

            char buffer[64]{};
            const auto conversion = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value,
                std::chars_format::general,
                std::numeric_limits<double>::max_digits10);
            output.append(buffer, conversion.ptr);
        }

        [[nodiscard]] CsvResult BuildCsv(
            std::span<const EvidenceSample> samples,
            EvidenceSampleKind kind,
            const BenchmarkPlan& plan)
        {
            const ScopeStatisticsResult validation =
                ComputeScopeStatistics(samples, kind, plan);
            if (!validation)
            {
                CsvResult result;
                result.error = validation.error;
                result.message = validation.message;
                return result;
            }

            std::vector<const EvidenceSample*> rows;
            for (const EvidenceSample& sample : samples)
            {
                if (sample.kind == kind)
                {
                    rows.push_back(&sample);
                }
            }
            std::sort(
                rows.begin(),
                rows.end(),
                [](const EvidenceSample* left, const EvidenceSample* right)
                {
                    return std::make_tuple(
                            left->scope,
                            left->unit,
                            left->repeatIndex,
                            left->frameIndex,
                            left->sampleIndex,
                            left->configGeneration,
                            left->sceneGeneration,
                            left->resourceGeneration,
                            SerializeConditions(left->conditions),
                            left->provenance.source,
                            left->provenance.provider,
                            left->provenance.detail,
                            left->value)
                        < std::make_tuple(
                            right->scope,
                            right->unit,
                            right->repeatIndex,
                            right->frameIndex,
                            right->sampleIndex,
                            right->configGeneration,
                            right->sceneGeneration,
                            right->resourceGeneration,
                            SerializeConditions(right->conditions),
                            right->provenance.source,
                            right->provenance.provider,
                            right->provenance.detail,
                            right->value);
                });

            CsvResult result;
            result.text = "scope,unit,measurement_method,value,repeat_index,frame_index,sample_index,config_generation,scene_generation,resource_generation,conditions,source,provider,detail\r\n";
            for (const EvidenceSample* sample : rows)
            {
                const EvidenceMetricRequirement* const requirement =
                    FindRequirement(plan, MakeMetricKey(*sample));
                AppendCsvField(result.text, sample->scope);
                result.text.push_back(',');
                AppendCsvField(result.text, sample->unit);
                result.text.push_back(',');
                AppendCsvField(result.text,
                    EvidenceMeasurementMethodToken(requirement->measurementMethod));
                result.text.push_back(',');
                AppendDouble(result.text, sample->value);
                result.text.push_back(',');
                AppendInteger(result.text, sample->repeatIndex);
                result.text.push_back(',');
                AppendInteger(result.text, sample->frameIndex);
                result.text.push_back(',');
                AppendInteger(result.text, sample->sampleIndex);
                result.text.push_back(',');
                AppendInteger(result.text, sample->configGeneration);
                result.text.push_back(',');
                AppendInteger(result.text, sample->sceneGeneration);
                result.text.push_back(',');
                AppendInteger(result.text, sample->resourceGeneration);
                result.text.push_back(',');
                AppendCsvField(result.text, SerializeConditions(sample->conditions));
                result.text.push_back(',');
                AppendCsvField(result.text, EvidenceSourceToken(sample->provenance.source));
                result.text.push_back(',');
                AppendCsvField(result.text, sample->provenance.provider);
                result.text.push_back(',');
                AppendCsvField(result.text, sample->provenance.detail);
                result.text.append("\r\n");
            }
            return result;
        }

        [[nodiscard]] bool TryGetExpectedComponentCount(
            const LinearRgbaImageView& image,
            std::size_t& componentCount) noexcept
        {
            if (image.width == 0u || image.height == 0u)
            {
                return false;
            }

            const std::size_t width = image.width;
            const std::size_t height = image.height;
            if (width > std::numeric_limits<std::size_t>::max() / height)
            {
                return false;
            }
            const std::size_t pixelCount = width * height;
            if (pixelCount > std::numeric_limits<std::size_t>::max() / 4u)
            {
                return false;
            }
            componentCount = pixelCount * 4u;
            return image.rgba.size() == componentCount;
        }
    }

    std::string_view EvidenceSourceToken(EvidenceSource source) noexcept
    {
        switch (source)
        {
        case EvidenceSource::CpuWallClock: return "cpu-wall-clock";
        case EvidenceSource::ProviderReported: return "provider-reported";
        case EvidenceSource::SyntheticTest: return "synthetic-test";
        }
        return {};
    }

    std::string_view EvidenceMeasurementMethodToken(
        EvidenceMeasurementMethod method) noexcept
    {
        switch (method)
        {
        case EvidenceMeasurementMethod::CpuWallClock: return "cpu-wall-clock";
        case EvidenceMeasurementMethod::GpuTimestampQuery: return "gpu-timestamp-query";
        case EvidenceMeasurementMethod::ProviderCounter: return "provider-counter";
        case EvidenceMeasurementMethod::Unspecified: break;
        }
        return {};
    }

    BenchmarkPlan BuildCanonicalPortfolioBenchmarkPlan(
        std::uint64_t configGeneration,
        std::uint64_t sceneGeneration,
        std::uint64_t resourceGeneration,
        std::vector<EvidenceCondition> conditions,
        std::vector<EvidenceMetricRequirement> requiredMetrics)
    {
        BenchmarkPlan plan;
        plan.warmupFrameCount = kCanonicalPortfolioWarmupFrameCount;
        plan.measurementFrameCount = kCanonicalPortfolioMeasurementFrameCount;
        plan.repeatCount = kCanonicalPortfolioRepeatCount;
        plan.configGeneration = configGeneration;
        plan.conditions = std::move(conditions);
        plan.requiredMetrics = std::move(requiredMetrics);
        plan.sceneGeneration = sceneGeneration;
        plan.resourceGeneration = resourceGeneration;
        return plan;
    }

    EvidenceStatus BenchmarkSequence::Start(const BenchmarkPlan& plan)
    {
        m_plan = {};
        m_samples.clear();
        m_currentRepeatIndex = 0u;
        m_warmupFramesAccepted = 0u;
        m_measurementFramesAccepted = 0u;
        m_lastFrameIndex = 0u;
        m_hasLastFrameIndex = false;
        m_lastSampleIndex = 0u;
        m_hasLastSampleIndex = false;
        m_phase = BenchmarkPhase::Idle;
        m_lastStatus = {};

        const EvidenceStatus planStatus = ValidatePlan(plan);
        if (!planStatus)
        {
            return Fail(planStatus.error, planStatus.message);
        }

        m_plan = plan;
        const std::size_t samplesPerMetric =
            static_cast<std::size_t>(plan.measurementFrameCount) * plan.repeatCount;
        m_samples.reserve(samplesPerMetric * plan.requiredMetrics.size());
        m_phase = plan.warmupFrameCount == 0u
            ? BenchmarkPhase::Measurement
            : BenchmarkPhase::Warmup;
        return m_lastStatus;
    }

    EvidenceStatus BenchmarkSequence::SubmitFrame(const BenchmarkFrameInput& frame)
    {
        if (m_phase != BenchmarkPhase::Warmup
            && m_phase != BenchmarkPhase::Measurement)
        {
            return Fail(EvidenceError::InvalidState,
                "Frames are accepted only during warmup or measurement.");
        }
        if (frame.configGeneration != m_plan.configGeneration)
        {
            return Fail(EvidenceError::GenerationMismatch,
                "Frame config generation does not match the benchmark plan.");
        }
        if (frame.sceneGeneration != m_plan.sceneGeneration)
        {
            return Fail(EvidenceError::GenerationMismatch,
                "Frame scene generation does not match the benchmark plan.");
        }
        if (frame.resourceGeneration != m_plan.resourceGeneration)
        {
            return Fail(EvidenceError::GenerationMismatch,
                "Frame resource generation does not match the benchmark plan.");
        }
        if (frame.repeatIndex != m_currentRepeatIndex)
        {
            return Fail(EvidenceError::GenerationMismatch,
                "Frame repeat index does not match the active repeat.");
        }
        if (m_hasLastFrameIndex && frame.frameIndex <= m_lastFrameIndex)
        {
            return Fail(EvidenceError::FrameOrderMismatch,
                "Frame indices must increase strictly across the benchmark.");
        }

        if (m_phase == BenchmarkPhase::Warmup)
        {
            if (!frame.samples.empty())
            {
                return Fail(EvidenceError::UnexpectedSample,
                    "Warmup frames must not be retained as measurement samples.");
            }

            m_lastFrameIndex = frame.frameIndex;
            m_hasLastFrameIndex = true;
            ++m_warmupFramesAccepted;
            if (m_warmupFramesAccepted == m_plan.warmupFrameCount)
            {
                m_phase = BenchmarkPhase::Measurement;
            }
            m_lastStatus = {};
            return m_lastStatus;
        }

        std::set<MetricKey> observed;
        std::uint64_t frameSampleIndex = 0u;
        bool hasFrameSampleIndex = false;
        for (const EvidenceSample& sample : frame.samples)
        {
            const EvidenceStatus sampleStatus = ValidateSample(sample, m_plan);
            if (!sampleStatus)
            {
                return Fail(sampleStatus.error, sampleStatus.message);
            }
            if (sample.frameIndex != frame.frameIndex)
            {
                return Fail(EvidenceError::FrameOrderMismatch,
                    "Sample frame index does not match its submitted frame.");
            }
            if (sample.repeatIndex != frame.repeatIndex)
            {
                return Fail(EvidenceError::GenerationMismatch,
                    "Sample repeat index does not match its submitted frame.");
            }
            if (sample.configGeneration != frame.configGeneration
                || sample.sceneGeneration != frame.sceneGeneration
                || sample.resourceGeneration != frame.resourceGeneration)
            {
                return Fail(EvidenceError::GenerationMismatch,
                    "Sample generations do not match their submitted frame.");
            }
            if (!hasFrameSampleIndex)
            {
                frameSampleIndex = sample.sampleIndex;
                hasFrameSampleIndex = true;
            }
            else if (sample.sampleIndex != frameSampleIndex)
            {
                return Fail(EvidenceError::SampleIdentityMismatch,
                    "All metric samples in one frame must share one sample index.");
            }

            const MetricKey key = MakeMetricKey(sample);
            const EvidenceMetricRequirement* const requirement =
                FindRequirement(m_plan, key);
            if (requirement == nullptr)
            {
                return Fail(EvidenceError::UnexpectedSample,
                    "A submitted sample was not declared by the benchmark plan.");
            }
            const EvidenceStatus sourceStatus =
                ValidateSampleMeasurementSource(sample, *requirement);
            if (!sourceStatus)
            {
                return Fail(sourceStatus.error, sourceStatus.message);
            }
            if (!observed.insert(key).second)
            {
                return Fail(EvidenceError::DuplicateSample,
                    "A measurement frame contains a duplicate metric sample.");
            }
        }

        for (const EvidenceMetricRequirement& required : m_plan.requiredMetrics)
        {
            if (!observed.contains(MakeMetricKey(required)))
            {
                return Fail(EvidenceError::MissingSample,
                    "A measurement frame is missing a required metric sample.");
            }
        }
        if (!hasFrameSampleIndex)
        {
            return Fail(EvidenceError::MissingSample,
                "A measurement frame contains no sample identity.");
        }
        if (m_hasLastSampleIndex && frameSampleIndex <= m_lastSampleIndex)
        {
            return Fail(EvidenceError::SampleIdentityMismatch,
                "Sample indices must increase strictly across the benchmark.");
        }

        m_samples.insert(m_samples.end(), frame.samples.begin(), frame.samples.end());
        m_lastFrameIndex = frame.frameIndex;
        m_hasLastFrameIndex = true;
        m_lastSampleIndex = frameSampleIndex;
        m_hasLastSampleIndex = true;
        ++m_measurementFramesAccepted;
        if (m_measurementFramesAccepted == m_plan.measurementFrameCount)
        {
            m_phase = m_currentRepeatIndex + 1u == m_plan.repeatCount
                ? BenchmarkPhase::Complete
                : BenchmarkPhase::AwaitingRepeat;
        }
        m_lastStatus = {};
        return m_lastStatus;
    }

    EvidenceStatus BenchmarkSequence::BeginNextRepeat()
    {
        if (m_phase != BenchmarkPhase::AwaitingRepeat)
        {
            return Fail(EvidenceError::InvalidState,
                "The next repeat can begin only at a repeat boundary.");
        }

        ++m_currentRepeatIndex;
        m_warmupFramesAccepted = 0u;
        m_measurementFramesAccepted = 0u;
        m_phase = m_plan.warmupFrameCount == 0u
            ? BenchmarkPhase::Measurement
            : BenchmarkPhase::Warmup;
        m_lastStatus = {};
        return m_lastStatus;
    }

    BenchmarkPhase BenchmarkSequence::Phase() const noexcept
    {
        return m_phase;
    }

    std::uint32_t BenchmarkSequence::CurrentRepeatIndex() const noexcept
    {
        return m_currentRepeatIndex;
    }

    std::uint32_t BenchmarkSequence::WarmupFramesAccepted() const noexcept
    {
        return m_warmupFramesAccepted;
    }

    std::uint32_t BenchmarkSequence::MeasurementFramesAccepted() const noexcept
    {
        return m_measurementFramesAccepted;
    }

    const BenchmarkPlan& BenchmarkSequence::Plan() const noexcept
    {
        return m_plan;
    }

    std::span<const EvidenceSample> BenchmarkSequence::Samples() const noexcept
    {
        return m_samples;
    }

    const EvidenceStatus& BenchmarkSequence::LastStatus() const noexcept
    {
        return m_lastStatus;
    }

    EvidenceStatus BenchmarkSequence::Fail(EvidenceError error, std::string message)
    {
        m_phase = BenchmarkPhase::Failed;
        m_lastStatus.error = error;
        m_lastStatus.message = std::move(message);
        return m_lastStatus;
    }

    ScopeStatisticsResult ComputeScopeStatistics(
        std::span<const EvidenceSample> samples,
        EvidenceSampleKind kind,
        const BenchmarkPlan& plan)
    {
        ScopeStatisticsResult result;
        const EvidenceStatus planStatus = ValidatePlan(plan);
        if (!planStatus)
        {
            result.error = planStatus.error;
            result.message = planStatus.message;
            return result;
        }
        if (!IsKnownKind(kind))
        {
            result.error = EvidenceError::InvalidPlan;
            result.message = "Statistics require a known sample kind.";
            return result;
        }

        std::vector<EvidenceMetricRequirement> requirements;
        for (const EvidenceMetricRequirement& required : plan.requiredMetrics)
        {
            if (required.kind == kind)
            {
                requirements.push_back(required);
            }
        }
        if (requirements.empty())
        {
            result.error = EvidenceError::MissingSample;
            result.message = "The benchmark plan declares no scope for this sample kind.";
            return result;
        }
        std::sort(
            requirements.begin(),
            requirements.end(),
            [](const EvidenceMetricRequirement& left,
               const EvidenceMetricRequirement& right)
            {
                return std::tie(left.scope, left.unit)
                    < std::tie(right.scope, right.unit);
            });

        using SampleIdentity =
            std::tuple<std::uint32_t, std::uint64_t, std::uint64_t>;
        std::map<MetricKey, std::vector<double>> valuesByMetric;
        std::map<MetricKey, std::set<SampleIdentity>> identitiesByMetric;
        std::map<MetricKey, EvidenceProvenance> provenanceByMetric;
        for (const EvidenceSample& sample : samples)
        {
            const EvidenceStatus sampleStatus = ValidateSample(sample, plan);
            if (!sampleStatus)
            {
                result.error = sampleStatus.error;
                result.message = sampleStatus.message;
                return result;
            }

            const MetricKey key = MakeMetricKey(sample);
            const EvidenceMetricRequirement* const requirement =
                FindRequirement(plan, key);
            if (requirement == nullptr)
            {
                result.error = EvidenceError::UnexpectedSample;
                result.message = "Statistics input contains an undeclared metric.";
                return result;
            }
            const EvidenceStatus sourceStatus =
                ValidateSampleMeasurementSource(sample, *requirement);
            if (!sourceStatus)
            {
                result.error = sourceStatus.error;
                result.message = sourceStatus.message;
                return result;
            }
            if (!identitiesByMetric[key]
                    .insert({ sample.repeatIndex, sample.frameIndex, sample.sampleIndex })
                    .second)
            {
                result.error = EvidenceError::DuplicateSample;
                result.message =
                    "Statistics input contains a duplicate metric sample identity.";
                return result;
            }
            const auto [provenance, inserted] = provenanceByMetric.emplace(
                key,
                sample.provenance);
            if (!inserted
                && (provenance->second.source != sample.provenance.source
                    || provenance->second.provider != sample.provenance.provider
                    || provenance->second.detail != sample.provenance.detail))
            {
                result.error = EvidenceError::InvalidProvenance;
                result.message =
                    "A metric scope cannot aggregate samples from different provenance.";
                return result;
            }
            valuesByMetric[key].push_back(sample.value);
        }

        const std::size_t expectedPerScope =
            static_cast<std::size_t>(plan.measurementFrameCount) * plan.repeatCount;
        std::set<SampleIdentity> sharedSampleIdentities;
        bool hasSharedSampleIdentities = false;
        for (const EvidenceMetricRequirement& required : plan.requiredMetrics)
        {
            const MetricKey key = MakeMetricKey(required);
            const auto valuesIterator = valuesByMetric.find(key);
            if (valuesIterator == valuesByMetric.end()
                || valuesIterator->second.size() < expectedPerScope)
            {
                result.error = EvidenceError::MissingSample;
                result.message = "A required metric scope has fewer samples than planned.";
                return result;
            }
            if (valuesIterator->second.size() > expectedPerScope)
            {
                result.error = EvidenceError::DuplicateSample;
                result.message = "A required metric scope has more samples than planned.";
                return result;
            }

            const std::set<SampleIdentity>& identities = identitiesByMetric.at(key);
            for (std::uint32_t repeat = 0u; repeat < plan.repeatCount; ++repeat)
            {
                const std::size_t count = static_cast<std::size_t>(std::count_if(
                    identities.begin(),
                    identities.end(),
                    [repeat](const SampleIdentity& identity)
                    {
                        return std::get<0>(identity) == repeat;
                    }));
                if (count != plan.measurementFrameCount)
                {
                    result.error = EvidenceError::MissingSample;
                    result.message = "A required metric scope is incomplete in one repeat.";
                    return result;
                }
            }

            if (!hasSharedSampleIdentities)
            {
                sharedSampleIdentities = identities;
                hasSharedSampleIdentities = true;
            }
            else if (identities != sharedSampleIdentities)
            {
                result.error = EvidenceError::MissingSample;
                result.message =
                    "All required metric scopes must use the same repeat/frame/sample identity set.";
                return result;
            }
        }

        std::uint64_t previousFrameIndex = 0u;
        std::uint64_t previousSampleIndex = 0u;
        bool hasPreviousFrameIndex = false;
        bool hasPreviousSampleIndex = false;
        for (const SampleIdentity& identity : sharedSampleIdentities)
        {
            const std::uint64_t frameIndex = std::get<1>(identity);
            const std::uint64_t sampleIndex = std::get<2>(identity);
            if (hasPreviousFrameIndex && frameIndex <= previousFrameIndex)
            {
                result.error = EvidenceError::FrameOrderMismatch;
                result.message =
                    "Measurement frame indices must increase globally across repeats.";
                return result;
            }
            if (hasPreviousSampleIndex && sampleIndex <= previousSampleIndex)
            {
                result.error = EvidenceError::SampleIdentityMismatch;
                result.message =
                    "Measurement sample indices must increase globally across repeats.";
                return result;
            }
            previousFrameIndex = frameIndex;
            hasPreviousFrameIndex = true;
            previousSampleIndex = sampleIndex;
            hasPreviousSampleIndex = true;
        }

        for (const EvidenceMetricRequirement& required : requirements)
        {
            const MetricKey key = MakeMetricKey(required);
            const auto valuesIterator = valuesByMetric.find(key);
            std::vector<double> sortedValues = valuesIterator->second;
            std::sort(sortedValues.begin(), sortedValues.end());
            const std::size_t count = sortedValues.size();
            const double median = (count % 2u) == 0u
                ? std::midpoint(sortedValues[count / 2u - 1u], sortedValues[count / 2u])
                : sortedValues[count / 2u];
            const std::size_t p95Index = count - count / 20u - 1u;
            result.scopes.push_back({
                required.scope,
                required.unit,
                count,
                sortedValues.front(),
                sortedValues.back(),
                median,
                sortedValues[p95Index],
                required.measurementMethod,
                provenanceByMetric.at(key)
            });
        }
        return result;
    }

    CsvResult BuildTimingsCsv(
        std::span<const EvidenceSample> samples,
        const BenchmarkPlan& plan)
    {
        return BuildCsv(samples, EvidenceSampleKind::Timing, plan);
    }

    CsvResult BuildCountersCsv(
        std::span<const EvidenceSample> samples,
        const BenchmarkPlan& plan)
    {
        return BuildCsv(samples, EvidenceSampleKind::Counter, plan);
    }

    ImageComparisonResult CompareLinearRgba(
        const LinearRgbaImageView& candidate,
        const LinearRgbaImageView& reference,
        double peakSignalValue)
    {
        ImageComparisonResult result;
        if (candidate.width != reference.width
            || candidate.height != reference.height)
        {
            result.error = EvidenceError::ExtentMismatch;
            result.message = "Candidate and reference image extents must match.";
            return result;
        }
        if (!std::isfinite(peakSignalValue) || peakSignalValue <= 0.0)
        {
            result.error = EvidenceError::InvalidImage;
            result.message = "peakSignalValue must be finite and greater than zero.";
            return result;
        }

        std::size_t candidateComponentCount = 0u;
        std::size_t referenceComponentCount = 0u;
        if (!TryGetExpectedComponentCount(candidate, candidateComponentCount)
            || !TryGetExpectedComponentCount(reference, referenceComponentCount))
        {
            result.error = EvidenceError::InvalidImage;
            result.message = "Linear RGBA spans must contain width * height * 4 components.";
            return result;
        }
        if (candidateComponentCount != referenceComponentCount)
        {
            result.error = EvidenceError::ExtentMismatch;
            result.message = "Candidate and reference component counts must match.";
            return result;
        }

        double meanSquaredError = 0.0;
        double maximumAbsoluteError = 0.0;
        for (std::size_t index = 0u; index < candidateComponentCount; ++index)
        {
            const double candidateValue = candidate.rgba[index];
            const double referenceValue = reference.rgba[index];
            if (!std::isfinite(candidateValue) || !std::isfinite(referenceValue))
            {
                result.error = EvidenceError::NonFiniteValue;
                result.message = "Linear RGBA comparison rejects non-finite components.";
                return result;
            }

            const double difference = candidateValue - referenceValue;
            const double absoluteError = std::abs(difference);
            const double squaredError = difference * difference;
            const double sampleCount = static_cast<double>(index + 1u);
            meanSquaredError += (squaredError - meanSquaredError) / sampleCount;
            maximumAbsoluteError = std::max(maximumAbsoluteError, absoluteError);
        }

        result.comparedComponentCount = candidateComponentCount;
        result.maximumAbsoluteError = maximumAbsoluteError;
        result.rmse = std::sqrt(meanSquaredError);
        if (meanSquaredError == 0.0)
        {
            result.psnr = std::numeric_limits<double>::infinity();
            result.psnrIsPositiveInfinity = true;
        }
        else
        {
            result.psnr = 20.0
                * (std::log10(peakSignalValue) - std::log10(result.rmse));
        }
        return result;
    }
}
