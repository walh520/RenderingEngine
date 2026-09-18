#include "reconstruction/AbiV2Bridge.hpp"
#include "reconstruction/History.hpp"
#include "reconstruction/MotionVectors.hpp"
#include "reconstruction/Svgf.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace rr = rendering::reconstruction;

namespace rendering::reconstruction::tests {
void RunVulkanReconstructionRecorderSelfTests();
}

namespace {

int gFailureCount = 0;

void Check(const bool condition, const std::string_view expression, const std::string_view testName, const int line) {
    if (!condition) {
        std::cerr << "[FAIL] " << testName << ':' << line << " - " << expression << '\n';
        ++gFailureCount;
    }
}

#define L8_CHECK(testName, expression) Check((expression), #expression, (testName), __LINE__)

[[nodiscard]] bool Near(const float a, const float b, const float tolerance = 1.0e-5F) noexcept {
    return std::abs(a - b) <= tolerance;
}

[[nodiscard]] rr::Matrix4x4 Translation(const float x, const float y, const float z) noexcept {
    rr::Matrix4x4 matrix = rr::Matrix4x4::Identity();
    matrix.values[3] = x;
    matrix.values[7] = y;
    matrix.values[11] = z;
    return matrix;
}

[[nodiscard]] rr::GBufferPixel ValidGBuffer(const std::uint32_t objectId = 7U) noexcept {
    rr::GBufferPixel gbuffer{};
    gbuffer.linearDepth = 5.0F;
    gbuffer.worldNormal = {0.0F, 0.0F, 1.0F};
    gbuffer.diffuseAlbedo = {1.0F, 1.0F, 1.0F};
    gbuffer.specularAlbedo = {1.0F, 1.0F, 1.0F};
    gbuffer.expectedPreviousLinearDepth = gbuffer.linearDepth;
    gbuffer.materialId = 3U;
    gbuffer.objectId = objectId;
    gbuffer.valid = true;
    gbuffer.motionValid = true;
    return gbuffer;
}

[[nodiscard]] rr::HistoryPixel MatchingHistory(const rr::GBufferPixel& gbuffer) noexcept {
    rr::HistoryPixel history{};
    history.linearDepth = gbuffer.linearDepth;
    history.worldNormal = gbuffer.worldNormal;
    history.materialId = gbuffer.materialId;
    history.objectId = gbuffer.objectId;
    history.historyLength = 2U;
    history.valid = true;
    return history;
}

[[nodiscard]] rr::Image<rr::GBufferPixel> UniformGBuffer(const rr::Extent2D extent) {
    return rr::Image<rr::GBufferPixel>(extent, ValidGBuffer());
}

[[nodiscard]] rr::Image<rr::SignalPixel> UniformSignal(const rr::Extent2D extent, const float value) {
    return rr::Image<rr::SignalPixel>(extent, rr::SignalPixel{{value, value, value}, {0.0F, 0.0F, 0.0F}});
}

void TestMotionVectorSignAndJitter() {
    constexpr std::string_view test = "MotionVectorSignAndJitter";
    rr::MotionVectorInput input{};
    input.previousViewProjection = Translation(0.2F, 0.0F, 0.0F);
    rr::MotionVectorResult result = rr::ComputeMotionVector(input);
    L8_CHECK(test, result.valid);
    L8_CHECK(test, Near(result.currentUv.x, 0.5F));
    L8_CHECK(test, Near(result.previousUv.x, 0.6F));
    L8_CHECK(test, Near(result.motion.x, 0.1F));
    L8_CHECK(test, Near(result.currentUv.x + result.motion.x, result.previousUv.x));
    L8_CHECK(test, Near(result.expectedPreviousLinearDepth, 0.0F));

    input = {};
    input.currentObjectToWorld = Translation(0.2F, 0.0F, 0.0F);
    result = rr::ComputeMotionVector(input);
    L8_CHECK(test, result.valid);
    L8_CHECK(test, Near(result.currentUv.x, 0.6F));
    L8_CHECK(test, Near(result.previousUv.x, 0.5F));
    L8_CHECK(test, Near(result.motion.x, -0.1F));

    input = {};
    input.currentJitterUv = {0.01F, -0.02F};
    input.previousJitterUv = {-0.02F, 0.03F};
    result = rr::ComputeMotionVector(input);
    L8_CHECK(test, Near(result.motion.x, -0.03F));
    L8_CHECK(test, Near(result.motion.y, 0.05F));

    input.previousWorldToView = Translation(0.0F, 0.0F, -2.0F);
    result = rr::ComputeMotionVector(input);
    L8_CHECK(test, result.valid);
    L8_CHECK(test, Near(result.expectedPreviousLinearDepth, 2.0F));

    input.currentViewProjection.values[15] = 0.0F;
    result = rr::ComputeMotionVector(input);
    L8_CHECK(test, !result.valid);

    input = {};
    input.currentViewProjection.values[15] = -1.0F;
    result = rr::ComputeMotionVector(input);
    L8_CHECK(test, !result.valid);

    input = {};
    input.objectPosition.x = std::numeric_limits<float>::max();
    input.currentViewProjection.values[15] = 2.0e-8F;
    input.previousViewProjection.values[15] = 2.0e-8F;
    result = rr::ComputeMotionVector(input);
    L8_CHECK(test, !result.valid);
}

void TestHistoryPingPongAndFramesInFlight() {
    constexpr std::string_view test = "HistoryPingPongAndFramesInFlight";
    const rr::Extent2D extent{2U, 2U};
    for (const std::uint32_t framesInFlight : {1U, 2U, 3U, 4U}) {
        rr::HistoryPingPong history(extent, framesInFlight);
        const std::uint64_t cycleLength = static_cast<std::uint64_t>(framesInFlight) * 2ULL;
        L8_CHECK(test, history.ResourceCount() == static_cast<std::size_t>(cycleLength));
        std::set<std::size_t> firstCycle{};
        for (std::uint64_t frame = 0ULL; frame < cycleLength; ++frame) {
            const std::size_t writeIndex = history.WriteResourceIndex(frame);
            firstCycle.insert(writeIndex);
            if (frame > 0ULL) {
                const auto readIndex = history.ReadResourceIndex(frame);
                L8_CHECK(test, readIndex.has_value());
                L8_CHECK(test, !readIndex.has_value() || *readIndex != writeIndex);
            }
            rr::HistorySurface& write = history.BeginWrite(frame, extent, rr::ResetTrigger::None);
            write[0].valid = true;
            history.Publish(frame);
        }
        L8_CHECK(test, firstCycle.size() == static_cast<std::size_t>(cycleLength));
        L8_CHECK(test, history.WriteResourceIndex(cycleLength) == history.WriteResourceIndex(0ULL));
    }

    rr::HistoryPingPong history(extent, 3U);
    static_cast<void>(history.BeginWrite(5ULL, extent, rr::ResetTrigger::None));
    history.Publish(5ULL);
    static_cast<void>(history.BeginWrite(6ULL, extent, rr::ResetTrigger::CameraCut));
    L8_CHECK(test, history.Previous(6ULL) == nullptr);
}

void TestValidationReasons() {
    constexpr std::string_view test = "ValidationReasons";
    const rr::ValidationConfig config{};
    rr::GBufferPixel current = ValidGBuffer();
    rr::HistoryPixel previous = MatchingHistory(current);
    L8_CHECK(test, rr::ValidateHistory(current, &previous, config, false) == rr::RejectReason::None);
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, nullptr, config, false), rr::RejectReason::NoHistory));
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &previous, config, true), rr::RejectReason::Reset));

    rr::HistoryPixel changed = previous;
    changed.linearDepth = 100.0F;
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::Depth));
    changed = previous;
    changed.worldNormal = {0.0F, 1.0F, 0.0F};
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::Normal));
    changed = previous;
    ++changed.materialId;
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::MaterialId));
    changed = previous;
    ++changed.objectId;
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::ObjectId));
    changed = previous;
    changed.valid = false;
    L8_CHECK(test, rr::ValidateHistory(current, &changed, config, false) == rr::RejectReason::InvalidHistory);
    changed = previous;
    changed.moments.first = std::numeric_limits<float>::quiet_NaN();
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::NonFinite));
    changed = previous;
    changed.variance = std::numeric_limits<float>::infinity();
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::NonFinite));
    changed = previous;
    changed.historyLength = 0U;
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &changed, config, false), rr::RejectReason::InvalidHistory));
    changed = previous;
    changed.historyLength = 33U;
    L8_CHECK(test, rr::HasReason(
        rr::ValidateHistory(current, &changed, config, false, 32U),
        rr::RejectReason::InvalidHistory));
    current.motionValid = false;
    L8_CHECK(test, rr::ValidateHistory(current, &previous, config, false) == rr::RejectReason::InvalidMotion);
    current = ValidGBuffer();
    current.expectedPreviousLinearDepth = 6.0F;
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &previous, config, false), rr::RejectReason::Depth));
    current.valid = false;
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &previous, config, false), rr::RejectReason::InvalidCurrent));
    current = ValidGBuffer();
    current.linearDepth = std::numeric_limits<float>::quiet_NaN();
    L8_CHECK(test, rr::HasReason(rr::ValidateHistory(current, &previous, config, false), rr::RejectReason::NonFinite));
}

