#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Double-precision, header-only mirror of the L6 HLSL BSDF implementation.
// It is intentionally private to the megakernel module and is suitable for
// deterministic numeric and shader-readback comparisons.
namespace RenderingEngine::Integrators::Megakernel::ReferenceBsdfL6
{
    constexpr double kPi = 3.141592653589793238462643383279502884;
    constexpr double kInversePi = 1.0 / kPi;
    constexpr double kDirectionEpsilon = 1.0e-7;
    constexpr double kMinimumAlpha = 1.0e-4;

    constexpr std::uint32_t kTransportRadiance = 0u;
    constexpr std::uint32_t kTransportImportance = 1u;

    constexpr std::uint32_t kMeasureInvalid = 0u;
    constexpr std::uint32_t kMeasureSolidAngle = 1u;
    constexpr std::uint32_t kMeasureDiscrete = 2u;

    constexpr std::uint32_t kLobeDiffuse = 1u << 0u;
    constexpr std::uint32_t kLobeGlossy = 1u << 1u;
    constexpr std::uint32_t kLobeSpecular = 1u << 2u;
    constexpr std::uint32_t kLobeReflection = 1u << 3u;
    constexpr std::uint32_t kLobeTransmission = 1u << 4u;
    constexpr std::uint32_t kLobeDiffuseReflection = kLobeDiffuse | kLobeReflection;
    constexpr std::uint32_t kLobeGlossyReflection = kLobeGlossy | kLobeReflection;
    constexpr std::uint32_t kLobeGlossyTransmission = kLobeGlossy | kLobeTransmission;
    constexpr std::uint32_t kLobeSpecularReflection = kLobeSpecular | kLobeReflection;
    constexpr std::uint32_t kLobeSpecularTransmission = kLobeSpecular | kLobeTransmission;
    constexpr std::uint32_t kLobeAll = kLobeDiffuse | kLobeGlossy | kLobeSpecular |
        kLobeReflection | kLobeTransmission;

    constexpr std::uint32_t kModelLambert = 0u;
    constexpr std::uint32_t kModelGgxConductor = 1u;
    constexpr std::uint32_t kModelGgxDielectricReflection = 2u;
    constexpr std::uint32_t kModelSmoothGlass = 3u;
    constexpr std::uint32_t kModelRoughDielectric = 4u;
    constexpr std::uint32_t kModelMetallicRoughness = 5u;

    struct Vec3
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    [[nodiscard]] constexpr Vec3 operator+(const Vec3 a, const Vec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3 a, const Vec3 b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3 value)
    {
        return { -value.x, -value.y, -value.z };
    }

    [[nodiscard]] constexpr Vec3 operator*(const Vec3 value, const double scalar)
    {
        return { value.x * scalar, value.y * scalar, value.z * scalar };
    }

    [[nodiscard]] constexpr Vec3 operator*(const double scalar, const Vec3 value)
    {
        return value * scalar;
    }

    [[nodiscard]] constexpr Vec3 operator*(const Vec3 a, const Vec3 b)
    {
        return { a.x * b.x, a.y * b.y, a.z * b.z };
    }

    [[nodiscard]] constexpr Vec3 operator/(const Vec3 value, const double scalar)
    {
        return { value.x / scalar, value.y / scalar, value.z / scalar };
    }

