#include "reconstruction/Svgf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace rendering::reconstruction {
namespace {

[[nodiscard]] Float3 Combined(const SignalPixel& signal) noexcept {
    return signal.diffuse + signal.specular;
}

[[nodiscard]] SignalPixel LerpSignal(const SignalPixel& history, const SignalPixel& current, const float alpha) noexcept {
    return {
        Lerp(history.diffuse, current.diffuse, alpha),
        Lerp(history.specular, current.specular, alpha),
    };
}

[[nodiscard]] bool IsFinite(const SignalPixel& signal) noexcept {
    return IsFinite(signal.diffuse) && IsFinite(signal.specular);
}

[[nodiscard]] bool IsFinite(const Moments moments) noexcept {
    return std::isfinite(moments.first) && std::isfinite(moments.second);
}

[[nodiscard]] bool IsUsableNormal(const Float3 normal) noexcept {
    return IsFinite(normal) && Dot(normal, normal) > 1.0e-20F;
}

[[nodiscard]] bool IsFiniteGeometry(const GBufferPixel& gbuffer) noexcept {
    return std::isfinite(gbuffer.linearDepth) && gbuffer.linearDepth >= 0.0F &&
        IsUsableNormal(gbuffer.worldNormal);
}

[[nodiscard]] Float3 SafeAlbedo(const Float3 albedo, const float minimumAlbedo) noexcept {
    const float safeMinimum = std::isfinite(minimumAlbedo) && minimumAlbedo > 0.0F
        ? minimumAlbedo
        : 1.0e-3F;
    const auto channel = [safeMinimum](const float value) noexcept {
        return std::isfinite(value) ? std::max(value, safeMinimum) : safeMinimum;
    };
    return {channel(albedo.x), channel(albedo.y), channel(albedo.z)};
}

void ValidateConfigOrThrow(const SvgfConfig& config) {
    const auto finite = [](const float value) noexcept { return std::isfinite(value); };
    if (!finite(config.validation.relativeDepthThreshold) || config.validation.relativeDepthThreshold < 0.0F ||
        !finite(config.validation.absoluteDepthThreshold) || config.validation.absoluteDepthThreshold < 0.0F ||
        !finite(config.validation.normalCosineThreshold) || config.validation.normalCosineThreshold < -1.0F ||
        config.validation.normalCosineThreshold > 1.0F) {
        throw std::invalid_argument("SVGF history-validation parameters are invalid");
    }
    if (config.temporal.maxHistoryLength == 0U ||
        !finite(config.temporal.minimumColorAlpha) || config.temporal.minimumColorAlpha < 0.0F ||
        config.temporal.minimumColorAlpha > 1.0F ||
        !finite(config.temporal.minimumMomentsAlpha) || config.temporal.minimumMomentsAlpha < 0.0F ||
        config.temporal.minimumMomentsAlpha > 1.0F) {
        throw std::invalid_argument("SVGF temporal parameters are invalid");
    }
    if (config.variance.shortHistoryLength == 0U || config.variance.spatialRadius > 64U ||
        !finite(config.variance.minimumVariance) || config.variance.minimumVariance <= 0.0F) {
        throw std::invalid_argument("SVGF variance parameters are invalid");
    }
    if (config.atrous.iterationCount > 16U ||
        !finite(config.atrous.phiDepth) || config.atrous.phiDepth <= 0.0F ||
        !finite(config.atrous.phiNormal) || config.atrous.phiNormal < 0.0F ||
        !finite(config.atrous.phiLuminance) || config.atrous.phiLuminance <= 0.0F ||
        !finite(config.minimumAlbedo) || config.minimumAlbedo <= 0.0F) {
        throw std::invalid_argument("SVGF spatial/demodulation parameters are invalid");
    }
}

[[nodiscard]] RejectReason ValidateCurrentState(
    const GBufferPixel& current,
    const bool resetRequested) noexcept {
    RejectReason reason = RejectReason::None;
    if (resetRequested) {
        reason |= RejectReason::Reset;
    }
    if (!current.valid || !IsFiniteGeometry(current)) {
        reason |= RejectReason::InvalidCurrent;
    }
    if (!current.motionValid) {
        reason |= RejectReason::InvalidMotion;
    }
    if (!IsFinite(current.motion) || !std::isfinite(current.linearDepth) || !IsFinite(current.worldNormal) ||
        !std::isfinite(current.expectedPreviousLinearDepth)) {
        reason |= RejectReason::NonFinite;
    }
    if (current.motionValid && current.expectedPreviousLinearDepth < 0.0F) {
        reason |= RejectReason::InvalidMotion;
    }
    return reason;
}

[[nodiscard]] bool InBounds(const std::int64_t x, const std::int64_t y, const Extent2D extent) noexcept {
    return x >= 0 && y >= 0 && x < static_cast<std::int64_t>(extent.width) && y < static_cast<std::int64_t>(extent.height);
}

[[nodiscard]] Float2 PixelCenterUv(const std::uint32_t x, const std::uint32_t y, const Extent2D extent) noexcept {
    return {
        (static_cast<float>(x) + 0.5F) / static_cast<float>(extent.width),
        (static_cast<float>(y) + 0.5F) / static_cast<float>(extent.height),
    };
}

struct ReprojectionResult final {
    HistoryPixel history{};
    RejectReason rejectReasons{RejectReason::None};
    bool accepted{};
};

void AccumulateHistory(HistoryPixel& sum, const HistoryPixel& sample, const float weight) noexcept {
    sum.demodulatedDiffuse += sample.demodulatedDiffuse * weight;
    sum.demodulatedSpecular += sample.demodulatedSpecular * weight;
    sum.moments.first += sample.moments.first * weight;
    sum.moments.second += sample.moments.second * weight;
    sum.variance += sample.variance * weight;
}

[[nodiscard]] ReprojectionResult ReprojectHistory(
    const std::uint32_t x,
    const std::uint32_t y,
    const GBufferPixel& current,
    const Extent2D extent,
    const HistorySurface* const previousHistory,
    const ValidationConfig& validation,
    const std::uint32_t maximumHistoryLength,
    const bool resetRequested) noexcept {
    ReprojectionResult result{};
    RejectReason baseReasons = ValidateCurrentState(current, resetRequested);
    const Float2 uv = PixelCenterUv(x, y, extent) + current.motion;
    if (!IsFinite(uv) || uv.x < 0.0F || uv.y < 0.0F || uv.x >= 1.0F || uv.y >= 1.0F) {
        baseReasons |= RejectReason::ScreenBounds;
    }
    if (previousHistory == nullptr) {
        baseReasons |= RejectReason::NoHistory;
    }
    if (baseReasons != RejectReason::None) {
        result.rejectReasons = baseReasons;
        return result;
    }

    // Pixel-center-aware bilinear footprint. Every tap is validated before it
    // contributes and surviving weights are normalized rather than clamped.
    const float sampleX = uv.x * static_cast<float>(extent.width) - 0.5F;
    const float sampleY = uv.y * static_cast<float>(extent.height) - 0.5F;
    const std::int64_t baseX = static_cast<std::int64_t>(std::floor(sampleX));
    const std::int64_t baseY = static_cast<std::int64_t>(std::floor(sampleY));
    const float fractionX = sampleX - static_cast<float>(baseX);
    const float fractionY = sampleY - static_cast<float>(baseY);
    float weightSum = 0.0F;
    float weightedHistoryLength = 0.0F;
    RejectReason failedReasons = RejectReason::None;
    for (std::int64_t tapY = 0; tapY < 2; ++tapY) {
        for (std::int64_t tapX = 0; tapX < 2; ++tapX) {
            const std::int64_t coordinateX = baseX + tapX;
            const std::int64_t coordinateY = baseY + tapY;
            const float weightX = tapX == 0 ? 1.0F - fractionX : fractionX;
            const float weightY = tapY == 0 ? 1.0F - fractionY : fractionY;
            const float weight = weightX * weightY;
            if (weight <= 0.0F) {
                continue;
            }
            if (!InBounds(coordinateX, coordinateY, extent)) {
                failedReasons |= RejectReason::ScreenBounds;
                continue;
            }
            const HistoryPixel& tap = previousHistory->At(
                static_cast<std::uint32_t>(coordinateX),
                static_cast<std::uint32_t>(coordinateY));
            const RejectReason tapReasons = ValidateHistory(
                current, &tap, validation, false, maximumHistoryLength);
            if (tapReasons != RejectReason::None) {
                failedReasons |= tapReasons;
                continue;
            }
            AccumulateHistory(result.history, tap, weight);
            weightedHistoryLength += static_cast<float>(tap.historyLength) * weight;
            weightSum += weight;
        }
    }

    if (weightSum > 0.0F) {
        const float reciprocalWeight = 1.0F / weightSum;
        result.history.demodulatedDiffuse = result.history.demodulatedDiffuse * reciprocalWeight;
        result.history.demodulatedSpecular = result.history.demodulatedSpecular * reciprocalWeight;
        result.history.moments.first *= reciprocalWeight;
        result.history.moments.second *= reciprocalWeight;
        result.history.variance *= reciprocalWeight;
        result.history.historyLength = static_cast<std::uint32_t>(weightedHistoryLength * reciprocalWeight + 0.5F);
        result.history.valid = true;
        result.accepted = true;
        return result;
    }

    // If all bilinear taps fail, search the surrounding 3x3 footprint for the
    // closest individually valid sample. This recovers thin sub-pixel motion
    // without accepting a tap that failed depth/normal/stable-ID validation.
    const std::int64_t centerX = static_cast<std::int64_t>(std::floor(uv.x * static_cast<float>(extent.width)));
    const std::int64_t centerY = static_cast<std::int64_t>(std::floor(uv.y * static_cast<float>(extent.height)));
    float bestDistanceSquared = std::numeric_limits<float>::max();
    const HistoryPixel* best = nullptr;
    for (std::int64_t offsetY = -1; offsetY <= 1; ++offsetY) {
        for (std::int64_t offsetX = -1; offsetX <= 1; ++offsetX) {
            const std::int64_t coordinateX = centerX + offsetX;
            const std::int64_t coordinateY = centerY + offsetY;
            if (!InBounds(coordinateX, coordinateY, extent)) {
                failedReasons |= RejectReason::ScreenBounds;
                continue;
            }
            const HistoryPixel& tap = previousHistory->At(
                static_cast<std::uint32_t>(coordinateX),
                static_cast<std::uint32_t>(coordinateY));
            const RejectReason tapReasons = ValidateHistory(
                current, &tap, validation, false, maximumHistoryLength);
            if (tapReasons != RejectReason::None) {
                failedReasons |= tapReasons;
                continue;
            }
            const float dx = (static_cast<float>(coordinateX) + 0.5F) - uv.x * static_cast<float>(extent.width);
            const float dy = (static_cast<float>(coordinateY) + 0.5F) - uv.y * static_cast<float>(extent.height);
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                best = &tap;
            }
        }
    }
    if (best != nullptr) {
        result.history = *best;
        result.accepted = true;
        return result;
    }
    result.rejectReasons = failedReasons == RejectReason::None ? RejectReason::NoHistory : failedReasons;
    return result;
}

