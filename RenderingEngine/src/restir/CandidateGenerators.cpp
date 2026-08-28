#include "restir/CandidateGenerators.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace RenderingEngine::Restir
{
    namespace
    {
        [[nodiscard]] std::size_t SelectUniformIndex(const std::size_t count, const float sample) noexcept
        {
            if (count == 0u)
            {
                return 0u;
            }
            const float clamped = std::clamp(sample, 0.0f, 0.99999994f);
            return std::min(static_cast<std::size_t>(clamped * static_cast<float>(count)), count - 1u);
        }

        [[nodiscard]] Candidate MakeAnalyticCandidate(
            const AnalyticLight& light,
            const SurfaceRecord& surface,
            const CandidateSource source,
            const float proposalPdf,
            const std::uint32_t sampleId) noexcept
        {
            Candidate candidate{};
            candidate.identity = { source, light.stableId, kInvalidStableId, sampleId, light.generation };
            candidate.proposalPdf = proposalPdf;
            candidate.correction = 1.0f;

            const Float3 offset = light.position - surface.position;
            const float distanceSquared = Dot(offset, offset);
            if (!(distanceSquared > 1.0e-12f) || !std::isfinite(distanceSquared))
            {
                return candidate;
            }

            candidate.distance = std::sqrt(distanceSquared);
            candidate.directionToLight = offset / candidate.distance;
            const float cosine = std::max(0.0f, Dot(Normalize(surface.normal), candidate.directionToLight));
            candidate.support = cosine > 0.0f ? 1.0f : 0.0f;
            candidate.unshadowedContribution = light.radiance * (cosine / distanceSquared);
            candidate.target = std::max(0.0f, Luminance(candidate.unshadowedContribution));
            return candidate;
        }

        [[nodiscard]] float AnalyticPower(const AnalyticLight& light) noexcept
        {
            if (std::isfinite(light.samplingPower) && light.samplingPower > 0.0f)
            {
                return light.samplingPower;
            }
            return std::max(0.0f, Luminance(light.radiance));
        }
    }

    Candidate GenerateUniformLightCandidate(
        const std::span<const AnalyticLight> lights,
        const SurfaceRecord& surface,
        const float selectionSample) noexcept
    {
        if (lights.empty())
        {
            return {};
        }
        const std::size_t index = SelectUniformIndex(lights.size(), selectionSample);
        const float proposal = 1.0f / static_cast<float>(lights.size());
        return MakeAnalyticCandidate(
            lights[index], surface, CandidateSource::UniformLight, proposal, static_cast<std::uint32_t>(index));
    }

    Candidate GeneratePowerWeightedLightCandidate(
        const std::span<const AnalyticLight> lights,
        const SurfaceRecord& surface,
        const float selectionSample) noexcept
    {
        if (lights.empty())
        {
            return {};
        }

        double totalPower = 0.0;
        for (const AnalyticLight& light : lights)
        {
            totalPower += static_cast<double>(AnalyticPower(light));
        }
        if (!(totalPower > 0.0) || !std::isfinite(totalPower))
        {
            Candidate fallback = GenerateUniformLightCandidate(lights, surface, selectionSample);
            fallback.identity.source = CandidateSource::PowerWeightedLight;
            return fallback;
        }

        const double target = std::clamp(static_cast<double>(selectionSample), 0.0, 0.9999999999999999) * totalPower;
        double cumulative = 0.0;
        std::size_t selected = lights.size() - 1u;
        for (std::size_t index = 0u; index < lights.size(); ++index)
        {
            cumulative += static_cast<double>(AnalyticPower(lights[index]));
            if (target < cumulative)
            {
                selected = index;
                break;
            }
        }

        const float proposal = static_cast<float>(
            static_cast<double>(AnalyticPower(lights[selected])) / totalPower);
        return MakeAnalyticCandidate(
            lights[selected], surface, CandidateSource::PowerWeightedLight, proposal,
            static_cast<std::uint32_t>(selected));
    }

    Candidate GenerateEmissiveTriangleCandidate(
        const EmissiveTriangle& light,
        const SurfaceRecord& surface,
        const float sample0,
        const float sample1) noexcept
    {
        Candidate candidate{};
        candidate.identity = {
            CandidateSource::EmissiveTriangle,
            light.stableLightId,
            light.stablePrimitiveId,
            0u,
            light.generation
        };
        candidate.correction = 1.0f;

        const Float3 edge01 = light.p1 - light.p0;
        const Float3 edge02 = light.p2 - light.p0;
        const Float3 cross = Cross(edge01, edge02);
        const float doubleArea = Length(cross);
        const float area = 0.5f * doubleArea;
        if (!(area > 1.0e-12f) || !(light.selectionProbability > 0.0f))
        {
            return candidate;
        }

        const float root = std::sqrt(std::clamp(sample0, 0.0f, 0.99999994f));
        const float bary0 = 1.0f - root;
        const float bary1 = root * (1.0f - std::clamp(sample1, 0.0f, 0.99999994f));
        const float bary2 = root * std::clamp(sample1, 0.0f, 0.99999994f);
        const Float3 sampledPosition = light.p0 * bary0 + light.p1 * bary1 + light.p2 * bary2;
        const Float3 offset = sampledPosition - surface.position;
        const float distanceSquared = Dot(offset, offset);
        candidate.proposalPdf = light.selectionProbability / area;
        if (!(distanceSquared > 1.0e-12f))
        {
            return candidate;
        }

        candidate.distance = std::sqrt(distanceSquared);
        candidate.directionToLight = offset / candidate.distance;
        const Float3 lightNormal = cross / doubleArea;
        const float receiverCosine = std::max(0.0f, Dot(Normalize(surface.normal), candidate.directionToLight));
        const float rawEmitterCosine = Dot(lightNormal, -1.0f * candidate.directionToLight);
        const float emitterCosine = light.twoSided ? std::abs(rawEmitterCosine) : std::max(0.0f, rawEmitterCosine);
        const float geometry = receiverCosine * emitterCosine / distanceSquared;
        candidate.support = geometry > 0.0f ? 1.0f : 0.0f;
        candidate.unshadowedContribution = light.radiance * geometry;
        candidate.target = std::max(0.0f, Luminance(candidate.unshadowedContribution));
        return candidate;
    }

    Candidate GenerateEnvironmentCandidate(
        const std::span<const EnvironmentCell> cells,
        const SurfaceRecord& surface,
        const float selectionSample) noexcept
    {
        if (cells.empty())
        {
            return {};
        }

        double totalPower = 0.0;
        for (const EnvironmentCell& cell : cells)
        {
            const float fallback = std::max(0.0f, Luminance(cell.radiance) * cell.solidAngle);
            totalPower += static_cast<double>(cell.samplingPower > 0.0f ? cell.samplingPower : fallback);
        }
        if (!(totalPower > 0.0) || !std::isfinite(totalPower))
        {
            return {};
        }

        const double target = std::clamp(static_cast<double>(selectionSample), 0.0, 0.9999999999999999) * totalPower;
        double cumulative = 0.0;
        std::size_t selected = cells.size() - 1u;
        float selectedPower = 0.0f;
        for (std::size_t index = 0u; index < cells.size(); ++index)
        {
            const float fallback = std::max(0.0f, Luminance(cells[index].radiance) * cells[index].solidAngle);
            const float power = cells[index].samplingPower > 0.0f ? cells[index].samplingPower : fallback;
            cumulative += static_cast<double>(power);
            if (target < cumulative)
            {
                selected = index;
                selectedPower = power;
                break;
            }
        }
        if (!(selectedPower > 0.0f))
        {
            const EnvironmentCell& cell = cells[selected];
            selectedPower = cell.samplingPower > 0.0f
                ? cell.samplingPower
                : std::max(0.0f, Luminance(cell.radiance) * cell.solidAngle);
        }

        const EnvironmentCell& cell = cells[selected];
        Candidate candidate{};
        candidate.identity = {
            CandidateSource::Environment,
            cell.stableLightId,
            kInvalidStableId,
            cell.cellId,
            cell.generation
        };
        candidate.directionToLight = Normalize(cell.direction);
        candidate.distance = std::numeric_limits<float>::max();
        candidate.correction = 1.0f;
        if (!(cell.solidAngle > 0.0f))
        {
            return candidate;
        }
        const double selectionProbability = static_cast<double>(selectedPower) / totalPower;
        candidate.proposalPdf = static_cast<float>(selectionProbability / static_cast<double>(cell.solidAngle));
        const float cosine = std::max(0.0f, Dot(Normalize(surface.normal), candidate.directionToLight));
        candidate.support = cosine > 0.0f ? 1.0f : 0.0f;
        candidate.unshadowedContribution = cell.radiance * cosine;
        candidate.target = std::max(0.0f, Luminance(candidate.unshadowedContribution));
        return candidate;
    }
}
