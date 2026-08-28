#include "MegakernelBridge.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace RenderingEngine::Integrators::Megakernel
{
    namespace
    {
        constexpr double kPi = 3.141592653589793238462643383279502884;

        [[nodiscard]] double LinearLuminance(const Float4& value)
        {
            return 0.2126 * static_cast<double>(value.x)
                + 0.7152 * static_cast<double>(value.y)
                + 0.0722 * static_cast<double>(value.z);
        }

        void ValidateNonNegativeFinite(const double value, const char* const label)
        {
            if (!std::isfinite(value) || value < 0.0)
            {
                throw std::invalid_argument(label);
            }
        }

        [[nodiscard]] std::uint32_t LowWord(const std::uint64_t value) noexcept
        {
            return static_cast<std::uint32_t>(value & 0xffffffffull);
        }

        [[nodiscard]] std::uint32_t HighWord(const std::uint64_t value) noexcept
        {
            return static_cast<std::uint32_t>(value >> 32u);
        }
    }

    AliasTable BuildVoseAliasTable(
        const std::span<const double> weights,
        const std::span<const std::uint32_t> items)
    {
        AliasTable result;
        if (weights.empty())
        {
            return result;
        }
        if (!items.empty() && items.size() != weights.size())
        {
            throw std::invalid_argument("alias item count must equal weight count");
        }
        if (weights.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            throw std::length_error("alias table exceeds uint32 indexing");
        }

        for (const double weight : weights)
        {
            ValidateNonNegativeFinite(weight, "alias weights must be finite and non-negative");
            result.weightSum += weight;
            if (!std::isfinite(result.weightSum))
            {
                throw std::overflow_error("alias weight sum overflowed");
            }
        }

        const std::size_t count = weights.size();
        const double normalization = result.weightSum > 0.0
            ? result.weightSum
            : static_cast<double>(count);
        result.usedUniformFallback = result.weightSum == 0.0;

        std::vector<double> probabilities(count);
        std::vector<double> scaled(count);
        std::vector<std::uint32_t> small;
        std::vector<std::uint32_t> large;
        small.reserve(count);
        large.reserve(count);
        result.entries.resize(count);

        const double countAsDouble = static_cast<double>(count);
        for (std::size_t index = 0u; index < count; ++index)
        {
            probabilities[index] = result.usedUniformFallback
                ? 1.0 / countAsDouble
                : weights[index] / normalization;
            scaled[index] = probabilities[index] * countAsDouble;

            const float pmf = static_cast<float>(probabilities[index]);
            if (probabilities[index] > 0.0 && pmf == 0.0f)
            {
                throw std::underflow_error("positive alias PMF cannot be represented as float");
            }
            result.entries[index].pmf = pmf;
            result.entries[index].item = items.empty()
                ? static_cast<std::uint32_t>(index)
                : items[index];
            result.entries[index].alias = static_cast<std::uint32_t>(index);

            if (scaled[index] < 1.0)
            {
                small.push_back(static_cast<std::uint32_t>(index));
            }
            else
            {
                large.push_back(static_cast<std::uint32_t>(index));
            }
        }

        while (!small.empty() && !large.empty())
        {
            const std::uint32_t smallIndex = small.back();
            small.pop_back();
            const std::uint32_t largeIndex = large.back();
            large.pop_back();

            const double q = std::clamp(scaled[smallIndex], 0.0, 1.0);
            result.entries[smallIndex].q = static_cast<float>(q);
            result.entries[smallIndex].alias = largeIndex;

            scaled[largeIndex] = scaled[largeIndex] + scaled[smallIndex] - 1.0;
            if (scaled[largeIndex] < 1.0)
            {
                small.push_back(largeIndex);
            }
            else
            {
                large.push_back(largeIndex);
            }
        }

        for (const std::uint32_t index : large)
        {
            result.entries[index].q = 1.0f;
            result.entries[index].alias = index;
        }
        for (const std::uint32_t index : small)
        {
            // Any residual is roundoff from a normalized distribution.
            result.entries[index].q = 1.0f;
            result.entries[index].alias = index;
        }
        return result;
    }

    AliasSample SampleAliasTable(
        const std::span<const AliasEntryGpu> table,
        const float uniformSample)
    {
        if (table.empty())
        {
            return {};
        }
        if (!std::isfinite(uniformSample) || uniformSample < 0.0f || uniformSample >= 1.0f)
        {
            throw std::invalid_argument("alias sample must be finite and in [0, 1)");
        }

        const float scaled = uniformSample * static_cast<float>(table.size());
        const std::size_t bin = std::min(
            static_cast<std::size_t>(scaled),
            table.size() - 1u);
        const float withinBin = scaled - static_cast<float>(bin);
        const AliasEntryGpu& entry = table[bin];
        const std::size_t selected = withinBin < entry.q
            ? bin
            : static_cast<std::size_t>(entry.alias);
        if (selected >= table.size())
        {
            throw std::out_of_range("alias entry references a bin outside its table");
        }
        return { table[selected].item, table[selected].pmf };
    }

    double TexelSolidAngle(
        const std::uint32_t row,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (width == 0u || height == 0u || row >= height)
        {
            throw std::invalid_argument("environment dimensions and row must be valid");
        }
        const double theta0 = kPi * static_cast<double>(row) / static_cast<double>(height);
        const double theta1 = kPi * static_cast<double>(row + 1u) / static_cast<double>(height);
        const double deltaPhi = 2.0 * kPi / static_cast<double>(width);
        return deltaPhi * (std::cos(theta0) - std::cos(theta1));
    }

    EnvironmentDistribution BuildEnvironmentDistribution(
        const std::uint32_t width,
        const std::uint32_t height,
        const std::span<const float> linearLuminance)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument("environment dimensions must be non-zero");
        }
        const std::uint64_t texelCount64 = static_cast<std::uint64_t>(width)
            * static_cast<std::uint64_t>(height);
        if (texelCount64 != static_cast<std::uint64_t>(linearLuminance.size()))
        {
            throw std::invalid_argument("environment luminance count does not match dimensions");
        }
        if (texelCount64 > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            throw std::length_error("environment distribution exceeds uint32 indexing");
        }

        EnvironmentDistribution result;
        result.width = width;
        result.height = height;
        std::vector<double> texelWeights(static_cast<std::size_t>(texelCount64));

        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const double solidAngle = TexelSolidAngle(row, width, height);
            for (std::uint32_t column = 0u; column < width; ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row) * width + column;
                const double luminance = static_cast<double>(linearLuminance[index]);
                ValidateNonNegativeFinite(
                    luminance,
                    "environment luminance must be finite and non-negative");
                const double weighted = luminance * solidAngle;
                texelWeights[index] = weighted;
                result.integratedLuminance += weighted;
            }
        }
        if (!std::isfinite(result.integratedLuminance))
        {
            throw std::overflow_error("environment luminance integral overflowed");
        }

        if (result.integratedLuminance == 0.0)
        {
            // A black map contributes no energy. Sampling it uniformly on the
            // sphere nevertheless leaves a valid, pole-safe diagnostic PDF.
            result.usedUniformSphereFallback = true;
            for (std::uint32_t row = 0u; row < height; ++row)
            {
                const double solidAngle = TexelSolidAngle(row, width, height);
                for (std::uint32_t column = 0u; column < width; ++column)
                {
                    const std::size_t index = static_cast<std::size_t>(row) * width + column;
                    texelWeights[index] = solidAngle;
                }
            }
        }

        std::vector<double> rowWeights(height, 0.0);
        result.columnAlias.reserve(static_cast<std::size_t>(texelCount64));
        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const std::size_t rowOffset = static_cast<std::size_t>(row) * width;
            const std::span<const double> rowSpan(texelWeights.data() + rowOffset, width);
            for (const double weight : rowSpan)
            {
                rowWeights[row] += weight;
            }

            const AliasTable columns = BuildVoseAliasTable(rowSpan);
            result.columnAlias.insert(
                result.columnAlias.end(),
                columns.entries.begin(),
                columns.entries.end());
        }

        result.rowAlias = BuildVoseAliasTable(rowWeights).entries;
        return result;
    }

    double LightPowerWeight(
        const PbrLightGpu& light,
        const float sceneRadius,
        const double environmentIntegratedLuminance)
    {
        if (!std::isfinite(sceneRadius) || sceneRadius <= 0.0f)
        {
            throw std::invalid_argument("scene radius must be finite and positive");
        }

        const double scale = static_cast<double>(light.radianceScale.w);
        ValidateNonNegativeFinite(scale, "light scale must be finite and non-negative");
        ValidateNonNegativeFinite(light.radianceScale.x, "light radiance must be finite and non-negative");
        ValidateNonNegativeFinite(light.radianceScale.y, "light radiance must be finite and non-negative");
        ValidateNonNegativeFinite(light.radianceScale.z, "light radiance must be finite and non-negative");
        const double luminance = LinearLuminance(light.radianceScale) * scale;
        ValidateNonNegativeFinite(luminance, "light luminance must be finite and non-negative");

        const auto type = static_cast<LightType>(light.identity.x);
        const double radius = static_cast<double>(sceneRadius);
        if (type == LightType::Directional || type == LightType::Spot)
        {
            const double directionLengthSquared =
                static_cast<double>(light.directionCosOuter.x) * light.directionCosOuter.x
                + static_cast<double>(light.directionCosOuter.y) * light.directionCosOuter.y
                + static_cast<double>(light.directionCosOuter.z) * light.directionCosOuter.z;
            if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 0.0)
            {
                throw std::invalid_argument("directional and spot lights require a finite non-zero direction");
            }
        }
        switch (type)
        {
        case LightType::Point:
            return 4.0 * kPi * luminance;
        case LightType::Directional:
            return kPi * radius * radius * luminance;
        case LightType::Spot:
        {
            const double cosOuter = static_cast<double>(light.directionCosOuter.w);
            const double cosInner = static_cast<double>(light.shapeParams.y);
            if (!std::isfinite(cosOuter) || !std::isfinite(cosInner)
                || cosOuter < -1.0 || cosOuter > 1.0
                || cosInner < -1.0 || cosInner > 1.0
                || cosInner < cosOuter)
            {
                throw std::invalid_argument("spot cone cosines are invalid");
            }
            return 2.0 * kPi * luminance
                * ((1.0 - cosInner) + 0.5 * (cosInner - cosOuter));
        }
        case LightType::SphereArea:
        {
            const double lightRadius = static_cast<double>(light.shapeParams.x);
            if (!std::isfinite(lightRadius) || lightRadius <= 0.0)
            {
                throw std::invalid_argument("sphere light radius must be finite and positive");
            }
            const double sideFactor = (light.identity.y & LightFlagTwoSided) != 0u ? 2.0 : 1.0;
            return 4.0 * kPi * kPi * lightRadius * lightRadius * luminance * sideFactor;
        }
        case LightType::EmissiveTriangle:
        {
            const double area = static_cast<double>(light.shapeParams.z);
            if (!std::isfinite(area) || area <= 0.0)
            {
                throw std::invalid_argument("triangle light area must be finite and positive");
            }
            const double sideFactor = (light.identity.y & LightFlagTwoSided) != 0u ? 2.0 : 1.0;
            return kPi * area * luminance * sideFactor;
        }
        case LightType::Environment:
            ValidateNonNegativeFinite(
                environmentIntegratedLuminance,
                "environment luminance integral must be finite and non-negative");
            return kPi * radius * radius * scale * environmentIntegratedLuminance;
        default:
            throw std::invalid_argument("unknown L6 light type");
        }
    }

    TopLevelLightDistribution BuildTopLevelLightDistribution(
        const std::span<const PbrLightGpu> lights,
        const LightProposal proposal,
        const float sceneRadius,
        const double environmentIntegratedLuminance)
    {
        TopLevelLightDistribution result;
        result.selectionPmfByLight.resize(lights.size(), 0.0f);

        std::vector<double> weights;
        std::vector<std::uint32_t> items;
        weights.reserve(lights.size());
        items.reserve(lights.size());
        for (std::size_t index = 0u; index < lights.size(); ++index)
        {
            const PbrLightGpu& light = lights[index];
            if ((light.identity.y & LightFlagEnabled) == 0u)
            {
                continue;
            }
            if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                throw std::length_error("light index exceeds uint32");
            }

            double weight = 0.0;
            switch (proposal)
            {
            case LightProposal::Uniform:
                weight = 1.0;
                break;
            case LightProposal::Power:
                weight = LightPowerWeight(light, sceneRadius, environmentIntegratedLuminance);
                break;
            default:
                throw std::invalid_argument("unknown light proposal");
            }
            weights.push_back(weight);
            items.push_back(static_cast<std::uint32_t>(index));
        }

        const AliasTable table = BuildVoseAliasTable(weights, items);
        result.alias = table.entries;
        result.weightSum = table.weightSum;
        result.usedUniformFallback = table.usedUniformFallback;
        for (const AliasEntryGpu& entry : result.alias)
        {
            result.selectionPmfByLight[entry.item] = entry.pmf;
        }
        return result;
    }

    bool ValidateMegakernelFrameConstants(
        const MegakernelFrameConstantsGpu& frame) noexcept
    {
        const float rouletteStart = frame.russianRoulette.x;
        const float rouletteMinimum = frame.russianRoulette.y;
        const float rouletteMaximum = frame.russianRoulette.z;
        const float rayEpsilon = frame.russianRoulette.w;
        const std::uint64_t pixelCount = static_cast<std::uint64_t>(frame.image.x)
            * static_cast<std::uint64_t>(frame.image.y);
        return frame.image.x != 0u
            && frame.image.y != 0u
            && frame.image.w != 0u
            && pixelCount <= std::numeric_limits<std::uint32_t>::max()
            && frame.sampling.z <= static_cast<std::uint32_t>(LightProposal::Power)
            && frame.sampling.w <= 1u
            && std::isfinite(rouletteStart)
            && rouletteStart >= 0.0f
            && std::floor(rouletteStart) == rouletteStart
            && std::isfinite(rouletteMinimum)
            && std::isfinite(rouletteMaximum)
            && rouletteMinimum > 0.0f
            && rouletteMinimum <= rouletteMaximum
            && rouletteMaximum <= 1.0f
            && std::isfinite(rayEpsilon)
            && rayEpsilon > 0.0f;
    }

    std::array<std::uint32_t, 4> Philox4x32TenRounds(
        std::array<std::uint32_t, 4> counter,
        std::array<std::uint32_t, 2> key) noexcept
    {
        constexpr std::uint32_t multiplier0 = 0xd2511f53u;
        constexpr std::uint32_t multiplier1 = 0xcd9e8d57u;
        constexpr std::uint32_t keyIncrement0 = 0x9e3779b9u;
        constexpr std::uint32_t keyIncrement1 = 0xbb67ae85u;

        for (std::uint32_t round = 0u; round < 10u; ++round)
        {
            const std::uint64_t product0 = static_cast<std::uint64_t>(multiplier0) * counter[0];
            const std::uint64_t product1 = static_cast<std::uint64_t>(multiplier1) * counter[2];
            counter = {
                HighWord(product1) ^ counter[1] ^ key[0],
                LowWord(product1),
                HighWord(product0) ^ counter[3] ^ key[1],
                LowWord(product0)
            };
            key[0] += keyIncrement0;
            key[1] += keyIncrement1;
        }
        return counter;
    }

    float CounterRandomFloat(
        const std::uint32_t pixelIndex,
        const std::uint32_t sampleIndex,
        const std::uint32_t dimension,
        const std::uint32_t streamTag,
        const std::uint64_t baseSeed) noexcept
    {
        const std::array<std::uint32_t, 4> value = Philox4x32TenRounds(
            { pixelIndex, sampleIndex, dimension, streamTag },
            { LowWord(baseSeed), HighWord(baseSeed) });
        return Uint32ToUnitFloat(value[0]);
    }
}