[[nodiscard]] float EdgeWeight(
    const GBufferPixel& center,
    const GBufferPixel& sample,
    const float centerLuminance,
    const float sampleLuminance,
    const float variance,
    const std::uint32_t step,
    const ATrousConfig& config) noexcept {
    if (!center.valid || !sample.valid || !IsFiniteGeometry(center) || !IsFiniteGeometry(sample) ||
        !std::isfinite(centerLuminance) || !std::isfinite(sampleLuminance) || !std::isfinite(variance) ||
        center.materialId != sample.materialId || center.objectId != sample.objectId) {
        return 0.0F;
    }

    const float depthScale = std::max(0.01F, std::abs(center.linearDepth) * 0.01F);
    const float depthDenominator = std::max(
        config.phiDepth * depthScale * static_cast<float>(step),
        1.0e-6F);
    const float depthWeight = std::exp(-std::abs(sample.linearDepth - center.linearDepth) / depthDenominator);
    const float normalCosine = std::clamp(
        Dot(Normalize(center.worldNormal), Normalize(sample.worldNormal)),
        0.0F,
        1.0F);
    const float normalWeight = std::pow(normalCosine, config.phiNormal);
    const float luminanceDenominator = std::max(config.phiLuminance * std::sqrt(std::max(variance, 0.0F)), 1.0e-4F);
    const float luminanceWeight = std::exp(-std::abs(sampleLuminance - centerLuminance) / luminanceDenominator);
    const float weight = depthWeight * normalWeight * luminanceWeight;
    return std::isfinite(weight) ? weight : 0.0F;
}

