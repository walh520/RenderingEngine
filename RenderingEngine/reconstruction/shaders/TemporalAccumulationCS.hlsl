#include "ReconstructionCommon.hlsli"

[[vk::binding(0, 4)]] StructuredBuffer<GpuGBufferRecordV2> gCurrentGBuffer;
[[vk::binding(5, 4)]] StructuredBuffer<L8SignalRecord> gCurrentDemodulated;
[[vk::binding(6, 4)]] StructuredBuffer<L8HistoryRecord> gPreviousHistory;
[[vk::binding(8, 4)]] RWStructuredBuffer<L8SignalRecord> gTemporalOutput;
[[vk::binding(7, 4)]] RWStructuredBuffer<L8HistoryRecord> gCurrentHistory;
[[vk::binding(9, 4)]] RWStructuredBuffer<L8TemporalDebugRecord> gTemporalDebug;

[[vk::binding(18, 4)]] cbuffer L8TemporalConstants
{
    uint2 gTemporalExtent;
    uint gHasPreviousHistory;
    uint gResetMask;
    uint gMaxHistoryLength;
    float gMinimumColorAlpha;
    float gMinimumMomentsAlpha;
    float gRelativeDepthThreshold;
    float gAbsoluteDepthThreshold;
    float gNormalCosineThreshold;
    uint gRequireMaterialId;
    uint gRequireObjectId;
};

