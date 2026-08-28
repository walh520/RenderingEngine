#pragma once

#include "demos/ShowcaseEvidence.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderingEngine::Demos
{
    enum class Availability : std::uint8_t
    {
        Available,
        Unavailable
    };

    struct AvailabilityStatus
    {
        Availability state = Availability::Unavailable;
        std::string reason;

        [[nodiscard]] bool IsAvailable() const noexcept
        {
            return state == Availability::Available;
        }
    };

    // Provider facts are supplied by the owning lane. Missing facts never imply
    // support: every resolver below reports Unavailable with a reason instead.
    struct ProviderAvailability
    {
        std::string_view providerToken;
        Availability state = Availability::Unavailable;
        std::string_view reason;
    };

    struct ShowcaseSceneCard
    {
        std::string stableToken;
        std::string label;
        std::string owner;
        std::string description;
    };

    // L2 (or another scene-owning lane) supplies these records. L10 copies only
    // metadata and provider facts; it never owns scene payloads or camera data.
    struct ShowcaseSceneRegistryEntry
    {
        std::string stableToken;
        std::string label;
        std::string description;
        std::string owner;

        std::string sceneProviderToken;
        Availability sceneAvailability = Availability::Unavailable;
        std::string sceneAvailabilityReason;

        std::string fixedCameraPresetToken;
        std::string cameraProviderToken;
        Availability cameraAvailability = Availability::Unavailable;
        std::string cameraAvailabilityReason;
    };

    struct ResolvedShowcaseSceneCard
    {
        ShowcaseSceneCard card;
        AvailabilityStatus availability;
        std::string sceneProviderToken;
        std::string fixedCameraPresetToken;
        std::string cameraProviderToken;
        AvailabilityStatus sceneAvailability;
        AvailabilityStatus cameraAvailability;
    };

    [[nodiscard]] std::span<const ShowcaseSceneCard> GetShowcaseSceneCards() noexcept;
    [[nodiscard]] std::vector<ResolvedShowcaseSceneCard> ResolveShowcaseSceneCards(
        std::span<const ProviderAvailability> providers);
    [[nodiscard]] std::vector<ResolvedShowcaseSceneCard> ResolveShowcaseSceneCards(
        std::span<const ShowcaseSceneRegistryEntry> sceneRegistry,
        std::span<const ProviderAvailability> providers);

    enum class AlgorithmCompletionState : std::uint8_t
    {
        Unavailable,
        Declared,
        Implemented,
        RuntimeValidated,
        VisualAccepted
    };

    // Identity for retained, non-synthetic evidence. Optional numeric fields
    // distinguish a legitimate zero index/generation from an omitted identity.
    struct ShowcaseEvidenceIdentity
    {
        std::string artifactIdentity;
        EvidenceProvenance provenance;
        std::optional<std::uint64_t> configGeneration;
        std::string sceneStableId;
        std::optional<std::uint64_t> sceneGeneration;
        std::optional<std::uint64_t> resourceGeneration;
        std::optional<std::uint64_t> frameIndex;
        std::optional<std::uint64_t> sampleIndex;
    };

    [[nodiscard]] AvailabilityStatus ValidateLiveEvidenceIdentity(
        const ShowcaseEvidenceIdentity& identity);

    struct AlgorithmDescriptor
    {
        std::string_view stableToken;
        std::string_view label;
        std::string_view category;
        std::string_view owner;
        std::string_view providerToken;
        std::string_view explanation;
        std::string_view knownLimitation;
    };

    struct AlgorithmCompletionClaim
    {
        std::string_view providerToken;
        AlgorithmCompletionState state = AlgorithmCompletionState::Unavailable;
        std::string_view reason;
        std::optional<ShowcaseEvidenceIdentity> evidence;
    };

    struct AlgorithmCompletionEntry
    {
        AlgorithmDescriptor algorithm;
        AlgorithmCompletionState state = AlgorithmCompletionState::Unavailable;
        std::string reason;
        std::optional<ShowcaseEvidenceIdentity> evidence;

        [[nodiscard]] bool IsRunnable() const noexcept
        {
            return state == AlgorithmCompletionState::Implemented
                || state == AlgorithmCompletionState::RuntimeValidated
                || state == AlgorithmCompletionState::VisualAccepted;
        }
    };

    [[nodiscard]] std::span<const AlgorithmDescriptor> GetAlgorithmCatalog() noexcept;
    [[nodiscard]] std::vector<AlgorithmCompletionEntry> BuildAlgorithmCompletionMatrix(
        std::span<const AlgorithmCompletionClaim> claims);

    struct ManyLightsPreset
    {
        std::string_view stableToken;
        std::string_view label;
        std::uint32_t lightCount = 0;
    };

    enum class ManyLightsComparisonVariant : std::uint8_t
    {
        Uniform,
        PowerWeighted,
        Restir,
        HighSppReference
    };

    enum class ManyLightsBiasClassification : std::uint8_t
    {
        Biased,
        Unbiased,
        NotApplicable
    };

    struct ManyLightsBiasModeRequirement
    {
        std::string_view stableToken;
        ManyLightsBiasClassification classification =
            ManyLightsBiasClassification::NotApplicable;
    };

    struct ManyLightsComparisonLeg
    {
        ManyLightsComparisonVariant variant = ManyLightsComparisonVariant::Uniform;
        std::string_view stableToken;
        std::string_view label;
        std::string_view directEstimatorToken;
        std::string_view lightProposalToken;
        std::string_view algorithmProviderToken;
        std::array<std::string_view, 2> requiredAlgorithmProviderTokens{};
        std::uint8_t requiredAlgorithmProviderCount = 0;
        std::string_view budgetIdentityToken;
        std::array<ManyLightsBiasModeRequirement, 2> requiredBiasModes{};
        std::uint8_t requiredBiasModeCount = 0;
        // Compatibility summary for single-mode legs. Multi-mode legs use
        // NotApplicable here and expose their authoritative modes above.
        ManyLightsBiasClassification bias =
            ManyLightsBiasClassification::NotApplicable;
        AlgorithmCompletionState completion = AlgorithmCompletionState::Unavailable;
        std::string reason;

        [[nodiscard]] bool IsRunnable() const noexcept
        {
            return completion == AlgorithmCompletionState::Implemented
                || completion == AlgorithmCompletionState::RuntimeValidated
                || completion == AlgorithmCompletionState::VisualAccepted;
        }
    };

    struct ManyLightsComparisonRequest
    {
        ManyLightsPreset preset;
        std::array<ManyLightsComparisonLeg, 4> legs;
        AvailabilityStatus availability;
    };

    [[nodiscard]] std::span<const ManyLightsPreset> GetManyLightsPresets() noexcept;
    [[nodiscard]] ManyLightsComparisonRequest BuildManyLightsComparisonRequest(
        const ManyLightsPreset& preset,
        std::span<const AlgorithmCompletionEntry> completionMatrix);

    enum class ComparisonVariant : std::uint8_t
    {
        A,
        B
    };

    struct FixedComparisonAnchor
    {
        std::string cameraPresetToken;
        std::uint64_t baseSeed = 0;
        std::uint64_t animationOriginTick = 0;
    };

    struct FixedComparisonSnapshot
    {
        FixedComparisonAnchor anchor;
        ComparisonVariant activeVariant = ComparisonVariant::A;
        std::uint64_t frameIndex = 0;
        std::uint64_t sampleIndex = 0;
    };

    // The anchor has no mutation API. Selecting A/B therefore cannot change the
    // camera, base seed, or animation origin, while Advance still progresses the
    // frame/sample sequence used by a realtime renderer.
    class FixedAbComparisonSession final
    {
    public:
        explicit FixedAbComparisonSession(FixedComparisonAnchor anchor);

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const FixedComparisonSnapshot& Snapshot() const noexcept;
        void SelectVariant(ComparisonVariant variant) noexcept;

        // Returns false and preserves both indices if either delta is zero or an
        // unsigned overflow would occur.
        [[nodiscard]] bool Advance(
            std::uint64_t frameDelta = 1,
            std::uint64_t sampleDelta = 1) noexcept;

    private:
        FixedComparisonSnapshot snapshot_;
    };

    enum class CaptureShotKind : std::uint8_t
    {
        Hero,
        Correctness,
        Quality,
        Performance,
        Comparison
    };

    inline constexpr std::string_view kRendererReadbackProviderToken =
        "capture:renderer-readback";

    struct CaptureShotDescriptor
    {
        std::string_view stableToken;
        std::string_view sceneToken;
        std::string_view label;
        std::string_view purpose;
        CaptureShotKind kind = CaptureShotKind::Hero;
        std::uint32_t manyLightsCount = 0;
        std::array<std::string_view, 8> requiredAlgorithmProviderTokens{};
        std::uint8_t requiredAlgorithmProviderCount = 0;

        // The roadmap catalog uses the stable selector "registry-fixed". A
        // resolved plan entry replaces it with the provider-authored preset token.
        std::string cameraPresetToken = "registry-fixed";
    };

    struct CaptureShotPlanEntry
    {
        CaptureShotDescriptor shot;
        AvailabilityStatus availability;
    };

    [[nodiscard]] std::span<const CaptureShotDescriptor> GetCaptureShotCatalog() noexcept;
    [[nodiscard]] std::vector<CaptureShotPlanEntry> BuildCaptureShotPlan(
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionEntry> completionMatrix);
    [[nodiscard]] std::vector<CaptureShotPlanEntry> BuildCaptureShotPlan(
        std::span<const ResolvedShowcaseSceneCard> scenes,
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionEntry> completionMatrix);

    struct AssetLicenseRequirement
    {
        std::string_view assetToken;
        std::string_view label;
        std::string_view owner;
        std::string_view sceneToken;
        std::string_view requiredLicenseDocument;
    };

    struct AssetLicenseEntry
    {
        std::string assetToken;
        std::string sourceUri;
        std::string attribution;
        std::string licenseName;
        std::string licenseDocument;
        std::string contentHash;
    };

    struct AssetLicenseIssue
    {
        std::string assetToken;
        std::string reason;
    };

    struct AssetLicenseGate
    {
        AvailabilityStatus availability;
        std::vector<AssetLicenseIssue> issues;

        [[nodiscard]] bool CanPublish() const noexcept
        {
            return availability.IsAvailable();
        }
    };

    [[nodiscard]] std::span<const AssetLicenseRequirement>
    GetAssetLicenseRequirements() noexcept;
    [[nodiscard]] AssetLicenseGate EvaluateAssetLicenseGate(
        std::span<const AssetLicenseEntry> entries);

    struct FinalVideoShotDescriptor
    {
        std::string_view stableToken;
        std::string_view label;
        std::string_view captureShotToken;
        std::string_view narrativePurpose;
    };

    enum class FinalVideoShotState : std::uint8_t
    {
        Unavailable,
        Ready,
        Capturing,
        Captured,
        Approved,
        Failed
    };

    struct FinalVideoShot
    {
        FinalVideoShotDescriptor descriptor;
        FinalVideoShotState state = FinalVideoShotState::Unavailable;
        std::string reason;
        std::optional<ShowcaseEvidenceIdentity> captureEvidence;
    };

    enum class FinalVideoShotEvent : std::uint8_t
    {
        BeginCapture,
        CaptureSucceeded,
        CaptureFailed,
        Approve,
        RequestRetake,
        Invalidate
    };

    struct FinalVideoShotTransition
    {
        bool applied = false;
        FinalVideoShotState before = FinalVideoShotState::Unavailable;
        FinalVideoShotState after = FinalVideoShotState::Unavailable;
        std::string reason;
    };

    class FinalVideoShotList final
    {
    public:
        FinalVideoShotList() = default;
        explicit FinalVideoShotList(std::vector<FinalVideoShot> shots);

        [[nodiscard]] std::span<const FinalVideoShot> Shots() const noexcept;
        [[nodiscard]] bool AllApproved() const noexcept;
        [[nodiscard]] FinalVideoShotTransition Apply(
            std::string_view shotToken,
            FinalVideoShotEvent event,
            std::string_view reason = {});
        [[nodiscard]] FinalVideoShotTransition Apply(
            std::string_view shotToken,
            FinalVideoShotEvent event,
            const ShowcaseEvidenceIdentity& evidence,
            std::string_view reason = {});

    private:
        [[nodiscard]] FinalVideoShotTransition ApplyInternal(
            std::string_view shotToken,
            FinalVideoShotEvent event,
            const ShowcaseEvidenceIdentity* evidence,
            std::string_view reason);

        std::vector<FinalVideoShot> shots_;
    };

    [[nodiscard]] std::span<const FinalVideoShotDescriptor>
    GetFinalVideoShotCatalog() noexcept;
    [[nodiscard]] FinalVideoShotList BuildFinalVideoShotList(
        std::span<const CaptureShotPlanEntry> capturePlan,
        const AssetLicenseGate& licenseGate);

    struct ShowcaseProgramModel
    {
        std::vector<ResolvedShowcaseSceneCard> scenes;
        std::vector<AlgorithmCompletionEntry> algorithmCompletion;
        std::vector<ManyLightsComparisonRequest> manyLightsComparisons;
        std::vector<CaptureShotPlanEntry> captureShots;
        AssetLicenseGate licenseGate;
        FinalVideoShotList finalVideoShots;
    };

    [[nodiscard]] ShowcaseProgramModel BuildShowcaseProgramModel(
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionClaim> algorithmClaims,
        std::span<const AssetLicenseEntry> licenseEntries);
    [[nodiscard]] ShowcaseProgramModel BuildShowcaseProgramModel(
        std::span<const ShowcaseSceneRegistryEntry> sceneRegistry,
        std::span<const ProviderAvailability> providers,
        std::span<const AlgorithmCompletionClaim> algorithmClaims,
        std::span<const AssetLicenseEntry> licenseEntries);
}