void TestDemodulationRoundTrip() {
    constexpr std::string_view test = "DemodulationRoundTrip";
    rr::GBufferPixel gbuffer = ValidGBuffer();
    gbuffer.diffuseAlbedo = {0.2F, 0.4F, 0.8F};
    gbuffer.specularAlbedo = {0.5F, 0.25F, 0.1F};
    const rr::SignalPixel signal{{0.4F, 0.8F, 1.6F}, {1.0F, 0.5F, 0.2F}};
    const rr::SignalPixel demodulated = rr::Demodulate(signal, gbuffer, 1.0e-3F, true);
    const rr::SignalPixel restored = rr::Remodulate(demodulated, gbuffer, 1.0e-3F, true);
    L8_CHECK(test, Near(restored.diffuse.x, signal.diffuse.x));
    L8_CHECK(test, Near(restored.diffuse.y, signal.diffuse.y));
    L8_CHECK(test, Near(restored.diffuse.z, signal.diffuse.z));
    L8_CHECK(test, Near(restored.specular.x, signal.specular.x));
    L8_CHECK(test, Near(restored.specular.y, signal.specular.y));
    L8_CHECK(test, Near(restored.specular.z, signal.specular.z));

    const rr::SignalPixel preserved = rr::Demodulate(signal, gbuffer, 1.0e-3F, false);
    L8_CHECK(test, Near(preserved.specular.x, signal.specular.x));
}

