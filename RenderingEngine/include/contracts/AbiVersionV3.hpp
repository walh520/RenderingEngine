#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV3
{
    // Human-facing contract name: abi-v3. Numeric values are append-only:
    // abi-v0 = 1, abi-v1 = 2, abi-v2 = 3, and zero is reserved for
    // uninitialized data.
    inline constexpr std::uint32_t kAbiVersion = 4u;
    inline constexpr std::uint32_t kInvalidId = 0xffffffffu;
}
