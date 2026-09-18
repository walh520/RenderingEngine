# L0 handoff: Wave 2 production GLFW runtime closure

## Scope and outcome

- Scope: Wave 2 production composition only. Wave 3 LBVH / RT Pipeline /
  Wavefront / SVGF and Wave 4 ReSTIR are not enabled.
- One GLFW window now owns scene, backend, integrator and light-sampling
  switching through the shared `RuntimeConfig` action queue.
- The first five experiment scenes are consumed as canonical scene data by one
  production path: L3 binned SAH -> L4 flattened records or L5 BLAS/TLAS Ray
  Query -> L6 Megakernel -> the existing HDR present/capture image.
- L10 consumes resolved GPU timestamps and readback counters with live-runtime
  provenance and a config/scene/resource generation tuple.

## Runtime controls

- `0` through `4`: change only the scene and its complete fixed camera
  (pose plus preset FOV). An incompatible current algorithm tuple is rejected
  without changing any selection. An explicit startup `--fov` remains the
  startup override until a scene key restores that scene's preset lens.
- `B` / `Shift+B`: cycle Legacy, Flattened SAH and Vulkan Ray Query through
  complete supported tuples.
- `I` / `Shift+I`: cycle legacy integrators and the Wave 2 Megakernel through
  complete supported tuples.
- `L` / `Shift+L`: cycle BSDF-only, NEE and MIS presets. In scene 4 the
  cycle also exposes `MIS + Environment Importance`, which atomically selects
  the white-furnace variant, its fixed camera and its environment resources.
- `R`: reset progressive accumulation and reserved histories.
- `F1`: print the current complete configuration and UTF-8 Chinese key help.
- `F4`: capture the currently rendered linear EXR, PNG preview and metadata.

## Static evidence

- The root Vulkan project composes `Wave2Runtime.cpp`, L5 hardware runtime
  sources, `MegakernelBridge.cpp`, the Software GPU project and four production
  SPIR-V outputs.
- The production shader items define only the Software/Ray-Query traversal
  selector for the two Megakernel variants; no fixture-only material, light, or
  raw-output macro removes a Wave 2 algorithm or auxiliary radiance signal.
- Both production Megakernel variants retain full BSDF/light code and write
  raw, camera-emission, direct-diffuse, direct-specular, indirect-diffuse and
  indirect-specular images. The present/capture path reads the raw image; all
  six are also published into the L10 debug-resource catalog with stable IDs,
  generation-scoped opaque tokens, RGBA32F extent/ownership metadata and a
  compute-to-transfer availability barrier for provider-owned readback.
- Scene ABI v0 remains unchanged: smooth-dielectric records route to the L6
  smooth-glass path, while metallic-roughness records with non-zero
  transmission route to L6's private rough-dielectric model. Scene 4 contains
  a roughness-0.18 transmissive sphere, so the production rough-dielectric
  evaluate/sample/PDF path is reachable; scenes 0 and 2 retain smooth glass.
- Scene 4's environment preset now reaches the unrestricted production
  Megakernel: the runtime activates `white-furnace`, enables only its canonical
  environment light, builds the two-dimensional luminance/solid-angle alias
  distribution, uploads the RGBA32F environment image, binds a longitude-wrap
  sampler and publishes environment dimensions/light index in frame constants.
  Returning to a finite-light preset rebuilds the default `material-grid`
  variant and removes the environment image. Capture metadata hashes the
  enabled environment payload separately from the canonical scene payload.
- L4 production traversal publishes node, triangle, invalid, leaf and occupancy
  counters from per-frame buffers; L6 publishes all 26 Megakernel counters.
- L5 builds one BLAS per canonical instance and one multi-instance TLAS, so the
  runtime no longer has the previous single-instance structural blocker.
- Invisible canonical instances use a zero TLAS mask, and both traversal
  backends apply `CastsShadow` only to shadow queries. The production ray total
  is `path + shadow`; the depth-zero camera segment is not counted twice.
- Scene/backend/integrator changes are capability-checked before commit, wait
  for outstanding GPU work, discard stale telemetry, rebuild resources where
  required and reset generation-scoped state.
- The capability table exposes exactly scenes 0-4, Flattened SAH/Ray Query,
  Megakernel, BSDF-only/NEE/MIS, compatible light proposals and Raw output for
  the Wave 2 domain. Scene digits mutate only scene/camera; `B`, `I`, and `L`
  operate on complete supported tuples. LBVH, RT Pipeline, Wavefront, SVGF and
  ReSTIR remain declared but fail closed as later-wave features.
- Windows console output is UTF-8 when attached to a console; GLFW titles and
  runtime status strings are UTF-8 Chinese.
- A targeted Wave 2 source scan found no `TODO`, placeholder, stub, or
  unimplemented marker in the attached renderer, traversal, hardware RT,
  Megakernel, GLFW, telemetry, or experiment-scene paths. The repository README
  now points to this handoff instead of describing the superseded pre-GLFW
  state.
- `git diff --check` reported no whitespace errors. Existing line-ending
  conversion warnings belong to the pre-existing mixed working tree.

## Build evidence

Not executed. The user explicitly requested no compilation. Project and shader
build descriptions are static evidence only.

## Runtime evidence

Not executed. No Vulkan device creation, GLFW input session, backend switch,
dispatch, capture or telemetry readback was run in this handoff.

## Numerical evidence

Not executed in this handoff. Earlier lane-local gates are not promoted into
evidence for the newly attached production runtime.

## Visual evidence

Not executed. No screenshot, EXR/PNG comparison or human visual acceptance is
claimed.

## Deferred data gate

The accepted Sponza payload, license/hash record and texture/normal/alpha data
remain an external content gate under `docs/contracts/sponza-asset-gate-v1.md`.
The runtime-side multi-instance BLAS/TLAS and alpha-aware traversal interfaces
are present, but no Sponza content or Sponza result is fabricated here. The
current delivery target remains the requested first five procedural scenes.

## Delivery boundary

- No commit was created.
- No build, test executable or renderer run was started.
- No unrelated worktree change was cleaned or reverted.
- The five-scene Wave 2 production attachment, including scene 4's rough
  dielectric and environment-importance white-furnace paths, is statically
  complete. A
  scene-resource allocation/AS failure remains fail-fast rather than
  recoverable transaction rollback. Build, runtime, numerical and visual
  acceptance remain for the user's validation session.
