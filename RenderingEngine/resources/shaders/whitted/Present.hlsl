struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct PresentConstants
{
    float exposure;
    uint applyManualGamma;
    float2 padding;
};

[[vk::binding(0, 0)]] Texture2D<float4> gHdrImage;
[[vk::binding(1, 0)]] SamplerState gHdrSampler;
[[vk::push_constant]] ConstantBuffer<PresentConstants> gPresent;

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

// HLSL adaptation of KhronosGroup/ToneMapping
// PBR_Neutral/pbrNeutral.glsl (Apache-2.0). Names, syntax, and control flow were
// adapted for this renderer; the published constants and transform are retained.
// PBR Neutral keeps authored base colors stable under neutral lighting while
// compressing HDR highlights without the strong hue shifts of filmic fits.
float3 PbrNeutralToneMap(float3 color)
{
    const float compressionStart = 0.76f;
    const float desaturation = 0.15f;

    const float darkestChannel = min(color.r, min(color.g, color.b));
    const float offset = darkestChannel < 0.08f
        ? darkestChannel - 6.25f * darkestChannel * darkestChannel
        : 0.04f;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < compressionStart)
    {
        return color;
    }

    const float distanceToWhite = 1.0f - compressionStart;
    const float compressedPeak = 1.0f
        - distanceToWhite * distanceToWhite / (peak + distanceToWhite - compressionStart);
    color *= compressedPeak / peak;
    const float desaturationWeight = 1.0f
        - 1.0f / (desaturation * (peak - compressedPeak) + 1.0f);
    return lerp(color, compressedPeak.xxx, desaturationWeight);
}

float3 LinearToSrgb(float3 color)
{
    const float3 low = color * 12.92f;
    const float3 high = 1.055f * pow(max(color, 0.0f), 1.0f / 2.4f) - 0.055f;
    return float3(
        color.r <= 0.0031308f ? low.r : high.r,
        color.g <= 0.0031308f ? low.g : high.g,
        color.b <= 0.0031308f ? low.b : high.b);
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    float3 color = max(gHdrImage.SampleLevel(gHdrSampler, input.uv, 0.0f).rgb, 0.0f);
    color = PbrNeutralToneMap(color * gPresent.exposure);

    // An SRGB swapchain performs this conversion in hardware. The fallback
    // UNORM path needs an explicit conversion.
    if (gPresent.applyManualGamma != 0u)
    {
        color = LinearToSrgb(color);
    }

    return float4(color, 1.0f);
}