void TestTemporalMomentsMotionAndReset() {
    constexpr std::string_view test = "TemporalMomentsMotionAndReset";
    const rr::Extent2D extent{2U, 1U};
    rr::SvgfConfig config{};
    config.temporal.minimumColorAlpha = 0.0F;
    config.temporal.minimumMomentsAlpha = 0.0F;
    config.atrous.iterationCount = 0U;
    rr::SvgfReconstruction reconstruction(extent, 2U, config);

    rr::Image<rr::GBufferPixel> gbuffer0 = UniformGBuffer(extent);
    rr::Image<rr::SignalPixel> signal0 = UniformSignal(extent, 1.0F);
    signal0.At(1U, 0U).diffuse = {2.0F, 2.0F, 2.0F};
    const rr::ReconstructionFrame frame0 = reconstruction.Process({gbuffer0, signal0, 0ULL, rr::ResetTrigger::None});
    L8_CHECK(test, frame0.historyLength.At(0U, 0U) == 1U);
    L8_CHECK(test, rr::HasReason(frame0.rejectReasons.At(0U, 0U), rr::RejectReason::NoHistory));

    rr::Image<rr::GBufferPixel> gbuffer1 = UniformGBuffer(extent);
    gbuffer1.At(1U, 0U).motion = {-0.5F, 0.0F};
    rr::Image<rr::SignalPixel> signal1 = UniformSignal(extent, 3.0F);
    const rr::ReconstructionFrame frame1 = reconstruction.Process({gbuffer1, signal1, 1ULL, rr::ResetTrigger::None});
    L8_CHECK(test, frame1.acceptedHistory.At(1U, 0U) == 1U);
    L8_CHECK(test, frame1.historyLength.At(1U, 0U) == 2U);
    L8_CHECK(test, Near(frame1.temporal.At(1U, 0U).diffuse.x, 2.0F));
    L8_CHECK(test, Near(frame1.moments.At(1U, 0U).first, 2.0F));
    L8_CHECK(test, Near(frame1.moments.At(1U, 0U).second, 5.0F));

    const rr::ReconstructionFrame cut = reconstruction.Process({gbuffer1, signal1, 2ULL, rr::ResetTrigger::CameraCut});
    L8_CHECK(test, cut.historyLength.At(1U, 0U) == 1U);
    L8_CHECK(test, rr::HasReason(cut.rejectReasons.At(1U, 0U), rr::RejectReason::Reset));

    gbuffer1.At(0U, 0U).motion = {2.0F, 0.0F};
    const rr::ReconstructionFrame offscreen = reconstruction.Process({gbuffer1, signal1, 3ULL, rr::ResetTrigger::None});
    L8_CHECK(test, offscreen.rejectReasons.At(0U, 0U) == rr::RejectReason::ScreenBounds);

    const std::array triggers{
        rr::ResetTrigger::CameraCut,
        rr::ResetTrigger::FieldOfView,
        rr::ResetTrigger::Resolution,
        rr::ResetTrigger::Scene,
        rr::ResetTrigger::Backend,
        rr::ResetTrigger::Transport,
        rr::ResetTrigger::ReconstructionParameters,
        rr::ResetTrigger::ShaderReload,
    };
    for (const rr::ResetTrigger trigger : triggers) {
        L8_CHECK(test, rr::RequiresTemporalReset(trigger));
    }
    L8_CHECK(test, !rr::RequiresTemporalReset(rr::ResetTrigger::None));
}

