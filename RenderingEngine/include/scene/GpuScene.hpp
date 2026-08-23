#pragma once

#include <cstdint>
#include <vector>

namespace RenderingEngine
{
    struct alignas(16) Float4
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct alignas(16) UInt4
    {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t z;
        std::uint32_t w;
    };

    // All GPU scene records use explicit 16-byte lanes. This keeps the C++ layout
    // identical to HLSL StructuredBuffer layout when DXC uses Vulkan DX layout.
    // Colors are linear Rec. 709 values.
    struct alignas(16) GpuMaterial
    {
        Float4 baseColorMetallic;        // rgb = base color, a = metallic [0, 1]
        Float4 emissiveRoughness;        // rgb = emitted radiance, a = perceptual roughness [0, 1]
        Float4 transmissionIor;          // x = transmission [0, 1], y = index of refraction
        Float4 attenuationColorDistance; // rgb = Beer-Lambert color, a = reference distance
    };

    struct alignas(16) GpuSphere
    {
        Float4 centerRadius;
        UInt4 metadata; // x = material index
    };

    struct alignas(16) GpuPlane
    {
        Float4 normalDistance; // Unit normal; dot(normal, position) + distance = 0.
        UInt4 metadata; // x = material index, y = flags (bit 0: checker pattern)
    };

    struct alignas(16) GpuLight
    {
        Float4 positionRadius; // xyz = center, w = sphere radius
        Float4 radiance;       // rgb = emitted radiance
    };

    struct SceneData
    {
        std::vector<GpuMaterial> materials;
        std::vector<GpuSphere> spheres;
        std::vector<GpuPlane> planes;
        std::vector<GpuLight> lights;
    };

    [[nodiscard]] SceneData CreateDemoScene();

    static_assert(sizeof(Float4) == 16);
    static_assert(sizeof(UInt4) == 16);
    static_assert(sizeof(GpuMaterial) == 64);
    static_assert(sizeof(GpuSphere) == 32);
    static_assert(sizeof(GpuPlane) == 32);
    static_assert(sizeof(GpuLight) == 32);
}
