# L10 handoff: Wave 2 Debug/Profiler wiring

> Historical lane-gate snapshot. Live production telemetry attachment is
> superseded by `docs/handoffs/L0/wave2-production-runtime.md`.

- Date: 2026-08-30
- Scope: Wave 2 (`Debug / Profiler wiring`)
- Contract: RuntimeConfig v0, integration-status v1, ABI v1 identities
- Status: typed adapter and Debug tests passed; live L4/L5/L6 producer attachment and visual UI acceptance remain open

## 1. Implemented scope

- Adds typed L4 flattened-traversal, L5 Ray Query, and L6 Megakernel telemetry
  frame records with producer ID, generation tuple, provenance, availability,
  counters, resources, and timestamp observations.
- Rejects invalid provider/provenance and generation regression at publication;
  malformed per-measurement enum/domain/value state is retained long enough to
  publish a fail-closed `Invalid` row with no fabricated numeric value.
- Preserves `Unavailable`, `Pending`, `Fresh`, `Stale`, and `Invalid`; an
  invalid member dominates a mixed frame and cannot be advertised as fresh.
- Converts accepted frames to the existing provider-driven Debug/Profiler
  snapshot without fabricating missing metrics or replacing unavailable data
  with synthetic values.
- Exposes reasons/provenance through the view model and existing Debug panel
  model.
- Reasserts the Showcase test executable's direct `miniz.dll` dependency after
  `Build`, because multiple manifest consumers share the central output
  directory; a missing pinned runtime now fails the build instead of producing
  a successful but unrunnable test binary.

## 2. Explicitly unimplemented scope

- L4/L5/L6 runtime code does not yet publish live frames into this adapter.
- No ImGui Vulkan draw/render acceptance was performed for these values.
- Benchmark orchestration, 120/1000/3 aggregation, memory acquisition,
  production scene/backend switching smoke, and visual screenshot review are
  not complete.

## 3. Modified files

```text
include/ui/Wave2TelemetryAdapter.hpp
src/ui/Wave2TelemetryAdapter.cpp
src/ui/ShowcaseViewModel.cpp
src/ui/RenderingEngine.Showcase.vcxproj
src/ui/RenderingEngine.Showcase.vcxproj.filters
src/ui/tests/Wave2TelemetryAdapterTests.cpp
src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj
src/ui/tests/RenderingEngine.Showcase.Tests.vcxproj.filters
src/ui/tests/ShowcaseWave1Tests.cpp
docs/handoffs/L10/wave2.md
```

## 4. Contract version

No RuntimeConfig numeric values were changed. The adapter consumes stable
Wave-2 lane identities and keeps all telemetry records L10-private. It does not
publish ABI-v2 or claim that central-build providers are production-runtime.

## 5. Build evidence

Debug `x64` single-node central solution and
`RenderingEngine.Showcase.Tests.vcxproj` passed with MSBuild 18.9.1 / MSVC
v145 / C++20 / warning-as-error. Pinned vcpkg
dependencies were already restored; manifest installation was disabled during
the evidence build. A standalone Debug rebuild recreated `miniz.dll` from the
pinned triplet and the rebuilt test executable ran successfully. No Release
build was performed.

## 6. Unit/statistical test evidence

`RenderingEngine.Showcase.Tests.exe` returned success for capture preview,
GLFW action adapter, provider-driven Debug/Profiler model, typed Wave 2
telemetry, showcase evidence/program/report/workflow tests, and EXR RGBA32F
round-trip (`max_abs_error=0`, threshold `1e-6`). Mixed Fresh/Invalid,
provenance, missing-value, generation-regression, and stale/pending cases are
covered. No stochastic-algorithm statistics are owned by L10.

## 7. Vulkan runtime/validation evidence

Not run for the Wave 2 telemetry adapter. It is a CPU-side consumer and has no
live Vulkan producer in production. The separate shared Wave 2 gate's Vulkan
success cannot be promoted into L10 runtime wiring evidence.

## 8. Visual/capture evidence

Not run. Encoding fixtures and renderless panel-model tests are not human UI or
scene visual acceptance.

## 9. Performance data and test conditions

Not run. The adapter validates timestamp observations but did not collect or
aggregate an accepted benchmark sequence.

## 10. Known risks and rollback

- Until producers attach at a well-defined completed-frame boundary, the
  production panel correctly remains unavailable.
- A producer that reuses generation IDs or mixes timestamp domains will be
  rejected; adapter reasons must stay visible to diagnose that integration.
- The deterministic runtime-copy target depends on the already restored pinned
  vcpkg triplet and fails closed if its exact Debug runtime is absent; it does
  not download dependencies.
- Multi-node solution builds in the current host return exit code 1 without an
  MSBuild diagnostic, while the same project set passes single-node and each
  dependency passes independently; parallel-build acceptance remains open.
- Roll back the adapter header/source, project entries, tests, and view-model
  consumption. This does not change renderer or traversal algorithms.