void TestTemporalDepthTimeDomainAndInvalidMotion() {
    constexpr std::string_view test = "TemporalDepthTimeDomainAndInvalidMotion";
    const rr::Extent2D extent{1U, 1U};
    rr::SvgfConfig config{};
    config.temporal.minimumColorAlpha = 0.0F;
    config.temporal.minimumMomentsAlpha = 0.0F;
    config.atrous.iterationCount = 0U;
    rr::SvgfReconstruction reconstruction(extent, 2U, config);

    rr::Image<rr::GBufferPixel> gbuffer0 = UniformGBuffer(extent);
    gbuffer0[0].linearDepth = 6.0F;
    gbuffer0[0].expectedPreviousLinearDepth = 6.0F;
    rr::Image<rr::SignalPixel> signal = UniformSignal(extent, 1.0F);
    static_cast<void>(reconstruction.Process({gbuffer0, signal, 0ULL, rr::ResetTrigger::None}));

    rr::Image<rr::GBufferPixel> gbuffer1 = UniformGBuffer(extent);
    gbuffer1[0].linearDepth = 5.0F;
    gbuffer1[0].expectedPreviousLinearDepth = 6.0F;
    signal[0].diffuse = {3.0F, 3.0F, 3.0F};
    const rr::ReconstructionFrame moved = reconstruction.Process({gbuffer1, signal, 1ULL, rr::ResetTrigger::None});
    L8_CHECK(test, moved.acceptedHistory[0] == 1U);
    L8_CHECK(test, moved.rejectReasons[0] == rr::RejectReason::None);

    rr::Image<rr::GBufferPixel> gbuffer2 = UniformGBuffer(extent);
    gbuffer2[0].linearDepth = 4.0F;
    gbuffer2[0].expectedPreviousLinearDepth = 5.0F;
    gbuffer2[0].motionValid = false;
    const rr::ReconstructionFrame invalidMotion = reconstruction.Process({gbuffer2, signal, 2ULL, rr::ResetTrigger::None});
    L8_CHECK(test, invalidMotion.acceptedHistory[0] == 0U);
    L8_CHECK(test, invalidMotion.rejectReasons[0] == rr::RejectReason::InvalidMotion);

    rr::Image<rr::GBufferPixel> gbuffer3 = UniformGBuffer(extent);
    gbuffer3[0].linearDepth = 3.0F;
    gbuffer3[0].expectedPreviousLinearDepth = 4.0F;
    const rr::ReconstructionFrame recovered = reconstruction.Process({gbuffer3, signal, 3ULL, rr::ResetTrigger::None});
    L8_CHECK(test, recovered.acceptedHistory[0] == 1U);
    L8_CHECK(test, recovered.historyLength[0] == 2U);
}