[[nodiscard]] Float3 RejectReasonColor(const RejectReason reason) noexcept {
    if (reason == RejectReason::None) {
        return {0.0F, 1.0F, 0.0F};
    }
    if (HasReason(reason, RejectReason::Reset)) {
        return {1.0F, 1.0F, 0.0F};
    }
    if (HasReason(reason, RejectReason::ScreenBounds)) {
        return {1.0F, 0.0F, 1.0F};
    }
    if (HasReason(reason, RejectReason::Depth)) {
        return {0.0F, 0.25F, 1.0F};
    }
    if (HasReason(reason, RejectReason::Normal)) {
        return {0.0F, 1.0F, 1.0F};
    }
    if (HasReason(reason, RejectReason::MaterialId)) {
        return {0.0F, 0.6F, 0.0F};
    }
    if (HasReason(reason, RejectReason::ObjectId)) {
        return {0.6F, 0.0F, 1.0F};
    }
    if (HasReason(reason, RejectReason::InvalidMotion)) {
        return {1.0F, 0.2F, 0.2F};
    }
    if (HasReason(reason, RejectReason::NonFinite)) {
        return {1.0F, 0.4F, 0.0F};
    }
    if (HasReason(reason, RejectReason::InvalidCurrent) || HasReason(reason, RejectReason::InvalidHistory)) {
        return {1.0F, 0.0F, 0.0F};
    }
    return {0.35F, 0.35F, 0.35F};
}

void ValidateMatchingExtents(const Extent2D a, const Extent2D b, const char* message) {
    if (a != b) {
        throw std::invalid_argument(message);
    }
}

} // namespace