    [[nodiscard]] constexpr double Dot(const Vec3 a, const Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    [[nodiscard]] constexpr Vec3 Cross(const Vec3 a, const Vec3 b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    [[nodiscard]] inline bool IsFinite(const double value)
    {
        return std::isfinite(value);
    }

    [[nodiscard]] inline bool IsFinite(const Vec3 value)
    {
        return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
    }

    [[nodiscard]] inline bool SafeNormalize(const Vec3 value, Vec3& normalized)
    {
        normalized = {};
        const double lengthSquared = Dot(value, value);
        if (!IsFinite(lengthSquared) ||
            lengthSquared <= kDirectionEpsilon * kDirectionEpsilon)
        {
            return false;
        }
        normalized = value / std::sqrt(lengthSquared);
        return IsFinite(normalized);
    }

    [[nodiscard]] constexpr double Luminance(const Vec3 value)
    {
        return value.x * 0.2126 + value.y * 0.7152 + value.z * 0.0722;
    }

    [[nodiscard]] constexpr Vec3 Splat(const double value)
    {
        return { value, value, value };
    }

    [[nodiscard]] constexpr bool ContainsFlags(
        const std::uint32_t mask,
        const std::uint32_t required)
    {
        return (mask & required) == required;
    }

    [[nodiscard]] inline bool ColorInUnitRange(const Vec3 value)
    {
        return IsFinite(value) && value.x >= 0.0 && value.x <= 1.0 &&
            value.y >= 0.0 && value.y <= 1.0 &&
            value.z >= 0.0 && value.z <= 1.0;
    }

    struct BsdfContextL6
    {
        Vec3 geometricNormal{};
        double etaIncident = 1.0;
        Vec3 shadingNormal{};
        double etaTransmitted = 1.5;
        Vec3 tangent{};
        std::uint32_t transportMode = kTransportRadiance;
    };

    struct BsdfParamsL6
    {
        Vec3 baseColor{};
        double metallic = 0.0;
        Vec3 f0{ 0.04, 0.04, 0.04 };
        double perceptualRoughness = 0.5;
        Vec3 conductorEta{ 1.0, 1.0, 1.0 };
        double transmission = 0.0;
        Vec3 conductorK{};
        std::uint32_t model = kModelLambert;
        std::uint32_t allowedLobes = kLobeAll;
        std::uint32_t useComplexConductorFresnel = 0u;
        std::uint32_t reserved0 = 0u;
        std::uint32_t reserved1 = 0u;
    };

    struct BsdfEvalL6
    {
        Vec3 value{};
        double pdf = 0.0;
        Vec3 diffuseValue{};
        double reserved0 = 0.0;
        Vec3 specularValue{};
        double reserved1 = 0.0;
        std::uint32_t measure = kMeasureInvalid;
        std::uint32_t lobeFlags = 0u;
        std::uint32_t isValid = 0u;
        std::uint32_t reserved2 = 0u;
    };

    struct BsdfSampleL6
    {
        Vec3 direction{};
        double pdf = 0.0;
        Vec3 value{};
        double eta = 1.0;
        Vec3 diffuseValue{};
        double reserved0 = 0.0;
        Vec3 specularValue{};
        double reserved1 = 0.0;
        std::uint32_t measure = kMeasureInvalid;
        std::uint32_t lobeFlags = 0u;
        std::uint32_t isDelta = 0u;
        std::uint32_t isValid = 0u;
    };

    [[nodiscard]] inline bool ValidateRoughness(const double roughness)
    {
        return IsFinite(roughness) && roughness > 0.0 && roughness <= 1.0 &&
            roughness * roughness >= kMinimumAlpha;
    }

    [[nodiscard]] inline bool ValidateEta(const BsdfContextL6& context)
    {
        return IsFinite(context.etaIncident) && IsFinite(context.etaTransmitted) &&
            context.etaIncident > 0.0 && context.etaTransmitted > 0.0;
    }

    [[nodiscard]] inline bool ValidateBsdfParamsL6(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters)
    {
        if (parameters.allowedLobes == 0u || context.transportMode > kTransportImportance)
        {
            return false;
        }
        if (parameters.model == kModelLambert)
        {
            return ColorInUnitRange(parameters.baseColor);
        }
        if (parameters.model == kModelGgxConductor)
        {
            if (!ValidateRoughness(parameters.perceptualRoughness))
            {
                return false;
            }
            if (parameters.useComplexConductorFresnel == 0u)
            {
                return ColorInUnitRange(parameters.f0);
            }
            return IsFinite(parameters.conductorEta) && IsFinite(parameters.conductorK) &&
                parameters.conductorEta.x > 0.0 && parameters.conductorEta.y > 0.0 &&
                parameters.conductorEta.z > 0.0 && parameters.conductorK.x >= 0.0 &&
                parameters.conductorK.y >= 0.0 && parameters.conductorK.z >= 0.0;
        }
        if (parameters.model == kModelGgxDielectricReflection)
        {
            return ValidateRoughness(parameters.perceptualRoughness) && ValidateEta(context);
        }
        if (parameters.model == kModelSmoothGlass)
        {
            return ValidateEta(context) && IsFinite(parameters.transmission) &&
                parameters.transmission >= 0.0 && parameters.transmission <= 1.0;
        }
        if (parameters.model == kModelRoughDielectric)
        {
            return ValidateRoughness(parameters.perceptualRoughness) &&
                ValidateEta(context) && IsFinite(parameters.transmission) &&
                parameters.transmission >= 0.0 && parameters.transmission <= 1.0;
        }
        if (parameters.model == kModelMetallicRoughness)
        {
            return ColorInUnitRange(parameters.baseColor) && ColorInUnitRange(parameters.f0) &&
                ValidateRoughness(parameters.perceptualRoughness) &&
                IsFinite(parameters.metallic) && parameters.metallic >= 0.0 &&
                parameters.metallic <= 1.0 && IsFinite(parameters.transmission) &&
                parameters.transmission >= 0.0 && parameters.transmission <= 1.0;
        }
        return false;
    }

    struct Frame
    {
        Vec3 geometricNormal{};
        Vec3 shadingNormal{};
        Vec3 tangent{};
        Vec3 bitangent{};
        Vec3 woWorld{};
        Vec3 woLocal{};
        bool valid = false;
    };

    [[nodiscard]] inline Frame BuildFrame(
        const BsdfContextL6& context,
        const Vec3 woWorldInput)
    {
        Frame frame;
        if (!SafeNormalize(context.geometricNormal, frame.geometricNormal) ||
            !SafeNormalize(context.shadingNormal, frame.shadingNormal) ||
            !SafeNormalize(woWorldInput, frame.woWorld))
        {
            return frame;
        }
        if (Dot(frame.shadingNormal, frame.geometricNormal) < 0.0)
        {
            frame.shadingNormal = -frame.shadingNormal;
        }
        if (Dot(frame.woWorld, frame.geometricNormal) < 0.0)
        {
            frame.geometricNormal = -frame.geometricNormal;
            frame.shadingNormal = -frame.shadingNormal;
        }
        if (Dot(frame.woWorld, frame.geometricNormal) <= kDirectionEpsilon ||
            Dot(frame.woWorld, frame.shadingNormal) <= kDirectionEpsilon)
        {
            return frame;
        }

        const Vec3 tangentCandidate = context.tangent -
            frame.shadingNormal * Dot(context.tangent, frame.shadingNormal);
        if (!SafeNormalize(tangentCandidate, frame.tangent))
        {
            const Vec3 helper = std::abs(frame.shadingNormal.z) < 0.999
                ? Vec3{ 0.0, 0.0, 1.0 }
                : Vec3{ 0.0, 1.0, 0.0 };
            if (!SafeNormalize(Cross(helper, frame.shadingNormal), frame.tangent))
            {
                return frame;
            }
        }
        if (!SafeNormalize(Cross(frame.shadingNormal, frame.tangent), frame.bitangent))
        {
            return frame;
        }
        frame.woLocal = {
            Dot(frame.woWorld, frame.tangent),
            Dot(frame.woWorld, frame.bitangent),
            Dot(frame.woWorld, frame.shadingNormal)
        };
        frame.valid = IsFinite(frame.woLocal) && frame.woLocal.z > kDirectionEpsilon;
        return frame;
    }

    [[nodiscard]] constexpr Vec3 ToLocal(const Frame& frame, const Vec3 direction)
    {
        return {
            Dot(direction, frame.tangent),
            Dot(direction, frame.bitangent),
            Dot(direction, frame.shadingNormal)
        };
    }

    [[nodiscard]] constexpr Vec3 ToWorld(const Frame& frame, const Vec3 direction)
    {
        return frame.tangent * direction.x + frame.bitangent * direction.y +
            frame.shadingNormal * direction.z;
    }

    [[nodiscard]] inline bool FresnelDielectricL6(
        const double cosineIncidentInput,
        const double etaIncident,
        const double etaTransmitted,
        double& fresnel)
    {
        fresnel = 0.0;
        if (!IsFinite(cosineIncidentInput) || !IsFinite(etaIncident) ||
            !IsFinite(etaTransmitted) || etaIncident <= 0.0 || etaTransmitted <= 0.0)
        {
            return false;
        }
        const double cosineIncident = std::min(std::abs(cosineIncidentInput), 1.0);
        const double etaRatio = etaIncident / etaTransmitted;
        const double sineTransmittedSquared = etaRatio * etaRatio *
            std::max(0.0, 1.0 - cosineIncident * cosineIncident);
        if (sineTransmittedSquared >= 1.0)
        {
            fresnel = 1.0;
            return true;
        }
        const double cosineTransmitted = std::sqrt(
            std::max(0.0, 1.0 - sineTransmittedSquared));
        const double parallelDenominator = etaTransmitted * cosineIncident +
            etaIncident * cosineTransmitted;
        const double perpendicularDenominator = etaIncident * cosineIncident +
            etaTransmitted * cosineTransmitted;
        if (std::abs(parallelDenominator) <= kDirectionEpsilon ||
            std::abs(perpendicularDenominator) <= kDirectionEpsilon)
        {
            return false;
        }
        const double parallel = (etaTransmitted * cosineIncident -
            etaIncident * cosineTransmitted) / parallelDenominator;
        const double perpendicular = (etaIncident * cosineIncident -
            etaTransmitted * cosineTransmitted) / perpendicularDenominator;
        fresnel = 0.5 * (parallel * parallel + perpendicular * perpendicular);
        return IsFinite(fresnel) && fresnel >= 0.0 && fresnel <= 1.0;
    }

    [[nodiscard]] inline Vec3 FresnelSchlickL6(const double cosine, const Vec3 f0)
    {
        const double x = 1.0 - std::min(std::abs(cosine), 1.0);
        const double factor = x * x * x * x * x;
        return f0 + (Splat(1.0) - f0) * factor;
    }

    [[nodiscard]] inline bool FresnelConductorL6(
        const double cosineIncidentInput,
        const Vec3 eta,
        const Vec3 extinction,
        Vec3& fresnel)
    {
        fresnel = {};
        if (!IsFinite(cosineIncidentInput) || !IsFinite(eta) || !IsFinite(extinction) ||
            eta.x <= 0.0 || eta.y <= 0.0 || eta.z <= 0.0 ||
            extinction.x < 0.0 || extinction.y < 0.0 || extinction.z < 0.0)
        {
            return false;
        }
        const double cosine = std::min(std::abs(cosineIncidentInput), 1.0);
        const double cosineSquared = cosine * cosine;
        const double sineSquared = std::max(0.0, 1.0 - cosineSquared);

        const auto channel = [=](const double etaValue, const double kValue, double& value)
        {
            const double etaSquared = etaValue * etaValue;
            const double kSquared = kValue * kValue;
            const double t0 = etaSquared - kSquared - sineSquared;
            const double a2PlusB2 = std::sqrt(std::max(
                0.0, t0 * t0 + 4.0 * etaSquared * kSquared));
            const double t1 = a2PlusB2 + cosineSquared;
            const double a = std::sqrt(std::max(0.0, 0.5 * (a2PlusB2 + t0)));
            const double t2 = 2.0 * cosine * a;
            const double rsDenominator = t1 + t2;
            const double t3 = cosineSquared * a2PlusB2 + sineSquared * sineSquared;
            const double t4 = t2 * sineSquared;
            const double rpDenominator = t3 + t4;
            if (std::abs(rsDenominator) <= kDirectionEpsilon ||
                std::abs(rpDenominator) <= kDirectionEpsilon)
            {
                return false;
            }
            const double rs = (t1 - t2) / rsDenominator;
            const double rp = rs * (t3 - t4) / rpDenominator;
            value = 0.5 * (rs + rp);
            return IsFinite(value) && value >= 0.0 && value <= 1.0;
        };
        return channel(eta.x, extinction.x, fresnel.x) &&
            channel(eta.y, extinction.y, fresnel.y) &&
            channel(eta.z, extinction.z, fresnel.z);
    }

    [[nodiscard]] inline double GgxDL6(const Vec3 microNormal, const double alpha)
    {
        if (microNormal.z <= 0.0 || alpha < kMinimumAlpha)
        {
            return 0.0;
        }
        const double alphaSquared = alpha * alpha;
        const double cosineSquared = microNormal.z * microNormal.z;
        const double denominator = cosineSquared * (alphaSquared - 1.0) + 1.0;
        return denominator > 0.0
            ? alphaSquared / (kPi * denominator * denominator)
            : 0.0;
    }

    [[nodiscard]] inline double GgxLambdaL6(const Vec3 direction, const double alpha)
    {
        const double absoluteCosine = std::abs(direction.z);
        if (absoluteCosine <= kDirectionEpsilon)
        {
            return 1.0e30;
        }
        const double sineSquared = std::max(0.0, 1.0 - absoluteCosine * absoluteCosine);
        const double tangentSquared = sineSquared / (absoluteCosine * absoluteCosine);
        return 0.5 * (std::sqrt(1.0 + alpha * alpha * tangentSquared) - 1.0);
    }

    [[nodiscard]] inline double GgxG1L6(const Vec3 direction, const double alpha)
    {
        return 1.0 / (1.0 + GgxLambdaL6(direction, alpha));
    }

    [[nodiscard]] inline double GgxG2L6(
        const Vec3 wo,
        const Vec3 wi,
        const double alpha)
    {
        return 1.0 / (1.0 + GgxLambdaL6(wo, alpha) + GgxLambdaL6(wi, alpha));
    }

    [[nodiscard]] inline double GgxVisibleNormalPdfL6(
        const Vec3 wo,
        const Vec3 microNormal,
        const double alpha)
    {
        if (wo.z <= kDirectionEpsilon || microNormal.z <= 0.0 ||
            Dot(wo, microNormal) <= 0.0)
        {
            return 0.0;
        }
        return GgxDL6(microNormal, alpha) * GgxG1L6(wo, alpha) *
            std::abs(Dot(wo, microNormal)) / std::abs(wo.z);
    }

    [[nodiscard]] inline bool SampleGgxVisibleNormalL6(
        const Vec3 wo,
        const double alpha,
        const double u0,
        const double u1,
        Vec3& microNormal)
    {
        microNormal = {};
        if (wo.z <= kDirectionEpsilon || alpha < kMinimumAlpha || !IsFinite(wo) ||
            !IsFinite(u0) || !IsFinite(u1) || u0 < 0.0 || u0 >= 1.0 ||
            u1 < 0.0 || u1 >= 1.0)
        {
            return false;
        }
        Vec3 stretchedView;
        if (!SafeNormalize({ alpha * wo.x, alpha * wo.y, wo.z }, stretchedView))
        {
            return false;
        }
        if (stretchedView.z < 0.0)
        {
            stretchedView = -stretchedView;
        }
        const double lensSquared = stretchedView.x * stretchedView.x +
            stretchedView.y * stretchedView.y;
        const Vec3 tangent1 = lensSquared > kDirectionEpsilon * kDirectionEpsilon
            ? Vec3{ -stretchedView.y / std::sqrt(lensSquared),
                    stretchedView.x / std::sqrt(lensSquared), 0.0 }
            : Vec3{ 1.0, 0.0, 0.0 };
        const Vec3 tangent2 = Cross(stretchedView, tangent1);
        const double radius = std::sqrt(u0);
        const double phi = 2.0 * kPi * u1;
        const double diskX = radius * std::cos(phi);
        const double initialDiskY = radius * std::sin(phi);
        const double blend = 0.5 * (1.0 + stretchedView.z);
        const double diskY = std::lerp(
            std::sqrt(std::max(0.0, 1.0 - diskX * diskX)),
            initialDiskY,
            blend);
        const double projectedZ = std::sqrt(std::max(
            0.0, 1.0 - diskX * diskX - diskY * diskY));
        const Vec3 hemisphereNormal = tangent1 * diskX + tangent2 * diskY +
            stretchedView * projectedZ;
        return SafeNormalize(
            { alpha * hemisphereNormal.x, alpha * hemisphereNormal.y,
              hemisphereNormal.z },
            microNormal) && microNormal.z > 0.0 && Dot(wo, microNormal) > 0.0;
    }

    struct GgxReflectionTerms
    {
        Vec3 microNormal{};
        double distribution = 0.0;
        double maskingShadowing = 0.0;
        double directionPdf = 0.0;
        bool valid = false;
    };

    [[nodiscard]] inline GgxReflectionTerms GetGgxReflectionTerms(
        const Vec3 wo,
        const Vec3 wi,
        const double alpha)
    {
        GgxReflectionTerms result;
        if (wo.z <= kDirectionEpsilon || wi.z <= kDirectionEpsilon ||
            !SafeNormalize(wo + wi, result.microNormal))
        {
            return result;
        }
        if (result.microNormal.z < 0.0)
        {
            result.microNormal = -result.microNormal;
        }
        const double woDotMicro = std::abs(Dot(wo, result.microNormal));
        if (woDotMicro <= kDirectionEpsilon)
        {
            return result;
        }
        result.distribution = GgxDL6(result.microNormal, alpha);
        result.maskingShadowing = GgxG2L6(wo, wi, alpha);
        result.directionPdf = GgxVisibleNormalPdfL6(wo, result.microNormal, alpha) /
            (4.0 * woDotMicro);
        result.valid = result.distribution > 0.0 && result.maskingShadowing >= 0.0 &&
            result.directionPdf > 0.0 && IsFinite(result.directionPdf);
        return result;
    }

    [[nodiscard]] inline bool ReflectionFresnel(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const double cosine,
        Vec3& fresnel)
    {
        fresnel = {};
        if (parameters.model == kModelGgxConductor)
        {
            if (parameters.useComplexConductorFresnel != 0u)
            {
                return FresnelConductorL6(
                    cosine, parameters.conductorEta, parameters.conductorK, fresnel);
            }
            fresnel = FresnelSchlickL6(cosine, parameters.f0);
            return IsFinite(fresnel);
        }
        if (parameters.model == kModelGgxDielectricReflection)
        {
            double scalar = 0.0;
            if (!FresnelDielectricL6(
                cosine, context.etaIncident, context.etaTransmitted, scalar))
            {
                return false;
            }
            fresnel = Splat(scalar);
            return true;
        }
        fresnel = FresnelSchlickL6(cosine, parameters.f0);
        return IsFinite(fresnel);
    }

    [[nodiscard]] inline BsdfEvalL6 EvaluateLambertLocal(
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        const Vec3 wi)
    {
        BsdfEvalL6 result;
        if (!ContainsFlags(parameters.allowedLobes, kLobeDiffuseReflection) ||
            wo.z <= kDirectionEpsilon || wi.z <= kDirectionEpsilon)
        {
            return result;
        }
        result.diffuseValue = parameters.baseColor * kInversePi;
        result.value = result.diffuseValue;
        result.pdf = wi.z * kInversePi;
        result.measure = kMeasureSolidAngle;
        result.lobeFlags = kLobeDiffuseReflection;
        result.isValid = IsFinite(result.value) && IsFinite(result.pdf) &&
            result.pdf > 0.0 ? 1u : 0u;
        return result;
    }

    [[nodiscard]] inline BsdfEvalL6 EvaluateGgxReflectionLocal(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        const Vec3 wi)
    {
        BsdfEvalL6 result;
        if (!ContainsFlags(parameters.allowedLobes, kLobeGlossyReflection))
        {
            return result;
        }
        const double alpha = parameters.perceptualRoughness *
            parameters.perceptualRoughness;
        const GgxReflectionTerms terms = GetGgxReflectionTerms(wo, wi, alpha);
        Vec3 fresnel;
        if (!terms.valid || !ReflectionFresnel(
            context, parameters, std::abs(Dot(wo, terms.microNormal)), fresnel))
        {
            return result;
        }
        const double denominator = 4.0 * std::abs(wo.z * wi.z);
        if (denominator <= kDirectionEpsilon)
        {
            return result;
        }
        result.specularValue = fresnel *
            (terms.distribution * terms.maskingShadowing / denominator);
        result.value = result.specularValue;
        result.pdf = terms.directionPdf;
        result.measure = kMeasureSolidAngle;
        result.lobeFlags = kLobeGlossyReflection;
        result.isValid = IsFinite(result.value) && IsFinite(result.pdf) &&
            result.pdf > 0.0 ? 1u : 0u;
        return result;
    }

    [[nodiscard]] inline bool MetallicLobeProbabilities(
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        double& diffuseProbability,
        double& specularProbability)
    {
        const bool allowDiffuse = ContainsFlags(
            parameters.allowedLobes, kLobeDiffuseReflection);
        const bool allowSpecular = ContainsFlags(
            parameters.allowedLobes, kLobeGlossyReflection);
        const double diffuseScale = (1.0 - parameters.metallic) *
            (1.0 - parameters.transmission);
        const double diffuseWeight = allowDiffuse
            ? Luminance(parameters.baseColor) * diffuseScale
            : 0.0;
        const double specularWeight = allowSpecular
            ? Luminance(FresnelSchlickL6(std::abs(wo.z), parameters.f0))
            : 0.0;
        const double total = diffuseWeight + specularWeight;
        if (!IsFinite(total) || total <= 0.0)
        {
            diffuseProbability = 0.0;
            specularProbability = 0.0;
            return false;
        }
        diffuseProbability = diffuseWeight / total;
        specularProbability = specularWeight / total;
        return IsFinite(diffuseProbability) && IsFinite(specularProbability);
    }

    [[nodiscard]] inline BsdfEvalL6 EvaluateMetallicLocal(
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        const Vec3 wi)
    {
        BsdfEvalL6 result;
        if (wo.z <= kDirectionEpsilon || wi.z <= kDirectionEpsilon)
        {
            return result;
        }
        double diffuseProbability = 0.0;
        double specularProbability = 0.0;
        if (!MetallicLobeProbabilities(
            parameters, wo, diffuseProbability, specularProbability))
        {
            return result;
        }
        const double alpha = parameters.perceptualRoughness *
            parameters.perceptualRoughness;
        const GgxReflectionTerms terms = GetGgxReflectionTerms(wo, wi, alpha);
        if (specularProbability > 0.0 && !terms.valid)
        {
            return result;
        }
        const Vec3 fresnel = terms.valid
            ? FresnelSchlickL6(std::abs(Dot(wo, terms.microNormal)), parameters.f0)
            : Vec3{};
        const double diffuseScale = (1.0 - parameters.metallic) *
            (1.0 - parameters.transmission);
        if (diffuseProbability > 0.0)
        {
            result.diffuseValue = (Splat(1.0) - fresnel) * parameters.baseColor *
                (diffuseScale * kInversePi);
            result.lobeFlags |= kLobeDiffuseReflection;
        }
        if (specularProbability > 0.0)
        {
            const double denominator = 4.0 * std::abs(wo.z * wi.z);
            if (denominator <= kDirectionEpsilon)
            {
                return {};
            }
            result.specularValue = fresnel *
                (terms.distribution * terms.maskingShadowing / denominator);
            result.lobeFlags |= kLobeGlossyReflection;
        }
        result.value = result.diffuseValue + result.specularValue;
        result.pdf = diffuseProbability * wi.z * kInversePi +
            specularProbability * terms.directionPdf;
        result.measure = kMeasureSolidAngle;
        result.isValid = IsFinite(result.value) && IsFinite(result.pdf) &&
            result.pdf > 0.0 ? 1u : 0u;
        return result;
    }

    [[nodiscard]] inline bool RoughDielectricEventProbabilities(
        const BsdfParamsL6& parameters,
        const double fresnel,
        double& reflectionProbability,
        double& transmissionProbability)
    {
        reflectionProbability = ContainsFlags(
            parameters.allowedLobes, kLobeGlossyReflection) ? fresnel : 0.0;
        transmissionProbability = ContainsFlags(
            parameters.allowedLobes, kLobeGlossyTransmission)
            ? (1.0 - fresnel) * parameters.transmission
            : 0.0;
        const double total = reflectionProbability + transmissionProbability;
        if (!IsFinite(total) || total <= 0.0)
        {
            return false;
        }
        reflectionProbability /= total;
        transmissionProbability /= total;
        return true;
    }

    [[nodiscard]] inline bool RoughDielectricHalfVector(
        const Vec3 wo,
        const Vec3 wi,
        const double etaRelative,
        bool& isReflection,
        Vec3& microNormal)
    {
        isReflection = wo.z * wi.z > 0.0;
        const Vec3 halfVector = isReflection ? wo + wi : wo + wi * etaRelative;
        if (std::abs(wo.z) <= kDirectionEpsilon ||
            std::abs(wi.z) <= kDirectionEpsilon ||
            !SafeNormalize(halfVector, microNormal))
        {
            return false;
        }
        if (microNormal.z < 0.0)
        {
            microNormal = -microNormal;
        }
        return Dot(microNormal, wi) * wi.z >= 0.0 &&
            Dot(microNormal, wo) * wo.z >= 0.0;
    }

    [[nodiscard]] inline BsdfEvalL6 EvaluateRoughDielectricLocal(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        const Vec3 wi)
    {
        BsdfEvalL6 result;
        const double etaRelative = context.etaTransmitted / context.etaIncident;
        bool isReflection = false;
        Vec3 microNormal;
        if (!IsFinite(etaRelative) || etaRelative <= 0.0 ||
            !RoughDielectricHalfVector(
                wo, wi, etaRelative, isReflection, microNormal))
        {
            return result;
        }
        double fresnel = 0.0;
        if (!FresnelDielectricL6(
            Dot(wo, microNormal), context.etaIncident, context.etaTransmitted, fresnel))
        {
            return result;
        }
        double reflectionProbability = 0.0;
        double transmissionProbability = 0.0;
        if (!RoughDielectricEventProbabilities(
            parameters, fresnel, reflectionProbability, transmissionProbability))
        {
            return result;
        }
        const double alpha = parameters.perceptualRoughness *
            parameters.perceptualRoughness;
        const double distribution = GgxDL6(microNormal, alpha);
        const double maskingShadowing = GgxG2L6(wo, wi, alpha);
        const double normalPdf = GgxVisibleNormalPdfL6(wo, microNormal, alpha);
        if (distribution <= 0.0 || maskingShadowing < 0.0 || normalPdf <= 0.0)
        {
            return result;
        }

        if (isReflection)
        {
            const double woDotMicro = std::abs(Dot(wo, microNormal));
            const double denominator = 4.0 * std::abs(wo.z * wi.z);
            if (reflectionProbability <= 0.0 || woDotMicro <= kDirectionEpsilon ||
                denominator <= kDirectionEpsilon)
            {
                return result;
            }
            result.specularValue = Splat(
                fresnel * distribution * maskingShadowing / denominator);
            result.pdf = normalPdf / (4.0 * woDotMicro) * reflectionProbability;
            result.lobeFlags = kLobeGlossyReflection;
        }
        else
        {
            const double woDotMicro = Dot(wo, microNormal);
            const double wiDotMicro = Dot(wi, microNormal);
            const double denominatorTerm = wiDotMicro + woDotMicro / etaRelative;
            const double jacobianDenominator = denominatorTerm * denominatorTerm;
            const double cosineProduct = wo.z * wi.z;
            if (transmissionProbability <= 0.0 ||
                jacobianDenominator <= kDirectionEpsilon ||
                std::abs(cosineProduct) <= kDirectionEpsilon)
            {
                return result;
            }
            const double dMicroDWi = std::abs(wiDotMicro) / jacobianDenominator;
            double value = parameters.transmission * (1.0 - fresnel) *
                distribution * maskingShadowing *
                std::abs(wiDotMicro * woDotMicro /
                    (cosineProduct * jacobianDenominator));
            if (context.transportMode == kTransportRadiance)
            {
                value /= etaRelative * etaRelative;
            }
            result.specularValue = Splat(value);
            result.pdf = normalPdf * dMicroDWi * transmissionProbability;
            result.lobeFlags = kLobeGlossyTransmission;
        }
        result.value = result.specularValue;
        result.measure = kMeasureSolidAngle;
        result.isValid = IsFinite(result.value) && IsFinite(result.pdf) &&
            result.pdf > 0.0 ? 1u : 0u;
        return result;
    }

    [[nodiscard]] inline bool ValidateWorldEvent(
        const Frame& frame,
        const Vec3 wiWorld,
        const std::uint32_t lobeFlags)
    {
        const bool reflection = ContainsFlags(lobeFlags, kLobeReflection);
        const bool transmission = ContainsFlags(lobeFlags, kLobeTransmission);
        if (reflection == transmission)
        {
            return false;
        }
        const double sideProduct = Dot(frame.woWorld, frame.geometricNormal) *
            Dot(wiWorld, frame.geometricNormal);
        return reflection ? sideProduct > 0.0 : sideProduct < 0.0;
    }

    [[nodiscard]] inline bool ShadingNormalCorrection(
        const BsdfContextL6& context,
        const Frame& frame,
        const Vec3 wiWorld,
        double& correction)
    {
        const double numerator = context.transportMode == kTransportRadiance
            ? std::abs(Dot(wiWorld, frame.shadingNormal))
            : std::abs(Dot(frame.woWorld, frame.shadingNormal));
        const double denominator = context.transportMode == kTransportRadiance
            ? std::abs(Dot(wiWorld, frame.geometricNormal))
            : std::abs(Dot(frame.woWorld, frame.geometricNormal));
        if (!IsFinite(numerator) || !IsFinite(denominator) ||
            denominator <= kDirectionEpsilon)
        {
            correction = 0.0;
            return false;
        }
        correction = numerator / denominator;
        return IsFinite(correction) && correction >= 0.0;
    }

    [[nodiscard]] inline BsdfEvalL6 EvaluateBsdfL6(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 woWorldInput,
        const Vec3 wiWorldInput)
    {
        BsdfEvalL6 result;
        if (!ValidateBsdfParamsL6(context, parameters))
        {
            return result;
        }
        const Frame frame = BuildFrame(context, woWorldInput);
        Vec3 wiWorld;
        if (!frame.valid || !SafeNormalize(wiWorldInput, wiWorld))
        {
            return result;
        }
        const Vec3 wiLocal = ToLocal(frame, wiWorld);
        if (parameters.model == kModelLambert)
        {
            result = EvaluateLambertLocal(parameters, frame.woLocal, wiLocal);
        }
        else if (parameters.model == kModelGgxConductor ||
                 parameters.model == kModelGgxDielectricReflection)
        {
            result = EvaluateGgxReflectionLocal(
                context, parameters, frame.woLocal, wiLocal);
        }
        else if (parameters.model == kModelRoughDielectric)
        {
            result = EvaluateRoughDielectricLocal(
                context, parameters, frame.woLocal, wiLocal);
        }
        else if (parameters.model == kModelMetallicRoughness)
        {
            result = EvaluateMetallicLocal(parameters, frame.woLocal, wiLocal);
        }
        else if (parameters.model == kModelSmoothGlass)
        {
            result.measure = kMeasureDiscrete;
            return result;
        }
        if (result.isValid == 0u || !ValidateWorldEvent(frame, wiWorld, result.lobeFlags))
        {
            return {};
        }
        double correction = 0.0;
        if (!ShadingNormalCorrection(context, frame, wiWorld, correction))
        {
            return {};
        }
        result.value = result.value * correction;
        result.diffuseValue = result.diffuseValue * correction;
        result.specularValue = result.specularValue * correction;
        return IsFinite(result.value) && IsFinite(result.diffuseValue) &&
            IsFinite(result.specularValue) ? result : BsdfEvalL6{};
    }

    [[nodiscard]] inline double PdfBsdfL6(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 woWorld,
        const Vec3 wiWorld)
    {
        const BsdfEvalL6 evaluation = EvaluateBsdfL6(
            context, parameters, woWorld, wiWorld);
        return evaluation.isValid != 0u && evaluation.measure == kMeasureSolidAngle
            ? evaluation.pdf
            : 0.0;
    }

    [[nodiscard]] inline Vec3 SampleUniformHemisphere(
        const double u0,
        const double u1)
    {
        const double sineTheta = std::sqrt(std::max(0.0, 1.0 - u0 * u0));
        const double phi = 2.0 * kPi * u1;
        return {
            sineTheta * std::cos(phi),
            sineTheta * std::sin(phi),
            u0
        };
    }

    [[nodiscard]] inline double UniformHemispherePdf(const Vec3 direction)
    {
        return direction.z >= 0.0 ? 0.5 * kInversePi : 0.0;
    }

    [[nodiscard]] inline Vec3 SampleCosineHemisphere(
        const double u0,
        const double u1)
    {
        const double radius = std::sqrt(u0);
        const double phi = 2.0 * kPi * u1;
        return {
            radius * std::cos(phi),
            radius * std::sin(phi),
            std::sqrt(std::max(0.0, 1.0 - u0))
        };
    }

    [[nodiscard]] inline bool RefractLocal(
        const Vec3 wo,
        Vec3 microNormal,
        const double etaRelative,
        Vec3& wi)
    {
        double cosineIncident = Dot(microNormal, wo);
        if (cosineIncident < 0.0)
        {
            microNormal = -microNormal;
            cosineIncident = -cosineIncident;
        }
        if (cosineIncident <= kDirectionEpsilon || !IsFinite(etaRelative) ||
            etaRelative <= 0.0)
        {
            return false;
        }
        const double sineTransmittedSquared =
            std::max(0.0, 1.0 - cosineIncident * cosineIncident) /
            (etaRelative * etaRelative);
        if (sineTransmittedSquared >= 1.0)
        {
            return false;
        }
        const double cosineTransmitted = std::sqrt(std::max(
            0.0, 1.0 - sineTransmittedSquared));
        return SafeNormalize(
            -wo / etaRelative + microNormal *
                (cosineIncident / etaRelative - cosineTransmitted),
            wi);
    }

    [[nodiscard]] inline BsdfSampleL6 SampleSmoothGlassLocal(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        const double eventSample)
    {
        BsdfSampleL6 result;
        double fresnel = 0.0;
        if (!FresnelDielectricL6(
            wo.z, context.etaIncident, context.etaTransmitted, fresnel))
        {
            return result;
        }
        const double reflectionWeight = ContainsFlags(
            parameters.allowedLobes, kLobeSpecularReflection) ? fresnel : 0.0;
        const double transmissionWeight = ContainsFlags(
            parameters.allowedLobes, kLobeSpecularTransmission)
            ? (1.0 - fresnel) * parameters.transmission
            : 0.0;
        const double total = reflectionWeight + transmissionWeight;
        if (!IsFinite(total) || total <= 0.0)
        {
            return result;
        }
        const double reflectionProbability = reflectionWeight / total;
        const double transmissionProbability = transmissionWeight / total;
        result.measure = kMeasureDiscrete;
        result.isDelta = 1u;
        if (eventSample < reflectionProbability)
        {
            result.direction = { -wo.x, -wo.y, wo.z };
            if (result.direction.z <= kDirectionEpsilon || reflectionProbability <= 0.0)
            {
                return {};
            }
            result.pdf = reflectionProbability;
            result.value = Splat(fresnel / std::abs(result.direction.z));
            result.specularValue = result.value;
            result.lobeFlags = kLobeSpecularReflection;
        }
        else
        {
            const double etaRelative = context.etaTransmitted / context.etaIncident;
            if (transmissionProbability <= 0.0 || !RefractLocal(
                wo, { 0.0, 0.0, 1.0 }, etaRelative, result.direction))
            {
                return {};
            }
            double value = (1.0 - fresnel) * parameters.transmission /
                std::abs(result.direction.z);
            if (context.transportMode == kTransportRadiance)
            {
                value /= etaRelative * etaRelative;
            }
            result.pdf = transmissionProbability;
            result.value = Splat(value);
            result.specularValue = result.value;
            result.eta = etaRelative;
            result.lobeFlags = kLobeSpecularTransmission;
        }
        result.isValid = IsFinite(result.direction) && IsFinite(result.value) &&
            IsFinite(result.pdf) && result.pdf > 0.0 ? 1u : 0u;
        return result;
    }

    [[nodiscard]] inline BsdfSampleL6 SampleFiniteLocal(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 wo,
        const Vec3 randomSample)
    {
        BsdfSampleL6 result;
        result.measure = kMeasureSolidAngle;
        if (parameters.model == kModelLambert)
        {
            if (!ContainsFlags(parameters.allowedLobes, kLobeDiffuseReflection))
            {
                return {};
            }
            result.direction = SampleCosineHemisphere(randomSample.y, randomSample.z);
            result.lobeFlags = kLobeDiffuseReflection;
            result.isValid = 1u;
            return result;
        }
        const double alpha = parameters.perceptualRoughness *
            parameters.perceptualRoughness;
        if (parameters.model == kModelGgxConductor ||
            parameters.model == kModelGgxDielectricReflection)
        {
            Vec3 microNormal;
            if (!ContainsFlags(parameters.allowedLobes, kLobeGlossyReflection) ||
                !SampleGgxVisibleNormalL6(
                    wo, alpha, randomSample.y, randomSample.z, microNormal))
            {
                return {};
            }
            result.direction = -wo + microNormal * (2.0 * Dot(wo, microNormal));
            if (result.direction.z <= kDirectionEpsilon)
            {
                return {};
            }
            result.lobeFlags = kLobeGlossyReflection;
            result.isValid = 1u;
            return result;
        }
        if (parameters.model == kModelMetallicRoughness)
        {
            double diffuseProbability = 0.0;
            double specularProbability = 0.0;
            if (!MetallicLobeProbabilities(
                parameters, wo, diffuseProbability, specularProbability))
            {
                return {};
            }
            if (randomSample.x < diffuseProbability)
            {
                result.direction = SampleCosineHemisphere(randomSample.y, randomSample.z);
                result.lobeFlags = kLobeDiffuseReflection;
            }
            else
            {
                Vec3 microNormal;
                if (specularProbability <= 0.0 || !SampleGgxVisibleNormalL6(
                    wo, alpha, randomSample.y, randomSample.z, microNormal))
                {
                    return {};
                }
                result.direction = -wo + microNormal * (2.0 * Dot(wo, microNormal));
                if (result.direction.z <= kDirectionEpsilon)
                {
                    return {};
                }
                result.lobeFlags = kLobeGlossyReflection;
            }
            result.isValid = 1u;
            return result;
        }
        if (parameters.model == kModelRoughDielectric)
        {
            Vec3 microNormal;
            if (!SampleGgxVisibleNormalL6(
                wo, alpha, randomSample.y, randomSample.z, microNormal))
            {
                return {};
            }
            double fresnel = 0.0;
            if (!FresnelDielectricL6(
                Dot(wo, microNormal), context.etaIncident,
                context.etaTransmitted, fresnel))
            {
                return {};
            }
            double reflectionProbability = 0.0;
            double transmissionProbability = 0.0;
            if (!RoughDielectricEventProbabilities(
                parameters, fresnel, reflectionProbability, transmissionProbability))
            {
                return {};
            }
            if (randomSample.x < reflectionProbability)
            {
                result.direction = -wo + microNormal * (2.0 * Dot(wo, microNormal));
                if (result.direction.z <= kDirectionEpsilon)
                {
                    return {};
                }
                result.lobeFlags = kLobeGlossyReflection;
            }
            else
            {
                const double etaRelative = context.etaTransmitted / context.etaIncident;
                if (transmissionProbability <= 0.0 || !RefractLocal(
                    wo, microNormal, etaRelative, result.direction) ||
                    result.direction.z >= -kDirectionEpsilon)
                {
                    return {};
                }
                result.eta = etaRelative;
                result.lobeFlags = kLobeGlossyTransmission;
            }
            result.isValid = 1u;
            return result;
        }
        return {};
    }

    [[nodiscard]] inline BsdfSampleL6 SampleBsdfL6(
        const BsdfContextL6& context,
        const BsdfParamsL6& parameters,
        const Vec3 woWorldInput,
        const Vec3 randomSample)
    {
        if (!ValidateBsdfParamsL6(context, parameters) || !IsFinite(randomSample) ||
            randomSample.x < 0.0 || randomSample.x >= 1.0 ||
            randomSample.y < 0.0 || randomSample.y >= 1.0 ||
            randomSample.z < 0.0 || randomSample.z >= 1.0)
        {
            return {};
        }
        const Frame frame = BuildFrame(context, woWorldInput);
        if (!frame.valid)
        {
            return {};
        }
        const BsdfSampleL6 localSample = parameters.model == kModelSmoothGlass
            ? SampleSmoothGlassLocal(context, parameters, frame.woLocal, randomSample.x)
            : SampleFiniteLocal(context, parameters, frame.woLocal, randomSample);
        if (localSample.isValid == 0u)
        {
            return {};
        }
        Vec3 wiWorld;
        if (!SafeNormalize(ToWorld(frame, localSample.direction), wiWorld) ||
            !ValidateWorldEvent(frame, wiWorld, localSample.lobeFlags))
        {
            return {};
        }
        if (localSample.measure == kMeasureSolidAngle)
        {
            const BsdfEvalL6 evaluation = EvaluateBsdfL6(
                context, parameters, frame.woWorld, wiWorld);
            if (evaluation.isValid == 0u)
            {
                return {};
            }
            BsdfSampleL6 result = localSample;
            result.direction = wiWorld;
            result.pdf = evaluation.pdf;
            result.value = evaluation.value;
            result.diffuseValue = evaluation.diffuseValue;
            result.specularValue = evaluation.specularValue;
            result.isValid = 1u;
            return result;
        }
        double correction = 0.0;
        if (!ShadingNormalCorrection(context, frame, wiWorld, correction))
        {
            return {};
        }
        BsdfSampleL6 result = localSample;
        result.direction = wiWorld;
        result.value = result.value * correction;
        result.diffuseValue = result.diffuseValue * correction;
        result.specularValue = result.specularValue * correction;
        result.isValid = IsFinite(result.value) && IsFinite(result.specularValue) ? 1u : 0u;
        return result.isValid != 0u ? result : BsdfSampleL6{};
    }
}
