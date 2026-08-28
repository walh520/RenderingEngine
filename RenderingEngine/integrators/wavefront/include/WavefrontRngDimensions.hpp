#pragma once

#include <cstdint>

namespace RenderingEngine::Wavefront::RngDimensions
{
    inline constexpr std::uint32_t CameraJitterX = 0;
    inline constexpr std::uint32_t CameraJitterY = 1;
    inline constexpr std::uint32_t CameraLensU = 2;
    inline constexpr std::uint32_t CameraLensV = 3;
    inline constexpr std::uint32_t CameraSampleDimensionCount = 4;
    inline constexpr std::uint32_t FirstBounce = CameraSampleDimensionCount;
    inline constexpr std::uint32_t DimensionsPerBounce = 16;

    inline constexpr std::uint32_t LightSelection = 0;
    inline constexpr std::uint32_t LightU = 1;
    inline constexpr std::uint32_t LightV = 2;
    inline constexpr std::uint32_t LightShape0 = 1;
    inline constexpr std::uint32_t LightShape1 = 2;
    inline constexpr std::uint32_t LightShape2 = 3;
    inline constexpr std::uint32_t LightShape3 = 4;
    inline constexpr std::uint32_t BsdfLobe = 5;
    inline constexpr std::uint32_t BsdfU = 6;
    inline constexpr std::uint32_t BsdfV = 7;
    inline constexpr std::uint32_t DielectricEvent = 8;
    inline constexpr std::uint32_t RussianRoulette = 9;
    inline constexpr std::uint32_t AlphaMask = 10;

    [[nodiscard]] constexpr std::uint32_t ForBounce(
        const std::uint32_t bounce,
        const std::uint32_t localDimension) noexcept
    {
        return FirstBounce + bounce * DimensionsPerBounce + localDimension;
    }

    static_assert(ForBounce(0, RussianRoulette) == 13);
    static_assert(ForBounce(1, LightSelection) == 20);
}