SignalPixel Demodulate(
    const SignalPixel& signal,
    const GBufferPixel& gbuffer,
    const float minimumAlbedo,
    const bool demodulateSpecular) noexcept {
    const Float3 diffuseAlbedo = SafeAlbedo(gbuffer.diffuseAlbedo, minimumAlbedo);
    const Float3 specularAlbedo = SafeAlbedo(gbuffer.specularAlbedo, minimumAlbedo);
    return {
        signal.diffuse / diffuseAlbedo,
        demodulateSpecular ? signal.specular / specularAlbedo : signal.specular,
    };
}

SignalPixel Remodulate(
    const SignalPixel& signal,
    const GBufferPixel& gbuffer,
    const float minimumAlbedo,
    const bool demodulateSpecular) noexcept {
    const Float3 diffuseAlbedo = SafeAlbedo(gbuffer.diffuseAlbedo, minimumAlbedo);
    const Float3 specularAlbedo = SafeAlbedo(gbuffer.specularAlbedo, minimumAlbedo);
    return {
        signal.diffuse * diffuseAlbedo,
        demodulateSpecular ? signal.specular * specularAlbedo : signal.specular,
    };
}

RejectReason ValidateHistory(
    const GBufferPixel& current,
    const HistoryPixel* const previous,
    const ValidationConfig& config,
    const bool resetRequested,
    const std::uint32_t maximumHistoryLength) noexcept {
    RejectReason reason = ValidateCurrentState(current, resetRequested);
    if (previous == nullptr) {
        reason |= RejectReason::NoHistory;
        return reason;
    }
    if (!previous->valid) {
        reason |= RejectReason::InvalidHistory;
        return reason;
    }
    if (!std::isfinite(previous->linearDepth) || previous->linearDepth < 0.0F ||
        !IsUsableNormal(previous->worldNormal) || !IsFinite(previous->demodulatedDiffuse) ||
        !IsFinite(previous->demodulatedSpecular) || !IsFinite(previous->moments) ||
        !std::isfinite(previous->variance) || previous->variance < 0.0F) {
        reason |= RejectReason::NonFinite;
    }
    if (previous->historyLength == 0U ||
        previous->historyLength > std::max(maximumHistoryLength, 1U)) {
        reason |= RejectReason::InvalidHistory;
    }

    const float depthTolerance = config.absoluteDepthThreshold +
        config.relativeDepthThreshold * std::max(
            std::abs(current.expectedPreviousLinearDepth),
            std::abs(previous->linearDepth));
    if (std::abs(current.expectedPreviousLinearDepth - previous->linearDepth) > depthTolerance) {
        reason |= RejectReason::Depth;
    }
    if (Dot(Normalize(current.worldNormal), Normalize(previous->worldNormal)) < config.normalCosineThreshold) {
        reason |= RejectReason::Normal;
    }
    if (config.requireMaterialId && current.materialId != previous->materialId) {
        reason |= RejectReason::MaterialId;
    }
    if (config.requireObjectId && current.objectId != previous->objectId) {
        reason |= RejectReason::ObjectId;
    }
    return reason;
}

SvgfReconstruction::SvgfReconstruction(
    const Extent2D extent,
    const std::uint32_t framesInFlight,
    SvgfConfig config)
    : config_(config), history_(extent, framesInFlight) {
    ValidateConfigOrThrow(config_);
}

void SvgfReconstruction::SetConfig(SvgfConfig config) {
    ValidateConfigOrThrow(config);
    config_ = config;
    parametersChanged_ = true;
}

void SvgfReconstruction::Reset(const Extent2D extent) {
    history_.Reset(extent);
    parametersChanged_ = false;
}

