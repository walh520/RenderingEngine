#include "ui/ImGuiShowcasePanels.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>

#if defined(_MSC_VER)
#if defined(_DEBUG)
#pragma comment(lib, "imguid.lib")
#else
#pragma comment(lib, "imgui.lib")
#endif
#endif

namespace RenderingEngine::Ui
{
    namespace
    {
        void Text(std::string_view value)
        {
            const char* const begin = value.empty() ? "" : value.data();
            ImGui::TextUnformatted(begin, begin + value.size());
        }

        void TextDisabled(std::string_view value)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            Text(value);
            ImGui::PopStyleColor();
        }

        [[nodiscard]] std::string_view TelemetryAvailabilityLabel(
            TelemetryAvailability availability) noexcept
        {
            switch (availability)
            {
            case TelemetryAvailability::Unavailable: return "unavailable";
            case TelemetryAvailability::Pending: return "pending";
            case TelemetryAvailability::Fresh: return "fresh";
            case TelemetryAvailability::Stale: return "stale";
            case TelemetryAvailability::Invalid: return "invalid";
            }
            return "invalid";
        }

        [[nodiscard]] std::string_view ProvenanceLabel(
            TelemetryProvenance provenance) noexcept
        {
            switch (provenance)
            {
            case TelemetryProvenance::LiveRuntime: return "live-runtime";
            case TelemetryProvenance::ImportedArtifact: return "imported-artifact";
            case TelemetryProvenance::SyntheticTest: return "synthetic-test";
            }
            return "invalid";
        }

        [[nodiscard]] std::string_view MetricUnitLabel(MetricUnit unit) noexcept
        {
            switch (unit)
            {
            case MetricUnit::Nanoseconds: return "ns";
            case MetricUnit::Microseconds: return "us";
            case MetricUnit::Milliseconds: return "ms";
            case MetricUnit::Seconds: return "s";
            case MetricUnit::Count: return "count";
            case MetricUnit::Bytes: return "bytes";
            case MetricUnit::RaysPerSecond: return "rays/s";
            case MetricUnit::Percentage: return "%";
            case MetricUnit::Ratio: return "ratio";
            }
            return "invalid";
        }

        [[nodiscard]] std::string_view BenchmarkPhaseLabel(
            Demos::BenchmarkPhase phase) noexcept
        {
            switch (phase)
            {
            case Demos::BenchmarkPhase::Idle: return "idle";
            case Demos::BenchmarkPhase::Warmup: return "warm-up";
            case Demos::BenchmarkPhase::Measurement: return "measurement";
            case Demos::BenchmarkPhase::AwaitingRepeat: return "awaiting repeat";
            case Demos::BenchmarkPhase::Complete: return "complete";
            case Demos::BenchmarkPhase::Failed: return "failed";
            }
            return "invalid";
        }

        [[nodiscard]] std::string_view CompletionLabel(
            Demos::AlgorithmCompletionState state) noexcept
        {
            switch (state)
            {
            case Demos::AlgorithmCompletionState::Unavailable: return "unavailable";
            case Demos::AlgorithmCompletionState::Declared: return "declared";
            case Demos::AlgorithmCompletionState::Implemented: return "implemented";
            case Demos::AlgorithmCompletionState::RuntimeValidated: return "runtime-validated";
            case Demos::AlgorithmCompletionState::VisualAccepted: return "visual-accepted";
            }
            return "invalid";
        }

        [[nodiscard]] std::string_view VideoShotStateLabel(
            Demos::FinalVideoShotState state) noexcept
        {
            switch (state)
            {
            case Demos::FinalVideoShotState::Unavailable: return "unavailable";
            case Demos::FinalVideoShotState::Ready: return "ready";
            case Demos::FinalVideoShotState::Capturing: return "capturing";
            case Demos::FinalVideoShotState::Captured: return "captured";
            case Demos::FinalVideoShotState::Approved: return "approved";
            case Demos::FinalVideoShotState::Failed: return "failed";
            }
            return "invalid";
        }