uint L8ValidateHistoryTap(L8GBufferRecord current, L8HistoryRecord previous)
{
    if (previous.valid == 0u) return L8_REJECT_INVALID_HISTORY;
    uint reasons = previous.historyLength == 0u || previous.historyLength > max(gMaxHistoryLength, 1u)
        ? L8_REJECT_INVALID_HISTORY
        : 0u;
    if (!all(isfinite(float4(previous.linearDepth, previous.worldNormal))) ||
        previous.linearDepth < 0.0f || !L8IsUsableNormal(previous.worldNormal) ||
        !all(isfinite(previous.demodulatedDiffuse)) || !all(isfinite(previous.demodulatedSpecular)) ||
        !all(isfinite(previous.moments)) || !isfinite(previous.variance) || previous.variance < 0.0f)
        reasons |= L8_REJECT_NON_FINITE;
    const float depthTolerance = gAbsoluteDepthThreshold + gRelativeDepthThreshold *
        max(abs(current.expectedPreviousLinearDepth), abs(previous.linearDepth));
    if (abs(current.expectedPreviousLinearDepth - previous.linearDepth) > depthTolerance)
        reasons |= L8_REJECT_DEPTH;
    if (dot(L8SafeNormalize(current.worldNormal), L8SafeNormalize(previous.worldNormal)) < gNormalCosineThreshold)
        reasons |= L8_REJECT_NORMAL;
    if (gRequireMaterialId != 0u && current.materialId != previous.materialId)
        reasons |= L8_REJECT_MATERIAL_ID;
    if (gRequireObjectId != 0u && current.objectId != previous.objectId)
        reasons |= L8_REJECT_OBJECT_ID;
    return reasons;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gTemporalExtent)) return;
    const uint index = L8LinearIndex(pixel, gTemporalExtent);
    const L8GBufferRecord currentGBuffer = L8LoadGBuffer(gCurrentGBuffer[index]);
    const L8SignalRecord currentSignal = gCurrentDemodulated[index];
    const float originalCurrentLuminance = L8Luminance(currentSignal.diffuse + currentSignal.specular);
    const float originalCurrentLuminanceSquared = originalCurrentLuminance * originalCurrentLuminance;
    const bool currentSignalFinite = L8IsFiniteSignal(currentSignal) &&
        isfinite(originalCurrentLuminance) && isfinite(originalCurrentLuminanceSquared);
    L8SignalRecord safeCurrentSignal = (L8SignalRecord)0;
    if (currentSignalFinite) safeCurrentSignal = currentSignal;
    const float currentLuminance = currentSignalFinite ? originalCurrentLuminance : 0.0f;
    const float currentLuminanceSquared = currentSignalFinite ? originalCurrentLuminanceSquared : 0.0f;
    const float2 currentUv = (float2(pixel) + 0.5f.xx) / float2(gTemporalExtent);
    const float2 historyUv = currentUv + currentGBuffer.motion;

    uint baseReasons = 0u;
    if (gResetMask != 0u) baseReasons |= L8_REJECT_RESET;
    if (currentGBuffer.valid == 0u || !isfinite(currentGBuffer.linearDepth) || currentGBuffer.linearDepth < 0.0f ||
        !L8IsUsableNormal(currentGBuffer.worldNormal)) baseReasons |= L8_REJECT_INVALID_CURRENT;
    if (currentGBuffer.motionValid == 0u) baseReasons |= L8_REJECT_INVALID_MOTION;
    if (!all(isfinite(float4(currentGBuffer.motion, currentGBuffer.linearDepth, 0.0f))) ||
        !all(isfinite(currentGBuffer.worldNormal)) ||
        !isfinite(currentGBuffer.expectedPreviousLinearDepth) || !currentSignalFinite)
        baseReasons |= L8_REJECT_NON_FINITE;
    if (currentGBuffer.motionValid != 0u && currentGBuffer.expectedPreviousLinearDepth < 0.0f)
        baseReasons |= L8_REJECT_INVALID_MOTION;

    const bool inBounds = all(historyUv >= 0.0f.xx) && all(historyUv < 1.0f.xx);
    if (!inBounds) baseReasons |= L8_REJECT_SCREEN_BOUNDS;
    if (gHasPreviousHistory == 0u) baseReasons |= L8_REJECT_NO_HISTORY;

    L8HistoryRecord previous = (L8HistoryRecord)0;
    bool accepted = false;
    uint failedReasons = 0u;
    if (baseReasons == 0u)
    {
        const float2 historyPosition = historyUv * float2(gTemporalExtent) - 0.5f.xx;
        const int2 basePixel = int2(floor(historyPosition));
        const float2 fraction = frac(historyPosition);
        float weightSum = 0.0f;
        float weightedHistoryLength = 0.0f;
        [unroll]
        for (int tapY = 0; tapY < 2; ++tapY)
        {
            [unroll]
            for (int tapX = 0; tapX < 2; ++tapX)
            {
                const int2 tapPixel = basePixel + int2(tapX, tapY);
                const float weight = (tapX == 0 ? 1.0f - fraction.x : fraction.x) *
                    (tapY == 0 ? 1.0f - fraction.y : fraction.y);
                if (weight <= 0.0f) continue;
                if (any(tapPixel < 0) || any(tapPixel >= int2(gTemporalExtent)))
                {
                    failedReasons |= L8_REJECT_SCREEN_BOUNDS;
                    continue;
                }
                const L8HistoryRecord tap = gPreviousHistory[L8LinearIndex(uint2(tapPixel), gTemporalExtent)];
                const uint tapReasons = L8ValidateHistoryTap(currentGBuffer, tap);
                if (tapReasons != 0u)
                {
                    failedReasons |= tapReasons;
                    continue;
                }
                previous.demodulatedDiffuse += tap.demodulatedDiffuse * weight;
                previous.demodulatedSpecular += tap.demodulatedSpecular * weight;
                previous.moments += tap.moments * weight;
                previous.variance += tap.variance * weight;
                weightedHistoryLength += float(tap.historyLength) * weight;
                weightSum += weight;
            }
        }
        if (weightSum > 0.0f)
        {
            const float reciprocalWeight = rcp(weightSum);
            previous.demodulatedDiffuse *= reciprocalWeight;
            previous.demodulatedSpecular *= reciprocalWeight;
            previous.moments *= reciprocalWeight;
            previous.variance *= reciprocalWeight;
            previous.historyLength = uint(weightedHistoryLength * reciprocalWeight + 0.5f);
            previous.valid = 1u;
            accepted = true;
        }
        else
        {
            // All four bilinear taps failed: choose the closest valid tap from
            // the surrounding 3x3 footprint. Out-of-range taps are skipped.
            const int2 centerPixel = int2(floor(historyUv * float2(gTemporalExtent)));
            float bestDistanceSquared = 3.402823466e+38f;
            [unroll]
            for (int fallbackY = -1; fallbackY <= 1; ++fallbackY)
            {
                [unroll]
                for (int fallbackX = -1; fallbackX <= 1; ++fallbackX)
                {
                    const int2 tapPixel = centerPixel + int2(fallbackX, fallbackY);
                    if (any(tapPixel < 0) || any(tapPixel >= int2(gTemporalExtent)))
                    {
                        failedReasons |= L8_REJECT_SCREEN_BOUNDS;
                        continue;
                    }
                    const L8HistoryRecord tap = gPreviousHistory[L8LinearIndex(uint2(tapPixel), gTemporalExtent)];
                    const uint tapReasons = L8ValidateHistoryTap(currentGBuffer, tap);
                    if (tapReasons != 0u)
                    {
                        failedReasons |= tapReasons;
                        continue;
                    }
                    const float2 delta = (float2(tapPixel) + 0.5f.xx) - historyUv * float2(gTemporalExtent);
                    const float distanceSquared = dot(delta, delta);
                    if (distanceSquared < bestDistanceSquared)
                    {
                        bestDistanceSquared = distanceSquared;
                        previous = tap;
                        accepted = true;
                    }
                }
            }
        }
    }
    uint historyLength = 1u;
    L8SignalRecord accumulated = safeCurrentSignal;
    float2 moments = float2(currentLuminance, currentLuminanceSquared);
    if (accepted)
    {
        const uint maximumHistoryLength = max(gMaxHistoryLength, 1u);
        historyLength = min(previous.historyLength, maximumHistoryLength - 1u) + 1u;
        const float reciprocalHistory = rcp(float(historyLength));
        const float colorAlpha = max(gMinimumColorAlpha, reciprocalHistory);
        const float momentsAlpha = max(gMinimumMomentsAlpha, reciprocalHistory);
        accumulated.diffuse = lerp(previous.demodulatedDiffuse, safeCurrentSignal.diffuse, colorAlpha);
        accumulated.specular = lerp(previous.demodulatedSpecular, safeCurrentSignal.specular, colorAlpha);
        moments = lerp(previous.moments, moments, momentsAlpha);
    }

    float rawTemporalVariance = moments.y - moments.x * moments.x;
    uint payloadReasons = 0u;
    if (!L8IsFiniteSignal(accumulated) || !all(isfinite(moments)) || !isfinite(rawTemporalVariance))
    {
        payloadReasons |= L8_REJECT_NON_FINITE;
        accepted = false;
        accumulated = safeCurrentSignal;
        moments = float2(currentLuminance, currentLuminanceSquared);
        historyLength = 1u;
        rawTemporalVariance = moments.y - moments.x * moments.x;
    }
    const uint reasons = accepted ? 0u : (baseReasons | failedReasons | payloadReasons);

    L8HistoryRecord outputHistory = (L8HistoryRecord)0;
    outputHistory.demodulatedDiffuse = accumulated.diffuse;
    outputHistory.demodulatedSpecular = accumulated.specular;
    outputHistory.moments = moments;
    outputHistory.variance = max(rawTemporalVariance, 0.0f);
    outputHistory.linearDepth = isfinite(currentGBuffer.linearDepth) && currentGBuffer.linearDepth >= 0.0f
        ? currentGBuffer.linearDepth
        : 0.0f;
    outputHistory.worldNormal = L8IsUsableNormal(currentGBuffer.worldNormal)
        ? L8SafeNormalize(currentGBuffer.worldNormal)
        : float3(0.0f, 0.0f, 1.0f);
    outputHistory.historyLength = historyLength;
    outputHistory.materialId = currentGBuffer.materialId;
    outputHistory.objectId = currentGBuffer.objectId;
    outputHistory.valid = currentSignalFinite && currentGBuffer.valid != 0u && currentGBuffer.linearDepth >= 0.0f &&
        L8IsUsableNormal(currentGBuffer.worldNormal) && L8IsFiniteSignal(accumulated) &&
        all(isfinite(moments)) && isfinite(rawTemporalVariance) ? 1u : 0u;
    gTemporalOutput[index] = accumulated;
    gCurrentHistory[index] = outputHistory;
    gTemporalDebug[index].accepted = accepted ? 1u : 0u;
    gTemporalDebug[index].rejectReasons = reasons;
}
