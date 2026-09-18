#include "../include/WavefrontShadowPolicy.hpp"

#include "app/RuntimeConfig.hpp"
#include "contracts/GpuRecordsAbiV1.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace RenderingEngine::Wavefront::Tests
{
    namespace
    {
        void Require(const bool condition, const char* const expression)
        {
            if (!condition)
            {
                throw std::runtime_error(expression);
            }
        }
    }

#define WF_SHADOW_REQUIRE(expression) Require(static_cast<bool>(expression), #expression)

    static_assert(static_cast<std::uint32_t>(ShadowSamplingMethod::Pcf)
        == static_cast<std::uint32_t>(ShadowMethod::Pcf));
    static_assert(static_cast<std::uint32_t>(ShadowSamplingMethod::Pcss)
        == static_cast<std::uint32_t>(ShadowMethod::Pcss));
    static_assert(static_cast<std::uint32_t>(ShadowSamplingMethod::Physical)
        == static_cast<std::uint32_t>(ShadowMethod::Physical));
    static_assert(kShadowQueueMetadataMethodLane == 3u);
    static_assert(sizeof(ShadowQueueItem)
        == sizeof(Contracts::AbiV1::GpuShadowQueueRecordV1));
    static_assert(offsetof(ShadowQueueItem, metadata)
        == offsetof(Contracts::AbiV1::GpuShadowQueueRecordV1, metadata));
    static_assert(sizeof(ShadowWorkItem) == 96u);
    static_assert(offsetof(ShadowWorkItem, sampling) == 80u);
    static_assert(std::is_standard_layout_v<ShadowWorkItem>);

    void RunWavefrontShadowPolicySelfTests()
    {
        WF_SHADOW_REQUIRE(IsValidShadowSamplingMethod(0u));
        WF_SHADOW_REQUIRE(IsValidShadowSamplingMethod(1u));
        WF_SHADOW_REQUIRE(IsValidShadowSamplingMethod(2u));
        WF_SHADOW_REQUIRE(!IsValidShadowSamplingMethod(3u));
        WF_SHADOW_REQUIRE(FatalShadowMethod == (1u << 20u));

        const ShadowSamplingPlan physical = MakeShadowSamplingPlan(
            static_cast<std::uint32_t>(ShadowSamplingMethod::Physical),
            0.5f,
            10.0f);
        WF_SHADOW_REQUIRE(physical.method == ShadowSamplingMethod::Physical);
        WF_SHADOW_REQUIRE(physical.blockerSearchTapCount == 0u);
        WF_SHADOW_REQUIRE(physical.filterTapCount == 1u);
        WF_SHADOW_REQUIRE(physical.VisibilityTraceCount() == 1u);
        WF_SHADOW_REQUIRE(physical.angularRadius == 0.0f);

        const ShadowSamplingPlan pcf = MakeShadowSamplingPlan(
            static_cast<std::uint32_t>(ShadowSamplingMethod::Pcf),
            0.5f,
            10.0f);
        WF_SHADOW_REQUIRE(pcf.method == ShadowSamplingMethod::Pcf);
        WF_SHADOW_REQUIRE(pcf.blockerSearchTapCount == 0u);
        WF_SHADOW_REQUIRE(pcf.filterTapCount == kShadowPcfFilterTapCount);
        WF_SHADOW_REQUIRE(pcf.VisibilityTraceCount() == 8u);
        WF_SHADOW_REQUIRE(std::abs(pcf.angularRadius - 0.05f) < 1.0e-6f);

        const ShadowSamplingPlan pcssNoBlocker = MakeShadowSamplingPlan(
            static_cast<std::uint32_t>(ShadowSamplingMethod::Pcss),
            0.5f,
            10.0f);
        WF_SHADOW_REQUIRE(
            pcssNoBlocker.blockerSearchTapCount == kShadowPcssBlockerTapCount);
        WF_SHADOW_REQUIRE(pcssNoBlocker.filterTapCount == 0u);
        WF_SHADOW_REQUIRE(pcssNoBlocker.VisibilityTraceCount() == 4u);
        WF_SHADOW_REQUIRE(pcssNoBlocker.fullyVisibleWithoutFilter);

        const ShadowSamplingPlan pcssNearBlocker = MakeShadowSamplingPlan(
            static_cast<std::uint32_t>(ShadowSamplingMethod::Pcss),
            0.5f,
            10.0f,
            2.0f);
        const ShadowSamplingPlan pcssFarBlocker = MakeShadowSamplingPlan(
            static_cast<std::uint32_t>(ShadowSamplingMethod::Pcss),
            0.5f,
            10.0f,
            8.0f);
        WF_SHADOW_REQUIRE(
            pcssNearBlocker.blockerSearchTapCount == kShadowPcssBlockerTapCount);
        WF_SHADOW_REQUIRE(
            pcssNearBlocker.filterTapCount == kShadowPcssFilterTapCount);
        WF_SHADOW_REQUIRE(pcssNearBlocker.VisibilityTraceCount() == 16u);
        WF_SHADOW_REQUIRE(pcssNearBlocker.angularRadius > pcf.angularRadius);
        WF_SHADOW_REQUIRE(pcssFarBlocker.angularRadius < pcf.angularRadius);
        WF_SHADOW_REQUIRE(!pcssNearBlocker.fullyVisibleWithoutFilter);

        const ShadowSamplingPlan deltaPcf = MakeShadowSamplingPlan(
            static_cast<std::uint32_t>(ShadowSamplingMethod::Pcf),
            0.0f,
            100.0f);
        WF_SHADOW_REQUIRE(
            std::abs(deltaPcf.angularRadius - 0.0025f) < 1.0e-7f);

        ShadowWorkItem work{};
        work.sampling.x = static_cast<std::uint32_t>(
            ShadowSamplingMethod::Pcss);
        ShadowQueueItem shared{};
        shared.metadata.w = work.sampling.x;
        WF_SHADOW_REQUIRE(shared.metadata.w == work.sampling.x);

        bool invalidMethodRejected = false;
        try
        {
            static_cast<void>(MakeShadowSamplingPlan(3u, 0.5f, 10.0f));
        }
        catch (const std::invalid_argument&)
        {
            invalidMethodRejected = true;
        }
        WF_SHADOW_REQUIRE(invalidMethodRejected);

        bool invalidBlockerRejected = false;
        try
        {
            static_cast<void>(MakeShadowSamplingPlan(
                static_cast<std::uint32_t>(ShadowSamplingMethod::Pcss),
                0.5f,
                10.0f,
                10.0f));
        }
        catch (const std::invalid_argument&)
        {
            invalidBlockerRejected = true;
        }
        WF_SHADOW_REQUIRE(invalidBlockerRejected);
    }

#undef WF_SHADOW_REQUIRE
}
