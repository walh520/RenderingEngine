#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV1
{
    // Human-facing contract name: abi-v1. Numeric ABI values are monotonic;
    // abi-v0 is 1 and zero remains reserved for uninitialized data.
    inline constexpr std::uint32_t kAbiVersion = 2u;
    inline constexpr std::uint32_t kInvalidId = 0xffffffffu;
}
