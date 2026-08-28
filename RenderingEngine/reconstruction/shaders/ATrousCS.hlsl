#include "ReconstructionCommon.hlsli"

[[vk::binding(0, 0)]] StructuredBuffer<L8GBufferRecord> gATrousGBuffer : register(t0);
[[vk::binding(1, 0)]] StructuredBuffer<L8SignalRecord> gATrousInput : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<float> gATrousVariance : register(t2);
[[vk::binding(3, 0)]] RWStructuredBuffer<L8SignalRecord> gATrousOutput : register(u0);
[[vk::binding(4, 0)]] RWStructuredBuffer<float> gATrousOutputVariance : register(u1);

[[vk::binding(5, 0)]] cbuffer L8ATrousConstants : register(b0)
{
    uint2 gATrousExtent;
    uint gATrousStep;
    float gPhiDepth;
    float gPhiNormal;
    float gPhiLuminance;
    float gMinimumVariance;
};

static const float gKernel[5] = {1.0f, 4.0f, 6.0f, 4.0f, 1.0f};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gATrousExtent)) return;
    const uint index = L8LinearIndex(pixel, gATrousExtent);
    const L8GBufferRecord centerGBuffer = gATrousGBuffer[index];
    const L8SignalRecord centerSignal = gATrousInput[index];
    L8SignalRecord safeCenterSignal = (L8SignalRecord)0;
    if (L8IsFiniteSignal(centerSignal)) safeCenterSignal = centerSignal;
    const float centerVariance = gATrousVariance[index];
    const float safeCenterVariance = isfinite(centerVariance) && centerVariance >= 0.0f
        ? max(centerVariance, gMinimumVariance)
        : gMinimumVariance;
    if (centerGBuffer.valid == 0u || centerGBuffer.linearDepth < 0.0f ||
        !isfinite(centerGBuffer.linearDepth) || !L8IsUsableNormal(centerGBuffer.worldNormal))
    {
        gATrousOutput[index] = safeCenterSignal;
        gATrousOutputVariance[index] = safeCenterVariance;
        return;
    }

    const float centerLuminance = L8Luminance(safeCenterSignal.diffuse + safeCenterSignal.specular);
    const float standardDeviation = sqrt(safeCenterVariance);
    const float3 centerNormal = L8SafeNormalize(centerGBuffer.worldNormal);
    L8SignalRecord sum = (L8SignalRecord)0;
    float weightSum = 0.0f;
    float weightedVariance = 0.0f;
    for (int kernelY = -2; kernelY <= 2; ++kernelY)
    {
        for (int kernelX = -2; kernelX <= 2; ++kernelX)
        {
            const int2 samplePixel = int2(pixel) + int2(kernelX, kernelY) * int(gATrousStep);
            if (any(samplePixel < 0) || any(samplePixel >= int2(gATrousExtent))) continue;
            const uint sampleIndex = L8LinearIndex(uint2(samplePixel), gATrousExtent);
            const L8GBufferRecord sampleGBuffer = gATrousGBuffer[sampleIndex];
            const L8SignalRecord sampleSignal = gATrousInput[sampleIndex];
            const float sampleVariance = gATrousVariance[sampleIndex];
            if (sampleGBuffer.valid == 0u || sampleGBuffer.linearDepth < 0.0f ||
                !isfinite(sampleGBuffer.linearDepth) || !L8IsUsableNormal(sampleGBuffer.worldNormal) ||
                !L8IsFiniteSignal(sampleSignal) || !isfinite(sampleVariance) || sampleVariance < 0.0f ||
                sampleGBuffer.materialId != centerGBuffer.materialId ||
                sampleGBuffer.objectId != centerGBuffer.objectId) continue;
            const float sampleLuminance = L8Luminance(sampleSignal.diffuse + sampleSignal.specular);
            const float depthScale = max(0.01f, abs(centerGBuffer.linearDepth) * 0.01f);
            const float depthWeight = exp(-abs(sampleGBuffer.linearDepth - centerGBuffer.linearDepth) /
                max(gPhiDepth * depthScale * float(gATrousStep), 1.0e-6f));
            const float normalWeight = pow(saturate(dot(
                centerNormal, L8SafeNormalize(sampleGBuffer.worldNormal))), gPhiNormal);
            const float luminanceWeight = exp(-abs(sampleLuminance - centerLuminance) /
                max(gPhiLuminance * standardDeviation, 1.0e-4f));
            const float weight = gKernel[kernelX + 2] * gKernel[kernelY + 2] *
                depthWeight * normalWeight * luminanceWeight;
            if (!isfinite(weight) || weight <= 0.0f || !isfinite(sampleLuminance)) continue;
            sum.diffuse += sampleSignal.diffuse * weight;
            sum.specular += sampleSignal.specular * weight;
            weightedVariance += sampleVariance * weight * weight;
            weightSum += weight;
        }
    }
    const bool sumsValid = weightSum > 0.0f && isfinite(weightSum) && isfinite(weightedVariance) &&
        L8IsFiniteSignal(sum);
    L8SignalRecord output = (L8SignalRecord)0;
    output.diffuse = sumsValid ? sum.diffuse / weightSum : safeCenterSignal.diffuse;
    output.specular = sumsValid ? sum.specular / weightSum : safeCenterSignal.specular;
    if (!L8IsFiniteSignal(output)) output = safeCenterSignal;
    gATrousOutput[index] = output;
    const float filteredVariance = sumsValid
        ? weightedVariance / (weightSum * weightSum)
        : safeCenterVariance;
    gATrousOutputVariance[index] = isfinite(filteredVariance)
        ? max(filteredVariance, gMinimumVariance)
        : safeCenterVariance;
}
