#pragma once

#include "rt/cpu/Math.hpp"
#include "rt/cpu/Random.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace RenderingEngine::Bsdf::ReferenceCpu
{
    struct LambertSample final
    {
        Rt::Cpu::Vec3<double> direction{};
        Rt::Cpu::Vec3<double> value{};
        double pdf = 0.0;
        bool valid = false;
    };

    class Lambert final
    {
    public:
        [[nodiscard]] static Rt::Cpu::Vec3<double> Evaluate(
            const Rt::Cpu::Vec3<double>& reflectance,
            const Rt::Cpu::Vec3<double>& normal,
            const Rt::Cpu::Vec3<double>& outgoing,
            const Rt::Cpu::Vec3<double>& incoming) noexcept
        {
            if (!Rt::Cpu::IsFinite(reflectance)
                || reflectance.x < 0.0
                || reflectance.y < 0.0
                || reflectance.z < 0.0
                || !Rt::Cpu::IsFinite(normal)
                || !Rt::Cpu::IsFinite(outgoing)
                || !Rt::Cpu::IsFinite(incoming)
                || !(Rt::Cpu::Dot(normal, outgoing) > 0.0)
                || !(Rt::Cpu::Dot(normal, incoming) > 0.0))
            {
                return {};
            }

            constexpr double kInversePi = std::numbers::inv_pi_v<double>;
            return Rt::Cpu::Vec3<double>{
                reflectance.x * kInversePi,
                reflectance.y * kInversePi,
                reflectance.z * kInversePi};
        }

        [[nodiscard]] static double Pdf(
            const Rt::Cpu::Vec3<double>& normal,
            const Rt::Cpu::Vec3<double>& outgoing,
            const Rt::Cpu::Vec3<double>& incoming) noexcept
        {
            if (!Rt::Cpu::IsFinite(normal)
                || !Rt::Cpu::IsFinite(outgoing)
                || !Rt::Cpu::IsFinite(incoming)
                || !(Rt::Cpu::Dot(normal, outgoing) > 0.0))
            {
                return 0.0;
            }

            return std::max(0.0, Rt::Cpu::Dot(normal, incoming))
                * std::numbers::inv_pi_v<double>;
        }

        [[nodiscard]] static LambertSample Sample(
            const Rt::Cpu::Vec3<double>& reflectance,
            const Rt::Cpu::Vec3<double>& normal,
            const Rt::Cpu::Vec3<double>& outgoing,
            Rt::Cpu::Pcg32& rng) noexcept
        {
            LambertSample result;
            if (!Rt::Cpu::IsFinite(reflectance)
                || reflectance.x < 0.0
                || reflectance.y < 0.0
                || reflectance.z < 0.0
                || !Rt::Cpu::IsFinite(normal)
                || !Rt::Cpu::IsFinite(outgoing)
                || !(Rt::Cpu::Dot(normal, outgoing) > 0.0))
            {
                return result;
            }

            const double sampleX = rng.Uniform<double>();
            const double sampleY = rng.Uniform<double>();
            const double radius = std::sqrt(sampleX);
            const double phi = 2.0 * std::numbers::pi_v<double> * sampleY;
            const double localX = radius * std::cos(phi);
            const double localY = radius * std::sin(phi);
            const double localZ = std::sqrt(std::max(0.0, 1.0 - sampleX));

            const Rt::Cpu::Vec3<double> unitNormal = Rt::Cpu::NormalizeOrZero(normal);
            const Rt::Cpu::Vec3<double> tangent = BuildTangent(unitNormal);
            const Rt::Cpu::Vec3<double> bitangent = Rt::Cpu::Cross(unitNormal, tangent);
            result.direction = Rt::Cpu::NormalizeOrZero(
                tangent * localX + bitangent * localY + unitNormal * localZ);
            result.pdf = Pdf(unitNormal, outgoing, result.direction);
            result.value = Evaluate(reflectance, unitNormal, outgoing, result.direction);
            result.valid = result.pdf > 0.0
                && Rt::Cpu::IsFinite(result.direction)
                && Rt::Cpu::IsFinite(result.value)
                && std::isfinite(result.pdf);
            return result;
        }

    private:
        [[nodiscard]] static Rt::Cpu::Vec3<double> BuildTangent(
            const Rt::Cpu::Vec3<double>& normal) noexcept
        {
            // Frisvad's special case is avoided here in favor of a compact,
            // deterministic cross-product basis that remains stable at both poles.
            const Rt::Cpu::Vec3<double> helper = std::abs(normal.z) < 0.999
                ? Rt::Cpu::Vec3<double>{0.0, 0.0, 1.0}
                : Rt::Cpu::Vec3<double>{0.0, 1.0, 0.0};
            return Rt::Cpu::NormalizeOrZero(Rt::Cpu::Cross(helper, normal));
        }
    };
}
