#pragma once

#include "reconstruction/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rendering::reconstruction {

inline constexpr std::uint32_t kInvalidStableId = std::numeric_limits<std::uint32_t>::max();

struct Extent2D final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return width > 0U && height > 0U; }
    [[nodiscard]] constexpr std::size_t PixelCount() const noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

[[nodiscard]] constexpr bool operator==(const Extent2D a, const Extent2D b) noexcept {
    return a.width == b.width && a.height == b.height;
}

[[nodiscard]] constexpr bool operator!=(const Extent2D a, const Extent2D b) noexcept {
    return !(a == b);
}

template <typename Pixel>
class Image final {
public:
    Image() = default;
    explicit Image(const Extent2D extent, const Pixel& initial = {})
        : extent_(extent), pixels_(extent.PixelCount(), initial) {
        if (!extent.IsValid()) {
            throw std::invalid_argument("Image extent must be non-zero");
        }
    }

    [[nodiscard]] Extent2D Extent() const noexcept { return extent_; }
    [[nodiscard]] bool Empty() const noexcept { return pixels_.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return pixels_.size(); }

    [[nodiscard]] Pixel& At(const std::uint32_t x, const std::uint32_t y) {
        if (x >= extent_.width || y >= extent_.height) {
            throw std::out_of_range("Image coordinate is out of bounds");
        }
        return pixels_[static_cast<std::size_t>(y) * extent_.width + x];
    }

    [[nodiscard]] const Pixel& At(const std::uint32_t x, const std::uint32_t y) const {
        if (x >= extent_.width || y >= extent_.height) {
            throw std::out_of_range("Image coordinate is out of bounds");
        }
        return pixels_[static_cast<std::size_t>(y) * extent_.width + x];
    }

    [[nodiscard]] Pixel& operator[](const std::size_t index) noexcept { return pixels_[index]; }
    [[nodiscard]] const Pixel& operator[](const std::size_t index) const noexcept { return pixels_[index]; }
    [[nodiscard]] std::vector<Pixel>& Pixels() noexcept { return pixels_; }
    [[nodiscard]] const std::vector<Pixel>& Pixels() const noexcept { return pixels_; }

private:
    Extent2D extent_{};
    std::vector<Pixel> pixels_{};
};

// L8-private primary-hit record. This is deliberately not a shared ABI record.
// Motion is previous jittered UV minus current jittered UV, so historyUv = uv + motion.
struct GBufferPixel final {
    float linearDepth{};
    Float3 worldNormal{0.0F, 0.0F, 1.0F};
    Float3 diffuseAlbedo{1.0F, 1.0F, 1.0F};
    Float3 specularAlbedo{1.0F, 1.0F, 1.0F};
    // Positive view-space depth of this same surface point in the previous
    // camera. History depth validation must compare values from the same time.
    float expectedPreviousLinearDepth{};
    Float2 motion{};
    std::uint32_t materialId{kInvalidStableId};
    std::uint32_t objectId{kInvalidStableId};
    bool valid{};
    // Kept separate from surface validity: a current hit can remain useful for
    // spatial filtering and next-frame history when reprojection is invalid.
    bool motionValid{};
};

struct SignalPixel final {
    Float3 diffuse{};
    Float3 specular{};
};

struct Moments final {
    float first{};
    float second{};
};

enum class RejectReason : std::uint32_t {
    None = 0U,
    NoHistory = 1U << 0U,
    Reset = 1U << 1U,
    ScreenBounds = 1U << 2U,
    InvalidCurrent = 1U << 3U,
    InvalidHistory = 1U << 4U,
    NonFinite = 1U << 5U,
    Depth = 1U << 6U,
    Normal = 1U << 7U,
    MaterialId = 1U << 8U,
    ObjectId = 1U << 9U,
    InvalidMotion = 1U << 10U,
};

[[nodiscard]] constexpr RejectReason operator|(const RejectReason a, const RejectReason b) noexcept {
    return static_cast<RejectReason>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr RejectReason& operator|=(RejectReason& a, const RejectReason b) noexcept {
    a = a | b;
    return a;
}

[[nodiscard]] constexpr bool HasReason(const RejectReason value, const RejectReason reason) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(reason)) != 0U;
}

enum class ResetTrigger : std::uint32_t {
    None = 0U,
    CameraCut = 1U << 0U,
    FieldOfView = 1U << 1U,
    Resolution = 1U << 2U,
    Scene = 1U << 3U,
    Backend = 1U << 4U,
    Integrator = 1U << 5U,
    ReconstructionParameters = 1U << 6U,
    ShaderReload = 1U << 7U,
};

[[nodiscard]] constexpr ResetTrigger operator|(const ResetTrigger a, const ResetTrigger b) noexcept {
    return static_cast<ResetTrigger>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr ResetTrigger& operator|=(ResetTrigger& a, const ResetTrigger b) noexcept {
    a = a | b;
    return a;
}

[[nodiscard]] constexpr bool RequiresTemporalReset(const ResetTrigger trigger) noexcept {
    return trigger != ResetTrigger::None;
}

enum class ReconstructionOutput : std::uint32_t {
    Raw,
    Temporal,
    ATrous,
    Svgf,
    Motion,
    HistoryLength,
    Moments,
    Variance,
    HistoryAcceptance,
    RejectReasons,
};

struct ValidationConfig final {
    float relativeDepthThreshold{0.02F};
    float absoluteDepthThreshold{0.01F};
    float normalCosineThreshold{0.9063078F}; // cos(25 degrees)
    bool requireMaterialId{true};
    bool requireObjectId{true};
};

struct TemporalConfig final {
    std::uint32_t maxHistoryLength{32U};
    float minimumColorAlpha{0.05F};
    float minimumMomentsAlpha{0.10F};
};

struct VarianceConfig final {
    std::uint32_t shortHistoryLength{4U};
    std::uint32_t spatialRadius{3U}; // 7x7 short-history bootstrap footprint
    float minimumVariance{1.0e-6F};
};

struct ATrousConfig final {
    std::uint32_t iterationCount{4U};
    float phiDepth{1.0F};
    float phiNormal{64.0F};
    float phiLuminance{4.0F};
};

struct SvgfConfig final {
    ValidationConfig validation{};
    TemporalConfig temporal{};
    VarianceConfig variance{};
    ATrousConfig atrous{};
    float minimumAlbedo{1.0e-3F};
    bool demodulateSpecular{true};
};

} // namespace rendering::reconstruction