ReconstructionFrame SvgfReconstruction::Process(const FrameInputs& inputs) {
    const Extent2D extent = inputs.gbuffer.Extent();
    ValidateMatchingExtents(extent, inputs.rawSignal.Extent(), "GBuffer and raw signal extents must match");

    ResetTrigger effectiveReset = inputs.resetTrigger;
    if (extent != history_.Extent()) {
        effectiveReset |= ResetTrigger::Resolution;
    }
    if (parametersChanged_) {
        effectiveReset |= ResetTrigger::ReconstructionParameters;
    }
    const bool resetRequested = RequiresTemporalReset(effectiveReset);

    HistorySurface& writeHistory = history_.BeginWrite(inputs.frameIndex, extent, effectiveReset);
    const HistorySurface* const previousHistory = resetRequested ? nullptr : history_.Previous(inputs.frameIndex);

    ReconstructionFrame result{
        Image<SignalPixel>(extent),
        Image<SignalPixel>(extent),
        Image<SignalPixel>(extent),
        Image<SignalPixel>(extent),
        Image<std::uint32_t>(extent),
        Image<Moments>(extent),
        Image<float>(extent),
        Image<std::uint32_t>(extent),
        Image<RejectReason>(extent),
    };
    Image<SignalPixel> demodulatedCurrent(extent);
    Image<SignalPixel> demodulatedTemporal(extent);

    for (std::uint32_t y = 0U; y < extent.height; ++y) {
        for (std::uint32_t x = 0U; x < extent.width; ++x) {
            const GBufferPixel& currentGBuffer = inputs.gbuffer.At(x, y);
            const SignalPixel& currentRaw = inputs.rawSignal.At(x, y);
            const SignalPixel currentDemodulated = Demodulate(
                currentRaw,
                currentGBuffer,
                config_.minimumAlbedo,
                config_.demodulateSpecular);
            result.raw.At(x, y) = currentRaw;

            const float originalCurrentLuminance = Luminance(Combined(currentDemodulated));
            const float originalCurrentLuminanceSquared =
                originalCurrentLuminance * originalCurrentLuminance;
            const bool currentSignalFinite = IsFinite(currentRaw) && IsFinite(currentDemodulated) &&
                std::isfinite(originalCurrentLuminance) &&
                std::isfinite(originalCurrentLuminanceSquared);
            const SignalPixel safeCurrentSignal = currentSignalFinite
                ? currentDemodulated
                : SignalPixel{};
            const float currentLuminance = currentSignalFinite ? originalCurrentLuminance : 0.0F;
            const float currentLuminanceSquared = currentSignalFinite
                ? originalCurrentLuminanceSquared
                : 0.0F;
            demodulatedCurrent.At(x, y) = safeCurrentSignal;

            const ReprojectionResult reprojected = ReprojectHistory(
                x,
                y,
                currentGBuffer,
                extent,
                previousHistory,
                config_.validation,
                config_.temporal.maxHistoryLength,
                resetRequested);
            RejectReason rejectReason = reprojected.rejectReasons;
            if (!currentSignalFinite) {
                rejectReason |= RejectReason::NonFinite;
            }

            bool accepted = reprojected.accepted && rejectReason == RejectReason::None;
            SignalPixel accumulated = safeCurrentSignal;
            Moments moments{};
            moments.first = currentLuminance;
            moments.second = currentLuminanceSquared;
            std::uint32_t historyLength = 1U;

            if (accepted) {
                const HistoryPixel& previousPixel = reprojected.history;
                const std::uint32_t maximumHistoryLength = std::max(config_.temporal.maxHistoryLength, 1U);
                historyLength = std::min(previousPixel.historyLength, maximumHistoryLength - 1U) + 1U;
                const float reciprocalHistory = 1.0F / static_cast<float>(historyLength);
                const float colorAlpha = std::max(config_.temporal.minimumColorAlpha, reciprocalHistory);
                const float momentsAlpha = std::max(config_.temporal.minimumMomentsAlpha, reciprocalHistory);
                const SignalPixel previousSignal{
                    previousPixel.demodulatedDiffuse,
                    previousPixel.demodulatedSpecular,
                };
                accumulated = LerpSignal(previousSignal, safeCurrentSignal, colorAlpha);
                moments.first = Lerp(previousPixel.moments.first, currentLuminance, momentsAlpha);
                moments.second = Lerp(previousPixel.moments.second, currentLuminanceSquared, momentsAlpha);
            }

            float rawTemporalVariance = moments.second - moments.first * moments.first;
            if (!IsFinite(accumulated) || !IsFinite(moments) || !std::isfinite(rawTemporalVariance)) {
                rejectReason |= RejectReason::NonFinite;
                accepted = false;
                accumulated = safeCurrentSignal;
                moments = {currentLuminance, currentLuminanceSquared};
                historyLength = 1U;
                rawTemporalVariance = moments.second - moments.first * moments.first;
            }
            const float temporalVariance = std::max(rawTemporalVariance, 0.0F);
            HistoryPixel& outputHistory = writeHistory.At(x, y);
            outputHistory.demodulatedDiffuse = accumulated.diffuse;
            outputHistory.demodulatedSpecular = accumulated.specular;
            outputHistory.moments = moments;
            outputHistory.variance = temporalVariance;
            outputHistory.linearDepth = std::isfinite(currentGBuffer.linearDepth) && currentGBuffer.linearDepth >= 0.0F
                ? currentGBuffer.linearDepth
                : 0.0F;
            outputHistory.worldNormal = IsUsableNormal(currentGBuffer.worldNormal)
                ? Normalize(currentGBuffer.worldNormal)
                : Float3{0.0F, 0.0F, 1.0F};
            outputHistory.historyLength = historyLength;
            outputHistory.materialId = currentGBuffer.materialId;
            outputHistory.objectId = currentGBuffer.objectId;
            outputHistory.valid = currentSignalFinite && currentGBuffer.valid && IsFiniteGeometry(currentGBuffer) &&
                IsFinite(accumulated) && IsFinite(moments) && std::isfinite(temporalVariance);

            demodulatedTemporal.At(x, y) = accumulated;
            result.temporal.At(x, y) = Remodulate(
                accumulated,
                currentGBuffer,
                config_.minimumAlbedo,
                config_.demodulateSpecular);
            result.historyLength.At(x, y) = historyLength;
            result.moments.At(x, y) = moments;
            result.variance.At(x, y) = temporalVariance;
            result.acceptedHistory.At(x, y) = accepted ? 1U : 0U;
            result.rejectReasons.At(x, y) = rejectReason;
        }
    }

    result.variance = BootstrapVariance(
        demodulatedTemporal,
        inputs.gbuffer,
        result.historyLength,
        result.variance);
    for (std::size_t index = 0; index < writeHistory.Size(); ++index) {
        writeHistory[index].variance = result.variance[index];
    }

    Image<std::uint32_t> rawHistoryLength(extent, 1U);
    Image<float> zeroVariance(extent, 0.0F);
    const Image<float> rawVariance = BootstrapVariance(
        demodulatedCurrent,
        inputs.gbuffer,
        rawHistoryLength,
        zeroVariance);
    const Image<SignalPixel> atrousOnly = FilterATrous(demodulatedCurrent, inputs.gbuffer, rawVariance);
    const Image<SignalPixel> svgf = FilterATrous(demodulatedTemporal, inputs.gbuffer, result.variance);

    for (std::uint32_t y = 0U; y < extent.height; ++y) {
        for (std::uint32_t x = 0U; x < extent.width; ++x) {
            const GBufferPixel& gbuffer = inputs.gbuffer.At(x, y);
            result.atrous.At(x, y) = Remodulate(
                atrousOnly.At(x, y),
                gbuffer,
                config_.minimumAlbedo,
                config_.demodulateSpecular);
            result.svgf.At(x, y) = Remodulate(
                svgf.At(x, y),
                gbuffer,
                config_.minimumAlbedo,
                config_.demodulateSpecular);
        }
    }

    history_.Publish(inputs.frameIndex);
    parametersChanged_ = false;
    return result;
}

