#include "demos/ShowcaseProgram.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace RenderingEngine::Demos
{
    namespace
    {
        const auto kSceneCards = std::to_array<ShowcaseSceneCard>({
            {
                "baseline", "Baseline Gallery", "L0",
                "Non-regression gallery for the existing analytic Whitted/PBR baseline."
            },
            {
                "intersection-bvh", "Intersection & BVH Lab", "L2",
                "Intersection edge cases, fixed ray corpora, and hierarchy diagnostics."
            },
            {
                "whitted-optics", "Whitted Optics Room", "L2",
                "Reflection, refraction, Fresnel, TIR, and ray-offset comparisons."
            },
            {
                "cornell", "Cornell Box", "L2",
                "Fixed-camera diffuse GI convergence and direct-lighting comparisons."
            },
            {
                "ggx-mis", "GGX & MIS Material Lab", "L2",
                "Material, PDF, MIS, and white-furnace evidence under controlled lights."
            },
            {
                "environment-dome", "Environment Sampling Dome", "L2",
                "Environment importance-sampling and BSDF/MIS variance comparisons."
            },
            {
                "sponza", "Sponza Traversal Hall", "L2",
                "Large-scene traversal, alpha-mask parity, build, trace, and memory evidence."
            },
            {
                "backend-parity", "Backend Parity Benchmark", "L2",
                "Fixed-ray correctness and separate performance evidence across backends."
            },
            {
                "temporal-stability", "Temporal Stability Corridor", "L2",
                "Motion, disocclusion, history validation, A-Trous, and SVGF comparisons."
            },
            {
                "many-lights", "Many Lights / ReSTIR Arena", "L2",
                "Controlled 100, 1,000, and 10,000-light sampling comparisons."
            }
        });

        constexpr auto kAlgorithms = std::to_array<AlgorithmDescriptor>({
            { "whitted", "Whitted Specular Transport", "Transport", "L0", "algorithm:whitted",
                "Local direct light followed by sampled ideal reflection/transmission chains.",
                "Uses stochastic branch selection and roulette, not an exhaustive Whitted tree or diffuse-GI oracle." },
            { "pbr", "PBR Path Transport", "Transport", "L6", "algorithm:pbr",
                "Monte Carlo transport over the shared PBR BSDF and light contracts.",
                "Execution architecture is selected independently." },
            { "cpu-brute-force", "CPU Brute Force", "Traversal", "L3", "algorithm:cpu-brute-force",
                "Tests every primitive to provide a simple hit-parity oracle.",
                "Linear primitive cost makes it unsuitable for performance comparison." },
            { "cpu-sah", "CPU SAH BVH", "Traversal", "L3", "algorithm:cpu-sah",
                "CPU BVH traversal built with a surface-area heuristic.",
                "Reference performance is CPU-specific and not a GPU throughput claim." },
            { "gpu-flattened-sah", "GPU Flattened SAH BVH", "Traversal", "L4", "algorithm:gpu-flattened-sah",
                "Compute traversal over a flattened SAH hierarchy.",
                "Requires the L4 provider and hit-parity evidence before use." },
            { "gpu-lbvh", "GPU LBVH", "Traversal", "L4", "algorithm:gpu-lbvh",
                "GPU-built Morton-code hierarchy for fast dynamic rebuilds.",
                "Build speed and traversal quality must be reported separately." },
            { "ray-query", "Vulkan Ray Query", "Traversal", "L5", "algorithm:ray-query",
                "Inline hardware traversal invoked from shader stages.",
                "Availability depends on device features and L5 acceleration structures." },
            { "rt-pipeline", "Vulkan RT Pipeline", "Traversal", "L5", "algorithm:rt-pipeline",
                "Dedicated ray-generation, hit, miss, and SBT execution path.",
                "SBT and pipeline support must be reported independently from Ray Query." },
            { "cpu-reference", "CPU Reference", "Execution", "L3", "algorithm:cpu-reference",
                "Independent seeded path tracer used as a numerical image oracle.",
                "Correctness use requires matched scene, camera, seed, and generations." },
            { "high-spp-reference", "High-SPP CPU Reference", "Reference", "L3", "algorithm:high-spp-reference",
                "Long-running CPU accumulation used for low-noise comparison images.",
                "It is a reference budget, not an equal-realtime-budget leg." },
            { "megakernel", "GPU Megakernel", "Execution", "L6", "algorithm:megakernel",
                "Keeps a complete path loop inside one GPU dispatch.",
                "Divergence and long-path occupancy can limit scalability." },
            { "wavefront", "GPU Wavefront", "Execution", "L7", "algorithm:wavefront",
                "Compacts path stages into queues and indirect dispatches.",
                "Queue and compaction overhead must be included in timings." },
            { "bsdf-only", "BSDF-only Direct Lighting", "Direct Lighting", "L6", "algorithm:bsdf-only",
                "Samples direct-light paths only through the BSDF proposal.",
                "High variance is expected for small or difficult light sources." },
            { "nee", "Next-event Estimation", "Direct Lighting", "L6", "algorithm:nee",
                "Explicitly samples a light and evaluates visibility each bounce.",
                "The estimator depends on a correctly reported light proposal PDF." },
            { "mis", "Multiple Importance Sampling", "Direct Lighting", "L6", "algorithm:mis",
                "Combines BSDF and light proposals with compatible MIS weights.",
                "Delta events and PDF measures must use consistent conventions." },
            { "ggx-bsdf", "GGX BSDF", "Material", "L6", "algorithm:ggx-bsdf",
                "Microfacet reflection using GGX distribution, masking, and Fresnel.",
                "Energy and PDF validation remain provider-owned evidence." },
            { "environment-importance", "Environment Importance Map", "Environment Direction", "L6", "algorithm:environment-importance",
                "Samples an environment map according to its luminance distribution.",
                "Distribution rebuilds must track environment resource generation." },
            { "uniform-one-light", "Uniform One-light Selection", "Light Selection", "L6", "algorithm:uniform-one-light",
                "Chooses one light uniformly and compensates with its selection PDF.",
                "Variance rises when light powers differ substantially." },
            { "power-weighted-one-light", "Power-weighted One-light Selection", "Light Selection", "L6", "algorithm:power-weighted-one-light",
                "Chooses one light from a power-weighted discrete distribution.",
                "Stale power tables invalidate the estimator and its evidence." },
            { "temporal", "Temporal Accumulation", "Reconstruction", "L8", "algorithm:temporal",
                "Reprojects and accumulates compatible history samples over frames.",
                "Disocclusion and tuple changes require explicit history invalidation." },
            { "atrous", "Fixed A-Trous", "Reconstruction", "L8", "algorithm:atrous",
                "Edge-aware multi-scale filtering with a fixed A-Trous schedule.",
                "Aggressive kernels can trade residual noise for detail loss." },
            { "svgf", "SVGF", "Reconstruction", "L8", "algorithm:svgf",
                "Combines temporal moments, variance guidance, and spatial filtering.",
                "Motion, GBuffer, and history identities must remain synchronized." },
            { "ris", "Reservoir Importance Sampling", "Many Lights", "L9", "algorithm:ris",
                "Selects one weighted candidate while retaining reservoir statistics.",
                "Target, proposal, and correction weights require statistical validation." },
            { "restir-initial", "ReSTIR Initial Sampling", "Many Lights", "L9", "algorithm:restir-initial",
                "Builds the current-frame reservoir from newly drawn light candidates.",
                "Candidate count and proposal budget must be preserved in comparisons." },
            { "restir-temporal", "ReSTIR Temporal Reuse", "Many Lights", "L9", "algorithm:restir-temporal",
                "Merges compatible reservoirs reprojected from prior frames.",
                "Disocclusion and generation changes must reject stale history." },
            { "restir-spatial", "ReSTIR Spatial Reuse", "Many Lights", "L9", "algorithm:restir-spatial",
                "Reuses neighboring reservoirs under geometric compatibility tests.",
                "Neighbor reuse can introduce bias unless the selected mode corrects it." },
            { "restir-di", "ReSTIR Direct Illumination", "Many Lights", "L9", "algorithm:restir-di",
                "Composes initial, temporal, and spatial reservoir reuse for direct light.",
                "Biased and unbiased modes must remain separately identified." }
        });

        constexpr auto kManyLightsPresets = std::to_array<ManyLightsPreset>({
            { "many-lights-100", "100 emissive lights", 100u },
            { "many-lights-1000", "1,000 emissive lights", 1'000u },
            { "many-lights-10000", "10,000 emissive lights", 10'000u }
        });

        struct ComparisonLegDescriptor
        {
            ManyLightsComparisonVariant variant;
            std::string_view stableToken;
            std::string_view label;
            std::string_view directEstimatorToken;
            std::string_view lightSelectionToken;
            std::string_view algorithmProviderToken;
            std::array<std::string_view, 2> requiredAlgorithmProviderTokens;
            std::uint8_t requiredAlgorithmProviderCount;
            std::string_view budgetIdentityToken;
            std::array<ManyLightsBiasModeRequirement, 2> requiredBiasModes;
            std::uint8_t requiredBiasModeCount;
        };

        constexpr auto kComparisonLegs = std::to_array<ComparisonLegDescriptor>({
            {
                ManyLightsComparisonVariant::Uniform,
                "uniform", "Uniform one-light", "nee", "uniform",
                "algorithm:uniform-one-light",
                { "algorithm:uniform-one-light" }, 1u,
                "many-lights-equal-realtime-budget-v1",
                { ManyLightsBiasModeRequirement{
                    "unbiased", ManyLightsBiasClassification::Unbiased } }, 1u
            },
            {
                ManyLightsComparisonVariant::PowerWeighted,
                "power", "Power-weighted one-light", "nee", "power",
                "algorithm:power-weighted-one-light",
                { "algorithm:power-weighted-one-light" }, 1u,
                "many-lights-equal-realtime-budget-v1",
                { ManyLightsBiasModeRequirement{
                    "unbiased", ManyLightsBiasClassification::Unbiased } }, 1u
            },
            {
                ManyLightsComparisonVariant::Restir,
                "restir", "ReSTIR DI", "restir-di", "power",
                "algorithm:restir-di",
                { "algorithm:restir-di" }, 1u,
                "many-lights-equal-realtime-budget-v1",
                {
                    ManyLightsBiasModeRequirement{
                        "biased", ManyLightsBiasClassification::Biased },
                    ManyLightsBiasModeRequirement{
                        "reference-correction", ManyLightsBiasClassification::ReferenceCorrection }
                }, 2u
            },
            {
                ManyLightsComparisonVariant::HighSppReference,
                "high-spp-reference", "High-SPP CPU reference",
                "cpu-reference", "not-applicable",
                "algorithm:high-spp-reference",
                { "algorithm:cpu-reference", "algorithm:high-spp-reference" }, 2u,
                "many-lights-high-spp-reference-budget-v1",
                { ManyLightsBiasModeRequirement{
                    "not-applicable",
                    ManyLightsBiasClassification::NotApplicable } }, 1u
            }
        });

        const auto kCaptureShots = std::to_array<CaptureShotDescriptor>({
            {
                "baseline-hero", "baseline", "Baseline hero",
                "Preserve the migrated baseline look and capture path.",
                CaptureShotKind::Hero, 0u,
                { "algorithm:whitted", "algorithm:pbr" }, 2u
            },
            {
                "intersection-correctness", "intersection-bvh", "Intersection parity",
                "Show fixed-ray hit parity and highlighted disagreement evidence.",
                CaptureShotKind::Correctness, 0u,
                {
                    "algorithm:cpu-brute-force", "algorithm:cpu-sah",
                    "algorithm:gpu-flattened-sah", "algorithm:gpu-lbvh"
                }, 4u
            },
            {
                "whitted-optics-hero", "whitted-optics", "Whitted optics",
                "Frame reflection, refraction, Fresnel, and TIR behavior.",
                CaptureShotKind::Hero, 0u,
                { "algorithm:whitted" }, 1u
            },
            {
                "cornell-convergence", "cornell", "Cornell convergence",
                "Capture fixed-budget BSDF-only, NEE, MIS, and reference comparisons.",
                CaptureShotKind::Quality, 0u,
                {
                    "algorithm:cpu-reference", "algorithm:bsdf-only",
                    "algorithm:nee", "algorithm:mis"
                }, 4u
            },
            {
                "ggx-mis-grid", "ggx-mis", "GGX and MIS grid",
                "Capture material monotonicity, PDF, and energy evidence.",
                CaptureShotKind::Quality, 0u,
                { "algorithm:ggx-bsdf", "algorithm:mis" }, 2u
            },
            {
                "environment-variance", "environment-dome", "Environment variance",
                "Compare uniform and importance-sampled environment lighting.",
                CaptureShotKind::Comparison, 0u,
                { "algorithm:environment-importance", "algorithm:mis" }, 2u
            },
            {
                "sponza-traversal", "sponza", "Sponza traversal",
                "Capture large-scene image, traversal timings, and memory evidence.",
                CaptureShotKind::Performance, 0u,
                {
                    "algorithm:cpu-sah", "algorithm:gpu-flattened-sah",
                    "algorithm:gpu-lbvh", "algorithm:ray-query", "algorithm:rt-pipeline"
                }, 5u
            },
            {
                "backend-parity", "backend-parity", "Backend parity",
                "Keep correctness and performance tables in the same reproducible setup.",
                CaptureShotKind::Correctness, 0u,
                {
                    "algorithm:cpu-brute-force", "algorithm:cpu-sah",
                    "algorithm:gpu-flattened-sah", "algorithm:gpu-lbvh",
                    "algorithm:ray-query", "algorithm:rt-pipeline",
                    "algorithm:megakernel", "algorithm:wavefront"
                }, 8u
            },
            {
                "temporal-stability", "temporal-stability", "Temporal stability",
                "Capture Raw, Temporal, A-Trous, SVGF, and reference views.",
                CaptureShotKind::Comparison, 0u,
                {
                    "algorithm:temporal", "algorithm:atrous",
                    "algorithm:svgf", "algorithm:cpu-reference"
                }, 4u
            },
            {
                "many-lights-100-comparison", "many-lights", "Many Lights 100",
                "Request uniform, power-weighted, ReSTIR, and high-SPP reference captures at 100 lights.",
                CaptureShotKind::Comparison, 100u,
                {
                    "algorithm:uniform-one-light", "algorithm:power-weighted-one-light",
                    "algorithm:restir-di", "algorithm:cpu-reference",
                    "algorithm:high-spp-reference"
                }, 5u
            },
            {
                "many-lights-1000-comparison", "many-lights", "Many Lights 1,000",
                "Request uniform, power-weighted, ReSTIR, and high-SPP reference captures at 1,000 lights.",
                CaptureShotKind::Comparison, 1'000u,
                {
                    "algorithm:uniform-one-light", "algorithm:power-weighted-one-light",
                    "algorithm:restir-di", "algorithm:cpu-reference",
                    "algorithm:high-spp-reference"
                }, 5u
            },
            {
                "many-lights-10000-comparison", "many-lights", "Many Lights 10,000",
                "Request uniform, power-weighted, ReSTIR, and high-SPP reference captures at 10,000 lights.",
                CaptureShotKind::Comparison, 10'000u,
                {
                    "algorithm:uniform-one-light", "algorithm:power-weighted-one-light",
                    "algorithm:restir-di", "algorithm:cpu-reference",
                    "algorithm:high-spp-reference"
                }, 5u
            }
        });

        constexpr auto kLicenseRequirements = std::to_array<AssetLicenseRequirement>({
            {
                "environment-hdri", "Environment Sampling Dome HDRI", "L2",
                "environment-dome", "assets/licenses/environment-hdri.md"
            },
            {
                "sponza", "Pinned Sponza asset", "L2",
                "sponza", "assets/licenses/sponza.md"
            }
        });

        constexpr auto kFinalVideoShots = std::to_array<FinalVideoShotDescriptor>({
            { "opening-baseline", "Opening: stable baseline", "baseline-hero", "Establish the renderer and interaction baseline." },
            { "intersection-lab", "Intersection and BVH lab", "intersection-correctness", "Show correctness diagnostics before performance claims." },
            { "optics-room", "Whitted optics room", "whitted-optics-hero", "Show ideal specular chains with sampled reflection and transmission." },
            { "cornell-gi", "Cornell GI", "cornell-convergence", "Show convergence and direct-lighting estimator comparisons." },
            { "material-lab", "GGX and MIS material lab", "ggx-mis-grid", "Show controlled material and energy evidence." },
            { "environment-dome", "Environment sampling dome", "environment-variance", "Show importance-sampling variance reduction." },
            { "sponza-scale", "Sponza traversal hall", "sponza-traversal", "Show asset scale, traversal, and memory reporting." },
            { "backend-parity", "Backend parity", "backend-parity", "Separate correctness parity from performance results." },
            { "temporal-stability", "Temporal stability", "temporal-stability", "Show motion, rejection, and reconstruction stability." },
            { "many-lights-finale", "Many Lights finale", "many-lights-10000-comparison", "Close with the fixed-budget 10,000-light comparison." }
        });

        void AppendReason(std::string& destination, std::string_view reason)
        {
            if (reason.empty())
            {
                return;
            }
            if (!destination.empty())
            {
                destination += " ";
            }
            destination.append(reason);
        }

        [[nodiscard]] AvailabilityStatus ResolveProvider(
            std::string_view providerToken,
            std::span<const ProviderAvailability> providers)
        {
            const ProviderAvailability* match = nullptr;
            std::size_t matchCount = 0;
            for (const ProviderAvailability& provider : providers)
            {
                if (provider.providerToken == providerToken)
                {
                    match = &provider;
                    ++matchCount;
                }
            }

            if (matchCount == 0)
            {
                return {
                    Availability::Unavailable,
                    "Missing upstream provider '" + std::string(providerToken) + "'."
                };
            }
            if (matchCount != 1)
            {
                return {
                    Availability::Unavailable,
                    "Upstream provider '" + std::string(providerToken)
                        + "' was supplied more than once."
                };
            }
            if (match->state == Availability::Unavailable)
            {
                return {
                    Availability::Unavailable,
                    match->reason.empty()
                        ? "Upstream provider '" + std::string(providerToken)
                            + "' reported Unavailable without a reason."
                        : std::string(match->reason)
                };
            }
            if (match->state == Availability::Available)
            {
                return { Availability::Available, {} };
            }
            return {
                Availability::Unavailable,
                "Upstream provider '" + std::string(providerToken)
                    + "' reported an invalid availability value."
            };
        }

        [[nodiscard]] bool IsKnownManyLightsPreset(const ManyLightsPreset& preset) noexcept
        {
            return std::any_of(
                kManyLightsPresets.begin(),
                kManyLightsPresets.end(),
                [&preset](const ManyLightsPreset& candidate)
                {
                    return candidate.stableToken == preset.stableToken
                        && candidate.lightCount == preset.lightCount;
                });
        }

        [[nodiscard]] const AlgorithmCompletionEntry* FindAlgorithm(
            std::span<const AlgorithmCompletionEntry> matrix,
            std::string_view providerToken) noexcept
        {
            const AlgorithmCompletionEntry* match = nullptr;
            for (const AlgorithmCompletionEntry& entry : matrix)
            {
                if (entry.algorithm.providerToken == providerToken)
                {
                    if (match != nullptr)
                    {
                        return nullptr;
                    }
                    match = &entry;
                }
            }
            return match;
        }

        [[nodiscard]] const ManyLightsPreset* FindManyLightsPreset(
            std::uint32_t lightCount) noexcept
        {
            const auto found = std::find_if(
                kManyLightsPresets.begin(),
                kManyLightsPresets.end(),
                [lightCount](const ManyLightsPreset& preset)
                {
                    return preset.lightCount == lightCount;
                });
            return found == kManyLightsPresets.end() ? nullptr : &*found;
        }

        [[nodiscard]] const CaptureShotPlanEntry* FindCaptureShot(
            std::span<const CaptureShotPlanEntry> plan,
            std::string_view shotToken) noexcept
        {
            const CaptureShotPlanEntry* match = nullptr;
            for (const CaptureShotPlanEntry& entry : plan)
            {
                if (entry.shot.stableToken == shotToken)
                {
                    if (match != nullptr)
                    {
                        return nullptr;
                    }
                    match = &entry;
                }
            }
            return match;
        }

        [[nodiscard]] const ResolvedShowcaseSceneCard* FindResolvedScene(
            std::span<const ResolvedShowcaseSceneCard> scenes,
            std::string_view sceneToken) noexcept
        {
            const ResolvedShowcaseSceneCard* match = nullptr;
            for (const ResolvedShowcaseSceneCard& scene : scenes)
            {
                if (scene.card.stableToken == sceneToken)
                {
                    if (match != nullptr)
                    {
                        return nullptr;
                    }
                    match = &scene;
                }
            }
            return match;
        }

        [[nodiscard]] AvailabilityStatus ResolveRegistryProvider(
            std::string_view providerToken,
            Availability state,
            std::string_view reason,
            std::string_view missingTokenReason)
        {
            if (providerToken.empty())
            {
                return { Availability::Unavailable, std::string(missingTokenReason) };
            }
            const ProviderAvailability provider = { providerToken, state, reason };
            return ResolveProvider(providerToken, std::span(&provider, 1));
        }

        [[nodiscard]] bool IsKnownCompletionState(
            AlgorithmCompletionState state) noexcept
        {
            switch (state)
            {
            case AlgorithmCompletionState::Unavailable:
            case AlgorithmCompletionState::Declared:
            case AlgorithmCompletionState::Implemented:
            case AlgorithmCompletionState::RuntimeValidated:
            case AlgorithmCompletionState::VisualAccepted:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsCompleteLiveEvidenceIdentity(
            const ShowcaseEvidenceIdentity& identity) noexcept
        {
            const bool knownLiveSource =
                identity.provenance.source == EvidenceSource::CpuWallClock
                || identity.provenance.source == EvidenceSource::ProviderReported;
            return knownLiveSource
                && !identity.artifactIdentity.empty()
                && !identity.provenance.provider.empty()
                && identity.configGeneration.has_value()
                && !identity.sceneStableId.empty()
                && identity.sceneGeneration.has_value()
                && identity.resourceGeneration.has_value()
                && identity.frameIndex.has_value()
                && identity.sampleIndex.has_value();
        }
    }

    std::span<const ShowcaseSceneCard> GetShowcaseSceneCards() noexcept
    {
        return kSceneCards;
    }

    std::vector<ResolvedShowcaseSceneCard> ResolveShowcaseSceneCards(
        std::span<const ProviderAvailability> providers)
    {
        // Provider facts alone are deliberately insufficient. Without an L2-owned
        // registry record L10 has no authoritative scene metadata or fixed camera.
        (void)providers;
        std::vector<ResolvedShowcaseSceneCard> result;
        result.reserve(kSceneCards.size());
        for (const ShowcaseSceneCard& card : kSceneCards)
        {
            const std::string reason = "Missing upstream scene registry entry for roadmap slot '"
                + card.stableToken + "'.";
            ResolvedShowcaseSceneCard resolved;
            resolved.card = card;
            resolved.sceneAvailability = { Availability::Unavailable, reason };
            resolved.cameraAvailability = { Availability::Unavailable, reason };
            resolved.availability = { Availability::Unavailable, reason };
            result.push_back(std::move(resolved));
        }
        return result;
    }

    std::vector<ResolvedShowcaseSceneCard> ResolveShowcaseSceneCards(
        std::span<const ShowcaseSceneRegistryEntry> sceneRegistry,
        std::span<const ProviderAvailability> providers)
    {
        // Scene/camera facts embedded in the registry are authoritative. The
        // generic provider span remains part of this overload so callers can use
        // the same input surface as BuildShowcaseProgramModel; it must not make a
        // missing registry entry appear present.
        (void)providers;

        std::vector<ResolvedShowcaseSceneCard> result;
        result.reserve(kSceneCards.size());
        for (const ShowcaseSceneCard& roadmapSlot : kSceneCards)
        {
            const ShowcaseSceneRegistryEntry* match = nullptr;
            std::size_t matchCount = 0;
            for (const ShowcaseSceneRegistryEntry& entry : sceneRegistry)
            {
                if (entry.stableToken == roadmapSlot.stableToken)
                {
                    match = &entry;
                    ++matchCount;
                }
            }

            ResolvedShowcaseSceneCard resolved;
            resolved.card = roadmapSlot;
            if (matchCount == 0)
            {
                const std::string reason =
                    "Missing upstream scene registry entry for roadmap slot '"
                    + roadmapSlot.stableToken + "'.";
                resolved.sceneAvailability = { Availability::Unavailable, reason };
                resolved.cameraAvailability = { Availability::Unavailable, reason };
                resolved.availability = { Availability::Unavailable, reason };
                result.push_back(std::move(resolved));
                continue;
            }
            if (matchCount != 1)
            {
                const std::string reason =
                    "Upstream scene registry contains duplicate entries for roadmap slot '"
                    + roadmapSlot.stableToken + "'.";
                resolved.sceneAvailability = { Availability::Unavailable, reason };
                resolved.cameraAvailability = { Availability::Unavailable, reason };
                resolved.availability = { Availability::Unavailable, reason };
                result.push_back(std::move(resolved));
                continue;
            }

            resolved.card = {
                match->stableToken,
                match->label,
                match->owner,
                match->description
            };
            resolved.sceneProviderToken = match->sceneProviderToken;
            resolved.fixedCameraPresetToken = match->fixedCameraPresetToken;
            resolved.cameraProviderToken = match->cameraProviderToken;

            std::string unavailableReasons;
            if (match->label.empty())
            {
                AppendReason(unavailableReasons, "Registry scene label is empty.");
            }
            if (match->description.empty())
            {
                AppendReason(unavailableReasons, "Registry scene description is empty.");
            }
            if (match->owner.empty())
            {
                AppendReason(unavailableReasons, "Registry scene owner is empty.");
            }

            resolved.sceneAvailability = ResolveRegistryProvider(
                match->sceneProviderToken,
                match->sceneAvailability,
                match->sceneAvailabilityReason,
                "Registry scene provider token is empty.");
            if (!resolved.sceneAvailability.IsAvailable())
            {
                AppendReason(unavailableReasons, resolved.sceneAvailability.reason);
            }

            if (match->fixedCameraPresetToken.empty())
            {
                resolved.cameraAvailability = {
                    Availability::Unavailable,
                    "Registry fixed camera preset token is empty."
                };
            }
            else
            {
                resolved.cameraAvailability = ResolveRegistryProvider(
                    match->cameraProviderToken,
                    match->cameraAvailability,
                    match->cameraAvailabilityReason,
                    "Registry fixed-camera provider token is empty.");
            }
            if (!resolved.cameraAvailability.IsAvailable())
            {
                AppendReason(unavailableReasons, resolved.cameraAvailability.reason);
            }

            resolved.availability = unavailableReasons.empty()
                ? AvailabilityStatus{ Availability::Available, {} }
                : AvailabilityStatus{ Availability::Unavailable, std::move(unavailableReasons) };
            result.push_back(std::move(resolved));
        }
        return result;
    }

    std::span<const AlgorithmDescriptor> GetAlgorithmCatalog() noexcept
    {
        return kAlgorithms;
    }

    AvailabilityStatus ValidateLiveEvidenceIdentity(
        const ShowcaseEvidenceIdentity& identity)
    {
        std::string reason;
        if (identity.artifactIdentity.empty())
        {
            AppendReason(reason, "Evidence artifact identity is missing.");
        }
        if (identity.provenance.provider.empty())
        {
            AppendReason(reason, "Evidence provider identity is missing.");
        }
        switch (identity.provenance.source)
        {
        case EvidenceSource::CpuWallClock:
        case EvidenceSource::ProviderReported:
            break;
        case EvidenceSource::SyntheticTest:
            AppendReason(reason, "SyntheticTest evidence cannot satisfy a live claim.");
            break;
        default:
            AppendReason(reason, "Evidence source is invalid.");
            break;
        }
        if (!identity.configGeneration.has_value())
        {
            AppendReason(reason, "Evidence config generation is missing.");
        }
        if (identity.sceneStableId.empty())
        {
            AppendReason(reason, "Evidence scene identity is missing.");
        }
        if (!identity.sceneGeneration.has_value())
        {
            AppendReason(reason, "Evidence scene generation is missing.");
        }
        if (!identity.resourceGeneration.has_value())
        {
            AppendReason(reason, "Evidence resource generation is missing.");
        }
        if (!identity.frameIndex.has_value())
        {
            AppendReason(reason, "Evidence frame index is missing.");
        }
        if (!identity.sampleIndex.has_value())
        {
            AppendReason(reason, "Evidence sample index is missing.");
        }
        return reason.empty()
            ? AvailabilityStatus{ Availability::Available, {} }
            : AvailabilityStatus{ Availability::Unavailable, std::move(reason) };
    }

    std::vector<AlgorithmCompletionEntry> BuildAlgorithmCompletionMatrix(
        std::span<const AlgorithmCompletionClaim> claims)
    {
        std::vector<AlgorithmCompletionEntry> result;
        result.reserve(kAlgorithms.size());
        for (const AlgorithmDescriptor& algorithm : kAlgorithms)
        {
            const AlgorithmCompletionClaim* match = nullptr;
            std::size_t matchCount = 0;
            for (const AlgorithmCompletionClaim& claim : claims)
            {
                if (claim.providerToken == algorithm.providerToken)
                {
                    match = &claim;
                    ++matchCount;
                }
            }

            AlgorithmCompletionEntry entry;
            entry.algorithm = algorithm;
            if (matchCount == 0)
            {
                entry.reason = "Missing upstream completion provider '"
                    + std::string(algorithm.providerToken) + "'.";
            }
            else if (matchCount != 1)
            {
                entry.reason = "Upstream completion provider '"
                    + std::string(algorithm.providerToken) + "' was supplied more than once.";
            }
            else
            {
                entry.state = match->state;
                entry.evidence = match->evidence;
                if (match->state == AlgorithmCompletionState::Unavailable)
                {
                    entry.reason = match->reason.empty()
                        ? "Upstream completion provider '"
                            + std::string(algorithm.providerToken)
                            + "' reported Unavailable without a reason."
                        : std::string(match->reason);
                }
                else if (!IsKnownCompletionState(match->state))
                {
                    entry.state = AlgorithmCompletionState::Unavailable;
                    entry.reason = "Upstream completion provider reported an invalid state.";
                }
                else if (match->state == AlgorithmCompletionState::RuntimeValidated
                    || match->state == AlgorithmCompletionState::VisualAccepted)
                {
                    if (!match->evidence.has_value())
                    {
                        entry.state = AlgorithmCompletionState::Unavailable;
                        entry.reason = "Runtime/visual completion requires a live evidence identity.";
                    }
                    else
                    {
                        const AvailabilityStatus evidenceStatus =
                            ValidateLiveEvidenceIdentity(*match->evidence);
                        if (!evidenceStatus.IsAvailable())
                        {
                            entry.state = AlgorithmCompletionState::Unavailable;
                            entry.reason = evidenceStatus.reason;
                        }
                        else
                        {
                            entry.reason = std::string(match->reason);
                        }
                    }
                }
                else
                {
                    entry.reason = std::string(match->reason);
                }
            }
            result.push_back(std::move(entry));
        }
        return result;
    }

    std::span<const ManyLightsPreset> GetManyLightsPresets() noexcept
    {
        return kManyLightsPresets;
    }

    ManyLightsComparisonRequest BuildManyLightsComparisonRequest(
        const ManyLightsPreset& preset,
        std::span<const AlgorithmCompletionEntry> completionMatrix)
    {
        ManyLightsComparisonRequest request;
        request.preset = preset;

        std::string unavailableReasons;
        if (!IsKnownManyLightsPreset(preset))
        {
            AppendReason(unavailableReasons, "The Many Lights preset is not in the L10 catalog.");
        }

        for (std::size_t index = 0; index < kComparisonLegs.size(); ++index)
        {
            const ComparisonLegDescriptor& source = kComparisonLegs[index];
            ManyLightsComparisonLeg& leg = request.legs[index];
            leg.variant = source.variant;
            leg.stableToken = source.stableToken;
            leg.label = source.label;
            leg.directEstimatorToken = source.directEstimatorToken;
            leg.lightSelectionToken = source.lightSelectionToken;
            leg.algorithmProviderToken = source.algorithmProviderToken;
            leg.requiredAlgorithmProviderTokens = source.requiredAlgorithmProviderTokens;
            leg.requiredAlgorithmProviderCount = source.requiredAlgorithmProviderCount;
            leg.budgetIdentityToken = source.budgetIdentityToken;
            leg.requiredBiasModes = source.requiredBiasModes;
            leg.requiredBiasModeCount = source.requiredBiasModeCount;
            leg.bias = source.requiredBiasModeCount == 1u
                ? source.requiredBiasModes[0].classification
                : ManyLightsBiasClassification::NotApplicable;

            if (leg.budgetIdentityToken.empty())
            {
                AppendReason(leg.reason, "Comparison budget identity is missing.");
            }
            if (leg.requiredBiasModeCount == 0
                || leg.requiredBiasModeCount > leg.requiredBiasModes.size())
            {
                AppendReason(leg.reason, "Comparison bias-mode count is invalid.");
            }
            else
            {
                for (std::size_t biasIndex = 0;
                    biasIndex < leg.requiredBiasModeCount;
                    ++biasIndex)
                {
                    if (leg.requiredBiasModes[biasIndex].stableToken.empty())
                    {
                        AppendReason(leg.reason, "Comparison bias-mode token is missing.");
                    }
                }
                if (leg.requiredBiasModeCount == 2u
                    && (leg.requiredBiasModes[0].stableToken
                            == leg.requiredBiasModes[1].stableToken
                        || leg.requiredBiasModes[0].classification
                            == leg.requiredBiasModes[1].classification))
                {
                    AppendReason(
                        leg.reason,
                        "Comparison bias-mode requirements must be distinct.");
                }
            }
            if (leg.requiredAlgorithmProviderCount == 0
                || leg.requiredAlgorithmProviderCount
                    > leg.requiredAlgorithmProviderTokens.size())
            {
                AppendReason(leg.reason, "Comparison algorithm requirement count is invalid.");
            }
            else
            {
                AlgorithmCompletionState leastComplete =
                    AlgorithmCompletionState::VisualAccepted;
                for (std::size_t requirementIndex = 0;
                    requirementIndex < leg.requiredAlgorithmProviderCount;
                    ++requirementIndex)
                {
                    const std::string_view providerToken =
                        leg.requiredAlgorithmProviderTokens[requirementIndex];
                    const AlgorithmCompletionEntry* const completion =
                        FindAlgorithm(completionMatrix, providerToken);
                    if (completion == nullptr)
                    {
                        AppendReason(
                            leg.reason,
                            "Missing unique algorithm completion entry for '"
                                + std::string(providerToken) + "'.");
                    }
                    else if (!completion->IsRunnable())
                    {
                        AppendReason(
                            leg.reason,
                            std::string(completion->algorithm.label) + ": "
                                + (completion->reason.empty()
                                    ? "Algorithm provider is not runnable."
                                    : completion->reason));
                    }
                    else if (static_cast<std::uint8_t>(completion->state)
                        < static_cast<std::uint8_t>(leastComplete))
                    {
                        leastComplete = completion->state;
                    }
                }
                if (leg.reason.empty())
                {
                    leg.completion = leastComplete;
                }
            }

            if (!leg.IsRunnable())
            {
                AppendReason(
                    unavailableReasons,
                    std::string(leg.label) + ": " + leg.reason);
            }
        }

        if (unavailableReasons.empty())
        {
            request.availability = { Availability::Available, {} };
        }
        else
        {
            request.availability = { Availability::Unavailable, std::move(unavailableReasons) };
        }
        return request;
    }

    FixedAbComparisonSession::FixedAbComparisonSession(FixedComparisonAnchor anchor)
    {
        snapshot_.anchor = std::move(anchor);
    }

    bool FixedAbComparisonSession::IsValid() const noexcept
    {
        return !snapshot_.anchor.cameraPresetToken.empty();
    }

    const FixedComparisonSnapshot& FixedAbComparisonSession::Snapshot() const noexcept
    {
        return snapshot_;
    }

    void FixedAbComparisonSession::SelectVariant(ComparisonVariant variant) noexcept
    {
        snapshot_.activeVariant = variant;
    }

    bool FixedAbComparisonSession::Advance(
        std::uint64_t frameDelta,
        std::uint64_t sampleDelta) noexcept
    {
        if (frameDelta == 0 || sampleDelta == 0
            || frameDelta > std::numeric_limits<std::uint64_t>::max() - snapshot_.frameIndex
            || sampleDelta > std::numeric_limits<std::uint64_t>::max() - snapshot_.sampleIndex)
        {
            return false;
        }
        snapshot_.frameIndex += frameDelta;
        snapshot_.sampleIndex += sampleDelta;
        return true;
    }

    std::span<const CaptureShotDescriptor> GetCaptureShotCatalog() noexcept
    {
        return kCaptureShots;
    }

    std::vector<CaptureShotPlanEntry> BuildCaptureShotPlan(
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionEntry> completionMatrix)
    {
        const std::vector<ResolvedShowcaseSceneCard> scenes =
            ResolveShowcaseSceneCards(providers);
        return BuildCaptureShotPlan(scenes, providers, completionMatrix);
    }

    std::vector<CaptureShotPlanEntry> BuildCaptureShotPlan(
        std::span<const ResolvedShowcaseSceneCard> scenes,
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionEntry> completionMatrix)
    {
        std::vector<CaptureShotPlanEntry> result;
        result.reserve(kCaptureShots.size());
        for (const CaptureShotDescriptor& shot : kCaptureShots)
        {
            std::string unavailableReasons;
            const ResolvedShowcaseSceneCard* const scene =
                FindResolvedScene(scenes, shot.sceneToken);
            const AvailabilityStatus readback = ResolveProvider(kRendererReadbackProviderToken, providers);
            if (scene == nullptr)
            {
                AppendReason(
                    unavailableReasons,
                    "Missing unique resolved scene registry entry for '"
                        + std::string(shot.sceneToken) + "'.");
            }
            else if (!scene->availability.IsAvailable())
            {
                AppendReason(unavailableReasons, scene->availability.reason);
            }
            if (!readback.IsAvailable())
            {
                AppendReason(unavailableReasons, readback.reason);
            }

            if (shot.requiredAlgorithmProviderCount == 0)
            {
                AppendReason(unavailableReasons, "Capture shot declares no algorithm requirements.");
            }
            else if (shot.requiredAlgorithmProviderCount
                > shot.requiredAlgorithmProviderTokens.size())
            {
                AppendReason(unavailableReasons, "Capture shot algorithm requirement count is invalid.");
            }
            else
            {
                for (std::size_t index = 0;
                    index < shot.requiredAlgorithmProviderCount;
                    ++index)
                {
                    const std::string_view providerToken =
                        shot.requiredAlgorithmProviderTokens[index];
                    if (providerToken.empty())
                    {
                        AppendReason(
                            unavailableReasons,
                            "Capture shot contains an empty algorithm provider token.");
                        continue;
                    }
                    const AlgorithmCompletionEntry* const algorithm =
                        FindAlgorithm(completionMatrix, providerToken);
                    if (algorithm == nullptr)
                    {
                        AppendReason(
                            unavailableReasons,
                            "Missing unique algorithm completion entry for '"
                                + std::string(providerToken) + "'.");
                    }
                    else if (!algorithm->IsRunnable())
                    {
                        AppendReason(
                            unavailableReasons,
                            std::string(algorithm->algorithm.label) + ": "
                                + (algorithm->reason.empty()
                                    ? "Algorithm provider is not runnable."
                                    : algorithm->reason));
                    }
                }
            }

            if (shot.manyLightsCount != 0)
            {
                const ManyLightsPreset* const preset = FindManyLightsPreset(shot.manyLightsCount);
                if (preset == nullptr)
                {
                    AppendReason(unavailableReasons, "Capture references an unknown Many Lights preset.");
                }
            }

            CaptureShotPlanEntry entry;
            entry.shot = shot;
            if (scene != nullptr && !scene->fixedCameraPresetToken.empty())
            {
                entry.shot.cameraPresetToken = scene->fixedCameraPresetToken;
            }
            entry.availability = unavailableReasons.empty()
                ? AvailabilityStatus{ Availability::Available, {} }
                : AvailabilityStatus{ Availability::Unavailable, std::move(unavailableReasons) };
            result.push_back(std::move(entry));
        }
        return result;
    }

    std::span<const AssetLicenseRequirement> GetAssetLicenseRequirements() noexcept
    {
        return kLicenseRequirements;
    }

    AssetLicenseGate EvaluateAssetLicenseGate(std::span<const AssetLicenseEntry> entries)
    {
        AssetLicenseGate gate;
        for (const AssetLicenseRequirement& requirement : kLicenseRequirements)
        {
            const AssetLicenseEntry* match = nullptr;
            std::size_t matchCount = 0;
            for (const AssetLicenseEntry& entry : entries)
            {
                if (entry.assetToken == requirement.assetToken)
                {
                    match = &entry;
                    ++matchCount;
                }
            }

            if (matchCount == 0)
            {
                gate.issues.push_back({
                    std::string(requirement.assetToken),
                    "Missing license entry."
                });
                continue;
            }
            if (matchCount != 1)
            {
                gate.issues.push_back({
                    std::string(requirement.assetToken),
                    "License entry was supplied more than once."
                });
                continue;
            }

            std::string missingFields;
            const auto requireField = [&missingFields](
                const std::string& value,
                std::string_view name)
            {
                if (value.empty())
                {
                    if (!missingFields.empty())
                    {
                        missingFields += ", ";
                    }
                    missingFields.append(name);
                }
            };
            requireField(match->sourceUri, "source URI");
            requireField(match->attribution, "attribution");
            requireField(match->licenseName, "license name");
            requireField(match->licenseDocument, "license document");
            requireField(match->contentHash, "content hash");

            if (!missingFields.empty())
            {
                gate.issues.push_back({
                    std::string(requirement.assetToken),
                    "Missing required fields: " + missingFields + "."
                });
            }
            else if (match->licenseDocument != requirement.requiredLicenseDocument)
            {
                gate.issues.push_back({
                    std::string(requirement.assetToken),
                    "License document must use the required stable path '"
                        + std::string(requirement.requiredLicenseDocument) + "'."
                });
            }
        }

        if (gate.issues.empty())
        {
            gate.availability = { Availability::Available, {} };
        }
        else
        {
            gate.availability = {
                Availability::Unavailable,
                "Asset license gate failed with " + std::to_string(gate.issues.size())
                    + " issue(s)."
            };
        }
        return gate;
    }

    FinalVideoShotList::FinalVideoShotList(std::vector<FinalVideoShot> shots)
        : shots_(std::move(shots))
    {
    }

    std::span<const FinalVideoShot> FinalVideoShotList::Shots() const noexcept
    {
        return shots_;
    }

    bool FinalVideoShotList::AllApproved() const noexcept
    {
        return !shots_.empty()
            && std::all_of(
                shots_.begin(),
                shots_.end(),
                [](const FinalVideoShot& shot)
                {
                    return shot.state == FinalVideoShotState::Approved
                        && shot.captureEvidence.has_value()
                        && IsCompleteLiveEvidenceIdentity(*shot.captureEvidence);
                });
    }

    FinalVideoShotTransition FinalVideoShotList::Apply(
        std::string_view shotToken,
        FinalVideoShotEvent event,
        std::string_view reason)
    {
        return ApplyInternal(shotToken, event, nullptr, reason);
    }

    FinalVideoShotTransition FinalVideoShotList::Apply(
        std::string_view shotToken,
        FinalVideoShotEvent event,
        const ShowcaseEvidenceIdentity& evidence,
        std::string_view reason)
    {
        return ApplyInternal(shotToken, event, &evidence, reason);
    }

    FinalVideoShotTransition FinalVideoShotList::ApplyInternal(
        std::string_view shotToken,
        FinalVideoShotEvent event,
        const ShowcaseEvidenceIdentity* evidence,
        std::string_view reason)
    {
        FinalVideoShot* match = nullptr;
        std::size_t matchCount = 0;
        for (FinalVideoShot& shot : shots_)
        {
            if (shot.descriptor.stableToken == shotToken)
            {
                match = &shot;
                ++matchCount;
            }
        }

        FinalVideoShotTransition result;
        if (matchCount == 0)
        {
            result.reason = "Unknown final video shot '" + std::string(shotToken) + "'.";
            return result;
        }
        if (matchCount != 1)
        {
            result.reason = "Final video shot token is not unique.";
            return result;
        }

        result.before = match->state;
        result.after = match->state;
        const auto reject = [&result](std::string_view message)
        {
            result.reason = std::string(message);
        };

        switch (event)
        {
        case FinalVideoShotEvent::BeginCapture:
            if (match->state != FinalVideoShotState::Ready)
            {
                reject("BeginCapture requires Ready state.");
                return result;
            }
            match->state = FinalVideoShotState::Capturing;
            match->reason.clear();
            match->captureEvidence.reset();
            break;
        case FinalVideoShotEvent::CaptureSucceeded:
            if (match->state != FinalVideoShotState::Capturing)
            {
                reject("CaptureSucceeded requires Capturing state.");
                return result;
            }
            if (evidence == nullptr)
            {
                reject("CaptureSucceeded requires a live capture evidence identity.");
                return result;
            }
            {
                const AvailabilityStatus evidenceStatus =
                    ValidateLiveEvidenceIdentity(*evidence);
                if (!evidenceStatus.IsAvailable())
                {
                    reject(evidenceStatus.reason);
                    return result;
                }
            }
            match->state = FinalVideoShotState::Captured;
            match->reason.clear();
            match->captureEvidence = *evidence;
            break;
        case FinalVideoShotEvent::CaptureFailed:
            if (match->state != FinalVideoShotState::Capturing)
            {
                reject("CaptureFailed requires Capturing state.");
                return result;
            }
            if (reason.empty())
            {
                reject("CaptureFailed requires a reason.");
                return result;
            }
            match->state = FinalVideoShotState::Failed;
            match->reason = reason;
            match->captureEvidence.reset();
            break;
        case FinalVideoShotEvent::Approve:
            if (match->state != FinalVideoShotState::Captured)
            {
                reject("Approve requires Captured state.");
                return result;
            }
            if (!match->captureEvidence.has_value())
            {
                reject("Approve requires retained capture evidence identity.");
                return result;
            }
            {
                const AvailabilityStatus evidenceStatus =
                    ValidateLiveEvidenceIdentity(*match->captureEvidence);
                if (!evidenceStatus.IsAvailable())
                {
                    reject(evidenceStatus.reason);
                    return result;
                }
            }
            match->state = FinalVideoShotState::Approved;
            match->reason.clear();
            break;
        case FinalVideoShotEvent::RequestRetake:
            if (match->state != FinalVideoShotState::Captured
                && match->state != FinalVideoShotState::Failed
                && match->state != FinalVideoShotState::Approved)
            {
                reject("RequestRetake requires Captured, Failed, or Approved state.");
                return result;
            }
            match->state = FinalVideoShotState::Ready;
            match->reason.clear();
            match->captureEvidence.reset();
            break;
        case FinalVideoShotEvent::Invalidate:
            if (reason.empty())
            {
                reject("Invalidate requires a reason.");
                return result;
            }
            match->state = FinalVideoShotState::Unavailable;
            match->reason = reason;
            match->captureEvidence.reset();
            break;
        default:
            reject("Final-video event is invalid.");
            return result;
        }

        result.applied = true;
        result.after = match->state;
        return result;
    }

    std::span<const FinalVideoShotDescriptor> GetFinalVideoShotCatalog() noexcept
    {
        return kFinalVideoShots;
    }

    FinalVideoShotList BuildFinalVideoShotList(
        std::span<const CaptureShotPlanEntry> capturePlan,
        const AssetLicenseGate& licenseGate)
    {
        std::vector<FinalVideoShot> shots;
        shots.reserve(kFinalVideoShots.size());
        for (const FinalVideoShotDescriptor& descriptor : kFinalVideoShots)
        {
            FinalVideoShot shot;
            shot.descriptor = descriptor;

            if (!licenseGate.CanPublish())
            {
                shot.reason = "Final publication is blocked: "
                    + licenseGate.availability.reason;
            }
            else
            {
                const CaptureShotPlanEntry* const capture =
                    FindCaptureShot(capturePlan, descriptor.captureShotToken);
                if (capture == nullptr)
                {
                    shot.reason = "Missing unique capture shot '"
                        + std::string(descriptor.captureShotToken) + "'.";
                }
                else if (!capture->availability.IsAvailable())
                {
                    shot.reason = capture->availability.reason.empty()
                        ? "Capture shot is Unavailable without a reason."
                        : capture->availability.reason;
                }
                else
                {
                    shot.state = FinalVideoShotState::Ready;
                }
            }
            shots.push_back(std::move(shot));
        }
        return FinalVideoShotList(std::move(shots));
    }

    ShowcaseProgramModel BuildShowcaseProgramModel(
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionClaim> algorithmClaims,
        std::span<const AssetLicenseEntry> licenseEntries)
    {
        return BuildShowcaseProgramModel(
            std::span<const ShowcaseSceneRegistryEntry>{},
            providers,
            algorithmClaims,
            licenseEntries);
    }

    ShowcaseProgramModel BuildShowcaseProgramModel(
        std::span<const ShowcaseSceneRegistryEntry> sceneRegistry,
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionClaim> algorithmClaims,
        std::span<const AssetLicenseEntry> licenseEntries)
    {
        ShowcaseProgramModel model;
        model.scenes = ResolveShowcaseSceneCards(sceneRegistry, providers);
        model.algorithmCompletion = BuildAlgorithmCompletionMatrix(algorithmClaims);
        model.manyLightsComparisons.reserve(kManyLightsPresets.size());
        for (const ManyLightsPreset& preset : kManyLightsPresets)
        {
            model.manyLightsComparisons.push_back(
                BuildManyLightsComparisonRequest(preset, model.algorithmCompletion));
        }
        model.captureShots = BuildCaptureShotPlan(
            model.scenes,
            providers,
            model.algorithmCompletion);
        model.licenseGate = EvaluateAssetLicenseGate(licenseEntries);
        model.finalVideoShots = BuildFinalVideoShotList(
            model.captureShots,
            model.licenseGate);
        return model;
    }
}
