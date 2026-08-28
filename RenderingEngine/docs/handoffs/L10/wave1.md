# L10 handoff: Wave 1 UI and capture harness skeleton

- Status: Lane-local Wave 1 acceptance passed; L0 composition pending
- Date: 2026-08-24
- Branch: `codex/rt-showcase-qa`
- Baseline: `10f791a` (`wave0: freeze Visual Studio ray tracing foundation`)
- Contracts: `abi-v0` (numeric wire version 1), `runtime-config-v0`
  (application version 0), `cli-v0`, `artifact-layout-v0`,
  `build-workflow-v0`
- Contract changes: none

This is the first L10 handoff. The previous factual handoff was
`docs/handoffs/L0/wave0.md`; no earlier `docs/handoffs/L10/` document existed.

## Implemented scope

### Semantic control and no-render UI model

- A platform-independent catalog covers the section-10 camera, application,
  algorithm, debug/capture, and scene chords without including GLFW,
  `IPlatformHost`, or ImGui headers.
- `ActionQueue` preserves FIFO order. `ApplyQueuedActions` is the only API that
  mutates the supplied live `RuntimeConfig`, and does so only when the caller
  reaches the fixed frame-start application point.
- Every mutation uses candidate copy -> complete-tuple
  `CapabilityTable::Evaluate` -> atomic commit. Rejection retains every field of
  the previous live config and returns the shared capability reason.
- Backend, integrator, light-sampling, reconstruction, and debug cycles visit
  only currently built and complete-tuple-valid values. Scene number actions do
  not repair unsupported combinations.
- The `L` catalog contains explicit estimator/proposal pairs. These remain two
  independent `RuntimeConfig` fields; no combined project-wide mode enum was
  introduced.
- Reset results distinguish A/T/Q/P/AS as requests. This module owns no
  accumulation, temporal, reservoir, profiler, or acceleration-structure
  resource.
- The renderless view model exposes all seven mode dimensions, the complete
  tuple, ten scene labels, future option owners, disabled states/reasons, light
  presets, and help rows from the same ActionMap catalog.

### Capture bundle writer

- The writer accepts caller-provided linear RGBA32F pixels and a separately
  prepared RGBA8 preview. It performs no renderer readback, tone map, or
  synthetic image generation.
- The caller must provide commit/build/device/Vulkan/scene/asset/shader/time
  facts plus requested and effective `RuntimeConfig` values. The writer does not
  probe or invent those facts.
- Pinned TinyEXR 3.2.0 and stb 2024-07-29#1 write the frozen names
  `captures/image.exr`, `captures/preview.png`, and `metadata.json`.
- Existing run directories are collisions and are never overwritten. Image
  files are written first; a temporary metadata file is renamed to
  `metadata.json` as the final completion marker. A failure after this writer
  creates the run directory attempts to remove only that new run directory.
- Metadata serializes the complete requested/effective tuple with normative
  `cli-v0` short tokens, stable JSON names, and UTF-8 `/` paths. It rejects
  missing caller truth, invalid UTF-8, invalid `runtime-config-v0` tuples, and
  non-canonical `artifact-layout-v0` paths before I/O. Declared-but-unbuilt
  tuples remain recordable as `Unsupported` evidence.

### Lane-local build and integration proposal

- `RenderingEngine.Showcase.vcxproj` is a non-empty static-library project.
- `RenderingEngine.Showcase.Tests.vcxproj` is an executable test project that
  consumes the library and the frozen app contracts read-only.
- Both projects import the shared C++ policy read-only and explicitly consume
  the repository vcpkg manifest. An external `VcpkgRoot` remains overridable;
  Visual Studio's bundled vcpkg is a conditional local fallback, not a committed
  absolute path.
- `docs/proposals/L10/wave1-integration.md` gives L0 the minimum production
  composition and capability-publication gates without changing a shared file.

## Explicitly not implemented

- L0 has not added the L10 projects to `RenderingEngine.sln`.
- `RenderingEngine.exe --capture` remains the shared Wave 0 behavior: recognized
  but unsupported (exit 4 before platform or filesystem creation).
- L1 physical GLFW events and ImGui keyboard/mouse arbitration are not connected.
- No ImGui draw backend or Vulkan UI rendering was added; the deliverable is the
  Wave 1 renderless panel/help model.
- No renderer readback provider, production capture dispatcher, or production
  headless path was added.
- Wave 2 Debug/Profiler resource wiring and live counters/timings were not
  started.
- Benchmark warm-up/median/p95, reference comparison, split-screen rendering,
  Many Lights UI, final reports, and video work remain later waves.
- No scene registry, camera preset data, placeholder scene, or algorithm debug
  value was copied or fabricated.

## Modified files

```text
RenderingEngine/include/ui/ActionMap.hpp
RenderingEngine/include/ui/RuntimeConfigHarness.hpp
RenderingEngine/include/ui/ShowcaseViewModel.hpp
RenderingEngine/src/ui/ActionMap.cpp
RenderingEngine/src/ui/RuntimeConfigHarness.cpp
RenderingEngine/src/ui/ShowcaseViewModel.cpp
RenderingEngine/include/demos/CaptureBundleWriter.hpp
RenderingEngine/src/demos/CaptureBundleWriter.cpp
RenderingEngine/src/ui/RenderingEngine.Showcase.vcxproj
RenderingEngine/src/ui/RenderingEngine.Showcase.vcxproj.filters
RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj
RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj.filters
RenderingEngine/src/ui/tests/ShowcaseWave1Tests.cpp
RenderingEngine/docs/proposals/L10/wave1-integration.md
RenderingEngine/docs/handoffs/L10/wave1.md
```

