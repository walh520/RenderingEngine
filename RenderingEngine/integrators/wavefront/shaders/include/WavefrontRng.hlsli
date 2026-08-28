#ifndef RENDERING_ENGINE_WAVEFRONT_RNG_HLSLI
#define RENDERING_ENGINE_WAVEFRONT_RNG_HLSLI

// This mapping intentionally mirrors L6 pbr_rng/MegakernelBridge exactly.
static const uint kWfCameraJitterX = 0u;
static const uint kWfCameraJitterY = 1u;
static const uint kWfCameraLensU = 2u;
static const uint kWfCameraLensV = 3u;
static const uint kWfCameraDimensionCount = 4u;
static const uint kWfDimensionsPerBounce = 16u;
static const uint kWfLightSelection = 0u;
static const uint kWfLightShape0 = 1u;
static const uint kWfLightShape1 = 2u;
static const uint kWfLightShape2 = 3u;
static const uint kWfLightShape3 = 4u;
static const uint kWfBsdfLobe = 5u;
static const uint kWfBsdfU = 6u;
static const uint kWfBsdfV = 7u;
static const uint kWfDielectricEvent = 8u;
static const uint kWfRussianRoulette = 9u;
static const uint kWfAlphaMask = 10u;

uint WfBounceDimension(uint bounce, uint localDimension)
{
    return kWfCameraDimensionCount + bounce * kWfDimensionsPerBounce + localDimension;
}

uint4 WfPhiloxRound(uint4 counter, uint2 key)
{
    const uint multiplier0 = 0xd2511f53u;
    const uint multiplier1 = 0xcd9e8d57u;
    const uint a0Low = multiplier0 & 0xffffu;
    const uint a0High = multiplier0 >> 16u;
    const uint b0Low = counter.x & 0xffffu;
    const uint b0High = counter.x >> 16u;
    const uint product0LowLow = a0Low * b0Low;
    const uint product0LowHigh = a0Low * b0High;
    const uint product0HighLow = a0High * b0Low;
    const uint product0HighHigh = a0High * b0High;
    const uint middle0 = (product0LowLow >> 16u)
        + (product0LowHigh & 0xffffu)
        + (product0HighLow & 0xffffu);
    const uint low0 = (product0LowLow & 0xffffu) | (middle0 << 16u);
    const uint high0 = product0HighHigh
        + (product0LowHigh >> 16u)
        + (product0HighLow >> 16u)
        + (middle0 >> 16u);

    const uint a1Low = multiplier1 & 0xffffu;
    const uint a1High = multiplier1 >> 16u;
    const uint b1Low = counter.z & 0xffffu;
    const uint b1High = counter.z >> 16u;
    const uint product1LowLow = a1Low * b1Low;
    const uint product1LowHigh = a1Low * b1High;
    const uint product1HighLow = a1High * b1Low;
    const uint product1HighHigh = a1High * b1High;
    const uint middle1 = (product1LowLow >> 16u)
        + (product1LowHigh & 0xffffu)
        + (product1HighLow & 0xffffu);
    const uint low1 = (product1LowLow & 0xffffu) | (middle1 << 16u);
    const uint high1 = product1HighHigh
        + (product1LowHigh >> 16u)
        + (product1HighLow >> 16u)
        + (middle1 >> 16u);
    return uint4(
        high1 ^ counter.y ^ key.x,
        low1,
        high0 ^ counter.w ^ key.y,
        low0);
}

uint4 WfPhilox4x32TenRounds(uint4 counter, uint2 key)
{
    [unroll]
    for (uint round = 0u; round < 10u; ++round)
    {
        counter = WfPhiloxRound(counter, key);
        key += uint2(0x9e3779b9u, 0xbb67ae85u);
    }
    return counter;
}

float WfSampleDimension(
    uint pixelIndex,
    uint sampleIndex,
    uint dimension,
    uint streamTag,
    uint seedLow,
    uint seedHigh)
{
    const uint randomBits = WfPhilox4x32TenRounds(
        uint4(pixelIndex, sampleIndex, dimension, streamTag),
        uint2(seedLow, seedHigh)).x;
    // Match L6 exactly. The top 24 bits are representable without rounding,
    // so even 0xffffffff maps below 1.0.
    return float(randomBits >> 8u) * (1.0f / 16777216.0f);
}

#endif
