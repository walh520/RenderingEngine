#pragma once

#include <tinyexr.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace RenderingEngine::Tests
{
    inline int CompareLinearExr(int argc, char** argv)
    {
        if (argc != 4 && argc != 8)
            throw std::runtime_error("EXR comparison requires two image paths and optional x y width height");
        const auto read = [](const char* path, int& width, int& height) {
            float* raw = nullptr;
            const char* error = nullptr;
            if (LoadEXR(&raw, &width, &height, path, &error) != TINYEXR_SUCCESS)
            {
                const std::string reason = error ? error : "EXR decode failed";
                if (error) FreeEXRErrorMessage(error);
                throw std::runtime_error(reason);
            }
            std::unique_ptr<float, decltype(&std::free)> owner(raw, &std::free);
            return std::vector<float>(raw,
                raw + static_cast<std::size_t>(width) * height * 4u);
        };
        int widthA = 0, heightA = 0, widthB = 0, heightB = 0;
        const auto a = read(argv[2], widthA, heightA);
        const auto b = read(argv[3], widthB, heightB);
        if (widthA <= 0 || heightA <= 0 || widthA != widthB || heightA != heightB)
            throw std::runtime_error("EXR comparison extents differ or are empty");
        const std::size_t roiX = argc == 8 ? std::stoul(argv[4]) : 0u;
        const std::size_t roiY = argc == 8 ? std::stoul(argv[5]) : 0u;
        const std::size_t roiWidth = argc == 8 ? std::stoul(argv[6]) : static_cast<std::size_t>(widthA);
        const std::size_t roiHeight = argc == 8 ? std::stoul(argv[7]) : static_cast<std::size_t>(heightA);
        if (roiWidth == 0u || roiHeight == 0u || roiX + roiWidth > static_cast<std::size_t>(widthA)
            || roiY + roiHeight > static_cast<std::size_t>(heightA))
            throw std::runtime_error("EXR comparison ROI is outside the image");
        double squared = 0.0, maxAbs = 0.0, energyA = 0.0, energyB = 0.0;
        std::size_t componentCount = 0u;
        for (std::size_t y = roiY; y < roiY + roiHeight; ++y)
        {
            for (std::size_t x = roiX; x < roiX + roiWidth; ++x)
            {
                const std::size_t base = (y * static_cast<std::size_t>(widthA) + x) * 4u;
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    const std::size_t i = base + channel;
                    if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || a[i] < -1.e-6f || b[i] < -1.e-6f)
                        throw std::runtime_error("EXR comparison found nonfinite or negative radiance");
                    const double difference = static_cast<double>(a[i]) - b[i];
                    squared += difference * difference;
                    maxAbs = std::max(maxAbs, std::abs(difference));
                    energyA += a[i];
                    energyB += b[i];
                    ++componentCount;
                }
            }
        }
        if (componentCount == 0u || !(energyA > 0.0) || !(energyB > 0.0))
            throw std::runtime_error("EXR comparison requires two non-black images");
        std::cout << "EXR comparison: " << widthA << 'x' << heightA
            << " roi=" << roiX << ',' << roiY << ',' << roiWidth << ',' << roiHeight
            << " rmse=" << std::sqrt(squared / componentCount)
            << " max_abs=" << maxAbs
            << " mean_a=" << energyA / componentCount
            << " mean_b=" << energyB / componentCount << '\n';
        return 0;
    }

    // Independent image-domain oracle. Arguments are mean.exr followed by
    // ordered current-frame EXRs produced with the same seed and scene.
    inline int VerifyProgressiveFilm(int argc, char** argv)
    {
        if (argc < 5) throw std::runtime_error("film oracle requires mean EXR and at least two current frames");
        int expectedWidth = 0, expectedHeight = 0;
        const auto read = [&](const char* path) {
            float* raw = nullptr;
            int width = 0, height = 0;
            const char* error = nullptr;
            if (LoadEXR(&raw, &width, &height, path, &error) != TINYEXR_SUCCESS)
            {
                const std::string reason = error ? error : "EXR decode failed";
                if (error) FreeEXRErrorMessage(error);
                throw std::runtime_error(reason);
            }
            std::unique_ptr<float, decltype(&std::free)> owner(raw, &std::free);
            if (width <= 0 || height <= 0) throw std::runtime_error("empty film image");
            if (expectedWidth == 0) { expectedWidth = width; expectedHeight = height; }
            if (width != expectedWidth || height != expectedHeight)
                throw std::runtime_error("film image extents differ");
            std::vector<float> pixels(raw, raw + static_cast<std::size_t>(width) * height * 4u);
            for (std::size_t i = 0; i < pixels.size(); ++i)
                if (!std::isfinite(pixels[i]) || (i % 4u != 3u && pixels[i] < -1.e-6f))
                    throw std::runtime_error("nonfinite or negative film signal");
            return pixels;
        };
        const auto film = read(argv[2]);
        std::vector<double> sum(film.size(), 0.0);
        std::vector<float> first;
        std::size_t changed = 0u;
        for (int frame = 3; frame < argc; ++frame)
        {
            const auto current = read(argv[frame]);
            if (frame == 3) first = current;
            for (std::size_t i = 0; i < current.size(); ++i)
            {
                if (i % 4u == 3u) continue;
                sum[i] += current[i];
                if (current[i] != first[i]) ++changed;
            }
        }
        double maxScaledError = 0.0, squaredError = 0.0, energy = 0.0;
        for (std::size_t i = 0; i < film.size(); ++i)
        {
            if (i % 4u == 3u) continue;
            const double expected = sum[i] / (argc - 3);
            const double error = std::abs(film[i] - expected);
            maxScaledError = std::max(maxScaledError, error / std::max(1.0, std::abs(expected)));
            squaredError += error * error;
            energy += expected;
        }
        std::cout << "Film oracle: frames=" << argc - 3 << " changed=" << changed
            << " max_scaled_error=" << maxScaledError
            << " rmse=" << std::sqrt(squaredError / (film.size() / 4u * 3u)) << '\n';
        if (changed == 0u || !(energy > 0.0) || maxScaledError > 2.e-5)
            throw std::runtime_error("progressive film does not match independent changing-frame arithmetic mean");
        return 0;
    }
}
