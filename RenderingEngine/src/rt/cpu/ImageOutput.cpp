#include "rt/cpu/ImageOutput.hpp"

#include <tinyexr.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace RenderingEngine::Rt::Cpu
{
    namespace
    {
        constexpr std::size_t kChannelCount = 3u;
        constexpr std::size_t kBmpHeaderSize = 54u;
        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        static_assert(std::numeric_limits<float>::is_iec559);

        [[nodiscard]] bool ValidateLinearRgb(
            const std::uint32_t width,
            const std::uint32_t height,
            const std::span<const float> linearRgb,
            std::string& error)
        {
            error.clear();

            if (width == 0u || height == 0u)
            {
                error = "Image dimensions must both be greater than zero.";
                return false;
            }

            const std::size_t widthSize = static_cast<std::size_t>(width);
            const std::size_t heightSize = static_cast<std::size_t>(height);
            constexpr std::size_t maxSize = (std::numeric_limits<std::size_t>::max)();

            if (widthSize > maxSize / heightSize)
            {
                error = "Image dimensions overflow the addressable pixel count.";
                return false;
            }

            const std::size_t pixelCount = widthSize * heightSize;
            if (pixelCount > maxSize / kChannelCount)
            {
                error = "Image dimensions overflow the addressable RGB component count.";
                return false;
            }

            const std::size_t expectedComponents = pixelCount * kChannelCount;
            if (linearRgb.size() != expectedComponents)
            {
                error = "Linear RGB span contains " + std::to_string(linearRgb.size()) +
                    " components; expected " + std::to_string(expectedComponents) + ".";
                return false;
            }

            for (std::size_t componentIndex = 0u;
                 componentIndex < linearRgb.size();
                 ++componentIndex)
            {
                if (!std::isfinite(linearRgb[componentIndex]))
                {
                    error = "Linear RGB component " + std::to_string(componentIndex) +
                        " is NaN or infinite.";
                    return false;
                }
            }

            return true;
        }

        void StoreUint16LittleEndian(
            std::array<std::uint8_t, kBmpHeaderSize>& destination,
            const std::size_t offset,
            const std::uint16_t value)
        {
            destination[offset] = static_cast<std::uint8_t>(value & 0xffu);
            destination[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
        }

        void StoreUint32LittleEndian(
            std::array<std::uint8_t, kBmpHeaderSize>& destination,
            const std::size_t offset,
            const std::uint32_t value)
        {
            destination[offset] = static_cast<std::uint8_t>(value & 0xffu);
            destination[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
            destination[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
            destination[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
        }

        [[nodiscard]] std::uint8_t EncodeSrgb8(
            const float linearValue,
            const double exposureMultiplier)
        {
            const double exposed = static_cast<double>(linearValue) * exposureMultiplier;
            double encoded = 0.0;

            if (exposed >= 1.0)
            {
                encoded = 1.0;
            }
            else if (exposed > 0.0)
            {
                encoded = exposed <= 0.0031308
                    ? 12.92 * exposed
                    : 1.055 * std::pow(exposed, 1.0 / 2.4) - 0.055;
            }

            const double rounded = std::floor(encoded * 255.0 + 0.5);
            return static_cast<std::uint8_t>(rounded);
        }

        void HashByte(std::uint64_t& hash, const std::uint8_t value)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= kFnvPrime;
        }

        void HashUint32LittleEndian(std::uint64_t& hash, const std::uint32_t value)
        {
            HashByte(hash, static_cast<std::uint8_t>(value & 0xffu));
            HashByte(hash, static_cast<std::uint8_t>((value >> 8u) & 0xffu));
            HashByte(hash, static_cast<std::uint8_t>((value >> 16u) & 0xffu));
            HashByte(hash, static_cast<std::uint8_t>((value >> 24u) & 0xffu));
        }

        struct TinyExrErrorGuard
        {
            const char* message = nullptr;

            ~TinyExrErrorGuard()
            {
                if (message != nullptr)
                {
                    FreeEXRErrorMessage(message);
                }
            }

            TinyExrErrorGuard() = default;
            TinyExrErrorGuard(const TinyExrErrorGuard&) = delete;
            TinyExrErrorGuard& operator=(const TinyExrErrorGuard&) = delete;
        };
    }

    bool WriteLinearRgbExr(
        const std::filesystem::path& outputPath,
        const std::uint32_t width,
        const std::uint32_t height,
        const std::span<const float> linearRgb,
        std::string& error)
    {
        try
        {
            if (!ValidateLinearRgb(width, height, linearRgb, error))
            {
                return false;
            }
            if (outputPath.empty())
            {
                error = "EXR output path must not be empty.";
                return false;
            }
            if (width > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
                height > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
            {
                error = "EXR dimensions exceed the TinyEXR signed integer limit.";
                return false;
            }

            const std::u8string utf8Path = outputPath.u8string();
            const std::string pathString(
                reinterpret_cast<const char*>(utf8Path.data()),
                utf8Path.size());

            TinyExrErrorGuard tinyExrError;
            const int result = SaveEXR(
                linearRgb.data(),
                static_cast<int>(width),
                static_cast<int>(height),
                static_cast<int>(kChannelCount),
                0,
                pathString.c_str(),
                &tinyExrError.message);

            if (result != TINYEXR_SUCCESS)
            {
                error = "TinyEXR failed to write 32-bit float RGB output";
                if (tinyExrError.message != nullptr)
                {
                    error += ": ";
                    error += tinyExrError.message;
                }
                error += ".";
                return false;
            }

            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "Failed to write EXR output: ";
            error += exception.what();
            return false;
        }
    }

    bool WriteSrgbBmpPreview(
        const std::filesystem::path& outputPath,
        const std::uint32_t width,
        const std::uint32_t height,
        const std::span<const float> linearRgb,
        const float exposure,
        std::string& error)
    {
        try
        {
            if (!ValidateLinearRgb(width, height, linearRgb, error))
            {
                return false;
            }
            if (outputPath.empty())
            {
                error = "BMP output path must not be empty.";
                return false;
            }
            if (!std::isfinite(exposure) || exposure < 0.0f)
            {
                error = "BMP preview exposure must be finite and non-negative.";
                return false;
            }
            if (width > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)()) ||
                height > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)()))
            {
                error = "BMP dimensions exceed the signed 32-bit header limit.";
                return false;
            }

            const double exposureMultiplier = static_cast<double>(exposure);

            const std::uint64_t unpaddedRowBytes = static_cast<std::uint64_t>(width) * 3ull;
            const std::uint64_t rowStride = (unpaddedRowBytes + 3ull) & ~3ull;
            constexpr std::uint64_t maxBmpFileSize =
                static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)());
            if (rowStride > (maxBmpFileSize - kBmpHeaderSize) /
                static_cast<std::uint64_t>(height))
            {
                error = "BMP dimensions exceed the 32-bit file-size limit.";
                return false;
            }

            const std::uint64_t pixelDataSize = rowStride * static_cast<std::uint64_t>(height);
            const std::uint64_t fileSize = kBmpHeaderSize + pixelDataSize;

            std::array<std::uint8_t, kBmpHeaderSize> header{};
            header[0] = static_cast<std::uint8_t>('B');
            header[1] = static_cast<std::uint8_t>('M');
            StoreUint32LittleEndian(header, 2u, static_cast<std::uint32_t>(fileSize));
            StoreUint32LittleEndian(header, 10u, static_cast<std::uint32_t>(kBmpHeaderSize));
            StoreUint32LittleEndian(header, 14u, 40u);
            StoreUint32LittleEndian(header, 18u, width);

            const std::int32_t topDownHeight = -static_cast<std::int32_t>(height);
            StoreUint32LittleEndian(header, 22u, std::bit_cast<std::uint32_t>(topDownHeight));
            StoreUint16LittleEndian(header, 26u, 1u);
            StoreUint16LittleEndian(header, 28u, 24u);
            StoreUint32LittleEndian(header, 34u, static_cast<std::uint32_t>(pixelDataSize));

            std::ofstream stream(outputPath, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Failed to open BMP output path: " + outputPath.string();
                return false;
            }

            stream.write(
                reinterpret_cast<const char*>(header.data()),
                static_cast<std::streamsize>(header.size()));
            if (!stream)
            {
                error = "Failed to write the BMP header.";
                return false;
            }

            std::vector<std::uint8_t> row(static_cast<std::size_t>(rowStride), 0u);
            const std::size_t widthSize = static_cast<std::size_t>(width);
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                std::fill(row.begin(), row.end(), std::uint8_t{0});
                const std::size_t inputRowOffset =
                    static_cast<std::size_t>(y) * widthSize * kChannelCount;

                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const std::size_t inputOffset =
                        inputRowOffset + static_cast<std::size_t>(x) * kChannelCount;
                    const std::size_t outputOffset = static_cast<std::size_t>(x) * 3u;

                    row[outputOffset] = EncodeSrgb8(linearRgb[inputOffset + 2u], exposureMultiplier);
                    row[outputOffset + 1u] = EncodeSrgb8(linearRgb[inputOffset + 1u], exposureMultiplier);
                    row[outputOffset + 2u] = EncodeSrgb8(linearRgb[inputOffset], exposureMultiplier);
                }

                stream.write(
                    reinterpret_cast<const char*>(row.data()),
                    static_cast<std::streamsize>(row.size()));
                if (!stream)
                {
                    error = "Failed while writing BMP pixel data.";
                    return false;
                }
            }

            stream.close();
            if (!stream)
            {
                error = "Failed to finalize BMP output.";
                return false;
            }

            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "Failed to write BMP preview: ";
            error += exception.what();
            return false;
        }
    }

    std::uint64_t HashLinearRgbPixels(
        const std::uint32_t width,
        const std::uint32_t height,
        const std::span<const float> linearRgb,
        std::string& error)
    {
        if (!ValidateLinearRgb(width, height, linearRgb, error))
        {
            return 0u;
        }

        std::uint64_t hash = kFnvOffsetBasis;
        constexpr std::array<std::uint8_t, 8u> domain = {
            static_cast<std::uint8_t>('R'),
            static_cast<std::uint8_t>('E'),
            static_cast<std::uint8_t>('R'),
            static_cast<std::uint8_t>('G'),
            static_cast<std::uint8_t>('B'),
            1u,
            3u,
            0u
        };
        for (const std::uint8_t byte : domain)
        {
            HashByte(hash, byte);
        }

        HashUint32LittleEndian(hash, width);
        HashUint32LittleEndian(hash, height);
        for (const float component : linearRgb)
        {
            const float normalized = component == 0.0f ? 0.0f : component;
            HashUint32LittleEndian(hash, std::bit_cast<std::uint32_t>(normalized));
        }

        error.clear();
        return hash;
    }
}
