#include "ReconstructionCommon.hlsli"

[[vk::binding(0, 0)]] StructuredBuffer<L8GBufferRecord> gVarianceGBuffer : register(t0);
[[vk::binding(1, 0)]] StructuredBuffer<L8SignalRecord> gVarianceSignal : register(t1);
[[vk::binding(2, 0)]] RWStructuredBuffer<L8HistoryRecord> gVarianceHistory : register(u0);
[[vk::binding(3, 0)]] RWStructuredBuffer<float> gVarianceOutput : register(u1);

[[vk::binding(4, 0)]] cbuffer L8VarianceConstants : register(b0)
{
    uint2 gVarianceExtent;
    uint gShortHistoryLength;
    uint gSpatialRadius;
    float gMinimumVariance;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gVarianceExtent)) return;
    const uint index = L8LinearIndex(pixel, gVarianceExtent);
    const L8GBufferRecord center = gVarianceGBuffer[index];
    L8HistoryRecord history = gVarianceHistory[index];
    if (center.valid == 0u || center.linearDepth < 0.0f || !isfinite(center.linearDepth) ||
        !L8IsUsableNormal(center.worldNormal))
    {
        history.variance = gMinimumVariance;
        gVarianceHistory[index] = history;
        gVarianceOutput[index] = gMinimumVariance;
        return;
    }
    const float3 centerNormal = L8SafeNormalize(center.worldNormal);
    float weightSum = 0.0f;
    float first = 0.0f;
    float second = 0.0f;
    const int radius = int(gSpatialRadius);

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const int2 samplePixel = int2(pixel) + int2(x, y);
            if (any(samplePixel < 0) || any(samplePixel >= int2(gVarianceExtent))) continue;
            const uint sampleIndex = L8LinearIndex(uint2(samplePixel), gVarianceExtent);
            const L8GBufferRecord sampleGBuffer = gVarianceGBuffer[sampleIndex];
            const L8SignalRecord signal = gVarianceSignal[sampleIndex];
            if (sampleGBuffer.valid == 0u || sampleGBuffer.linearDepth < 0.0f ||
                !isfinite(sampleGBuffer.linearDepth) || !L8IsUsableNormal(sampleGBuffer.worldNormal) ||
                !L8IsFiniteSignal(signal) || sampleGBuffer.materialId != center.materialId ||
                sampleGBuffer.objectId != center.objectId) continue;
            const float depthScale = max(0.01f, abs(center.linearDepth) * 0.02f);
            const float depthWeight = exp(-abs(sampleGBuffer.linearDepth - center.linearDepth) / depthScale);
            const float normalWeight = pow(saturate(dot(
                centerNormal, L8SafeNormalize(sampleGBuffer.worldNormal))), 32.0f);
            const float weight = depthWeight * normalWeight;
            const float luminance = L8Luminance(signal.diffuse + signal.specular);
            const float luminanceSquared = luminance * luminance;
            if (!isfinite(weight) || weight <= 0.0f || !isfinite(luminance) || !isfinite(luminanceSquared))
                continue;
            first += luminance * weight;
            second += luminanceSquared * weight;
            weightSum += weight;
        }
    }

    const bool sumsValid = weightSum > 0.0f && isfinite(weightSum) && isfinite(first) && isfinite(second);
    const float mean = sumsValid ? first / weightSum : 0.0f;
    const float rawSpatialVariance = sumsValid ? second / weightSum - mean * mean : 0.0f;
    const float spatialVariance = isfinite(rawSpatialVariance) ? max(rawSpatialVariance, 0.0f) : 0.0f;
    const float temporalBlend = saturate(float(history.historyLength) / float(max(gShortHistoryLength, 1u)));
    const float safeTemporalVariance = isfinite(history.variance) && history.variance >= 0.0f
        ? history.variance
        : spatialVariance;
    const float bootstrapped = lerp(spatialVariance, safeTemporalVariance, temporalBlend);
    history.variance = isfinite(bootstrapped) ? max(bootstrapped, gMinimumVariance) : gMinimumVariance;
    gVarianceHistory[index] = history;
    gVarianceOutput[index] = history.variance;
}
