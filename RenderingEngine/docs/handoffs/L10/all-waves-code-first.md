# L10 handoff: all waves code-first showcase and QA surface

- Status: lane-local all-wave code acceptance passed; production composition
  and renderer-data acceptance deferred
- Date: 2026-08-27
- Branch: `codex/rt-showcase-all`
- Baseline: `47fa2dc` (`chore: migrate renderer workspace to D drive`)
- Worktree: `C:\Users\nitong\source\repos\RenderingEngine\showcase-qa-all`
- Contract handling: the user explicitly waived contract gates for this
  code-first milestone. Shared contracts, ADRs, root solution/build files, and
  other lanes remained read-only; this handoff makes no contract-acceptance or
  integrated-release claim.

## Delivered scope

### Wave 1: semantic control and capture foundation

- A platform-independent ActionMap covers the section-10 semantic input/help
  surface, including distinct F4/Alt+F4 and Wheel/Alt+Wheel chords.
- RuntimeConfig actions use one FIFO fixed-frame mutation path, validate the
  complete tuple atomically, retain the effective tuple per routed request,
  and expose only accepted reset masks.
- The capture transaction writes caller-supplied RGBA32F EXR, RGBA8 PNG,
  metadata, and optional evidence files without overwriting an existing run.
  Metadata is committed last and failures roll back only a newly created run.
- Complete capture metadata requires Fresh named evidence, matching scene and
  generation identities, an image extent matching effective RuntimeConfig,
  hashes, requested/effective capability facts, and exact produced paths.

### Wave 2: Debug, Profiler, status, and ImGui

- Provider-owned counters, timings, debug resources, legends, and opaque ImGui
  texture tokens carry provenance plus config/scene/resource generation.
- Stale, future, regressive, mixed-provenance, and invalid snapshots fail
  closed. Runtime-only Frame/SPP/GPU values disappear when Fresh is revoked;
  config-owned resolution/seed/bounce remain truthful config facts.
- ImGui implements Algorithm, Scene, Debug, Profiler, Capture/QA, and Help
  panels plus an always-visible status strip. F1/F2 cards include method
  summaries, known limitations, completion state, and provider reasons.
- L10 owns no Vulkan image, descriptor, synchronization, or backend lifetime;
  the texture seam is an opaque provider token resolver.

### Wave 3: comparison, reference, and benchmark orchestration

- A controller-owned one-click A/B workflow freezes camera, base seed,
  animation origin, frame/sample coordinate, config generation, and
  scene/resource generations. Old-generation submissions cannot complete a
  pair.
- The completed A/B manifest retains both provider artifact identities and is
  persisted as `reports/ab-manifest.json` by the showcase bundle transaction.
- Linear RGBA comparison reports RMSE, PSNR, and maximum absolute error.
  Candidate/reference records must share scene, camera, seed, extent, and all
  generations while retaining independent frame/sample/SPP identities.
- The provider-driven benchmark state machine supports warm-up, measurement,
  repeats, exact metric sets, median, nearest-rank p95, stable CSV, and explicit
  CPU wall-clock/GPU timestamp/provider-counter measurement methods. The
  canonical portfolio preset is 120 warm-up frames, 1000 measured frames, and
  three repeats.

### Wave 4: Many Lights

- 100, 1,000, and 10,000-light plans expose exactly four legs: uniform
  one-light, power-weighted one-light, ReSTIR DI, and high-SPP CPU reference.
- Equal-realtime-budget identity is explicit for the first three legs; the
  reference has a separate high-SPP budget identity and requires both CPU
  reference and high-SPP providers.
- ReSTIR retains separate biased and unbiased validation modes rather than
  collapsing them into one label.

### Wave 5: portfolio closure code

- Provider-driven scene registry cards retain stable scene/camera IDs without
  copying L2 scene or camera payloads.
- Algorithm completion claims require retained non-synthetic live evidence for
  runtime-validated or visual-accepted states.
- Asset-license publication gates, capture shot plans, final-video shot state,
  evidence-bound approval/retake/invalidation, deterministic CSV, reference
  JSON, and a six-boundary Markdown report are implemented.
- Invalidating an approved video shot clears its retained capture identity;
  synthetic fixtures cannot be serialized as live runtime or visual evidence.

## Evidence

### Static evidence: passed

- The final source allowlist contains 38 untracked L10-owned files and zero
  tracked/shared diffs. Every file is under `include/{ui,demos}`,
  `src/{ui,demos}`, or `docs/{proposals,handoffs}/L10`.
- Both projects and both `.filters` files parse as XML.
- The L10 scan found zero trailing-whitespace hits, unfinished markers,
  native `Vk*` types/Vulkan includes, and generated `imgui.ini` files.
- The projects import the shared C++ policy read-only: C++20, conformance mode,
  warning level 4, and warnings as errors.
- Artifact paths reject traversal, ADS/device names, control characters,
  non-normalized forms, ASCII case aliases, and non-ASCII names before I/O.

### Build evidence: passed