void TestResizeAndParameterReset() {
    constexpr std::string_view test = "ResizeAndParameterReset";
    rr::SvgfReconstruction reconstruction({1U, 1U}, 2U);
    rr::Image<rr::GBufferPixel> gbuffer1 = UniformGBuffer({1U, 1U});
    rr::Image<rr::SignalPixel> signal1 = UniformSignal({1U, 1U}, 1.0F);
    static_cast<void>(reconstruction.Process({gbuffer1, signal1, 0ULL, rr::ResetTrigger::None}));

    rr::Image<rr::GBufferPixel> gbuffer2 = UniformGBuffer({2U, 1U});
    rr::Image<rr::SignalPixel> signal2 = UniformSignal({2U, 1U}, 1.0F);
    const rr::ReconstructionFrame resized = reconstruction.Process({gbuffer2, signal2, 1ULL, rr::ResetTrigger::None});
    L8_CHECK(test, rr::HasReason(resized.rejectReasons.At(0U, 0U), rr::RejectReason::Reset));
    L8_CHECK(test, resized.historyLength.At(0U, 0U) == 1U);

    rr::SvgfConfig changed = reconstruction.Config();
    changed.atrous.phiDepth += 1.0F;
    reconstruction.SetConfig(changed);
    const rr::ReconstructionFrame parameterReset = reconstruction.Process({gbuffer2, signal2, 2ULL, rr::ResetTrigger::None});
    L8_CHECK(test, rr::HasReason(parameterReset.rejectReasons.At(0U, 0U), rr::RejectReason::Reset));
}

void TestDisocclusionAndEdgeStopping() {
    constexpr std::string_view test = "DisocclusionAndEdgeStopping";
    const rr::Extent2D extent{10U, 1U};
    rr::SvgfConfig config{};
    config.atrous.iterationCount = 1U;
    config.atrous.phiLuminance = 100000.0F;
    rr::SvgfReconstruction reconstruction(extent, 2U, config);
    rr::Image<rr::GBufferPixel> gbuffer = UniformGBuffer(extent);
    rr::Image<rr::SignalPixel> signal(extent);
    for (std::uint32_t x = 0U; x < extent.width; ++x) {
        const bool right = x >= 5U;
        gbuffer.At(x, 0U).objectId = right ? 2U : 1U;
        const float value = right ? 4.0F : 0.25F;
        signal.At(x, 0U).diffuse = {value, value, value};
    }
    gbuffer.At(3U, 0U).objectId = 3U;
    const rr::ReconstructionFrame frame0 = reconstruction.Process({gbuffer, signal, 0ULL, rr::ResetTrigger::None});
    L8_CHECK(test, Near(frame0.atrous.At(4U, 0U).diffuse.x, 0.25F, 1.0e-4F));
    L8_CHECK(test, Near(frame0.atrous.At(5U, 0U).diffuse.x, 4.0F, 1.0e-4F));

    rr::Image<rr::GBufferPixel> fallback = gbuffer;
    fallback.At(5U, 0U).motion = {-0.08F, 0.0F};
    fallback.At(5U, 0U).objectId = 3U;
    const rr::ReconstructionFrame frame1 = reconstruction.Process({fallback, signal, 1ULL, rr::ResetTrigger::None});
    L8_CHECK(test, frame1.acceptedHistory.At(5U, 0U) == 1U);

    rr::Image<rr::GBufferPixel> disoccluded = fallback;
    disoccluded.At(5U, 0U).objectId = 99U;
    const rr::ReconstructionFrame frame2 = reconstruction.Process({disoccluded, signal, 2ULL, rr::ResetTrigger::None});
    L8_CHECK(test, frame2.acceptedHistory.At(5U, 0U) == 0U);
    L8_CHECK(test, rr::HasReason(frame2.rejectReasons.At(5U, 0U), rr::RejectReason::ObjectId));
}

