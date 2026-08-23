#pragma once

#include <cstdint>

namespace RenderingEngine::Contracts::AbiV0
{
    struct alignas(16) AbiFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct alignas(16) AbiUInt4
    {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t z;
        std::uint32_t w;
    };

    // Four explicit mathematical rows. Both C++ and HLSL evaluate M * v as
    // dot(rowN, v); no native matrix type crosses the ABI boundary.
    struct alignas(16) AbiMat4Rows
    {
        AbiFloat4 row0;
        AbiFloat4 row1;
        AbiFloat4 row2;
        AbiFloat4 row3;
    };
}
