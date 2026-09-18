#include "demos/ShowcaseProgram.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace RenderingEngine::Demos;

    class TestContext final
    {
    public:
        explicit TestContext(std::ostream& output) noexcept
            : output_(output)
        {
        }

        void Expect(bool condition, std::string_view message)
        {
            if (!condition)
            {
                output_ << "L10 ShowcaseProgram test failed: " << message << '\n';
                ++failureCount_;
            }
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return failureCount_ == 0;
        }

    private:
        std::ostream& output_;
        std::uint32_t failureCount_ = 0;
    };

    template <typename Range, typename TokenGetter>
    [[nodiscard]] bool HasUniqueNonEmptyTokens(
        const Range& range,
        TokenGetter getToken)
    {
        std::set<std::string_view> tokens;
        for (const auto& entry : range)
        {
            const std::string_view token = getToken(entry);
            if (token.empty() || !tokens.insert(token).second)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<ProviderAvailability> MakeAllProvidersAvailable()
    {
        return {{ kRendererReadbackProviderToken, Availability::Available, {} }};
    }

    [[nodiscard]] std::vector<ShowcaseSceneRegistryEntry> MakeSceneRegistry()
    {
        std::vector<ShowcaseSceneRegistryEntry> registry;
        registry.reserve(GetShowcaseSceneCards().size());
        for (const ShowcaseSceneCard& card : GetShowcaseSceneCards())
        {
            ShowcaseSceneRegistryEntry entry;
            entry.stableToken = card.stableToken;
            entry.label = "Registry " + card.label;
            entry.description = "Provider-authored metadata for " + card.stableToken + ".";
            entry.owner = "L2-provider";
            entry.sceneProviderToken = "registry:scene:" + card.stableToken;
            entry.sceneAvailability = Availability::Available;
            entry.fixedCameraPresetToken = "registry:camera-preset:" + card.stableToken;
            entry.cameraProviderToken = "registry:camera:" + card.stableToken;
            entry.cameraAvailability = Availability::Available;
            registry.push_back(std::move(entry));
        }
        return registry;
    }

    [[nodiscard]] std::vector<AlgorithmCompletionClaim> MakeAllAlgorithmsImplemented()
    {
        std::vector<AlgorithmCompletionClaim> claims;
        claims.reserve(GetAlgorithmCatalog().size());
        for (const AlgorithmDescriptor& algorithm : GetAlgorithmCatalog())
        {
            claims.push_back({
                algorithm.providerToken,
                AlgorithmCompletionState::Implemented,
                {}
            });
        }
        return claims;
    }

    [[nodiscard]] std::vector<AssetLicenseEntry> MakeValidLicenseEntries()
    {
        std::vector<AssetLicenseEntry> entries;
        entries.reserve(GetAssetLicenseRequirements().size());
        for (const AssetLicenseRequirement& requirement : GetAssetLicenseRequirements())
        {
            entries.push_back({
                std::string(requirement.assetToken),
                "https://assets.example.invalid/source",
                "Pinned upstream attribution",
                "Pinned upstream license",
                std::string(requirement.requiredLicenseDocument),
                "sha256:test-fixture"
            });
        }
        return entries;
    }

    [[nodiscard]] ShowcaseEvidenceIdentity MakeLiveEvidence(
        std::string artifactIdentity = "captures/provider-run/metadata.json")
    {
        ShowcaseEvidenceIdentity identity;
        identity.artifactIdentity = std::move(artifactIdentity);
        identity.provenance = {
            EvidenceSource::ProviderReported,
            "renderer-capture-provider",
            "provider-authored live capture"
        };
        identity.configGeneration = 17u;
        identity.sceneStableId = "cornell";
        identity.sceneGeneration = 5u;
        identity.resourceGeneration = 11u;
        identity.frameIndex = 23u;
        identity.sampleIndex = 91u;
        return identity;
    }

    [[nodiscard]] const AlgorithmCompletionEntry* FindAlgorithmEntry(
        std::span<const AlgorithmCompletionEntry> matrix,
        std::string_view providerToken)
    {
        const auto found = std::find_if(
            matrix.begin(),
            matrix.end(),
            [providerToken](const AlgorithmCompletionEntry& entry)
            {
                return entry.algorithm.providerToken == providerToken;
            });
        return found == matrix.end() ? nullptr : &*found;
    }

    void TestSceneCards(TestContext& test)
    {
        const std::span<const ShowcaseSceneCard> cards = GetShowcaseSceneCards();
        test.Expect(cards.size() == 10, "scene catalog must contain exactly ten cards");
        test.Expect(
            HasUniqueNonEmptyTokens(
                cards,
                [](const ShowcaseSceneCard& card) -> std::string_view
                {
                    return card.stableToken;
                }),
            "scene tokens must be unique and non-empty");

        const std::array<std::string_view, 10> expectedTokens = {
            "baseline", "intersection-bvh", "whitted-optics", "cornell", "ggx-mis",
            "environment-dome", "sponza", "backend-parity", "temporal-stability",
            "many-lights"
        };
        for (std::size_t index = 0; index < cards.size(); ++index)
        {
            test.Expect(cards[index].stableToken == expectedTokens[index],
                "scene token order must remain stable");
            test.Expect(!cards[index].label.empty(), "scene card needs a label");
            test.Expect(!cards[index].owner.empty(), "scene card needs an owner");
            test.Expect(!cards[index].description.empty(), "scene card needs a description");
        }

        const std::vector<ResolvedShowcaseSceneCard> unresolved =
            ResolveShowcaseSceneCards({});
        test.Expect(
            std::all_of(
                unresolved.begin(),
                unresolved.end(),
                [](const ResolvedShowcaseSceneCard& card)
                {
                    return !card.availability.IsAvailable()
                        && !card.availability.reason.empty();
                }),
            "missing scene registry must resolve every roadmap slot to Unavailable with reasons");

        const ProviderAvailability baselineProvider = {
            "scene:baseline", Availability::Available, {}
        };
        const std::vector<ResolvedShowcaseSceneCard> providerWithoutRegistry =
            ResolveShowcaseSceneCards(std::span(&baselineProvider, 1));
        test.Expect(
            std::none_of(
                providerWithoutRegistry.begin(),
                providerWithoutRegistry.end(),
                [](const ResolvedShowcaseSceneCard& card)
                {
                    return card.availability.IsAvailable() || card.availability.reason.empty();
                }),
            "provider facts without a registry must not masquerade as L2 scene data");

        std::vector<ShowcaseSceneRegistryEntry> registry = MakeSceneRegistry();
        registry.front().label = "L2 authored baseline label";
        registry.front().description = "L2 authored baseline description";
        registry.front().owner = "L2-scene-registry";
        registry.front().fixedCameraPresetToken = "l2-camera:baseline-fixed";
        const std::vector<ResolvedShowcaseSceneCard> resolved =
            ResolveShowcaseSceneCards(registry, {});
        test.Expect(
            std::all_of(
                resolved.begin(),
                resolved.end(),
                [](const ResolvedShowcaseSceneCard& card)
                {
                    return card.availability.IsAvailable()
                        && card.sceneAvailability.IsAvailable()
                        && card.cameraAvailability.IsAvailable();
                }),
            "an explicit complete registry must resolve all ten roadmap slots");
        test.Expect(resolved.front().card.label == registry.front().label
                && resolved.front().card.description == registry.front().description
                && resolved.front().card.owner == registry.front().owner
                && resolved.front().fixedCameraPresetToken
                    == registry.front().fixedCameraPresetToken,
            "resolved scene and fixed-camera metadata must come from the registry input");

        std::vector<ShowcaseSceneRegistryEntry> duplicateRegistry = registry;
        duplicateRegistry.push_back(registry.front());
        const std::vector<ResolvedShowcaseSceneCard> duplicateResult =
            ResolveShowcaseSceneCards(duplicateRegistry, {});
        test.Expect(!duplicateResult.front().availability.IsAvailable()
                && !duplicateResult.front().availability.reason.empty(),
            "duplicate registry entries must fail closed");

        std::vector<ShowcaseSceneRegistryEntry> invalidRegistry = registry;
        invalidRegistry.front().sceneAvailability = static_cast<Availability>(255);
        const std::vector<ResolvedShowcaseSceneCard> invalidResult =
            ResolveShowcaseSceneCards(invalidRegistry, {});
        test.Expect(!invalidResult.front().availability.IsAvailable()
                && invalidResult.front().availability.reason.find("invalid availability")
                    != std::string::npos,
            "unknown provider availability enum values must fail closed");

        std::vector<ShowcaseSceneRegistryEntry> missingCameraRegistry = registry;
        missingCameraRegistry.front().cameraProviderToken.clear();
        const std::vector<ResolvedShowcaseSceneCard> missingCameraResult =
            ResolveShowcaseSceneCards(missingCameraRegistry, {});
        test.Expect(!missingCameraResult.front().availability.IsAvailable()
                && !missingCameraResult.front().cameraAvailability.reason.empty(),
            "missing fixed-camera provider must be Unavailable with a reason");

        std::vector<ShowcaseSceneRegistryEntry> missingSceneProviderRegistry = registry;
        missingSceneProviderRegistry[1].sceneProviderToken.clear();
        const std::vector<ResolvedShowcaseSceneCard> missingProviderResult =
            ResolveShowcaseSceneCards(missingSceneProviderRegistry, {});
        test.Expect(!missingProviderResult[1].availability.IsAvailable()
                && !missingProviderResult[1].sceneAvailability.reason.empty(),
            "missing scene provider must be Unavailable with a reason");
    }

    void TestAlgorithmMatrixAndManyLights(TestContext& test)
    {
        const std::span<const AlgorithmDescriptor> catalog = GetAlgorithmCatalog();
        test.Expect(catalog.size() >= 24, "completion matrix must cover all planned algorithm families");
        test.Expect(
            HasUniqueNonEmptyTokens(
                catalog,
                [](const AlgorithmDescriptor& algorithm) { return algorithm.stableToken; }),
            "algorithm stable tokens must be unique");
        test.Expect(
            HasUniqueNonEmptyTokens(
                catalog,
                [](const AlgorithmDescriptor& algorithm) { return algorithm.providerToken; }),
            "algorithm provider tokens must be unique");
        test.Expect(
            std::all_of(
                catalog.begin(),
                catalog.end(),
                [](const AlgorithmDescriptor& algorithm)
                {
                    return !algorithm.explanation.empty()
                        && !algorithm.knownLimitation.empty();
                }),
            "every F1/F2 algorithm card must explain the method and its current limitation");

        const std::vector<AlgorithmCompletionEntry> emptyMatrix =
            BuildAlgorithmCompletionMatrix({});
        test.Expect(emptyMatrix.size() == catalog.size(),
            "completion matrix must preserve the full algorithm catalog");
        test.Expect(
            std::all_of(
                emptyMatrix.begin(),
                emptyMatrix.end(),
                [](const AlgorithmCompletionEntry& entry)
                {
                    return entry.state == AlgorithmCompletionState::Unavailable
                        && !entry.reason.empty()
                        && !entry.IsRunnable();
                }),
            "missing completion providers must be Unavailable with reasons");

        const std::array<AlgorithmCompletionClaim, 4> partialClaims = {
            AlgorithmCompletionClaim{
                "algorithm:uniform-one-light", AlgorithmCompletionState::Implemented, {}
            },
            AlgorithmCompletionClaim{
                "algorithm:power-weighted-one-light", AlgorithmCompletionState::Declared,
                "Declared but not executable."
            },
            AlgorithmCompletionClaim{
                "algorithm:restir-di", AlgorithmCompletionState::Unavailable,
                "L9 runtime provider is absent."
            },
            AlgorithmCompletionClaim{
                "algorithm:whitted", AlgorithmCompletionState::Implemented, {}
            }
        };
        const std::vector<AlgorithmCompletionEntry> partialMatrix =
            BuildAlgorithmCompletionMatrix(partialClaims);
        const AlgorithmCompletionEntry* const uniform =
            FindAlgorithmEntry(partialMatrix, "algorithm:uniform-one-light");
        const AlgorithmCompletionEntry* const power =
            FindAlgorithmEntry(partialMatrix, "algorithm:power-weighted-one-light");
        const AlgorithmCompletionEntry* const restir =
            FindAlgorithmEntry(partialMatrix, "algorithm:restir-di");
        test.Expect(uniform != nullptr && uniform->IsRunnable(),
            "Implemented uniform provider must be runnable");
        test.Expect(power != nullptr && !power->IsRunnable(),
            "Declared power provider must not be runnable");
        test.Expect(restir != nullptr && !restir->IsRunnable() && !restir->reason.empty(),
            "Unavailable ReSTIR provider must preserve its reason");

        const std::span<const ManyLightsPreset> presets = GetManyLightsPresets();
        test.Expect(presets.size() == 3, "Many Lights must expose exactly three presets");
        test.Expect(presets[0].lightCount == 100
                && presets[1].lightCount == 1'000
                && presets[2].lightCount == 10'000,
            "Many Lights counts must be 100, 1,000, and 10,000");
        test.Expect(
            HasUniqueNonEmptyTokens(
                presets,
                [](const ManyLightsPreset& preset) { return preset.stableToken; }),
            "Many Lights preset tokens must be stable and unique");

        const ManyLightsComparisonRequest blocked =
            BuildManyLightsComparisonRequest(presets.front(), partialMatrix);
        test.Expect(!blocked.availability.IsAvailable()
                && !blocked.availability.reason.empty(),
            "comparison request must remain unavailable while a leg is not runnable");
        test.Expect(blocked.legs[0].stableToken == "uniform"
                && blocked.legs[0].lightSelectionToken == "uniform",
            "uniform comparison leg must keep the uniform proposal token");
        test.Expect(blocked.legs[1].stableToken == "power"
                && blocked.legs[1].lightSelectionToken == "power",
            "power comparison leg must keep the power proposal token");
        test.Expect(blocked.legs[2].stableToken == "restir"
                && blocked.legs[2].directEstimatorToken == "restir-di",
            "ReSTIR comparison leg must keep the ReSTIR estimator token");
        test.Expect(blocked.legs[3].stableToken == "high-spp-reference"
                && blocked.legs[3].directEstimatorToken == "cpu-reference"
                && blocked.legs[3].requiredAlgorithmProviderCount == 2
                && blocked.legs[3].requiredAlgorithmProviderTokens[0]
                    == "algorithm:cpu-reference"
                && blocked.legs[3].requiredAlgorithmProviderTokens[1]
                    == "algorithm:high-spp-reference",
            "fourth comparison leg must require CPU reference and high-SPP completion");
        test.Expect(blocked.legs[0].budgetIdentityToken
                    == "many-lights-equal-realtime-budget-v1"
                && blocked.legs[1].budgetIdentityToken
                    == blocked.legs[0].budgetIdentityToken
                && blocked.legs[2].budgetIdentityToken
                    == blocked.legs[0].budgetIdentityToken
                && blocked.legs[3].budgetIdentityToken
                    == "many-lights-high-spp-reference-budget-v1",
            "every comparison leg must expose a stable budget identity without invented values");
        test.Expect(blocked.legs[0].requiredBiasModeCount == 1u
                && blocked.legs[0].requiredBiasModes[0].stableToken == "unbiased"
                && blocked.legs[0].requiredBiasModes[0].classification
                    == ManyLightsBiasClassification::Unbiased
                && blocked.legs[1].requiredBiasModeCount == 1u
                && blocked.legs[1].requiredBiasModes[0].stableToken == "unbiased"
                && blocked.legs[1].requiredBiasModes[0].classification
                    == ManyLightsBiasClassification::Unbiased
                && blocked.legs[3].requiredBiasModeCount == 1u
                && blocked.legs[3].requiredBiasModes[0].stableToken
                    == "not-applicable"
                && blocked.legs[3].requiredBiasModes[0].classification
                    == ManyLightsBiasClassification::NotApplicable,
            "uniform, power, and reference legs must expose their stable bias modes");
        test.Expect(blocked.legs[2].requiredBiasModeCount == 2u
                && blocked.legs[2].requiredBiasModes[0].stableToken == "biased"
                && blocked.legs[2].requiredBiasModes[0].classification
                    == ManyLightsBiasClassification::Biased
                && blocked.legs[2].requiredBiasModes[1].stableToken == "reference-correction"
                && blocked.legs[2].requiredBiasModes[1].classification
                    == ManyLightsBiasClassification::ReferenceCorrection
                && blocked.legs[2].bias
                    == ManyLightsBiasClassification::NotApplicable,
            "ReSTIR must expose reference correction without claiming proven unbiasedness");

        const std::vector<AlgorithmCompletionClaim> allClaims =
            MakeAllAlgorithmsImplemented();
        const std::vector<AlgorithmCompletionEntry> fullMatrix =
            BuildAlgorithmCompletionMatrix(allClaims);
        for (const ManyLightsPreset& preset : presets)
        {
            const ManyLightsComparisonRequest request =
                BuildManyLightsComparisonRequest(preset, fullMatrix);
            test.Expect(request.availability.IsAvailable(),
                "comparison becomes executable only with all four explicit legs");
            test.Expect(
                std::all_of(
                    request.legs.begin(),
                    request.legs.end(),
                    [](const ManyLightsComparisonLeg& leg) { return leg.IsRunnable(); }),
                "an available comparison must have four runnable legs");
        }

        std::vector<AlgorithmCompletionClaim> missingReferenceClaims = allClaims;
        std::erase_if(
            missingReferenceClaims,
            [](const AlgorithmCompletionClaim& claim)
            {
                return claim.providerToken == "algorithm:high-spp-reference";
            });
        const std::vector<AlgorithmCompletionEntry> missingReferenceMatrix =
            BuildAlgorithmCompletionMatrix(missingReferenceClaims);
        const ManyLightsComparisonRequest missingReference =
            BuildManyLightsComparisonRequest(presets.back(), missingReferenceMatrix);
        test.Expect(!missingReference.availability.IsAvailable()
                && !missingReference.legs[3].IsRunnable()
                && !missingReference.legs[3].reason.empty(),
            "missing high-SPP reference completion must fail-close the four-way request");

        const std::array<AlgorithmCompletionClaim, 2> duplicateClaims = {
            AlgorithmCompletionClaim{
                "algorithm:uniform-one-light", AlgorithmCompletionState::Implemented, {}
            },
            AlgorithmCompletionClaim{
                "algorithm:uniform-one-light", AlgorithmCompletionState::Implemented, {}
            }
        };
        const std::vector<AlgorithmCompletionEntry> duplicateMatrix =
            BuildAlgorithmCompletionMatrix(duplicateClaims);
        const AlgorithmCompletionEntry* const duplicateUniform =
            FindAlgorithmEntry(duplicateMatrix, "algorithm:uniform-one-light");
        test.Expect(duplicateUniform != nullptr
                && duplicateUniform->state == AlgorithmCompletionState::Unavailable
                && !duplicateUniform->reason.empty(),
            "duplicate algorithm claims must fail closed");

        ShowcaseEvidenceIdentity syntheticEvidence = MakeLiveEvidence();
        syntheticEvidence.provenance.source = EvidenceSource::SyntheticTest;
        const ShowcaseEvidenceIdentity liveEvidence = MakeLiveEvidence();
        ShowcaseEvidenceIdentity missingSceneGeneration = liveEvidence;
        missingSceneGeneration.sceneGeneration.reset();
        ShowcaseEvidenceIdentity missingResourceGeneration = liveEvidence;
        missingResourceGeneration.resourceGeneration.reset();
        test.Expect(!ValidateLiveEvidenceIdentity(missingSceneGeneration).IsAvailable()
                && !ValidateLiveEvidenceIdentity(missingResourceGeneration).IsAvailable(),
            "scene and resource generations must both be present in a live identity");

        const std::array<AlgorithmCompletionClaim, 5> evidenceClaims = {
            AlgorithmCompletionClaim{
                "algorithm:whitted",
                AlgorithmCompletionState::RuntimeValidated,
                {},
                std::nullopt
            },
            AlgorithmCompletionClaim{
                "algorithm:pbr",
                AlgorithmCompletionState::VisualAccepted,
                {},
                syntheticEvidence
            },
            AlgorithmCompletionClaim{
                "algorithm:cpu-reference",
                AlgorithmCompletionState::RuntimeValidated,
                {},
                liveEvidence
            },
            AlgorithmCompletionClaim{
                "algorithm:cpu-sah",
                AlgorithmCompletionState::RuntimeValidated,
                {},
                missingSceneGeneration
            },
            AlgorithmCompletionClaim{
                "algorithm:gpu-lbvh",
                AlgorithmCompletionState::VisualAccepted,
                {},
                missingResourceGeneration
            }
        };
        const std::vector<AlgorithmCompletionEntry> evidenceMatrix =
            BuildAlgorithmCompletionMatrix(evidenceClaims);
        const AlgorithmCompletionEntry* const missingEvidence =
            FindAlgorithmEntry(evidenceMatrix, "algorithm:whitted");
        const AlgorithmCompletionEntry* const syntheticClaim =
            FindAlgorithmEntry(evidenceMatrix, "algorithm:pbr");
        const AlgorithmCompletionEntry* const validatedClaim =
            FindAlgorithmEntry(evidenceMatrix, "algorithm:cpu-reference");
        const AlgorithmCompletionEntry* const missingSceneClaim =
            FindAlgorithmEntry(evidenceMatrix, "algorithm:cpu-sah");
        const AlgorithmCompletionEntry* const missingResourceClaim =
            FindAlgorithmEntry(evidenceMatrix, "algorithm:gpu-lbvh");
        test.Expect(missingEvidence != nullptr
                && missingEvidence->state == AlgorithmCompletionState::Unavailable
                && !missingEvidence->reason.empty(),
            "RuntimeValidated without structured evidence must fail closed");
        test.Expect(syntheticClaim != nullptr
                && syntheticClaim->state == AlgorithmCompletionState::Unavailable
                && syntheticClaim->evidence.has_value()
                && !syntheticClaim->reason.empty(),
            "SyntheticTest evidence must be retained for audit but cannot satisfy live completion");
        test.Expect(validatedClaim != nullptr
                && validatedClaim->state == AlgorithmCompletionState::RuntimeValidated
                && validatedClaim->evidence.has_value()
                && validatedClaim->evidence->artifactIdentity == liveEvidence.artifactIdentity
                && validatedClaim->evidence->sceneGeneration
                    == liveEvidence.sceneGeneration
                && validatedClaim->evidence->resourceGeneration
                    == liveEvidence.resourceGeneration,
            "valid runtime completion must retain its full live evidence identity");
        test.Expect(missingSceneClaim != nullptr
                && missingSceneClaim->state == AlgorithmCompletionState::Unavailable
                && missingSceneClaim->evidence.has_value()
                && !missingSceneClaim->reason.empty()
                && missingResourceClaim != nullptr
                && missingResourceClaim->state == AlgorithmCompletionState::Unavailable
                && missingResourceClaim->evidence.has_value()
                && !missingResourceClaim->reason.empty(),
            "runtime/visual claims missing either structured generation must fail closed");
    }

    void TestFixedAbSession(TestContext& test)
    {
        const FixedComparisonAnchor anchor = {
            "cornell-reference", 0x1234'5678'9abc'def0ull, 42u
        };
        FixedAbComparisonSession session(anchor);
        test.Expect(session.IsValid(), "non-empty camera token must create a valid A/B session");
        test.Expect(session.Snapshot().activeVariant == ComparisonVariant::A,
            "A/B session must begin on variant A");

        session.SelectVariant(ComparisonVariant::B);
        test.Expect(session.Advance(3u, 12u),
            "locked comparison session must still advance frame and sample indices");
        const FixedComparisonSnapshot progressed = session.Snapshot();
        test.Expect(progressed.activeVariant == ComparisonVariant::B,
            "variant selection must be retained");
        test.Expect(progressed.frameIndex == 3u && progressed.sampleIndex == 12u,
            "frame/sample progression must use supplied deltas");
        test.Expect(progressed.anchor.cameraPresetToken == anchor.cameraPresetToken
                && progressed.anchor.baseSeed == anchor.baseSeed
                && progressed.anchor.animationOriginTick == anchor.animationOriginTick,
            "camera, base seed, and animation origin must stay fixed across A/B progression");

        test.Expect(!session.Advance(0u, 1u), "zero frame delta must be rejected");
        test.Expect(session.Snapshot().frameIndex == 3u && session.Snapshot().sampleIndex == 12u,
            "rejected A/B advance must preserve both indices");

        FixedAbComparisonSession overflowSession(anchor);
        const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        test.Expect(overflowSession.Advance(maximum, maximum),
            "indices may advance exactly to their maximum value");
        test.Expect(!overflowSession.Advance(), "overflowing A/B advance must be rejected");
        test.Expect(overflowSession.Snapshot().frameIndex == maximum
                && overflowSession.Snapshot().sampleIndex == maximum,
            "overflow rejection must be atomic");

        FixedAbComparisonSession invalid({ {}, 7u, 9u });
        test.Expect(!invalid.IsValid(), "empty camera token must make an A/B session invalid");
    }

    void TestCaptureAndLicensePlan(TestContext& test)
    {
        const std::vector<AlgorithmCompletionEntry> emptyMatrix =
            BuildAlgorithmCompletionMatrix({});
        const std::vector<CaptureShotPlanEntry> blockedPlan =
            BuildCaptureShotPlan({}, emptyMatrix);
        test.Expect(blockedPlan.size() == 12,
            "capture catalog must cover nine regular scenes and three Many Lights presets");
        test.Expect(
            HasUniqueNonEmptyTokens(
                blockedPlan,
                [](const CaptureShotPlanEntry& entry) { return entry.shot.stableToken; }),
            "capture shot tokens must be unique");
        test.Expect(
            std::all_of(
                blockedPlan.begin(),
                blockedPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return !entry.availability.IsAvailable()
                        && !entry.availability.reason.empty();
                }),
            "capture shots with missing upstream providers must be Unavailable with reasons");
        test.Expect(
            std::count_if(
                blockedPlan.begin(),
                blockedPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return entry.shot.manyLightsCount != 0;
                }) == 3,
            "capture plan must contain three distinct Many Lights comparison shots");
        test.Expect(
            std::all_of(
                GetCaptureShotCatalog().begin(),
                GetCaptureShotCatalog().end(),
                [](const CaptureShotDescriptor& shot)
                {
                    if (shot.requiredAlgorithmProviderCount == 0
                        || shot.requiredAlgorithmProviderCount
                            > shot.requiredAlgorithmProviderTokens.size())
                    {
                        return false;
                    }
                    for (std::size_t index = 0;
                        index < shot.requiredAlgorithmProviderCount;
                        ++index)
                    {
                        if (shot.requiredAlgorithmProviderTokens[index].empty())
                        {
                            return false;
                        }
                    }
                    return true;
                }),
            "every capture shot must declare at least one valid algorithm provider dependency");

        const std::vector<ProviderAvailability> providers = MakeAllProvidersAvailable();
        const std::vector<ShowcaseSceneRegistryEntry> registry = MakeSceneRegistry();
        const std::vector<ResolvedShowcaseSceneCard> scenes =
            ResolveShowcaseSceneCards(registry, providers);
        const std::vector<CaptureShotPlanEntry> algorithmsMissingPlan =
            BuildCaptureShotPlan(scenes, providers, emptyMatrix);
        test.Expect(
            std::all_of(
                algorithmsMissingPlan.begin(),
                algorithmsMissingPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return !entry.availability.IsAvailable()
                        && !entry.availability.reason.empty();
                }),
            "scene/camera/readback providers alone must not make algorithm-dependent shots Ready");

        const std::vector<AlgorithmCompletionClaim> claims = MakeAllAlgorithmsImplemented();
        const std::vector<AlgorithmCompletionEntry> fullMatrix =
            BuildAlgorithmCompletionMatrix(claims);
        const std::vector<CaptureShotPlanEntry> readyPlan =
            BuildCaptureShotPlan(scenes, providers, fullMatrix);
        test.Expect(
            std::all_of(
                readyPlan.begin(),
                readyPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return entry.availability.IsAvailable();
                }),
            "capture plan becomes ready only when scene, camera, readback, and algorithms are explicit");

        std::vector<AlgorithmCompletionClaim> noHighSppClaims = claims;
        std::erase_if(
            noHighSppClaims,
            [](const AlgorithmCompletionClaim& claim)
            {
                return claim.providerToken == "algorithm:high-spp-reference";
            });
        const std::vector<AlgorithmCompletionEntry> noHighSppMatrix =
            BuildAlgorithmCompletionMatrix(noHighSppClaims);
        const std::vector<CaptureShotPlanEntry> noHighSppPlan =
            BuildCaptureShotPlan(scenes, providers, noHighSppMatrix);
        test.Expect(
            std::count_if(
                noHighSppPlan.begin(),
                noHighSppPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return entry.shot.manyLightsCount != 0
                        && !entry.availability.IsAvailable()
                        && !entry.availability.reason.empty();
                }) == 3,
            "all three Many Lights capture presets must gate high-SPP reference completion");

        const std::vector<CaptureShotPlanEntry> missingRegistryPlan =
            BuildCaptureShotPlan(providers, fullMatrix);
        test.Expect(
            std::none_of(
                missingRegistryPlan.begin(),
                missingRegistryPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return entry.availability.IsAvailable();
                }),
            "legacy capture overload must fail closed when no scene registry is supplied");

        const ProviderAvailability invalidReadback = {
            kRendererReadbackProviderToken, static_cast<Availability>(255), {}
        };
        const std::vector<CaptureShotPlanEntry> invalidReadbackPlan =
            BuildCaptureShotPlan(
                scenes,
                std::span(&invalidReadback, 1),
                fullMatrix);
        test.Expect(
            std::all_of(
                invalidReadbackPlan.begin(),
                invalidReadbackPlan.end(),
                [](const CaptureShotPlanEntry& entry)
                {
                    return !entry.availability.IsAvailable()
                        && entry.availability.reason.find("invalid availability")
                            != std::string::npos;
                }),
            "unknown readback availability values must fail closed for every capture shot");

        const AssetLicenseGate missingGate = EvaluateAssetLicenseGate({});
        test.Expect(!missingGate.CanPublish()
                && missingGate.issues.size() == GetAssetLicenseRequirements().size()
                && !missingGate.availability.reason.empty(),
            "missing license entries must close the publication gate with issues");

        std::vector<AssetLicenseEntry> licenses = MakeValidLicenseEntries();
        const AssetLicenseGate validGate = EvaluateAssetLicenseGate(licenses);
        test.Expect(validGate.CanPublish() && validGate.issues.empty(),
            "complete license entries must open the publication gate");

        licenses.front().licenseDocument = "licenses/wrong.md";
        const AssetLicenseGate wrongPathGate = EvaluateAssetLicenseGate(licenses);
        test.Expect(!wrongPathGate.CanPublish() && !wrongPathGate.issues.empty(),
            "license document path mismatch must close the gate");
        licenses = MakeValidLicenseEntries();
        licenses.front().contentHash.clear();
        const AssetLicenseGate incompleteGate = EvaluateAssetLicenseGate(licenses);
        test.Expect(!incompleteGate.CanPublish() && !incompleteGate.issues.empty(),
            "missing content hash must close the gate");
    }

    void TestFinalVideoStateMachine(TestContext& test)
    {
        const std::vector<AlgorithmCompletionClaim> claims = MakeAllAlgorithmsImplemented();
        const std::vector<AlgorithmCompletionEntry> matrix =
            BuildAlgorithmCompletionMatrix(claims);
        const std::vector<ProviderAvailability> providers = MakeAllProvidersAvailable();
        const std::vector<ShowcaseSceneRegistryEntry> registry = MakeSceneRegistry();
        const std::vector<ResolvedShowcaseSceneCard> scenes =
            ResolveShowcaseSceneCards(registry, providers);
        const std::vector<CaptureShotPlanEntry> readyPlan =
            BuildCaptureShotPlan(scenes, providers, matrix);
        const std::vector<AssetLicenseEntry> licenses = MakeValidLicenseEntries();
        const AssetLicenseGate validGate = EvaluateAssetLicenseGate(licenses);

        FinalVideoShotList blockedList = BuildFinalVideoShotList(
            readyPlan,
            EvaluateAssetLicenseGate({}));
        test.Expect(blockedList.Shots().size() == 10,
            "final video list must contain exactly ten narrative shots");
        test.Expect(
            std::all_of(
                blockedList.Shots().begin(),
                blockedList.Shots().end(),
                [](const FinalVideoShot& shot)
                {
                    return shot.state == FinalVideoShotState::Unavailable
                        && !shot.reason.empty();
                }),
            "missing license gate must make every final publication shot unavailable");

        FinalVideoShotList list = BuildFinalVideoShotList(readyPlan, validGate);
        test.Expect(
            std::all_of(
                list.Shots().begin(),
                list.Shots().end(),
                [](const FinalVideoShot& shot)
                {
                    return shot.state == FinalVideoShotState::Ready;
                }),
            "satisfied capture and license prerequisites must create Ready shots");
        test.Expect(!list.AllApproved(), "Ready shots must not count as approved");

        const std::string_view firstToken = list.Shots().front().descriptor.stableToken;
        const FinalVideoShotTransition invalidSuccess = list.Apply(
            firstToken,
            FinalVideoShotEvent::CaptureSucceeded);
        test.Expect(!invalidSuccess.applied
                && invalidSuccess.before == FinalVideoShotState::Ready
                && invalidSuccess.after == FinalVideoShotState::Ready
                && !invalidSuccess.reason.empty(),
            "invalid transition must report a reason and preserve state");

        test.Expect(list.Apply(firstToken, FinalVideoShotEvent::BeginCapture).applied,
            "Ready -> Capturing must be allowed");
        const FinalVideoShotTransition missingCaptureEvidence = list.Apply(
            firstToken,
            FinalVideoShotEvent::CaptureSucceeded);
        test.Expect(!missingCaptureEvidence.applied
                && missingCaptureEvidence.after == FinalVideoShotState::Capturing
                && !missingCaptureEvidence.reason.empty(),
            "CaptureSucceeded without a structured evidence identity must fail closed");
        ShowcaseEvidenceIdentity syntheticCapture = MakeLiveEvidence();
        syntheticCapture.provenance.source = EvidenceSource::SyntheticTest;
        const FinalVideoShotTransition syntheticCaptureResult = list.Apply(
            firstToken,
            FinalVideoShotEvent::CaptureSucceeded,
            syntheticCapture);
        test.Expect(!syntheticCaptureResult.applied
                && syntheticCaptureResult.after == FinalVideoShotState::Capturing
                && !syntheticCaptureResult.reason.empty(),
            "SyntheticTest capture evidence must not complete a final-video shot");
        const FinalVideoShotTransition missingFailureReason = list.Apply(
            firstToken,
            FinalVideoShotEvent::CaptureFailed);
        test.Expect(!missingFailureReason.applied
                && missingFailureReason.after == FinalVideoShotState::Capturing,
            "CaptureFailed without a reason must be rejected atomically");
        test.Expect(list.Apply(
                firstToken,
                FinalVideoShotEvent::CaptureFailed,
                "encoder returned an error").applied,
            "Capturing -> Failed must accept an explicit reason");
        test.Expect(list.Apply(firstToken, FinalVideoShotEvent::RequestRetake).applied,
            "Failed -> Ready retake must be allowed");

        FinalVideoShotList approvalList = BuildFinalVideoShotList(readyPlan, validGate);
        for (const FinalVideoShotDescriptor& descriptor : GetFinalVideoShotCatalog())
        {
            ShowcaseEvidenceIdentity evidence = MakeLiveEvidence(
                "captures/" + std::string(descriptor.stableToken) + "/metadata.json");
            test.Expect(approvalList.Apply(
                    descriptor.stableToken,
                    FinalVideoShotEvent::BeginCapture).applied,
                "every Ready final shot must begin capture");
            test.Expect(approvalList.Apply(
                    descriptor.stableToken,
                    FinalVideoShotEvent::CaptureSucceeded,
                    evidence).applied,
                "every Capturing final shot must bind live evidence on capture success");
            test.Expect(approvalList.Apply(
                    descriptor.stableToken,
                    FinalVideoShotEvent::Approve).applied,
                "every Captured final shot must accept approval");
        }
        test.Expect(approvalList.AllApproved(),
            "final video list must report complete only after every shot is approved");
        test.Expect(
            std::all_of(
                approvalList.Shots().begin(),
                approvalList.Shots().end(),
                [](const FinalVideoShot& shot)
                {
                    return shot.captureEvidence.has_value()
                        && !shot.captureEvidence->artifactIdentity.empty();
                }),
            "approved final-video shots must retain their capture evidence identities");

        FinalVideoShot forgedCaptured;
        forgedCaptured.descriptor = GetFinalVideoShotCatalog().front();
        forgedCaptured.state = FinalVideoShotState::Captured;
        FinalVideoShotList forgedList({ std::move(forgedCaptured) });
        const FinalVideoShotTransition forgedApproval = forgedList.Apply(
            GetFinalVideoShotCatalog().front().stableToken,
            FinalVideoShotEvent::Approve);
        test.Expect(!forgedApproval.applied
                && forgedApproval.after == FinalVideoShotState::Captured
                && !forgedApproval.reason.empty(),
            "Captured state without retained evidence must not be approved");

        const std::string_view approvedToken = approvalList.Shots().front().descriptor.stableToken;
        const FinalVideoShotTransition invalidated = approvalList.Apply(
            approvedToken,
            FinalVideoShotEvent::Invalidate,
            "asset license was revoked");
        test.Expect(invalidated.applied
                && invalidated.after == FinalVideoShotState::Unavailable
                && !approvalList.AllApproved()
                && !approvalList.Shots().front().captureEvidence.has_value(),
            "late prerequisite invalidation must revoke completion and retained evidence");
    }

    void TestComposedModelDefaults(TestContext& test)
    {
        const ShowcaseProgramModel empty = BuildShowcaseProgramModel({}, {}, {});
        test.Expect(empty.scenes.size() == 10, "composed model must retain all scene cards");
        test.Expect(empty.algorithmCompletion.size() == GetAlgorithmCatalog().size(),
            "composed model must retain the full completion matrix");
        test.Expect(empty.manyLightsComparisons.size() == 3,
            "composed model must retain all Many Lights presets");
        test.Expect(empty.captureShots.size() == 12,
            "composed model must retain the complete capture plan");
        test.Expect(!empty.licenseGate.CanPublish(),
            "composed model must fail closed when upstream data is absent");
        test.Expect(empty.finalVideoShots.Shots().size() == 10
                && std::all_of(
                    empty.finalVideoShots.Shots().begin(),
                    empty.finalVideoShots.Shots().end(),
                    [](const FinalVideoShot& shot)
                    {
                        return shot.state == FinalVideoShotState::Unavailable
                            && !shot.reason.empty();
                    }),
            "composed model must retain an unavailable final-video plan with reasons");

        const std::vector<ProviderAvailability> providers = MakeAllProvidersAvailable();
        const std::vector<ShowcaseSceneRegistryEntry> registry = MakeSceneRegistry();
        const std::vector<AlgorithmCompletionClaim> claims = MakeAllAlgorithmsImplemented();
        const std::vector<AssetLicenseEntry> licenses = MakeValidLicenseEntries();
        const ShowcaseProgramModel noRegistry = BuildShowcaseProgramModel(
            providers,
            claims,
            licenses);
        test.Expect(
            std::none_of(
                noRegistry.scenes.begin(),
                noRegistry.scenes.end(),
                [](const ResolvedShowcaseSceneCard& card)
                {
                    return card.availability.IsAvailable();
                }),
            "legacy composed-model overload must not infer L2 scene data from provider tokens");

        const ShowcaseProgramModel ready = BuildShowcaseProgramModel(
            registry,
            providers,
            claims,
            licenses);
        test.Expect(
            std::all_of(
                ready.scenes.begin(),
                ready.scenes.end(),
                [](const ResolvedShowcaseSceneCard& card)
                {
                    return card.availability.IsAvailable();
                }),
            "explicit scene registry must resolve the complete catalog");
        test.Expect(
            std::all_of(
                ready.manyLightsComparisons.begin(),
                ready.manyLightsComparisons.end(),
                [](const ManyLightsComparisonRequest& request)
                {
                    return request.availability.IsAvailable();
                }),
            "explicit algorithm providers must resolve every Many Lights request");
        test.Expect(ready.licenseGate.CanPublish(),
            "complete explicit licenses must pass in the composed model");
        test.Expect(ready.finalVideoShots.Shots().size() == 10
                && std::all_of(
                    ready.finalVideoShots.Shots().begin(),
                    ready.finalVideoShots.Shots().end(),
                    [](const FinalVideoShot& shot)
                    {
                        return shot.state == FinalVideoShotState::Ready;
                    }),
            "complete explicit providers must make the composed video plan Ready");
    }
}

bool RunShowcaseProgramTests(std::ostream& output)
{
    TestContext test(output);
    TestSceneCards(test);
    TestAlgorithmMatrixAndManyLights(test);
    TestFixedAbSession(test);
    TestCaptureAndLicensePlan(test);
    TestFinalVideoStateMachine(test);
    TestComposedModelDefaults(test);

    if (test.Passed())
    {
        output << "L10 ShowcaseProgram orchestration tests passed.\n";
    }
    return test.Passed();
}