void TestVarianceBootstrapAndDenoisingMetric() {
    constexpr std::string_view test = "VarianceBootstrapAndDenoisingMetric";
    const rr::Extent2D extent{16U, 16U};
    rr::SvgfConfig config{};
    config.atrous.iterationCount = 2U;
    config.atrous.phiLuminance = 100000.0F;
    rr::SvgfReconstruction reconstruction(extent, 2U, config);
    rr::Image<rr::GBufferPixel> gbuffer = UniformGBuffer(extent);
    rr::Image<rr::SignalPixel> noisy(extent);
    for (std::uint32_t y = 0U; y < extent.height; ++y) {
        for (std::uint32_t x = 0U; x < extent.width; ++x) {
            const float value = ((x + y) & 1U) == 0U ? 0.5F : 1.5F;
            noisy.At(x, y).diffuse = {value, value, value};
        }
    }
    const rr::ReconstructionFrame frame = reconstruction.Process({gbuffer, noisy, 0ULL, rr::ResetTrigger::None});
    const rr::Image<rr::Float3> raw = reconstruction.SelectOutput(frame, gbuffer, rr::ReconstructionOutput::Raw);
    const rr::Image<rr::Float3> filtered = reconstruction.SelectOutput(frame, gbuffer, rr::ReconstructionOutput::ATrous);
    const rr::Image<rr::Float3> reference(extent, rr::Float3{1.0F, 1.0F, 1.0F});
    const float rawRmse = rr::ComputeRmse(raw, reference);
    const float filteredRmse = rr::ComputeRmse(filtered, reference);
    const float rawPsnr = rr::ComputePsnr(raw, reference);
    const float filteredPsnr = rr::ComputePsnr(filtered, reference);
    L8_CHECK(test, rawRmse > 0.49F);
    L8_CHECK(test, filteredRmse < rawRmse);
    L8_CHECK(test, filteredPsnr > rawPsnr);
    L8_CHECK(test, frame.variance.At(8U, 8U) > config.variance.minimumVariance);
    L8_CHECK(test, std::isinf(rr::ComputePsnr(reference, reference)));
    std::cout << "[metric] checkerboard raw_rmse=" << rawRmse
              << " atrous_rmse=" << filteredRmse
              << " raw_psnr=" << rawPsnr
              << " atrous_psnr=" << filteredPsnr << '\n';
}