Image<float> SvgfReconstruction::BootstrapVariance(
    const Image<SignalPixel>& signal,
    const Image<GBufferPixel>& gbuffer,
    const Image<std::uint32_t>& historyLength,
    const Image<float>& temporalVariance) const {
    const Extent2D extent = signal.Extent();
    ValidateMatchingExtents(extent, gbuffer.Extent(), "Signal and GBuffer extents must match");
    ValidateMatchingExtents(extent, historyLength.Extent(), "Signal and history-length extents must match");
    ValidateMatchingExtents(extent, temporalVariance.Extent(), "Signal and variance extents must match");
    Image<float> output(extent);
    const std::int64_t radius = static_cast<std::int64_t>(config_.variance.spatialRadius);

    for (std::uint32_t y = 0U; y < extent.height; ++y) {
        for (std::uint32_t x = 0U; x < extent.width; ++x) {
            float weightSum = 0.0F;
            float first = 0.0F;
            float second = 0.0F;
            const GBufferPixel& centerGBuffer = gbuffer.At(x, y);
            if (!centerGBuffer.valid || !IsFiniteGeometry(centerGBuffer)) {
                output.At(x, y) = config_.variance.minimumVariance;
                continue;
            }
            const Float3 centerNormal = Normalize(centerGBuffer.worldNormal);

            for (std::int64_t offsetY = -radius; offsetY <= radius; ++offsetY) {
                for (std::int64_t offsetX = -radius; offsetX <= radius; ++offsetX) {
                    const std::int64_t sampleX = static_cast<std::int64_t>(x) + offsetX;
                    const std::int64_t sampleY = static_cast<std::int64_t>(y) + offsetY;
                    if (!InBounds(sampleX, sampleY, extent)) {
                        continue;
                    }
                    const std::uint32_t sx = static_cast<std::uint32_t>(sampleX);
                    const std::uint32_t sy = static_cast<std::uint32_t>(sampleY);
                    const GBufferPixel& sampleGBuffer = gbuffer.At(sx, sy);
                    const SignalPixel& sampleSignal = signal.At(sx, sy);
                    if (!sampleGBuffer.valid || !IsFiniteGeometry(sampleGBuffer) || !IsFinite(sampleSignal) ||
                        sampleGBuffer.materialId != centerGBuffer.materialId ||
                        sampleGBuffer.objectId != centerGBuffer.objectId) {
                        continue;
                    }

                    const float depthScale = std::max(0.01F, std::abs(centerGBuffer.linearDepth) * 0.02F);
                    const float depthWeight = std::exp(-std::abs(sampleGBuffer.linearDepth - centerGBuffer.linearDepth) / depthScale);
                    const float normalWeight = std::pow(
                        std::clamp(Dot(centerNormal, Normalize(sampleGBuffer.worldNormal)), 0.0F, 1.0F),
                        32.0F);
                    const float weight = depthWeight * normalWeight;
                    const float luminance = Luminance(Combined(sampleSignal));
                    const float luminanceSquared = luminance * luminance;
                    if (!std::isfinite(weight) || weight <= 0.0F ||
                        !std::isfinite(luminance) || !std::isfinite(luminanceSquared)) {
                        continue;
                    }
                    first += luminance * weight;
                    second += luminanceSquared * weight;
                    weightSum += weight;
                }
            }

            const float spatialVariance = weightSum > 0.0F && std::isfinite(weightSum) &&
                std::isfinite(first) && std::isfinite(second)
                ? std::max(second / weightSum - (first / weightSum) * (first / weightSum), 0.0F)
                : 0.0F;
            const std::uint32_t shortLength = std::max(config_.variance.shortHistoryLength, 1U);
            const float temporalBlend = std::clamp(
                static_cast<float>(historyLength.At(x, y)) / static_cast<float>(shortLength),
                0.0F,
                1.0F);
            const float inputTemporalVariance = temporalVariance.At(x, y);
            const float safeTemporalVariance = std::isfinite(inputTemporalVariance) && inputTemporalVariance >= 0.0F
                ? inputTemporalVariance
                : spatialVariance;
            const float bootstrapped = Lerp(spatialVariance, safeTemporalVariance, temporalBlend);
            output.At(x, y) = std::isfinite(bootstrapped)
                ? std::max(bootstrapped, config_.variance.minimumVariance)
                : config_.variance.minimumVariance;
        }
    }
    return output;
}

