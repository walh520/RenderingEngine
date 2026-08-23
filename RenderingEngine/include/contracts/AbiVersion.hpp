#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV0
{
    // Human-facing contract name: abi-v0. Numeric zero remains reserved for
    // uninitialized memory, so the first published wire revision is 1.
    inline constexpr std::uint32_t kAbiVersion = 1u;
    inline constexpr std::uint32_t kInvalidId = 0xffffffffu;
}