void TestFiniteProtectionAndConfigValidation() {
    constexpr std::string_view test = "FiniteProtectionAndConfigValidation";
    const rr::Extent2D extent{5U, 5U};
    rr::SvgfConfig config{};
    config.atrous.iterationCount = 2U;
    rr::SvgfReconstruction reconstruction(extent, 2U, config);
    rr::Image<rr::GBufferPixel> gbuffer = UniformGBuffer(extent);
    rr::Image<rr::SignalPixel> signal = UniformSignal(extent, 1.0F);
    for (rr::GBufferPixel& pixel : gbuffer.Pixels()) {
        pixel.worldNormal = {0.0F, 0.0F, 10.0F};
    }
    signal.At(2U, 2U).diffuse.x = std::numeric_limits<float>::quiet_NaN();
    const rr::ReconstructionFrame frame = reconstruction.Process({gbuffer, signal, 0ULL, rr::ResetTrigger::None});
    for (std::size_t index = 0; index < frame.svgf.Size(); ++index) {
        L8_CHECK(test, rr::IsFinite(frame.temporal[index].diffuse));
        L8_CHECK(test, rr::IsFinite(frame.temporal[index].specular));
        L8_CHECK(test, std::isfinite(frame.moments[index].first));
        L8_CHECK(test, std::isfinite(frame.moments[index].second));
        L8_CHECK(test, rr::IsFinite(frame.atrous[index].diffuse));
        L8_CHECK(test, rr::IsFinite(frame.atrous[index].specular));
        L8_CHECK(test, rr::IsFinite(frame.svgf[index].diffuse));
        L8_CHECK(test, rr::IsFinite(frame.svgf[index].specular));
        L8_CHECK(test, std::isfinite(frame.variance[index]));
        L8_CHECK(test, frame.variance[index] >= config.variance.minimumVariance);
    }

    rr::SvgfReconstruction isolated({1U, 1U}, 2U, config);
    rr::Image<rr::GBufferPixel> isolatedGBuffer = UniformGBuffer({1U, 1U});
    rr::Image<rr::SignalPixel> nonFiniteSignal = UniformSignal({1U, 1U}, 1.0F);
    nonFiniteSignal[0].diffuse.x = std::numeric_limits<float>::quiet_NaN();
    const rr::ReconstructionFrame sanitized = isolated.Process({
        isolatedGBuffer, nonFiniteSignal, 0ULL, rr::ResetTrigger::None});
    L8_CHECK(test, rr::HasReason(sanitized.rejectReasons[0], rr::RejectReason::NonFinite));
    L8_CHECK(test, rr::IsFinite(sanitized.temporal[0].diffuse));
    L8_CHECK(test, rr::IsFinite(sanitized.temporal[0].specular));
    L8_CHECK(test, std::isfinite(sanitized.moments[0].first));
    L8_CHECK(test, std::isfinite(sanitized.moments[0].second));

    rr::Image<rr::SignalPixel> recoveredSignal = UniformSignal({1U, 1U}, 2.0F);
    const rr::ReconstructionFrame afterSanitizedHistory = isolated.Process({
        isolatedGBuffer, recoveredSignal, 1ULL, rr::ResetTrigger::None});
    L8_CHECK(test, afterSanitizedHistory.acceptedHistory[0] == 0U);
    L8_CHECK(test, rr::HasReason(
        afterSanitizedHistory.rejectReasons[0], rr::RejectReason::InvalidHistory));
    L8_CHECK(test, rr::IsFinite(afterSanitizedHistory.temporal[0].diffuse));
    L8_CHECK(test, rr::IsFinite(afterSanitizedHistory.temporal[0].specular));

    bool rejectedInvalidConfig = false;
    try {
        rr::SvgfConfig invalid = config;
        invalid.minimumAlbedo = 0.0F;
        rr::SvgfReconstruction invalidReconstruction(extent, 1U, invalid);
        static_cast<void>(invalidReconstruction);
    } catch (const std::invalid_argument&) {
        rejectedInvalidConfig = true;
    }
    L8_CHECK(test, rejectedInvalidConfig);
}

void TestAllOutputSurfaces() {
    constexpr std::string_view test = "AllOutputSurfaces";
    const rr::Extent2D extent{2U, 2U};
    rr::SvgfReconstruction reconstruction(extent, 1U);
    rr::Image<rr::GBufferPixel> gbuffer = UniformGBuffer(extent);
    rr::Image<rr::SignalPixel> signal = UniformSignal(extent, 1.0F);
    const rr::ReconstructionFrame frame = reconstruction.Process({gbuffer, signal, 0ULL, rr::ResetTrigger::None});
    const std::array outputs{
        rr::ReconstructionOutput::Raw,
        rr::ReconstructionOutput::Temporal,
        rr::ReconstructionOutput::ATrous,
        rr::ReconstructionOutput::Svgf,
        rr::ReconstructionOutput::Motion,
        rr::ReconstructionOutput::HistoryLength,
        rr::ReconstructionOutput::Moments,
        rr::ReconstructionOutput::Variance,
        rr::ReconstructionOutput::HistoryAcceptance,
        rr::ReconstructionOutput::RejectReasons,
    };
    for (const rr::ReconstructionOutput output : outputs) {
        const rr::Image<rr::Float3> selected = reconstruction.SelectOutput(frame, gbuffer, output);
        L8_CHECK(test, selected.Extent() == extent);
        L8_CHECK(test, rr::IsFinite(selected[0]));
    }
}

