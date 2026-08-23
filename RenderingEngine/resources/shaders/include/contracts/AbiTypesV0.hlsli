#ifndef RENDERING_ENGINE_ABI_TYPES_V0_HLSLI
#define RENDERING_ENGINE_ABI_TYPES_V0_HLSLI

struct AbiMat4Rows
{
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
};

float4 AbiMul(AbiMat4Rows matrixValue, float4 vectorValue)
{
    return float4(
        dot(matrixValue.row0, vectorValue),
        dot(matrixValue.row1, vectorValue),
        dot(matrixValue.row2, vectorValue),
        dot(matrixValue.row3, vectorValue));
}

#endif
