#pragma once

#include "reconstruction/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace rendering::reconstruction {

struct HistoryPixel final {
    Float3 demodulatedDiffuse{};
    Float3 demodulatedSpecular{};
    Moments moments{};
    float variance{};
    float linearDepth{};
    Float3 worldNormal{0.0F, 0.0F, 1.0F};
    std::uint32_t historyLength{};
    std::uint32_t materialId{kInvalidStableId};
    std::uint32_t objectId{kInvalidStableId};
    bool valid{};
};

using HistorySurface = Image<HistoryPixel>;

// Two logical history generations, each replicated once per in-flight frame.
// A frame writes only its own (parity, flight-slot) resource and reads the exact
// resource published for frameIndex - 1. Vulkan integration can therefore bind
// these indices without aliasing a resource still owned by another submission.
class HistoryPingPong final {
public:
    explicit HistoryPingPong(Extent2D extent, std::uint32_t framesInFlight);

    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept { return framesInFlight_; }
    [[nodiscard]] Extent2D Extent() const noexcept { return extent_; }
    [[nodiscard]] std::size_t ResourceCount() const noexcept { return resources_.size(); }
    [[nodiscard]] std::size_t WriteResourceIndex(std::uint64_t frameIndex) const noexcept;
    [[nodiscard]] std::optional<std::size_t> ReadResourceIndex(std::uint64_t frameIndex) const noexcept;
    [[nodiscard]] const HistorySurface* Previous(std::uint64_t frameIndex) const noexcept;

    HistorySurface& BeginWrite(std::uint64_t frameIndex, Extent2D extent, ResetTrigger trigger);
    void Publish(std::uint64_t frameIndex) noexcept;
    void Reset(Extent2D extent);

private:
    struct Resource final {
        explicit Resource(const Extent2D extent) : surface(extent) {}

        HistorySurface surface;
        std::uint64_t publishedFrame{std::numeric_limits<std::uint64_t>::max()};
        bool published{};
    };

    [[nodiscard]] std::size_t ResourceIndex(std::uint64_t frameIndex) const noexcept;

    Extent2D extent_{};
    std::uint32_t framesInFlight_{};
    std::vector<Resource> resources_{};
};

} // namespace rendering::reconstruction