Image<SignalPixel> SvgfReconstruction::FilterATrous(
    const Image<SignalPixel>& signal,
    const Image<GBufferPixel>& gbuffer,
    const Image<float>& variance) const {
    const Extent2D extent = signal.Extent();
    ValidateMatchingExtents(extent, gbuffer.Extent(), "Signal and GBuffer extents must match");
    ValidateMatchingExtents(extent, variance.Extent(), "Signal and variance extents must match");
    Image<SignalPixel> current = signal;
    Image<SignalPixel> next(extent);
    Image<float> currentVariance = variance;
    Image<float> nextVariance(extent);
    constexpr std::array<float, 5> kernel{1.0F, 4.0F, 6.0F, 4.0F, 1.0F};

    for (std::uint32_t iteration = 0U; iteration < config_.atrous.iterationCount; ++iteration) {
        const std::uint32_t step = 1U << std::min(iteration, 15U);
        for (std::uint32_t y = 0U; y < extent.height; ++y) {
            for (std::uint32_t x = 0U; x < extent.width; ++x) {
                const GBufferPixel& centerGBuffer = gbuffer.At(x, y);
                const SignalPixel& centerSignal = current.At(x, y);
                const SignalPixel safeCenterSignal = IsFinite(centerSignal) ? centerSignal : SignalPixel{};
                const float centerVariance = currentVariance.At(x, y);
                const float safeCenterVariance = std::isfinite(centerVariance) && centerVariance >= 0.0F
                    ? std::max(centerVariance, config_.variance.minimumVariance)
                    : config_.variance.minimumVariance;
                if (!centerGBuffer.valid || !IsFiniteGeometry(centerGBuffer)) {
                    next.At(x, y) = safeCenterSignal;
                    nextVariance.At(x, y) = safeCenterVariance;
                    continue;
                }

                const float centerLuminance = Luminance(Combined(safeCenterSignal));
                SignalPixel sum{};
                float weightSum = 0.0F;
                float weightedVariance = 0.0F;
                for (std::int32_t kernelY = -2; kernelY <= 2; ++kernelY) {
                    for (std::int32_t kernelX = -2; kernelX <= 2; ++kernelX) {
                        const std::int64_t sampleX = static_cast<std::int64_t>(x) +
                            static_cast<std::int64_t>(kernelX) * step;
                        const std::int64_t sampleY = static_cast<std::int64_t>(y) +
                            static_cast<std::int64_t>(kernelY) * step;
                        if (!InBounds(sampleX, sampleY, extent)) {
                            continue;
                        }
                        const std::uint32_t sx = static_cast<std::uint32_t>(sampleX);
                        const std::uint32_t sy = static_cast<std::uint32_t>(sampleY);
                        const SignalPixel& sampleSignal = current.At(sx, sy);
                        const float sampleVariance = currentVariance.At(sx, sy);
                        if (!IsFinite(sampleSignal) || !std::isfinite(sampleVariance) || sampleVariance < 0.0F) {
                            continue;
                        }
                        const float sampleLuminance = Luminance(Combined(sampleSignal));
                        const float spatialWeight = kernel[static_cast<std::size_t>(kernelX + 2)] *
                            kernel[static_cast<std::size_t>(kernelY + 2)];
                        const float edgeWeight = EdgeWeight(
                            centerGBuffer,
                            gbuffer.At(sx, sy),
                            centerLuminance,
                            sampleLuminance,
                            safeCenterVariance,
                            step,
                            config_.atrous);
                        const float weight = spatialWeight * edgeWeight;
                        if (!std::isfinite(weight) || weight <= 0.0F) {
                            continue;
                        }
                        sum.diffuse += sampleSignal.diffuse * weight;
                        sum.specular += sampleSignal.specular * weight;
                        weightedVariance += sampleVariance * weight * weight;
                        weightSum += weight;
                    }
                }
                const bool sumsValid = weightSum > 0.0F && std::isfinite(weightSum) &&
                    std::isfinite(weightedVariance) && IsFinite(sum);
                const SignalPixel filtered = sumsValid
                    ? SignalPixel{sum.diffuse / weightSum, sum.specular / weightSum}
                    : safeCenterSignal;
                next.At(x, y) = IsFinite(filtered) ? filtered : safeCenterSignal;
                const float filteredVariance = sumsValid
                    ? weightedVariance / (weightSum * weightSum)
                    : safeCenterVariance;
                nextVariance.At(x, y) = std::isfinite(filteredVariance)
                    ? std::max(filteredVariance, config_.variance.minimumVariance)
                    : safeCenterVariance;
            }
        }
        current = next;
        currentVariance = nextVariance;
    }
    return current;
}