Both configurations were rebuilt with the lane test project, one MSBuild worker,
the pinned installed dependency tree, and manifest installation disabled:

```powershell
MSBuild.exe RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj `
  /t:Rebuild /p:Configuration=Debug /p:Platform=x64 `
  /p:VcpkgRoot="C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg" `
  /p:VcpkgManifestInstall=false /m:1 /p:BuildInParallel=false `
  /p:UseMultiToolTask=false /v:minimal

MSBuild.exe RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj `
  /t:Rebuild /p:Configuration=Release /p:Platform=x64 `
  /p:VcpkgRoot="C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg" `
  /p:VcpkgManifestInstall=false /m:1 /p:BuildInParallel=false `
  /p:UseMultiToolTask=false /v:minimal
```

Debug and Release both completed with exit code 0. The central solution was not
modified, so this is lane-project build evidence rather than a full solution
integration gate.

### Runtime evidence: passed for the lane-local headless harness

```powershell
bin/x64/Debug/RenderingEngine.Showcase.Tests.exe
bin/x64/Release/RenderingEngine.Showcase.Tests.exe
```

Both returned exit code 0 and reported:

```text
L10 provider-driven debug/profiler model checks passed.
L10 showcase evidence tests passed.
L10 ShowcaseProgram orchestration tests passed.
L10 ShowcaseReport serialization tests passed.
L10 ShowcaseWorkflow orchestration tests passed.
L10 all-wave headless showcase checks passed.
EXR RGBA32F max_abs_error=0 (threshold 1e-6).
```

The ImGui tests create a real Dear ImGui context, draw the panels, and verify
generated draw data with persistence disabled. This proves CPU-side panel
construction only; no GLFW/Vulkan backend or interactive renderer was run.

### Numerical evidence: passed only for deterministic CPU fixtures

- A 64 x 64 RGBA32F pattern round-tripped through TinyEXR with maximum absolute
  error 0 against the `1e-6` threshold.
- The RGBA8 PNG round-trip preserved every caller-provided byte.
- Tests cover deterministic image-comparison math, high-SPP identity separation,
  benchmark sample sets/statistics/CSV, report serialization, and evidence
  bundle transactions.

These are encoder, math, and orchestration fixtures. They are not renderer
radiance, convergence, seeded scene repeatability, or production reference
evidence.

### Visual evidence: not run

No rendered scene, screenshot review, split-screen inspection, debug texture,
or human visual acceptance was performed. ImGui draw-data generation is not a
visual-quality result.

### Performance evidence: not run

The benchmark sequence, canonical cadence, measurement-method validation,
median, and p95 mechanics are implemented and tested with labelled synthetic
values. No GPU timestamp, renderer throughput, CPU/GPU comparison, or target
performance result is claimed.

## Deferred production gates

- L0/L1 have not composed the L10 projects into the central solution,
  production CLI, GLFW input path, ImGui renderer backend, or application loop.
- L2/L3/L4/L5/L6/L7/L8/L9 have not supplied real scene registry records,
  camera presets, readback, references, debug resources, timings, counters,
  algorithm completion evidence, or Many Lights captures to this worktree.
- Vulkan/GPU runtime and validation layers were not run by L10.
- Asset licenses, final captures, human visual approvals, and the encoded final
  portfolio video remain data/production tasks after provider integration.
- Shared CapabilityTable entries remain authoritative and unavailable features
  stay disabled with owner/reason; compiling L10 orchestration does not publish
  another lane's capability.

## Files

```text
RenderingEngine/include/ui/{ActionMap,DebugProfilerModel,ImGuiShowcasePanels,RuntimeConfigHarness,ShowcaseController,ShowcaseViewModel}.hpp
RenderingEngine/include/demos/{CaptureBundleWriter,ShowcaseEvidence,ShowcaseProgram,ShowcaseReport,ShowcaseWorkflow}.hpp
RenderingEngine/src/ui/{ActionMap,DebugProfilerModel,ImGuiShowcasePanels,RuntimeConfigHarness,ShowcaseController,ShowcaseViewModel}.cpp
RenderingEngine/src/demos/{CaptureBundleWriter,ShowcaseEvidence,ShowcaseProgram,ShowcaseReport,ShowcaseWorkflow}.cpp
RenderingEngine/src/ui/RenderingEngine.Showcase.vcxproj
RenderingEngine/src/ui/RenderingEngine.Showcase.vcxproj.filters
RenderingEngine/src/ui/tests/{DebugProfilerModelTests,ImGuiShowcasePanelsTests,ShowcaseControllerTests,ShowcaseEvidenceTests,ShowcaseProgramTests,ShowcaseReportTests,ShowcaseWave1Tests,ShowcaseWorkflowTests}.cpp
RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj
RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj.filters
RenderingEngine/docs/proposals/L10/{wave1-integration,all-waves-provider-integration}.md
RenderingEngine/docs/handoffs/L10/{wave1,all-waves-code-first}.md
```

Rollback is module-local: omit or revert these L10-owned files and any future
central composition. No shared ABI migration, root build edit, or other-lane
cleanup is required.
