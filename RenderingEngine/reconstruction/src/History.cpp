#include "reconstruction/History.hpp"

#include <stdexcept>

namespace rendering::reconstruction {

HistoryPingPong::HistoryPingPong(const Extent2D extent, const std::uint32_t framesInFlight)
    : extent_(extent), framesInFlight_(framesInFlight) {
    if (!extent.IsValid()) {
        throw std::invalid_argument("History extent must be non-zero");
    }
    if (framesInFlight == 0U) {
        throw std::invalid_argument("framesInFlight must be at least one");
    }

    resources_.reserve(static_cast<std::size_t>(framesInFlight) * 2U);
    for (std::uint32_t index = 0U; index < framesInFlight * 2U; ++index) {
        resources_.emplace_back(extent);
    }
}

std::size_t HistoryPingPong::ResourceIndex(const std::uint64_t frameIndex) const noexcept {
    // Advance to the other logical generation only after every flight slot in
    // the current generation has been used. Using frame parity here aliases
    // generations and slots for even frames-in-flight (for N=2: 0,3,0,3...).
    const std::uint64_t generation = (frameIndex / framesInFlight_) & 1ULL;
    const std::uint64_t flightSlot = frameIndex % framesInFlight_;
    return static_cast<std::size_t>(generation * framesInFlight_ + flightSlot);
}

std::size_t HistoryPingPong::WriteResourceIndex(const std::uint64_t frameIndex) const noexcept {
    return ResourceIndex(frameIndex);
}

std::optional<std::size_t> HistoryPingPong::ReadResourceIndex(const std::uint64_t frameIndex) const noexcept {
    if (frameIndex == 0ULL) {
        return std::nullopt;
    }
    const std::size_t index = ResourceIndex(frameIndex - 1ULL);
    const Resource& resource = resources_[index];
    if (!resource.published || resource.publishedFrame != frameIndex - 1ULL) {
        return std::nullopt;
    }
    return index;
}

const HistorySurface* HistoryPingPong::Previous(const std::uint64_t frameIndex) const noexcept {
    const std::optional<std::size_t> index = ReadResourceIndex(frameIndex);
    return index.has_value() ? &resources_[*index].surface : nullptr;
}

HistorySurface& HistoryPingPong::BeginWrite(
    const std::uint64_t frameIndex,
    const Extent2D extent,
    const ResetTrigger trigger) {
    if (extent != extent_ || RequiresTemporalReset(trigger)) {
        Reset(extent);
    }

    Resource& resource = resources_[ResourceIndex(frameIndex)];
    resource.published = false;
    resource.publishedFrame = std::numeric_limits<std::uint64_t>::max();
    resource.surface = HistorySurface(extent_);
    return resource.surface;
}

void HistoryPingPong::Publish(const std::uint64_t frameIndex) noexcept {
    Resource& resource = resources_[ResourceIndex(frameIndex)];
    resource.published = true;
    resource.publishedFrame = frameIndex;
}

void HistoryPingPong::Reset(const Extent2D extent) {
    if (!extent.IsValid()) {
        throw std::invalid_argument("History extent must be non-zero");
    }

    extent_ = extent;
    for (Resource& resource : resources_) {
        resource.surface = HistorySurface(extent_);
        resource.published = false;
        resource.publishedFrame = std::numeric_limits<std::uint64_t>::max();
    }
}

} // namespace rendering::reconstruction
