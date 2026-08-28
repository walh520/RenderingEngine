# L10 proposal: compose the Wave 1 showcase harness

- Status: Proposed for L0 integration
- Lane: L10
- Date: 2026-08-24
- Contracts: `abi-v0` (numeric 1), `runtime-config-v0` (application 0),
  `cli-v0`, `artifact-layout-v0`, `build-workflow-v0`
- Contract change requested: none

## Purpose

L10 now provides a lane-local, platform-independent ActionMap/control harness,
renderless showcase view model, and capture-bundle writer. The production
application still rejects `--capture` before platform creation, and the current
platform seam does not yet publish the GLFW press/axis events required by the
ActionMap. Those shared composition changes remain L0/L1 work.

This proposal describes the minimum integration seam. It does not authorize L10
to edit `Application`, `CapabilityTable`, the central solution, renderer source,
or the platform interface on its feature branch.

## Proposed composition

1. L1 translates physical GLFW press/held/axis input into L10 `ActionEvent`
   values. L1 retains raw callbacks and physical input state; L10 retains the
   semantic binding catalog and help labels.
2. The application composition owner keeps the one live `RuntimeConfig`. At one
   fixed point at frame start it calls `ApplyQueuedActions`. It forwards only
   committed reset bits and routed commands to their owning modules.
3. An ImGui consumer renders `BuildShowcaseViewModel(liveConfig)`. It must not
   keep a second mode tuple or enable an option that the view model marks
   unavailable.
4. A capture provider supplies real renderer output as linear RGBA32F plus an
   explicitly transformed RGBA8 preview. The application supplies truthful Git,
   build, device, scene, asset, shader, time, and requested/effective-config
   metadata, then calls `WriteCaptureBundle`.
5. L0 adds the non-empty L10 library/test projects to the central solution. The
   existing module-local projects remain the source lists; no L10 glob is added
   to shared MSBuild files.

## Capability publication rule

`--capture` may change from exit code 4 to supported only after the selected
complete tuple has both a real readback provider and the capture dispatcher.
Parsing the token, routing F4, constructing a test image, or linking the writer
is not sufficient. Unsupported tuples must still fail before platform or file
creation and must never fall back to another backend, integrator, scene, or
capture format.

The production path must pass the exact requested and effective
`RuntimeConfig` values to metadata. Any future normalization must remain
observable rather than being hidden as a capability fallback.

## Wave 1 integration acceptance

- GLFW discrete actions reach L10 only on press, never repeat; ImGui keyboard
  and mouse capture rules prevent input leakage.
- UI and CLI mutate the same live `RuntimeConfig` through the complete-tuple
  capability gate.
- Unsupported scene/mode selection leaves the previous tuple intact and shows
  the returned reason.
- A real baseline capture creates only
  `captures/image.exr`, `captures/preview.png`, and `metadata.json` under the
  frozen artifact layout; a run-ID collision refuses overwrite.
- `metadata.json` is published last and contains the required caller-supplied
  truth fields plus requested/effective configs.
- Debug and Release solution builds include the L10 library/tests; CPU tests and
  the existing renderer smoke suite pass.

## Explicitly deferred

- Wave 2 Debug/Profiler resource wiring and live timing data;
- automated benchmark warm-up/median/p95 orchestration;
- reference image comparison and split-screen rendering;
- Many Lights/ReSTIR UI, final portfolio reports, and video shot list;
- production headless rendering.
