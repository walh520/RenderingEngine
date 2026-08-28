#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RenderingEngine::Wavefront
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

    // L7-private mirrors of WfFrameConstants/WfPassConstants. They are not a
    // published shared ABI; the static assertions make the private DX-layout
    // bridge explicit and mechanically checkable.
    struct alignas(16) WavefrontFrameConstants
    {
        Float4 cameraPositionTanHalfFov;
        Float4 cameraForwardAspect;
        Float4 cameraRightTime;
        Float4 cameraUpExposure;
        UInt4 imageSample;
        UInt4 capacityModeSeed;
        UInt4 dispatchLimits;
        Float4 fixtureLight;
        Float4 fixtureAlbedo;
    };

    struct alignas(16) WavefrontPassConstants
    {
        UInt4 pass;
        UInt4 params0;
        UInt4 params1;
        UInt4 params2;
    };

    enum class QueueMode : std::uint32_t
    {
        AtomicAppend = 0,
        PrefixScan = 1
    };

    enum class QueueId : std::uint32_t
    {
        RayA = 0,
        RayB = 1,
        Next = 2,
        Shadow = 3,
        Material = 4,
        Global = 5,
        Count = 6
    };

    enum FatalFlags : std::uint32_t
    {
        FatalNone = 0,
        FatalRayAOverflow = 1u << 0u,
        FatalRayBOverflow = 1u << 1u,
        FatalNextOverflow = 1u << 2u,
        FatalShadowOverflow = 1u << 3u,
        FatalMaterialOverflow = 1u << 4u,
        FatalDispatchOverflow = 1u << 5u,
        FatalInvalidCapacity = 1u << 6u,
        FatalScanOverflow = 1u << 7u
    };

    enum ResetFlags : std::uint32_t
    {
        ResetNone = 0,
        ResetCounters = 1u << 0u,
        ResetIndirectArguments = 1u << 1u,
        ResetValidateFrame = 1u << 2u
    };

    enum ScanFlags : std::uint32_t
    {
        ScanNone = 0,
        ScanInputIsFlags = 1u << 0u,
        ScanPrefixIsBase = 1u << 1u,
        ScanTopLevel = 1u << 2u,
        ScanChildIsBase = 1u << 3u
    };

    struct alignas(16) WavefrontRayItem
    {
        Float4 originTMin;
        Float4 directionTMax;
        UInt4 path; // path index, pixel index, bounce, flags.
    };

    struct alignas(16) MaterialWorkItem
    {
        Float4 positionT;
        Float4 geometricNormalBaryU;
        Float4 shadingNormalBaryV;
        UInt4 ids;      // instance, primitive, geometry, material.
        UInt4 identity; // path index, ray queue index, hit kind, hit flags.
    };

    struct alignas(16) NextBounceCandidate
    {
        Float4 originTMin;
        Float4 directionTMax;
        Float4 throughputEta;
        Float4 diffuseThroughput;
        Float4 specularThroughput;
        Float4 previousPositionPdf;
        Float4 previousGeometricNormal;
        Float4 previousShadingNormal;
        UInt4 identity; // path index, next bounce, event flags, reserved.
    };

    struct alignas(16) ShadowWorkItem
    {
        Float4 originTMin;
        Float4 directionTMax;
        Float4 diffuseContributionPdf;
        Float4 specularContributionLight;
        UInt4 identity; // path index, bounce, light index, ignored primitive.
    };

    struct alignas(16) WavefrontPathState
    {
        Float4 throughputEta;
        Float4 diffuseThroughput;
        Float4 specularThroughput;
        Float4 cameraEmission;
        Float4 directDiffuse;
        Float4 directSpecular;
        Float4 indirectDiffuse;
        Float4 indirectSpecular;
        Float4 previousPositionPdf;
        Float4 previousGeometricNormal;
        Float4 previousShadingNormal;
        UInt4 identity; // pixel index, sample index, bounce, path flags.
    };

    struct alignas(16) QueueHeader
    {
        std::uint32_t attemptedCount;
        std::uint32_t activeCount;
        std::uint32_t capacity;
        std::uint32_t overflowCount;
    };

    struct alignas(16) BounceCounters
    {
        UInt4 work;   // active, hit, shadow, next.
        UInt4 errors; // fatal mask, non-finite, negative PDF, non-empty dispatches.
    };

    // vkCmdDispatchIndirect reads the first 12 bytes as VkDispatchIndirectCommand.
    // A 16-byte stride keeps every slot naturally aligned for shader stores.
    struct alignas(16) DispatchCommandSlot
    {
        std::uint32_t groupCountX;
        std::uint32_t groupCountY;
        std::uint32_t groupCountZ;
        std::uint32_t reserved;
    };

    struct ScanPair
    {
        std::uint32_t next;
        std::uint32_t shadow;
    };

    static_assert(std::is_standard_layout_v<WavefrontRayItem>);
    static_assert(sizeof(Float4) == 16);
    static_assert(sizeof(UInt4) == 16);
    static_assert(sizeof(WavefrontFrameConstants) == 144);
    static_assert(sizeof(WavefrontPassConstants) == 64);
    static_assert(offsetof(WavefrontFrameConstants, imageSample) == 64);
    static_assert(offsetof(WavefrontFrameConstants, dispatchLimits) == 96);
    static_assert(offsetof(WavefrontPassConstants, params2) == 48);
    static_assert(sizeof(WavefrontRayItem) == 48);
    static_assert(sizeof(MaterialWorkItem) == 80);
    static_assert(sizeof(NextBounceCandidate) == 144);
    static_assert(sizeof(ShadowWorkItem) == 80);
    static_assert(sizeof(WavefrontPathState) == 192);
    static_assert(sizeof(QueueHeader) == 16);
    static_assert(sizeof(BounceCounters) == 32);
    static_assert(sizeof(DispatchCommandSlot) == 16);
    static_assert(sizeof(ScanPair) == 8);
    static_assert(offsetof(DispatchCommandSlot, groupCountX) == 0);
    static_assert(offsetof(DispatchCommandSlot, groupCountY) == 4);
    static_assert(offsetof(DispatchCommandSlot, groupCountZ) == 8);
    static_assert(offsetof(DispatchCommandSlot, reserved) == 12);
}