        [[nodiscard]] const HelpEntryViewModel* FindHelpEntry(
            const ShowcaseViewModel& model,
            SemanticAction action) noexcept
        {
            const auto found = std::find_if(
                model.help.begin(),
                model.help.end(),
                [action](const HelpEntryViewModel& entry)
                {
                    return entry.binding != nullptr && entry.binding->action == action;
                });
            return found == model.help.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::string_view WorkflowStateLabel(
            Demos::ShowcaseWorkflowState state) noexcept
        {
            switch (state)
            {
            case Demos::ShowcaseWorkflowState::Idle: return "idle";
            case Demos::ShowcaseWorkflowState::AwaitingA: return "awaiting A";
            case Demos::ShowcaseWorkflowState::AwaitingB: return "awaiting B";
            case Demos::ShowcaseWorkflowState::Complete: return "complete";
            case Demos::ShowcaseWorkflowState::Failed: return "failed";
            }
            return "invalid";
        }

        [[nodiscard]] const CapabilityOptionViewModel* FindSelectedOption(
            const ShowcaseViewModel& model,
            std::string_view dimensionId) noexcept
        {
            const auto dimension = std::find_if(
                model.dimensions.begin(),
                model.dimensions.end(),
                [dimensionId](const CapabilityDimensionViewModel& candidate)
                {
                    return candidate.id == dimensionId;
                });
            if (dimension == model.dimensions.end())
            {
                return nullptr;
            }
            const auto option = std::find_if(
                dimension->options.begin(),
                dimension->options.end(),
                [](const CapabilityOptionViewModel& candidate)
                {
                    return candidate.selected;
                });
            return option == dimension->options.end() ? nullptr : &*option;
        }

        [[nodiscard]] bool SelectedTokenMatchesAlgorithm(
            std::string_view selectedToken,
            std::string_view algorithmToken) noexcept
        {
            if (selectedToken == algorithmToken)
            {
                return true;
            }
            constexpr std::array<std::pair<std::string_view, std::string_view>, 6> aliases = {{
                { "whitted", "legacy-whitted" },
                { "pbr", "legacy-pbr" },
                { "environment", "environment-importance" },
                { "uniform", "uniform-one-light" },
                { "power", "power-weighted-one-light" },
                { "temporal-atrous", "atrous" }
            }};
            return std::any_of(
                aliases.begin(),
                aliases.end(),
                [selectedToken, algorithmToken](const auto& alias)
                {
                    return alias.first == selectedToken && alias.second == algorithmToken;
                });
        }

        [[nodiscard]] bool IsCurrentAlgorithm(
            const ShowcaseViewModel& model,
            std::string_view algorithmToken) noexcept
        {
            constexpr std::array<std::string_view, 5> dimensions = {
                "backend", "integrator", "direct-lighting", "light-proposal", "reconstruction"
            };
            return std::any_of(
                dimensions.begin(),
                dimensions.end(),
                [&model, algorithmToken](std::string_view dimensionId)
                {
                    const CapabilityOptionViewModel* const selected =
                        FindSelectedOption(model, dimensionId);
                    return selected != nullptr
                        && SelectedTokenMatchesAlgorithm(selected->token, algorithmToken);
                });
        }

        void DrawCurrentProgramExplanation(
            const ShowcaseViewModel& model,
            const Demos::ShowcaseProgramModel* program)
        {
            ImGui::SeparatorText("Current scene and algorithms");
            if (program == nullptr)
            {
                TextDisabled("showcase program provider is unavailable");
                return;
            }

            const CapabilityOptionViewModel* const selectedScene =
                FindSelectedOption(model, "scene");
            const auto scene = selectedScene == nullptr
                ? program->scenes.end()
                : std::find_if(
                    program->scenes.begin(),
                    program->scenes.end(),
                    [selectedScene](const Demos::ResolvedShowcaseSceneCard& candidate)
                    {
                        return candidate.card.stableToken == selectedScene->token;
                    });
            if (scene == program->scenes.end())
            {
                TextDisabled("current scene has no program registry entry");
            }
            else
            {
                Text(scene->card.label);
                Text(scene->card.description);
                if (!scene->availability.IsAvailable())
                {
                    TextDisabled(scene->availability.reason);
                }
            }

            bool matchedAlgorithm = false;
            for (const Demos::AlgorithmCompletionEntry& entry : program->algorithmCompletion)
            {
                if (!IsCurrentAlgorithm(model, entry.algorithm.stableToken))
                {
                    continue;
                }
                matchedAlgorithm = true;
                ImGui::BulletText("%.*s | %.*s | %.*s",
                    static_cast<int>(entry.algorithm.label.size()), entry.algorithm.label.data(),
                    static_cast<int>(entry.algorithm.category.size()), entry.algorithm.category.data(),
                    static_cast<int>(CompletionLabel(entry.state).size()),
                    CompletionLabel(entry.state).data());
                if (!entry.algorithm.explanation.empty())
                {
                    Text(entry.algorithm.explanation);
                }
                if (!entry.algorithm.knownLimitation.empty())
                {
                    const std::string limitation = "Limitation: "
                        + std::string(entry.algorithm.knownLimitation);
                    TextDisabled(limitation);
                }
                if (!entry.reason.empty())
                {
                    TextDisabled(entry.reason);
                }
            }
            if (!matchedAlgorithm)
            {
                TextDisabled("current tuple has no matching program algorithm entry");
            }
            if (!model.currentTupleCapability.reason.empty())
            {
                TextDisabled(model.currentTupleCapability.reason);
            }
        }

        void DrawStatusOverlay(const ShowcaseViewModel& model)
        {
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin("Showcase Status", nullptr, flags))
            {
                Text(model.tupleText);
                Text(model.runtimeStatus.text);
                if (!model.runtimeStatus.providerId.empty())
                {
                    ImGui::Text("Provider: %s | %.*s | %.*s | config=%llu scene=%llu resource=%llu",
                        model.runtimeStatus.providerId.c_str(),
                        static_cast<int>(ProvenanceLabel(model.runtimeStatus.provenance).size()),
                        ProvenanceLabel(model.runtimeStatus.provenance).data(),
                        static_cast<int>(TelemetryAvailabilityLabel(model.runtimeStatus.availability).size()),
                        TelemetryAvailabilityLabel(model.runtimeStatus.availability).data(),
                        static_cast<unsigned long long>(model.runtimeStatus.configGeneration),
                        static_cast<unsigned long long>(model.runtimeStatus.sceneGeneration),
                        static_cast<unsigned long long>(model.runtimeStatus.resourceGeneration));
                }
                if (!model.runtimeStatus.reason.empty())
                {
                    TextDisabled(model.runtimeStatus.reason);
                }
                if (model.currentTupleCapability.status == CapabilityStatus::Supported)
                {
                    Text(model.currentTupleCapability.text);
                }
                else
                {
                    TextDisabled(model.currentTupleCapability.text);
                }
                if (!model.currentTupleCapability.reason.empty())
                {
                    TextDisabled(model.currentTupleCapability.reason);
                }
            }
            ImGui::End();
        }