Image<Float3> SvgfReconstruction::SelectOutput(
    const ReconstructionFrame& frame,
    const Image<GBufferPixel>& gbuffer,
    const ReconstructionOutput output) const {
    const Extent2D extent = gbuffer.Extent();
    Image<Float3> selected(extent);
    for (std::uint32_t y = 0U; y < extent.height; ++y) {
        for (std::uint32_t x = 0U; x < extent.width; ++x) {
            Float3 value{};
            switch (output) {
            case ReconstructionOutput::Raw:
                value = Combined(frame.raw.At(x, y));
                break;
            case ReconstructionOutput::Temporal:
                value = Combined(frame.temporal.At(x, y));
                break;
            case ReconstructionOutput::ATrous:
                value = Combined(frame.atrous.At(x, y));
                break;
            case ReconstructionOutput::Svgf:
                value = Combined(frame.svgf.At(x, y));
                break;
            case ReconstructionOutput::Motion:
                value = {
                    0.5F + gbuffer.At(x, y).motion.x * 16.0F,
                    0.5F + gbuffer.At(x, y).motion.y * 16.0F,
                    0.5F,
                };
                break;
            case ReconstructionOutput::HistoryLength: {
                const float normalized = static_cast<float>(frame.historyLength.At(x, y)) /
                    static_cast<float>(std::max(config_.temporal.maxHistoryLength, 1U));
                value = {normalized, normalized, normalized};
                break;
            }
            case ReconstructionOutput::Moments:
                value = {
                    frame.moments.At(x, y).first,
                    frame.moments.At(x, y).second,
                    0.0F,
                };
                break;
            case ReconstructionOutput::Variance: {
                const float standardDeviation = std::sqrt(std::max(frame.variance.At(x, y), 0.0F));
                value = {standardDeviation, standardDeviation, standardDeviation};
                break;
            }
            case ReconstructionOutput::HistoryAcceptance:
                value = frame.acceptedHistory.At(x, y) != 0U
                    ? Float3{0.0F, 1.0F, 0.0F}
                    : Float3{1.0F, 0.0F, 0.0F};
                break;
            case ReconstructionOutput::RejectReasons:
                value = RejectReasonColor(frame.rejectReasons.At(x, y));
                break;
            default:
                value = {};
                break;
            }
            selected.At(x, y) = value;
        }
    }
    return selected;
}

float ComputeRmse(const Image<Float3>& candidate, const Image<Float3>& reference) {
    ValidateMatchingExtents(candidate.Extent(), reference.Extent(), "RMSE images must have matching extents");
    double squaredError = 0.0;
    for (std::size_t index = 0; index < candidate.Size(); ++index) {
        const Float3 difference = candidate[index] - reference[index];
        squaredError += static_cast<double>(Dot(difference, difference));
    }
    const double sampleCount = static_cast<double>(candidate.Size()) * 3.0;
    return static_cast<float>(std::sqrt(squaredError / sampleCount));
}

float ComputePsnr(const Image<Float3>& candidate, const Image<Float3>& reference, const float peakValue) {
    if (!(peakValue > 0.0F)) {
        throw std::invalid_argument("PSNR peak value must be positive");
    }
    const float rmse = ComputeRmse(candidate, reference);
    if (rmse <= std::numeric_limits<float>::epsilon()) {
        return std::numeric_limits<float>::infinity();
    }
    return 20.0F * std::log10(peakValue / rmse);
}

} // namespace rendering::reconstruction
