#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV2
{
    // Human-facing contract name: abi-v2. Numeric values remain append-only:
    // abi-v0 = 1, abi-v1 = 2, and zero is reserved for uninitialized data.
    inline constexpr std::uint32_t kAbiVersion = 3u;
    inline constexpr std::uint32_t kInvalidId = 0xffffffffu;
}