        void DrawRequestButton(
            const char* label,
            SemanticAction action,
            const ShowcaseViewModel& model,
            ActionQueue& queue)
        {
            const HelpEntryViewModel* const help = FindHelpEntry(model, action);
            const bool enabled = help != nullptr && help->enabled;
            ImGui::BeginDisabled(!enabled);
            if (ImGui::Button(label))
            {
                queue.Push(action);
            }
            ImGui::EndDisabled();
            if (!enabled && help != nullptr && !help->reason.empty())
            {
                ImGui::SameLine();
                TextDisabled(help->reason);
            }
        }

        [[nodiscard]] bool IsSceneRuntimeEnabled(
            const ShowcaseViewModel& model,
            std::size_t sceneIndex,
            std::string_view& reason) noexcept
        {
            const auto scenes = std::find_if(
                model.dimensions.begin(),
                model.dimensions.end(),
                [](const CapabilityDimensionViewModel& dimension)
                {
                    return dimension.id == "scene";
                });
            if (scenes == model.dimensions.end() || sceneIndex >= scenes->options.size())
            {
                reason = "RuntimeConfig scene capability is absent";
                return false;
            }
            reason = scenes->options[sceneIndex].reason;
            return scenes->options[sceneIndex].enabled;
        }

        void DrawAlgorithmPanel(
            bool& open,
            const ShowcaseViewModel& model,
            ActionQueue& queue,
            const Demos::ShowcaseProgramModel* program)
        {
            if (!open)
            {
                return;
            }
            if (!ImGui::Begin("Algorithm", &open))
            {
                ImGui::End();
                return;
            }

            Text(model.tupleText);
            Text(model.runtimeStatus.text);
            if (!model.runtimeStatus.providerId.empty())
            {
                const std::string source =
                    "Runtime status provider: " + model.runtimeStatus.providerId;
                TextDisabled(source);
            }
            if (!model.runtimeStatus.reason.empty())
            {
                TextDisabled(model.runtimeStatus.reason);
            }
            if (model.currentTupleCapability.status == CapabilityStatus::Supported)
            {
                Text(model.currentTupleCapability.text);
            }
            else
            {
                TextDisabled(model.currentTupleCapability.text);
            }
            DrawCurrentProgramExplanation(model, program);
            ImGui::Separator();
            for (const CapabilityDimensionViewModel& dimension : model.dimensions)
            {
                if (dimension.id == "scene")
                {
                    continue;
                }
                ImGui::PushID(dimension.id.data(), dimension.id.data() + dimension.id.size());
                Text(dimension.label);
                ImGui::SameLine();
                if (ImGui::SmallButton("<"))
                {
                    static_cast<void>(QueueShowcaseDimensionCycle(queue, dimension.id, false));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(">"))
                {
                    static_cast<void>(QueueShowcaseDimensionCycle(queue, dimension.id, true));
                }
                for (const CapabilityOptionViewModel& option : dimension.options)
                {
                    if (!option.selected)
                    {
                        continue;
                    }
                    ImGui::SameLine();
                    Text(option.label);
                    if (!option.enabled && !option.reason.empty())
                    {
                        TextDisabled(option.reason);
                    }
                    break;
                }
                ImGui::PopID();
            }

            if (ImGui::CollapsingHeader("Capability details"))
            {
                for (const CapabilityDimensionViewModel& dimension : model.dimensions)
                {
                    ImGui::PushID(dimension.id.data(), dimension.id.data() + dimension.id.size());
                    const std::string dimensionLabel(dimension.label);
                    if (ImGui::TreeNode(dimensionLabel.c_str()))
                    {
                        if (ImGui::BeginTable(
                            "options",
                            6,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                | ImGuiTableFlags_Resizable))
                        {
                            ImGui::TableSetupColumn("Option");
                            ImGui::TableSetupColumn("Selected");
                            ImGui::TableSetupColumn("Built");
                            ImGui::TableSetupColumn("Available");
                            ImGui::TableSetupColumn("Owner");
                            ImGui::TableSetupColumn("Reason");
                            ImGui::TableHeadersRow();
                            for (const CapabilityOptionViewModel& option : dimension.options)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); Text(option.label);
                                ImGui::TableSetColumnIndex(1); Text(option.selected ? "yes" : "no");
                                ImGui::TableSetColumnIndex(2); Text(option.built ? "yes" : "no");
                                ImGui::TableSetColumnIndex(3); Text(option.enabled ? "yes" : "no");
                                ImGui::TableSetColumnIndex(4); Text(option.owner);
                                ImGui::TableSetColumnIndex(5);
                                if (option.reason.empty())
                                {
                                    Text("--");
                                }
                                else
                                {
                                    TextDisabled(option.reason);
                                }
                            }
                            ImGui::EndTable();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }

            ImGui::SeparatorText("Completion matrix");
            if (program == nullptr)
            {
                TextDisabled("algorithm completion provider is unavailable");
            }
            else if (ImGui::BeginTable(
                "completion-matrix",
                4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Algorithm");
                ImGui::TableSetupColumn("Category");
                ImGui::TableSetupColumn("Owner");
                ImGui::TableSetupColumn("Evidence state");
                ImGui::TableHeadersRow();
                for (const Demos::AlgorithmCompletionEntry& entry : program->algorithmCompletion)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); Text(entry.algorithm.label);
                    ImGui::TableSetColumnIndex(1); Text(entry.algorithm.category);
                    ImGui::TableSetColumnIndex(2); Text(entry.algorithm.owner);
                    ImGui::TableSetColumnIndex(3); Text(CompletionLabel(entry.state));
                    if (!entry.reason.empty() && ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%.*s", static_cast<int>(entry.reason.size()), entry.reason.data());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        void DrawScenePanel(
            bool& open,
            const ShowcaseViewModel& model,
            ActionQueue& queue,
            const Demos::ShowcaseProgramModel* program)
        {
            if (!open)
            {
                return;
            }
            if (!ImGui::Begin("Scene", &open))
            {
                ImGui::End();
                return;
            }
            if (program == nullptr)
            {
                TextDisabled("scene registry provider is unavailable");
                ImGui::End();
                return;
            }

            for (std::size_t index = 0; index < program->scenes.size(); ++index)
            {
                const Demos::ResolvedShowcaseSceneCard& scene = program->scenes[index];
                std::string_view runtimeReason;
                const bool runtimeEnabled = IsSceneRuntimeEnabled(model, index, runtimeReason);
                const bool enabled = scene.availability.IsAvailable() && runtimeEnabled;
                ImGui::PushID(static_cast<int>(index));
                ImGui::BeginDisabled(!enabled);
                const std::string buttonLabel(scene.card.label);
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1.0f, 0.0f)))
                {
                    static_cast<void>(QueueShowcaseSceneSelection(queue, index));
                }
                ImGui::EndDisabled();
                Text(scene.card.description);
                if (!scene.availability.IsAvailable())
                {
                    TextDisabled(scene.availability.reason);
                }
                else if (!runtimeEnabled)
                {
                    TextDisabled(runtimeReason);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::End();
        }

        void DrawDebugPanel(
            bool& open,
            bool showLegend,
            ImGuiShowcaseSelectionState& selection,
            const DebugProfilerModel* model,
            const IImGuiDebugTextureResolver* resolver)
        {
            if (!open)
            {
                return;
            }
            if (!ImGui::Begin("Debug", &open))
            {
                ImGui::End();
                return;
            }
            if (model == nullptr)
            {
                TextDisabled("debug-resource catalog provider is unavailable");
                ImGui::End();
                return;
            }

            const std::span<const DebugResourceView> resources = model->DebugResources();
            if (resources.empty())
            {
                TextDisabled("no debug resources have been published");
                ImGui::End();
                return;
            }
            selection.selectedDebugResource = std::min(
                selection.selectedDebugResource,
                resources.size() - 1u);

            if (ImGui::BeginListBox("Resources", ImVec2(-1.0f, 140.0f)))
            {
                for (std::size_t index = 0; index < resources.size(); ++index)
                {
                    const DebugResourceView& resource = resources[index];
                    const bool selected = selection.selectedDebugResource == index;
                    ImGui::PushID(static_cast<int>(index));
                    const std::string label(resource.descriptor.label);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        selection.selectedDebugResource = index;
                    }
                    ImGui::SameLine();
                    TextDisabled(TelemetryAvailabilityLabel(resource.availability));
                    ImGui::PopID();
                }
                ImGui::EndListBox();
            }

            const DebugResourceView& selected = resources[selection.selectedDebugResource];
            Text(selected.descriptor.stableId);
            ImGui::Text("%u x %u x %u, %.*s",
                selected.descriptor.extent.width,
                selected.descriptor.extent.height,
                selected.descriptor.extent.depth,
                static_cast<int>(selected.descriptor.format.size()),
                selected.descriptor.format.data());
            ImGui::Text("provider=%.*s, provenance=%.*s, frame=%llu, config=%llu, scene=%llu, resource=%llu",
                static_cast<int>(selected.providerId.size()), selected.providerId.data(),
                static_cast<int>(ProvenanceLabel(selected.provenance).size()),
                ProvenanceLabel(selected.provenance).data(),
                static_cast<unsigned long long>(selected.frameGeneration),
                static_cast<unsigned long long>(selected.configGeneration),
                static_cast<unsigned long long>(selected.sceneGeneration),
                static_cast<unsigned long long>(selected.resourceGeneration));

            if (selected.availability != TelemetryAvailability::Fresh
                && selected.availability != TelemetryAvailability::Stale)
            {
                TextDisabled(selected.reason);
            }
            else
            {
                if (selected.availability == TelemetryAvailability::Stale)
                {
                    TextDisabled(selected.reason);
                }
                if (resolver == nullptr)
                {
                    TextDisabled("ImGui texture resolver is unavailable");
                }
                else
                {
                    const std::optional<std::uint64_t> textureId =
                        resolver->ResolveTextureId(selected.descriptor.opaqueUiToken);
                    if (!textureId.has_value() || *textureId == 0u)
                    {
                        TextDisabled("opaque debug token is not registered with the ImGui backend");
                    }
                    else
                    {
                        const float width = static_cast<float>(selected.descriptor.extent.width);
                        const float height = static_cast<float>(selected.descriptor.extent.height);
                        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                        const float drawWidth = std::min(availableWidth, 768.0f);
                        const float drawHeight = height > 0.0f
                            ? drawWidth * height / std::max(width, 1.0f)
                            : drawWidth;
                        const ImTextureRef texture(static_cast<ImTextureID>(*textureId));
                        ImGui::Image(texture, ImVec2(drawWidth, drawHeight));
                    }
                }
            }
            if (showLegend && !selected.descriptor.legend.empty())
            {
                ImGui::SeparatorText("Legend");
                Text(selected.descriptor.legend);
            }
            ImGui::End();
        }

        void DrawProfilerPanel(bool& open, const DebugProfilerModel* model)
        {
            if (!open)
            {
                return;
            }
            if (!ImGui::Begin("Profiler", &open))
            {
                ImGui::End();
                return;
            }
            if (model == nullptr)
            {
                TextDisabled("profiler provider is unavailable");
                ImGui::End();
                return;
            }

            for (const ProviderView& provider : model->Providers())
            {
                ImGui::BulletText("%.*s: %.*s (%.*s)",
                    static_cast<int>(provider.providerId.size()), provider.providerId.data(),
                    static_cast<int>(TelemetryAvailabilityLabel(provider.availability).size()),
                    TelemetryAvailabilityLabel(provider.availability).data(),
                    static_cast<int>(ProvenanceLabel(provider.provenance).size()),
                    ProvenanceLabel(provider.provenance).data());
                if (!provider.reason.empty())
                {
                    ImGui::SameLine();
                    TextDisabled(provider.reason);
                }
            }

            if (ImGui::BeginTable(
                "profiler-metrics",
                8,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                    | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 360.0f)))
            {
                constexpr std::array<const char*, 8> columns = {
                    "Metric", "State", "Current", "Min", "Max", "Median", "P95", "Unit"
                };
                for (const char* column : columns)
                {
                    ImGui::TableSetupColumn(column);
                }
                ImGui::TableHeadersRow();
                for (const MetricView& metric : model->Metrics())
                {
                    const auto drawValue = [](const std::optional<double>& value)
                    {
                        if (value.has_value())
                        {
                            ImGui::Text("%.9g", *value);
                        }
                        else
                        {
                            TextDisabled("--");
                        }
                    };

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); Text(metric.descriptor.label);
                    ImGui::TableSetColumnIndex(1); Text(TelemetryAvailabilityLabel(metric.availability));
                    ImGui::TableSetColumnIndex(2); drawValue(metric.currentValue);
                    ImGui::TableSetColumnIndex(3); drawValue(metric.rolling.minimum);
                    ImGui::TableSetColumnIndex(4); drawValue(metric.rolling.maximum);
                    ImGui::TableSetColumnIndex(5); drawValue(metric.rolling.median);
                    ImGui::TableSetColumnIndex(6); drawValue(metric.rolling.percentile95);
                    ImGui::TableSetColumnIndex(7); Text(MetricUnitLabel(metric.descriptor.unit));
                    if (!metric.reason.empty() && ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%.*s", static_cast<int>(metric.reason.size()), metric.reason.data());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        [[nodiscard]] std::string FixedAbStartDisabledReason(
            const ImGuiShowcaseInputs& inputs)
        {
            if (inputs.controller == nullptr)
            {
                return "showcase workflow controller is unavailable";
            }
            if (inputs.controller->AbWorkflowState() != Demos::ShowcaseWorkflowState::Idle)
            {
                return "cancel or reset the current A/B workflow before starting another pair";
            }
            if (inputs.fixedAbStart == nullptr)
            {
                return inputs.fixedAbStartUnavailableReason.empty()
                    ? "composition did not provide a fixed A/B start input"
                    : std::string(inputs.fixedAbStartUnavailableReason);
            }
            if (!inputs.controller->FeatureAvailability().captureProvider)
            {
                return "controller capture provider is unavailable";
            }
            if (inputs.fixedAbStart->captureProvider.state != Demos::Availability::Available)
            {
                return inputs.fixedAbStart->captureProvider.reason.empty()
                    ? "composition reported the fixed A/B capture provider unavailable"
                    : std::string(inputs.fixedAbStart->captureProvider.reason);
            }
            if (inputs.fixedAbStart->configGeneration
                    != inputs.controller->ConfigGeneration()
                || inputs.fixedAbStart->sceneGeneration
                    != inputs.controller->SceneGeneration()
                || inputs.fixedAbStart->resourceGeneration
                    != inputs.controller->ResourceGeneration())
            {
                return "composition A/B start generations do not match the current controller tuple";
            }
            return {};
        }

        void DrawFixedAbWorkflow(const ImGuiShowcaseInputs& inputs)
        {
            ImGui::SeparatorText("Fixed A/B evidence workflow");
            const ImGuiShowcaseActionAvailability availability =
                EvaluateFixedAbStartAvailability(inputs);
            ImGui::BeginDisabled(!availability.enabled);
            if (ImGui::Button("Start fixed A/B capture"))
            {
                static_cast<void>(inputs.controller->StartAbComparison(*inputs.fixedAbStart));
            }
            ImGui::EndDisabled();
            if (!availability.enabled)
            {
                ImGui::SameLine();
                TextDisabled(availability.reason);
            }

            if (inputs.controller == nullptr)
            {
                TextDisabled("A/B state is unavailable without the workflow controller");
                return;
            }

            const Demos::ShowcaseWorkflowState state =
                inputs.controller->AbWorkflowState();
            ImGui::Text("State: %.*s",
                static_cast<int>(WorkflowStateLabel(state).size()),
                WorkflowStateLabel(state).data());
            const Demos::ShowcaseWorkflowStatus& status =
                inputs.controller->AbWorkflowStatus();
            if (!status && !status.message.empty())
            {
                TextDisabled(status.message);
            }

            const Demos::ShowcaseCaptureWorkRequest* const pending =
                inputs.controller->PendingAbRequest();
            if (pending != nullptr)
            {
                ImGui::Text(
                    "Pending %.*s: workflow=%llu request=%llu config=%llu scene=%llu resource=%llu frame=%llu sample=%llu",
                    static_cast<int>(WorkflowStateLabel(state).size()),
                    WorkflowStateLabel(state).data(),
                    static_cast<unsigned long long>(pending->workflowIdentity),
                    static_cast<unsigned long long>(pending->requestIdentity),
                    static_cast<unsigned long long>(pending->configGeneration),
                    static_cast<unsigned long long>(pending->sceneGeneration),
                    static_cast<unsigned long long>(pending->resourceGeneration),
                    static_cast<unsigned long long>(pending->frameIndex),
                    static_cast<unsigned long long>(pending->sampleIndex));
                Text(pending->sceneStableId);
                Text(pending->variantStableId);
            }

            const Demos::ShowcaseAbManifest* const manifest =
                inputs.controller->CompletedAbManifest();
            if (manifest != nullptr)
            {
                ImGui::SeparatorText("Completed A/B manifest JSON");
                const std::string json = inputs.controller->CompletedAbManifestJson();
                Text(json);
            }

            if (state != Demos::ShowcaseWorkflowState::Idle)
            {
                if (ImGui::Button("Cancel / reset A/B workflow"))
                {
                    inputs.controller->CancelAbComparison();
                }
            }
        }

        void DrawCapturePanel(
            bool& open,
            ImGuiShowcaseSelectionState& selection,
            const ImGuiShowcaseInputs& inputs)
        {
            if (!open)
            {
                return;
            }
            if (!ImGui::Begin("Capture and QA", &open))
            {
                ImGui::End();
                return;
            }
            DrawRequestButton(
                "Capture EXR + PNG + metadata",
                SemanticAction::RequestCapture,
                *inputs.showcase,
                *inputs.actionQueue);
            DrawRequestButton(
                "Run short benchmark",
                SemanticAction::RequestBenchmark,
                *inputs.showcase,
                *inputs.actionQueue);
            DrawRequestButton(
                "Compare reference",
                SemanticAction::RequestReferenceComparison,
                *inputs.showcase,
                *inputs.actionQueue);
            DrawFixedAbWorkflow(inputs);

            ImGui::SeparatorText("Benchmark sequence");
            if (inputs.benchmark == nullptr)
            {
                TextDisabled("benchmark provider is unavailable");
            }
            else
            {
                Text(BenchmarkPhaseLabel(inputs.benchmark->Phase()));
                ImGui::Text("repeat=%u, warm-up=%u/%u, measurement=%u/%u, retained=%llu",
                    inputs.benchmark->CurrentRepeatIndex(),
                    inputs.benchmark->WarmupFramesAccepted(),
                    inputs.benchmark->Plan().warmupFrameCount,
                    inputs.benchmark->MeasurementFramesAccepted(),
                    inputs.benchmark->Plan().measurementFrameCount,
                    static_cast<unsigned long long>(inputs.benchmark->Samples().size()));
                if (!inputs.benchmark->LastStatus())
                {
                    TextDisabled(inputs.benchmark->LastStatus().message);
                }
            }

            ImGui::SeparatorText("Many Lights comparisons");
            if (inputs.program == nullptr || inputs.program->manyLightsComparisons.empty())
            {
                TextDisabled("Many Lights orchestration provider is unavailable");
            }
            else
            {
                selection.selectedManyLightsPreset = std::min(
                    selection.selectedManyLightsPreset,
                    inputs.program->manyLightsComparisons.size() - 1u);
                for (std::size_t index = 0;
                    index < inputs.program->manyLightsComparisons.size();
                    ++index)
                {
                    const Demos::ManyLightsComparisonRequest& request =
                        inputs.program->manyLightsComparisons[index];
                    ImGui::PushID(static_cast<int>(index));
                    const std::string label(request.preset.label);
                    const bool selected = selection.selectedManyLightsPreset == index;
                    if (ImGui::RadioButton(label.c_str(), selected))
                    {
                        selection.selectedManyLightsPreset = index;
                    }
                    ImGui::PopID();
                }
                const Demos::ManyLightsComparisonRequest& request =
                    inputs.program->manyLightsComparisons[selection.selectedManyLightsPreset];
                if (!request.availability.IsAvailable())
                {
                    TextDisabled(request.availability.reason);
                }
                for (const Demos::ManyLightsComparisonLeg& leg : request.legs)
                {
                    ImGui::BulletText("%.*s: %.*s",
                        static_cast<int>(leg.label.size()), leg.label.data(),
                        static_cast<int>(CompletionLabel(leg.completion).size()),
                        CompletionLabel(leg.completion).data());
                    if (!leg.reason.empty())
                    {
                        ImGui::SameLine();
                        TextDisabled(leg.reason);
                    }
                }
            }

            ImGui::SeparatorText("Asset-license publication gate");
            if (inputs.program == nullptr)
            {
                TextDisabled("asset-license registry is unavailable");
            }
            else if (inputs.program->licenseGate.CanPublish())
            {
                Text("ready");
            }
            else
            {
                TextDisabled(inputs.program->licenseGate.availability.reason);
                for (const Demos::AssetLicenseIssue& issue : inputs.program->licenseGate.issues)
                {
                    ImGui::BulletText("%.*s: %.*s",
                        static_cast<int>(issue.assetToken.size()), issue.assetToken.data(),
                        static_cast<int>(issue.reason.size()), issue.reason.data());
                }
            }

            ImGui::SeparatorText("Final video shot list");
            if (inputs.videoShots == nullptr)
            {
                TextDisabled("video shot-list state is unavailable");
            }
            else
            {
                for (const Demos::FinalVideoShot& shot : inputs.videoShots->Shots())
                {
                    ImGui::BulletText("%.*s: %.*s",
                        static_cast<int>(shot.descriptor.label.size()), shot.descriptor.label.data(),
                        static_cast<int>(VideoShotStateLabel(shot.state).size()),
                        VideoShotStateLabel(shot.state).data());
                    if (!shot.reason.empty())
                    {
                        ImGui::SameLine();
                        TextDisabled(shot.reason);
                    }
                }
            }
            ImGui::End();
        }

        void DrawHelpPanel(
            bool& open,
            const ShowcaseViewModel& model,
            const Demos::ShowcaseProgramModel* program)
        {
            if (!open)
            {
                return;
            }
            if (!ImGui::Begin("Showcase Help", &open))
            {
                ImGui::End();
                return;
            }
            DrawCurrentProgramExplanation(model, program);
            if (ImGui::BeginTable(
                "help-actions",
                4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Chord");
                ImGui::TableSetupColumn("Action");
                ImGui::TableSetupColumn("Owner");
                ImGui::TableSetupColumn("Availability");
                ImGui::TableHeadersRow();
                for (const HelpEntryViewModel& entry : model.help)
                {
                    if (entry.binding == nullptr)
                    {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); Text(entry.binding->chordLabel);
                    ImGui::TableSetColumnIndex(1); Text(entry.binding->description);
                    ImGui::TableSetColumnIndex(2); Text(entry.owner);
                    ImGui::TableSetColumnIndex(3);
                    if (entry.enabled)
                    {
                        Text("available");
                    }
                    else
                    {
                        TextDisabled(entry.reason);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        void DrawPanelMenu(ShowcasePanelState& panels)
        {
            if (!ImGui::BeginMainMenuBar())
            {
                return;
            }
            if (ImGui::BeginMenu("Showcase"))
            {
                ImGui::MenuItem("Algorithm", nullptr, &panels.algorithm);
                ImGui::MenuItem("Scene", nullptr, &panels.scene);
                ImGui::MenuItem("Debug", nullptr, &panels.debug);
                ImGui::MenuItem("Profiler", nullptr, &panels.profiler);
                ImGui::MenuItem("Capture and QA", nullptr, &panels.capture);
                ImGui::MenuItem("Help", nullptr, &panels.help);
                ImGui::Separator();
                ImGui::MenuItem("Debug legend", nullptr, &panels.debugLegend);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    bool QueueShowcaseSceneSelection(ActionQueue& queue, std::size_t sceneIndex)
    {
        constexpr std::size_t sceneCount = 10u;
        if (sceneIndex >= sceneCount)
        {
            return false;
        }
        const auto actionValue = static_cast<std::uint16_t>(SemanticAction::SelectScene0)
            + static_cast<std::uint16_t>(sceneIndex);
        queue.Push(static_cast<SemanticAction>(actionValue));
        return true;
    }

    bool QueueShowcaseDimensionCycle(
        ActionQueue& queue,
        std::string_view dimensionId,
        bool forward)
    {
        SemanticAction action = SemanticAction::ToggleHelp;
        if (dimensionId == "backend")
        {
            action = forward
                ? SemanticAction::CycleBackendForward
                : SemanticAction::CycleBackendBackward;
        }
        else if (dimensionId == "integrator")
        {
            action = forward
                ? SemanticAction::CycleIntegratorForward
                : SemanticAction::CycleIntegratorBackward;
        }
        else if (dimensionId == "direct-lighting" || dimensionId == "light-proposal")
        {
            action = forward
                ? SemanticAction::CycleLightSamplingForward
                : SemanticAction::CycleLightSamplingBackward;
        }
        else if (dimensionId == "reconstruction")
        {
            action = forward
                ? SemanticAction::CycleReconstructionForward
                : SemanticAction::CycleReconstructionBackward;
        }
        else if (dimensionId == "debug-view")
        {
            action = forward
                ? SemanticAction::CycleDebugViewForward
                : SemanticAction::CycleDebugViewBackward;
        }
        else
        {
            return false;
        }
        queue.Push(action);
        return true;
    }

    ImGuiShowcaseActionAvailability EvaluateFixedAbStartAvailability(
        const ImGuiShowcaseInputs& inputs)
    {
        ImGuiShowcaseActionAvailability availability;
        availability.reason = FixedAbStartDisabledReason(inputs);
        availability.enabled = availability.reason.empty();
        return availability;
    }

    ImGuiShowcaseDrawStatus DrawImGuiShowcasePanels(
        ShowcasePanelState& panels,
        ImGuiShowcaseSelectionState& selection,
        const ImGuiShowcaseInputs& inputs)
    {
        if (inputs.showcase == nullptr || inputs.actionQueue == nullptr)
        {
            return ImGuiShowcaseDrawStatus::InvalidInput;
        }
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return ImGuiShowcaseDrawStatus::NoImGuiContext;
        }

        DrawPanelMenu(panels);
        DrawStatusOverlay(*inputs.showcase);
        DrawAlgorithmPanel(
            panels.algorithm,
            *inputs.showcase,
            *inputs.actionQueue,
            inputs.program);
        DrawScenePanel(
            panels.scene,
            *inputs.showcase,
            *inputs.actionQueue,
            inputs.program);
        DrawDebugPanel(
            panels.debug,
            panels.debugLegend,
            selection,
            inputs.debugProfiler,
            inputs.textureResolver);
        DrawProfilerPanel(panels.profiler, inputs.debugProfiler);
        DrawCapturePanel(panels.capture, selection, inputs);
        DrawHelpPanel(panels.help, *inputs.showcase, inputs.program);
        return ImGuiShowcaseDrawStatus::Drawn;
    }
}