void TestAbiV2Bridge() {
    constexpr std::string_view test = "AbiV2Bridge";
    rr::GBufferPixel gbuffer{};
    gbuffer.linearDepth = 4.0F;
    gbuffer.worldNormal = {0.0F, 1.0F, 0.0F};
    gbuffer.diffuseAlbedo = {0.2F, 0.3F, 0.4F};
    gbuffer.specularAlbedo = {0.7F, 0.8F, 0.9F};
    gbuffer.expectedPreviousLinearDepth = 4.25F;
    gbuffer.motion = {-0.125F, 0.25F};
    gbuffer.materialId = 3U;
    gbuffer.objectId = 5U;
    gbuffer.valid = true;
    gbuffer.motionValid = true;
    rr::AbiV2SurfaceInputs surface{};
    surface.worldPosition = {1.0F, 2.0F, 3.0F};
    surface.geometricNormal = {0.0F, 2.0F, 0.0F};
    surface.roughness = 0.4F;
    surface.metallic = 0.6F;
    surface.primitiveId = 7U;
    rr::SplitSignalPixel signal{};
    signal.directDiffuse = {1.0F, 2.0F, 3.0F};
    signal.directSpecular = {4.0F, 5.0F, 6.0F};
    signal.indirectDiffuse = {7.0F, 8.0F, 9.0F};
    signal.indirectSpecular = {10.0F, 11.0F, 12.0F};

    const auto packed = rr::PackGBufferV2(
        gbuffer, surface, signal, rr::Float2{0.5F, 0.25F});
    L8_CHECK(test, packed.primary.identity.x == 3U);
    L8_CHECK(test, packed.primary.identity.y == 5U);
    L8_CHECK(test, packed.primary.identity.z == 7U);
    L8_CHECK(test, Near(packed.motion.currentPreviousUv.z, 0.375F));
    L8_CHECK(test, Near(packed.motion.currentPreviousUv.w, 0.5F));
    L8_CHECK(test, Near(packed.signal.indirectSpecular.z, 12.0F));

    const rr::AbiV2UnpackedGBuffer unpacked = rr::UnpackGBufferV2(packed);
    L8_CHECK(test, unpacked.gbuffer.valid && unpacked.gbuffer.motionValid);
    L8_CHECK(test, unpacked.gbuffer.materialId == 3U);
    L8_CHECK(test, unpacked.gbuffer.objectId == 5U);
    L8_CHECK(test, unpacked.surface.primitiveId == 7U);
    L8_CHECK(test, Near(unpacked.gbuffer.motion.x, -0.125F));
    L8_CHECK(test, Near(unpacked.signal.directSpecular.y, 5.0F));

    const auto history = rr::PackHistoryMetadataV2(
        rr::Moments{2.0F, 5.0F}, 1.25F, 17U, gbuffer, surface,
        0x0000000200000001ULL, 9U,
        RenderingEngine::Contracts::AbiV2::HistoryFlagValid);
    L8_CHECK(test, history.frameIdentity.x == 1U);
    L8_CHECK(test, history.frameIdentity.y == 2U);
    L8_CHECK(test, history.frameIdentity.z == 9U);
    L8_CHECK(test, Near(history.momentsVarianceHistory.w, 17.0F));
}

} // namespace

int main() {
    try {
        TestMotionVectorSignAndJitter();
        TestHistoryPingPongAndFramesInFlight();
        TestValidationReasons();
        TestDemodulationRoundTrip();
        TestTemporalMomentsMotionAndReset();
        TestTemporalDepthTimeDomainAndInvalidMotion();
        TestResizeAndParameterReset();
        TestDisocclusionAndEdgeStopping();
        TestVarianceBootstrapAndDenoisingMetric();
        TestFiniteProtectionAndConfigValidation();
        TestAllOutputSurfaces();
        TestAbiV2Bridge();
        rr::tests::RunVulkanReconstructionRecorderSelfTests();
    } catch (const std::exception& error) {
        std::cerr << "[UNCAUGHT] " << error.what() << '\n';
        ++gFailureCount;
    }

    if (gFailureCount != 0) {
        std::cerr << "L8 reconstruction tests failed: " << gFailureCount << '\n';
        return 1;
    }
    std::cout << "L8 reconstruction tests passed (12 CPU suites + Vulkan command-recorder suite).\n";
    return 0;
}
