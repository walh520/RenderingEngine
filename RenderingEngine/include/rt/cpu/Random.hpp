#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace RenderingEngine::Rt::Cpu
{
    [[nodiscard]] inline constexpr std::uint64_t MixBits64(std::uint64_t value) noexcept
    {
        value += 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    class Pcg32 final
    {
    public:
        explicit Pcg32(
            const std::uint64_t seed,
            const std::uint64_t sequence = 0xda3e39cb94b95bdbull) noexcept
        {
            Seed(seed, sequence);
        }

        void Seed(const std::uint64_t seed, const std::uint64_t sequence) noexcept
        {
            state_ = 0u;
            increment_ = (sequence << 1u) | 1u;
            static_cast<void>(NextUInt());
            state_ += seed;
            static_cast<void>(NextUInt());
        }

        [[nodiscard]] std::uint32_t NextUInt() noexcept
        {
            const std::uint64_t oldState = state_;
            state_ = oldState * 6364136223846793005ull + increment_;
            const std::uint32_t xorshifted =
                static_cast<std::uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
            const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59u);
            return (xorshifted >> rotation) |
                (xorshifted << ((0u - rotation) & 31u));
        }

        template <typename Scalar>
        [[nodiscard]] Scalar Uniform() noexcept
        {
            static_assert(std::is_floating_point_v<Scalar>);
            if constexpr (std::is_same_v<Scalar, float>)
            {
                constexpr float kScale = 0x1.0p-24f;
                return static_cast<float>(NextUInt() >> 8u) * kScale;
            }
            else
            {
                constexpr double kScale = 0x1.0p-53;
                const std::uint64_t high = static_cast<std::uint64_t>(NextUInt() >> 5u);
                const std::uint64_t low = static_cast<std::uint64_t>(NextUInt() >> 6u);
                return static_cast<Scalar>((high * 67108864ull + low) * kScale);
            }
        }

    private:
        std::uint64_t state_ = 0u;
        std::uint64_t increment_ = 1u;
    };

    [[nodiscard]] inline Pcg32 MakeSampleRng(
        const std::uint64_t baseSeed,
        const std::uint64_t pixelIndex,
        const std::uint64_t sampleIndex) noexcept
    {
        const std::uint64_t sampleKey = MixBits64(
            baseSeed ^ MixBits64(pixelIndex) ^ MixBits64(sampleIndex + 0x632be59bd9b4e019ull));
        const std::uint64_t sequence = MixBits64(
            pixelIndex ^ (sampleIndex * 0x9e3779b97f4a7c15ull));
        return Pcg32(sampleKey, sequence);
    }
}
