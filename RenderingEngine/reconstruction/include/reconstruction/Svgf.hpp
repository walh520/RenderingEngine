#pragma once

#include "reconstruction/History.hpp"

#include <cstdint>
#include <limits>

namespace rendering::reconstruction {

struct FrameInputs final {
    const Image<GBufferPixel>& gbuffer;
    const Image<SignalPixel>& rawSignal;
    std::uint64_t frameIndex{};
    ResetTrigger resetTrigger{ResetTrigger::None};
};

struct ReconstructionFrame final {
    Image<SignalPixel> raw;
    Image<SignalPixel> temporal;
    Image<SignalPixel> atrous;
    Image<SignalPixel> svgf;
    Image<std::uint32_t> historyLength;
    Image<Moments> moments;
    Image<float> variance;
    Image<std::uint32_t> acceptedHistory;
    Image<RejectReason> rejectReasons;
};

[[nodiscard]] SignalPixel Demodulate(
    const SignalPixel& signal,
    const GBufferPixel& gbuffer,
    float minimumAlbedo,
    bool demodulateSpecular) noexcept;

[[nodiscard]] SignalPixel Remodulate(
    const SignalPixel& signal,
    const GBufferPixel& gbuffer,
    float minimumAlbedo,
    bool demodulateSpecular) noexcept;

[[nodiscard]] RejectReason ValidateHistory(
    const GBufferPixel& current,
    const HistoryPixel* previous,
    const ValidationConfig& config,
    bool resetRequested,
    std::uint32_t maximumHistoryLength = std::numeric_limits<std::uint32_t>::max()) noexcept;

[[nodiscard]] float ComputeRmse(const Image<Float3>& candidate, const Image<Float3>& reference);
[[nodiscard]] float ComputePsnr(const Image<Float3>& candidate, const Image<Float3>& reference, float peakValue = 1.0F);

class SvgfReconstruction final {
public:
    SvgfReconstruction(Extent2D extent, std::uint32_t framesInFlight, SvgfConfig config = {});

    [[nodiscard]] const SvgfConfig& Config() const noexcept { return config_; }
    void SetConfig(SvgfConfig config);
    void Reset(Extent2D extent);

    [[nodiscard]] ReconstructionFrame Process(const FrameInputs& inputs);
    [[nodiscard]] Image<Float3> SelectOutput(
        const ReconstructionFrame& frame,
        const Image<GBufferPixel>& gbuffer,
        ReconstructionOutput output) const;

private:
    [[nodiscard]] Image<float> BootstrapVariance(
        const Image<SignalPixel>& signal,
        const Image<GBufferPixel>& gbuffer,
        const Image<std::uint32_t>& historyLength,
        const Image<float>& temporalVariance) const;

    [[nodiscard]] Image<SignalPixel> FilterATrous(
        const Image<SignalPixel>& signal,
        const Image<GBufferPixel>& gbuffer,
        const Image<float>& variance) const;

    SvgfConfig config_{};
    HistoryPingPong history_;
    bool parametersChanged_{};
};

} // namespace rendering::reconstruction