No root build, central solution, shared MSBuild, dependency manifest, contract,
ADR, app/control-plane, platform, renderer, shader, scene, or other-lane source
file was modified.

## Evidence

### Static evidence: passed

- Source-path allowlist audit covered all 15 changed/untracked files and found
  no path outside
  `include/{ui,demos}`, `src/{ui,demos}`, and `docs/{proposals,handoffs}/L10`.
- Project and filters files parsed as XML.
- MSBuild evaluation resolved the lane manifest root to this worktree, enabled
  manifest mode for `x64-windows`, and used the Visual Studio bundled vcpkg only
  as the local fallback.
- The ActionMap contains separate F4/Alt+F4 and Wheel/Alt+Wheel chords, all ten
  scenes, forward/back algorithm cycles, and section-10 help labels.
- Capture code inspection confirms collision-before-create, caller-truth
  validation before I/O, canonical layout/run-ID enforcement,
  data-before-metadata ordering, and no deliberate I/O after the metadata
  completion marker is published.
- A direct trailing-whitespace scan over all 15 untracked lane files reported no
  issue. `git diff --check` also returned no output, but was not treated as
  evidence for those untracked files.

Static inspection does not prove GLFW routing, ImGui rendering, renderer
readback, Vulkan correctness, or a production CLI capture.

### Build evidence: passed lane-locally

Toolchain:

```text
MSBuild 18.9.1.35102
MSVC v145 / C++20 / permissive- / W4 / WX
Platform x64
```

Commands, run with `/m:1`, `BuildInParallel=false`, and
`UseMultiToolTask=false`:

```powershell
& <MSBuild.exe> RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> RenderingEngine/src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=x64
```

Results:

```text
Debug   passed, 0 warnings, 0 errors
Release passed, 0 warnings, 0 errors
```

The first manifest restore downloaded the exact versions frozen by the root
manifest. In this Codex process only, duplicate `Path`/`PATH` environment keys
had to be normalized before the final MSBuild launches; this was a child-process
environment repair, not a source or machine-policy change.

The central solution was not modified and therefore does not yet compose L10.
These results are lane-project evidence, not a full integrated solution gate.

### CPU/runtime evidence: passed for the lane harness

Both executables ran:

```powershell
bin/x64/Debug/RenderingEngine.Showcase.Tests.exe
bin/x64/Release/RenderingEngine.Showcase.Tests.exe
```

Both returned exit code 0 and printed:

```text
L10 Wave 1 ActionMap, RuntimeConfig harness, view-model, and capture checks passed.
EXR RGBA32F max_abs_error=0 (threshold 1e-6).
```

Executed checks include ActionMap chord resolution, FIFO/fixed-point mutation,
built-only cycles, complete-tuple rejection atomicity, reset masks, explicit
light estimator/proposal presets, future capability owner/reason presentation,
three-file capture output, canonical `cli-v0` metadata tokens and JSON escaping,
collision no-overwrite, missing/invalid metadata preflight, invalid image
preflight, non-canonical layout rejection, and invalid UTF-8 rejection.

This is a CPU test executable run. It is not production headless, interactive
application, GLFW, ImGui, GPU, or Vulkan runtime evidence.

### Numerical evidence: passed for capture encoding only

- A non-symmetric 2 x 2 RGBA32F fixture, including negative and HDR values,
  round-tripped through TinyEXR with measured maximum absolute error `0`
  (threshold `1e-6`).
- The decoded 2 x 2 PNG matched all 16 caller-provided RGBA8 bytes.

This proves the tested encoding/readback path only. It does not prove renderer
radiance, image convergence, RMSE/PSNR, scene correctness, or seeded render
repeatability.

### GPU runtime and Vulkan validation: not run

L10 Wave 1 owns no Vulkan execution path, renderer readback, or production UI
consumer. No GPU runtime or Vulkan validation claim is made.

### Visual evidence: not run

The 2 x 2 data fixture is a numerical encoding test, not visual acceptance. No
interactive panel, rendered scene, screenshot review, or human visual comparison
was performed.

### Performance evidence: not applicable / not run

No benchmark or timing target belongs to the Wave 1 skeleton. No performance
claim is made.

## Known risks and integration gates

- Until L0 accepts the integration proposal, production CLI/F4 capture remains
  unavailable even though the lane-local writer is executable and tested.
- Metadata correctness beyond structural validation depends on the future
  application/readback provider supplying truthful values from the same run.
- The deterministic JSON serializer's escaping and UTF-8 rejection were tested,
  but the emitted document was not parsed by an independent JSON implementation
  in this milestone. Production schema/parser validation remains an integration
  hardening step.
- The cleanup code for a failure after partial image output is present, but this
  milestone did not inject a filesystem failure after directory creation; that
  rollback branch has static rather than executed fault-injection evidence.
- L1 must translate held/press/axis input and enforce ImGui capture arbitration;
  current tests start at semantic `ActionEvent`.
- Reset masks are requests only until L0 and the resource-owning lines consume
  them at the fixed frame point.
- A clean machine's first lane build may restore every pinned root-manifest
  dependency, not only stb/TinyEXR.

Rollback is module-local: omit/revert the L10-owned files and central solution
composition. No shared data migration, ABI change, or artifact conversion is
required.
