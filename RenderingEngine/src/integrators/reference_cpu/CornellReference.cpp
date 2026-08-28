#include "integrators/reference_cpu/CornellReference.hpp"

#include "bsdf/reference_cpu/Lambert.hpp"
#include "rt/cpu/Bvh.hpp"
#include "rt/cpu/Geometry.hpp"
#include "rt/cpu/Math.hpp"
#include "rt/cpu/Random.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace RenderingEngine::Integrators::ReferenceCpu
{
    namespace
    {
        using Vec3d = Rt::Cpu::Vec3<double>;
        using Rayd = Rt::Cpu::Ray<double>;
        using Triangled = Rt::Cpu::Triangle<double>;
        using Hitd = Rt::Cpu::Hit<double>;
        using Bvhd = Rt::Cpu::Bvh<double>;

        constexpr std::uint32_t kWhiteMaterialId = 0u;
        constexpr std::uint32_t kRedMaterialId = 1u;
        constexpr std::uint32_t kGreenMaterialId = 2u;
        constexpr std::uint32_t kEmitterMaterialId = 3u;
        constexpr std::uint32_t kMaterialCount = 4u;
        constexpr std::uint32_t kRussianRouletteStartBounce = 3u;
        constexpr std::uint64_t kFixtureVersion = 0x4c33434f524e454cull; // "L3CORNEL"
        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        struct Material final
        {
            std::uint32_t stableId = 0u;
            Vec3d reflectance{};
            Vec3d emission{};

            [[nodiscard]] bool IsEmitter() const noexcept
            {
                return emission.x > 0.0 || emission.y > 0.0 || emission.z > 0.0;
            }
        };

        struct RectangleLight final
        {
            Vec3d corner{};
            Vec3d edgeU{};
            Vec3d edgeV{};
            Vec3d normal{};
            double area = 0.0;
        };

        struct Camera final
        {
            Vec3d position{};
            double verticalFieldOfViewRadians = 0.0;
        };

        struct CornellScene final
        {
            std::vector<Triangled> triangles;
            std::vector<std::uint32_t> materialByPrimitive;
            std::array<Material, kMaterialCount> materials{};
            RectangleLight light{};
            Camera camera{};
            std::uint64_t hash = 0u;
        };

        struct Counters final
        {
            std::uint64_t rayCount = 0u;
            std::uint64_t shadowRayCount = 0u;
            std::uint64_t nonFiniteCount = 0u;
        };

        [[nodiscard]] Vec3d Multiply(const Vec3d& lhs, const Vec3d& rhs) noexcept
        {
            return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
        }

        [[nodiscard]] double MaximumComponent(const Vec3d& value) noexcept
        {
            return std::max(value.x, std::max(value.y, value.z));
        }

        [[nodiscard]] bool IsFiniteNonNegative(const Vec3d& value) noexcept
        {
            return Rt::Cpu::IsFinite(value)
                && value.x >= 0.0
                && value.y >= 0.0
                && value.z >= 0.0;
        }

        void HashByte(std::uint64_t& hash, const std::uint8_t value) noexcept
        {
            hash ^= value;
            hash *= kFnvPrime;
        }

        void HashWord(std::uint64_t& hash, const std::uint64_t value) noexcept
        {
            // Canonical little-endian byte order makes the fixture identity
            // independent of the host's in-memory integer representation.
            for (std::uint32_t byteIndex = 0u; byteIndex < 8u; ++byteIndex)
            {
                HashByte(hash, static_cast<std::uint8_t>(value >> (byteIndex * 8u)));
            }
        }

        void HashDouble(std::uint64_t& hash, const double value) noexcept
        {
            HashWord(hash, std::bit_cast<std::uint64_t>(value));
        }

        void HashVector(std::uint64_t& hash, const Vec3d& value) noexcept
        {
            HashDouble(hash, value.x);
            HashDouble(hash, value.y);
            HashDouble(hash, value.z);
        }

        void AddTriangle(
            CornellScene& scene,
            const Vec3d& vertex0,
            const Vec3d& vertex1,
            const Vec3d& vertex2,
            const std::uint32_t materialId)
        {
            if (scene.triangles.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                throw std::overflow_error("Cornell fixture primitive ID overflow");
            }
            if (materialId >= scene.materials.size())
            {
                throw std::logic_error("Cornell fixture material ID is out of range");
            }

            const std::uint32_t primitiveId = static_cast<std::uint32_t>(scene.triangles.size());
            scene.triangles.emplace_back(vertex0, vertex1, vertex2, primitiveId);
            scene.materialByPrimitive.push_back(materialId);
        }

        void AddQuad(
            CornellScene& scene,
            const Vec3d& vertex0,
            Vec3d vertex1,
            const Vec3d& vertex2,
            Vec3d vertex3,
            const Vec3d& desiredNormal,
            const std::uint32_t materialId)
        {
            if (!(Rt::Cpu::Dot(
                Rt::Cpu::Cross(vertex1 - vertex0, vertex2 - vertex0),
                desiredNormal) > 0.0))
            {
                std::swap(vertex1, vertex3);
            }
            AddTriangle(scene, vertex0, vertex1, vertex2, materialId);
            AddTriangle(scene, vertex0, vertex2, vertex3, materialId);
        }

        void AddBox(
            CornellScene& scene,
            const Vec3d& minimum,
            const Vec3d& maximum,
            const std::uint32_t materialId)
        {
            const Vec3d p000{minimum.x, minimum.y, minimum.z};
            const Vec3d p001{minimum.x, minimum.y, maximum.z};
            const Vec3d p010{minimum.x, maximum.y, minimum.z};
            const Vec3d p011{minimum.x, maximum.y, maximum.z};
            const Vec3d p100{maximum.x, minimum.y, minimum.z};
            const Vec3d p101{maximum.x, minimum.y, maximum.z};
            const Vec3d p110{maximum.x, maximum.y, minimum.z};
            const Vec3d p111{maximum.x, maximum.y, maximum.z};

            AddQuad(scene, p000, p001, p011, p010, {-1.0, 0.0, 0.0}, materialId);
            AddQuad(scene, p100, p110, p111, p101, {1.0, 0.0, 0.0}, materialId);
            AddQuad(scene, p000, p100, p101, p001, {0.0, -1.0, 0.0}, materialId);
            AddQuad(scene, p010, p011, p111, p110, {0.0, 1.0, 0.0}, materialId);
            AddQuad(scene, p000, p010, p110, p100, {0.0, 0.0, -1.0}, materialId);
            AddQuad(scene, p001, p101, p111, p011, {0.0, 0.0, 1.0}, materialId);
        }

        [[nodiscard]] std::uint64_t ComputeSceneHash(const CornellScene& scene) noexcept
        {
            std::uint64_t hash = kFnvOffsetBasis;
            HashWord(hash, kFixtureVersion);
            HashWord(hash, static_cast<std::uint64_t>(scene.materials.size()));
            for (const Material& material : scene.materials)
            {
                HashWord(hash, material.stableId);
                HashVector(hash, material.reflectance);
                HashVector(hash, material.emission);
            }

            HashWord(hash, static_cast<std::uint64_t>(scene.triangles.size()));
            for (std::size_t triangleIndex = 0u; triangleIndex < scene.triangles.size(); ++triangleIndex)
            {
                const Triangled& triangle = scene.triangles[triangleIndex];
                HashWord(hash, triangle.primitiveId);
                HashVector(hash, triangle.v0);
                HashVector(hash, triangle.v1);
                HashVector(hash, triangle.v2);
                HashWord(hash, scene.materialByPrimitive[triangleIndex]);
            }

            HashVector(hash, scene.light.corner);
            HashVector(hash, scene.light.edgeU);
            HashVector(hash, scene.light.edgeV);
            HashVector(hash, scene.light.normal);
            HashDouble(hash, scene.light.area);
            HashVector(hash, scene.camera.position);
            HashDouble(hash, scene.camera.verticalFieldOfViewRadians);
            return hash;
        }

        [[nodiscard]] CornellScene CreateCornellScene()
        {
            CornellScene scene;
            scene.materials = {
                Material{kWhiteMaterialId, {0.73, 0.73, 0.73}, {}},
                Material{kRedMaterialId, {0.63, 0.065, 0.05}, {}},
                Material{kGreenMaterialId, {0.14, 0.45, 0.091}, {}},
                Material{kEmitterMaterialId, {}, {17.0, 15.0, 12.0}}};

            // L3-private, meter-scale Cornell fixture. The open front faces +Z;
            // the camera sits outside it and looks along the contract's -Z axis.
            constexpr double kMinimumX = -1.0;
            constexpr double kMaximumX = 1.0;
            constexpr double kMinimumY = -1.0;
            constexpr double kMaximumY = 1.0;
            constexpr double kFrontZ = -1.0;
            constexpr double kBackZ = -3.0;

            AddQuad(scene,
                {kMinimumX, kMinimumY, kFrontZ},
                {kMaximumX, kMinimumY, kFrontZ},
                {kMaximumX, kMinimumY, kBackZ},
                {kMinimumX, kMinimumY, kBackZ},
                {0.0, 1.0, 0.0},
                kWhiteMaterialId);
            AddQuad(scene,
                {kMinimumX, kMaximumY, kFrontZ},
                {kMinimumX, kMaximumY, kBackZ},
                {kMaximumX, kMaximumY, kBackZ},
                {kMaximumX, kMaximumY, kFrontZ},
                {0.0, -1.0, 0.0},
                kWhiteMaterialId);
            AddQuad(scene,
                {kMinimumX, kMinimumY, kBackZ},
                {kMaximumX, kMinimumY, kBackZ},
                {kMaximumX, kMaximumY, kBackZ},
                {kMinimumX, kMaximumY, kBackZ},
                {0.0, 0.0, 1.0},
                kWhiteMaterialId);
            AddQuad(scene,
                {kMinimumX, kMinimumY, kFrontZ},
                {kMinimumX, kMinimumY, kBackZ},
                {kMinimumX, kMaximumY, kBackZ},
                {kMinimumX, kMaximumY, kFrontZ},
                {1.0, 0.0, 0.0},
                kRedMaterialId);
            AddQuad(scene,
                {kMaximumX, kMinimumY, kFrontZ},
                {kMaximumX, kMaximumY, kFrontZ},
                {kMaximumX, kMaximumY, kBackZ},
                {kMaximumX, kMinimumY, kBackZ},
                {-1.0, 0.0, 0.0},
                kGreenMaterialId);

            scene.light.corner = {-0.30, 0.985, -2.30};
            scene.light.edgeU = {0.60, 0.0, 0.0};
            scene.light.edgeV = {0.0, 0.0, 0.60};
            scene.light.normal = {0.0, -1.0, 0.0};
            scene.light.area = Rt::Cpu::Length(Rt::Cpu::Cross(
                scene.light.edgeU,
                scene.light.edgeV));
            AddQuad(scene,
                scene.light.corner,
                scene.light.corner + scene.light.edgeU,
                scene.light.corner + scene.light.edgeU + scene.light.edgeV,
                scene.light.corner + scene.light.edgeV,
                scene.light.normal,
                kEmitterMaterialId);

            AddBox(scene, {-0.72, -0.999, -2.55}, {-0.10, -0.30, -1.72}, kWhiteMaterialId);
            AddBox(scene, {0.16, -0.999, -2.75}, {0.74, 0.22, -1.93}, kWhiteMaterialId);

            scene.camera.position = {0.0, 0.0, 0.15};
            scene.camera.verticalFieldOfViewRadians = 39.0 * std::numbers::pi_v<double> / 180.0;
            scene.hash = ComputeSceneHash(scene);
            return scene;
        }

        [[nodiscard]] double PowerHeuristic(const double pdfA, const double pdfB) noexcept
        {
            if (!(pdfA > 0.0) || !std::isfinite(pdfA))
            {
                return 0.0;
            }
            if (!(pdfB > 0.0) || !std::isfinite(pdfB))
            {
                return 1.0;
            }
            const double ratio = pdfB / pdfA;
            if (!std::isfinite(ratio))
            {
                return 0.0;
            }
            return 1.0 / (1.0 + ratio * ratio);
        }

        [[nodiscard]] bool OffsetRayOrigin(
            const Vec3d& position,
            const Vec3d& geometricNormal,
            const Vec3d& direction,
            Vec3d& offsetPosition) noexcept
        {
            if (!Rt::Cpu::IsFinite(position)
                || !Rt::Cpu::IsFinite(geometricNormal)
                || !Rt::Cpu::IsFinite(direction))
            {
                return false;
            }

            Vec3d normal = Rt::Cpu::NormalizeOrZero(geometricNormal);
            if (!(Rt::Cpu::LengthSquared(normal) > 0.0))
            {
                return false;
            }
            if (Rt::Cpu::Dot(normal, direction) < 0.0)
            {
                normal = -normal;
            }

            constexpr double kErrorScale = 32.0 * std::numeric_limits<double>::epsilon();
            for (std::size_t axis = 0u; axis < 3u; ++axis)
            {
                const double magnitude = std::max(1.0, std::abs(position[axis]));
                const double shifted = position[axis] + normal[axis] * magnitude * kErrorScale;
                if (!std::isfinite(shifted))
                {
                    return false;
                }
                if (normal[axis] > 0.0)
                {
                    offsetPosition[axis] = std::nextafter(
                        shifted,
                        std::numeric_limits<double>::infinity());
                }
                else if (normal[axis] < 0.0)
                {
                    offsetPosition[axis] = std::nextafter(
                        shifted,
                        -std::numeric_limits<double>::infinity());
                }
                else
                {
                    offsetPosition[axis] = shifted;
                }
            }
            return Rt::Cpu::IsFinite(offsetPosition);
        }

        [[nodiscard]] double LightSolidAnglePdf(
            const RectangleLight& light,
            const Vec3d& referencePosition,
            const Vec3d& lightPosition) noexcept
        {
            const Vec3d displacement = lightPosition - referencePosition;
            const double distanceSquared = Rt::Cpu::LengthSquared(displacement);
            if (!(distanceSquared > 0.0) || !std::isfinite(distanceSquared) || !(light.area > 0.0))
            {
                return 0.0;
            }
            const Vec3d direction = displacement / std::sqrt(distanceSquared);
            const double lightCosine = Rt::Cpu::Dot(light.normal, -direction);
            if (!(lightCosine > 0.0) || !std::isfinite(lightCosine))
            {
                return 0.0;
            }
            const double pdf = distanceSquared / (lightCosine * light.area);
            return std::isfinite(pdf) ? pdf : 0.0;
        }

        [[nodiscard]] Vec3d EvaluateDirectLighting(
            const CornellScene& scene,
            const Bvhd& bvh,
            const Vec3d& position,
            const Vec3d& normal,
            const Vec3d& outgoing,
            const Material& material,
            Rt::Cpu::Pcg32& rng,
            Counters& counters)
        {
            const double sampleU = rng.Uniform<double>();
            const double sampleV = rng.Uniform<double>();
            const Vec3d lightPosition = scene.light.corner
                + scene.light.edgeU * sampleU
                + scene.light.edgeV * sampleV;

            const Vec3d toLight = lightPosition - position;
            const double distanceSquared = Rt::Cpu::LengthSquared(toLight);
            if (!(distanceSquared > 0.0) || !std::isfinite(distanceSquared))
            {
                ++counters.nonFiniteCount;
                return {};
            }
            const double distance = std::sqrt(distanceSquared);
            const Vec3d incoming = toLight / distance;
            const double surfaceCosine = Rt::Cpu::Dot(normal, incoming);
            const double lightCosine = Rt::Cpu::Dot(scene.light.normal, -incoming);
            if (!(surfaceCosine > 0.0) || !(lightCosine > 0.0))
            {
                return {};
            }

            // Evaluate both MIS PDFs at the actual surface vertex. The offset
            // below is only a visibility-ray robustness device and must not
            // silently change the estimator's sampling measure.
            const double lightPdf = distanceSquared / (lightCosine * scene.light.area);
            const double bsdfPdf = Bsdf::ReferenceCpu::Lambert::Pdf(normal, outgoing, incoming);
            if (!(lightPdf > 0.0) || !std::isfinite(lightPdf))
            {
                ++counters.nonFiniteCount;
                return {};
            }

            Vec3d shadowOrigin;
            if (!OffsetRayOrigin(position, normal, incoming, shadowOrigin))
            {
                ++counters.nonFiniteCount;
                return {};
            }

            const Vec3d offsetToLight = lightPosition - shadowOrigin;
            const double shadowDistanceSquared = Rt::Cpu::LengthSquared(offsetToLight);
            if (!(shadowDistanceSquared > 0.0) || !std::isfinite(shadowDistanceSquared))
            {
                ++counters.nonFiniteCount;
                return {};
            }
            const double shadowDistance = std::sqrt(shadowDistanceSquared);
            const Vec3d shadowDirection = offsetToLight / shadowDistance;

            const double shadowMaximum = std::nextafter(shadowDistance, 0.0);
            if (!(shadowMaximum > 0.0) || !std::isfinite(shadowMaximum))
            {
                ++counters.nonFiniteCount;
                return {};
            }
            const Rayd shadowRay{shadowOrigin, shadowDirection, 0.0, shadowMaximum};
            if (!shadowRay.IsValid())
            {
                ++counters.nonFiniteCount;
                return {};
            }
            ++counters.shadowRayCount;
            if (bvh.TraceAny(shadowRay))
            {
                return {};
            }

            const Vec3d bsdf = Bsdf::ReferenceCpu::Lambert::Evaluate(
                material.reflectance,
                normal,
                outgoing,
                incoming);
            const Material& emitter = scene.materials[kEmitterMaterialId];
            const double misWeight = PowerHeuristic(lightPdf, bsdfPdf);
            const double scale = surfaceCosine * misWeight / lightPdf;
            const Vec3d direct = Multiply(bsdf, emitter.emission) * scale;
            if (!IsFiniteNonNegative(direct))
            {
                ++counters.nonFiniteCount;
                return {};
            }
            return direct;
        }

        [[nodiscard]] Vec3d TracePath(
            const CornellScene& scene,
            const Bvhd& bvh,
            Rayd ray,
            Rt::Cpu::Pcg32& rng,
            const std::uint32_t maximumBounces,
            Counters& counters)
        {
            Vec3d radiance{};
            Vec3d throughput{1.0};
            Vec3d previousPosition{};
            double previousBsdfPdf = 0.0;
            bool hasPreviousSurface = false;

            for (std::uint32_t bounce = 0u; bounce < maximumBounces; ++bounce)
            {
                ++counters.rayCount;
                const Hitd hit = bvh.TraceClosest(ray);
                if (!hit.IsHit())
                {
                    break;
                }
                if (hit.primitiveId >= scene.materialByPrimitive.size())
                {
                    ++counters.nonFiniteCount;
                    break;
                }

                const Vec3d position = ray.At(hit.t);
                Vec3d normal = Rt::Cpu::NormalizeOrZero(hit.geometricNormal);
                const Vec3d outgoing = -ray.direction;
                if (!Rt::Cpu::IsFinite(position)
                    || !Rt::Cpu::IsFinite(normal)
                    || !Rt::Cpu::IsFinite(outgoing)
                    || !(Rt::Cpu::LengthSquared(normal) > 0.0))
                {
                    ++counters.nonFiniteCount;
                    break;
                }
                if (Rt::Cpu::Dot(normal, outgoing) < 0.0)
                {
                    normal = -normal;
                }

                const std::uint32_t materialIndex = scene.materialByPrimitive[hit.primitiveId];
                if (materialIndex >= scene.materials.size())
                {
                    ++counters.nonFiniteCount;
                    break;
                }
                const Material& material = scene.materials[materialIndex];

                if (material.IsEmitter())
                {
                    // The ceiling emitter is one-sided. A camera/BSDF path that
                    // reaches its back face contributes no emitted radiance.
                    if (Rt::Cpu::Dot(hit.geometricNormal, outgoing) > 0.0)
                    {
                        double misWeight = 1.0;
                        if (hasPreviousSurface)
                        {
                            const double lightPdf = LightSolidAnglePdf(
                                scene.light,
                                previousPosition,
                                position);
                            misWeight = PowerHeuristic(previousBsdfPdf, lightPdf);
                        }
                        const Vec3d emitted = Multiply(throughput, material.emission) * misWeight;
                        if (IsFiniteNonNegative(emitted))
                        {
                            radiance += emitted;
                        }
                        else
                        {
                            ++counters.nonFiniteCount;
                        }
                    }
                    break;
                }

                const Vec3d direct = EvaluateDirectLighting(
                    scene,
                    bvh,
                    position,
                    normal,
                    outgoing,
                    material,
                    rng,
                    counters);
                const Vec3d weightedDirect = Multiply(throughput, direct);
                if (IsFiniteNonNegative(weightedDirect))
                {
                    radiance += weightedDirect;
                }
                else
                {
                    ++counters.nonFiniteCount;
                    break;
                }

                if (bounce + 1u >= maximumBounces)
                {
                    break;
                }

                const Bsdf::ReferenceCpu::LambertSample sample =
                    Bsdf::ReferenceCpu::Lambert::Sample(
                        material.reflectance,
                        normal,
                        outgoing,
                        rng);
                if (!sample.valid)
                {
                    break;
                }
                const double cosine = Rt::Cpu::Dot(normal, sample.direction);
                if (!(cosine > 0.0) || !(sample.pdf > 0.0))
                {
                    break;
                }
                throughput = Multiply(throughput, sample.value) * (cosine / sample.pdf);
                if (!IsFiniteNonNegative(throughput))
                {
                    ++counters.nonFiniteCount;
                    break;
                }

                previousPosition = position;
                previousBsdfPdf = sample.pdf;
                hasPreviousSurface = true;

                if (bounce >= kRussianRouletteStartBounce)
                {
                    const double survivalProbability = std::clamp(
                        MaximumComponent(throughput),
                        0.05,
                        0.95);
                    if (rng.Uniform<double>() >= survivalProbability)
                    {
                        break;
                    }
                    throughput /= survivalProbability;
                }

                Vec3d nextOrigin;
                if (!OffsetRayOrigin(position, normal, sample.direction, nextOrigin))
                {
                    ++counters.nonFiniteCount;
                    break;
                }
                ray = Rayd{
                    nextOrigin,
                    sample.direction,
                    0.0,
                    std::numeric_limits<double>::infinity()};
                if (!ray.IsValid())
                {
                    ++counters.nonFiniteCount;
                    break;
                }
            }

            if (!IsFiniteNonNegative(radiance))
            {
                ++counters.nonFiniteCount;
                return {};
            }
            return radiance;
        }

        [[nodiscard]] Rayd GenerateCameraRay(
            const CornellScene& scene,
            const CornellReferenceOptions& options,
            const std::uint32_t pixelX,
            const std::uint32_t pixelY,
            Rt::Cpu::Pcg32& rng) noexcept
        {
            const double jitterX = rng.Uniform<double>();
            const double jitterY = rng.Uniform<double>();
            const double width = static_cast<double>(options.width);
            const double height = static_cast<double>(options.height);
            const double aspectRatio = width / height;
            const double tangentHalfFov = std::tan(scene.camera.verticalFieldOfViewRadians * 0.5);
            const double screenX =
                ((static_cast<double>(pixelX) + jitterX) / width * 2.0 - 1.0)
                * aspectRatio
                * tangentHalfFov;
            const double screenY =
                (1.0 - (static_cast<double>(pixelY) + jitterY) / height * 2.0)
                * tangentHalfFov;
            const Vec3d direction = Rt::Cpu::NormalizeOrZero(
                Vec3d{screenX, screenY, -1.0});
            return Rayd{
                scene.camera.position,
                direction,
                0.0,
                std::numeric_limits<double>::infinity()};
        }

        [[nodiscard]] bool ValidateOptions(
            const CornellReferenceOptions& options,
            std::string& error)
        {
            if (options.width == 0u || options.height == 0u)
            {
                error = "Cornell reference dimensions must be non-zero";
                return false;
            }
            if (options.spp == 0u)
            {
                error = "Cornell reference SPP must be non-zero";
                return false;
            }
            if (options.maxBounces == 0u)
            {
                error = "Cornell reference maximum bounce count must be non-zero";
                return false;
            }

            const std::size_t width = static_cast<std::size_t>(options.width);
            const std::size_t height = static_cast<std::size_t>(options.height);
            if (width > std::numeric_limits<std::size_t>::max() / height)
            {
                error = "Cornell reference pixel count overflows size_t";
                return false;
            }
            const std::size_t pixelCount = width * height;
            if (pixelCount > std::numeric_limits<std::size_t>::max() / 3u)
            {
                error = "Cornell reference RGB storage size overflows size_t";
                return false;
            }
            return true;
        }
    }

    bool RenderCornellReference(
        const CornellReferenceOptions& options,
        CornellReferenceResult& result,
        std::string& error)
    {
        result = CornellReferenceResult{};
        error.clear();
        if (!ValidateOptions(options, error))
        {
            return false;
        }

        const auto startTime = std::chrono::steady_clock::now();
        try
        {
            const CornellScene scene = CreateCornellScene();
            const Bvhd bvh{
                std::span<const Triangled>{scene.triangles.data(), scene.triangles.size()},
                Rt::Cpu::BvhBuildMethod::BinnedSah,
                4u};

            const std::size_t pixelCount = static_cast<std::size_t>(options.width)
                * static_cast<std::size_t>(options.height);
            result.pixels.assign(pixelCount * 3u, 0.0f);
            result.sceneHash = scene.hash;

            const std::uint32_t hardwareThreads = std::thread::hardware_concurrency();
            const std::uint32_t requestedThreads = options.threads == 0u
                ? std::max(1u, hardwareThreads)
                : options.threads;
            const std::uint32_t threadCount = std::max(
                1u,
                std::min(requestedThreads, options.height));
            std::vector<Counters> threadCounters(threadCount);
            std::atomic<std::uint32_t> nextRow{0u};
            std::exception_ptr workerException;
            std::mutex workerExceptionMutex;

            const auto renderRows = [&](const std::uint32_t threadIndex)
            {
                try
                {
                    Counters& counters = threadCounters[threadIndex];
                    while (true)
                    {
                        const std::uint32_t pixelY = nextRow.fetch_add(1u);
                        if (pixelY >= options.height)
                        {
                            break;
                        }

                        for (std::uint32_t pixelX = 0u; pixelX < options.width; ++pixelX)
                        {
                            const std::uint64_t pixelIndex =
                                static_cast<std::uint64_t>(pixelY) * options.width + pixelX;
                            Vec3d accumulated{};
                            for (std::uint32_t sampleIndex = 0u;
                                sampleIndex < options.spp;
                                ++sampleIndex)
                            {
                                Rt::Cpu::Pcg32 rng = Rt::Cpu::MakeSampleRng(
                                    options.seed,
                                    pixelIndex,
                                    sampleIndex);
                                const Rayd cameraRay = GenerateCameraRay(
                                    scene,
                                    options,
                                    pixelX,
                                    pixelY,
                                    rng);
                                if (!cameraRay.IsValid())
                                {
                                    ++counters.nonFiniteCount;
                                    continue;
                                }
                                accumulated += TracePath(
                                    scene,
                                    bvh,
                                    cameraRay,
                                    rng,
                                    options.maxBounces,
                                    counters);
                            }

                            const Vec3d average = accumulated / static_cast<double>(options.spp);
                            const std::size_t outputOffset = static_cast<std::size_t>(pixelIndex) * 3u;
                            const double floatMaximum =
                                static_cast<double>(std::numeric_limits<float>::max());
                            if (!IsFiniteNonNegative(average)
                                || MaximumComponent(average) > floatMaximum)
                            {
                                ++counters.nonFiniteCount;
                                result.pixels[outputOffset + 0u] = 0.0f;
                                result.pixels[outputOffset + 1u] = 0.0f;
                                result.pixels[outputOffset + 2u] = 0.0f;
                            }
                            else
                            {
                                result.pixels[outputOffset + 0u] = static_cast<float>(average.x);
                                result.pixels[outputOffset + 1u] = static_cast<float>(average.y);
                                result.pixels[outputOffset + 2u] = static_cast<float>(average.z);
                            }
                        }
                    }
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(workerExceptionMutex);
                    if (!workerException)
                    {
                        workerException = std::current_exception();
                    }
                }
            };

            if (threadCount == 1u)
            {
                renderRows(0u);
            }
            else
            {
                std::vector<std::thread> workers;
                workers.reserve(threadCount);
                try
                {
                    for (std::uint32_t threadIndex = 0u; threadIndex < threadCount; ++threadIndex)
                    {
                        workers.emplace_back(renderRows, threadIndex);
                    }
                }
                catch (...)
                {
                    for (std::thread& worker : workers)
                    {
                        if (worker.joinable())
                        {
                            worker.join();
                        }
                    }
                    throw;
                }
                for (std::thread& worker : workers)
                {
                    worker.join();
                }
            }

            if (workerException)
            {
                std::rethrow_exception(workerException);
            }
            for (const Counters& counters : threadCounters)
            {
                result.rayCount += counters.rayCount;
                result.shadowRayCount += counters.shadowRayCount;
                result.nonFiniteCount += counters.nonFiniteCount;
            }

            const auto endTime = std::chrono::steady_clock::now();
            result.elapsedMilliseconds =
                std::chrono::duration<double, std::milli>(endTime - startTime).count();
            return true;
        }
        catch (const std::exception& exception)
        {
            result = CornellReferenceResult{};
            error = std::string{"Cornell reference render failed: "} + exception.what();
            return false;
        }
        catch (...)
        {
            result = CornellReferenceResult{};
            error = "Cornell reference render failed with an unknown exception";
            return false;
        }
    }
}
