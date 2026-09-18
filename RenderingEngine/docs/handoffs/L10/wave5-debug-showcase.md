# Wave 5 Debug showcase candidate

- Date: 2026-09-02
- Branch: `codex/rt-integration-all`
- Status: Debug showcase candidate; not a Release 5 acceptance claim
- Runtime: production GLFW/Vulkan loop on NVIDIA GeForce RTX 4070 Laptop GPU

## Delivered result

Wave 5 now has one provider-driven Debug surface attached to the live Wave 2
renderer. The always-visible status strip and the Algorithm, Scene, Debug,
Profiler, Capture/QA, and Help panels consume the same requested/effective
`RuntimeConfig`, capability table, action queue, generation identities, live GPU
timestamps, debug resources, and capture provider used by the renderer. There is
no second UI-only algorithm state.

The connected production domain is deliberately exact:

- scenes `0` through `5` and `7` through `9`: Baseline, Intersection/BVH,
  Whitted Optics, Cornell, GGX/MIS, Environment Sampling Dome, Backend Parity,
  Temporal Stability Corridor, and the 100-light Many Lights arena;
- scene `6` remains fail-closed because the pinned, licensed, texture-capable
  Sponza asset/provider is not present;
- traversal: CPU-built GPU Flattened SAH and Vulkan Ray Query;
- integration: GPU Megakernel with BSDF-only, NEE, and MIS;
- light proposals: uniform and power-weighted, plus the scene-4 environment
  proposal where the capability table permits it;
- output: progressive Raw and five selected debug AOVs: CameraEmission,
  DirectDiffuse, DirectSpecular, IndirectDiffuse, and IndirectSpecular.

Production Raw is not a monolithic driver-JIT workaround. Every Wave 2 scene
uses a finite `Seed -> (Trace -> barrier -> Shade) * bounce -> Resolve` schedule.
The same finite continuation schedule is used for the two indirect AOVs. It
preserves full multi-bounce emission, direct lighting, BSDF sampling, MIS,
transmission attenuation, roulette, and one online-mean publication per sample.
This removed the verified device-loss boundary without truncating Raw at the
primary surface.

Only the selected AOV is evaluated and published. Its continuation state is
seven `float4` values (112 bytes per pixel per frame slot); the other four AOV
images are provider-owned scratch until Resolve. Changing the selection resets
Raw and the selected signal to sample zero before recomputation. At 1920x1080,
the two continuation buffers alone reserve about 443 MiB, so this Debug candidate
is evidence-oriented rather than a memory-optimized final release.

The profiler publishes a real Vulkan timestamp interval around the complete
staged Raw schedule. Monolithic per-ray counter semantics are marked unavailable
for this path instead of being inferred from incompatible stage-local counters.

## Normal controls

All discrete commands are `GLFW_PRESS` only, enter one FIFO `ActionQueue`, and
commit at the fixed frame boundary. `GLFW_REPEAT`, unfocused input, and keyboard
or mouse input captured by ImGui do not pass through.

| Control | Action |
|---|---|
| `W/A/S/D`, `Q/E` | move; Left Shift fast; Left Ctrl fine |
| mouse, wheel, `Alt+wheel` | look, movement speed, vertical FOV |
| `Tab`, `Esc`, `Alt+F4` | capture/release, release only, exit |
| `Home`, `P`, `O`, `K` | fixed camera, pause, paused step, lock camera/base seed/origin |
| `B/I/L/N/V` | cycle Backend/complete Integrator showcase profile/Light/Reconstruction/Debug; Shift reverses |
| main-row or keypad `0`-`9` | request scene only; never mutate the algorithm tuple |
| `R` | reset accumulation, temporal history, and reservoir history |
| `[`/`]`, `-`/`=`, `PageDown`/`PageUp` | bounce, exposure, render scale |
| `F1`-`F3` | help, algorithm, profiler |
| `F4` | linear RGBA32F EXR, PNG preview, metadata JSON |
| `F5` | transactional shader reload; failure retains the old runtime |
| `F6`-`F9` | fixed-seed A/B, legend, short benchmark, reference comparison |
| `F10` | print the current scene purpose and complete algorithm study card |

Cycling visits only capability-table-supported values. Integrator cycling is a
showcase-specific complete-profile operation: Whitted -> PBR -> GPU Megakernel
-> GPU Wavefront. Crossing the Legacy/modern boundary atomically supplies the
dependent tuple; Legacy selects Baseline because its analytic shaders do not
consume canonical experiment scenes. If any resulting complete tuple is still
unavailable, the request is rejected and the previous state remains live.

The Visual Studio `RenderingEngine.Vulkan` Debug profile launches the attached
Wave 5 display configuration directly. The equivalent repository-root command
is:

```powershell
.\bin\x64\Debug\RenderingEngine.exe --scene baseline --backend ray-query `
  --transport pbr --execution megakernel --direct-lighting mis `
  --light-selection power --environment-sampler uniform-sphere `
  --reconstruction raw --resolution 1280x720 --max-depth 8 --validation on
```

`1280x720` is the current interactive display size. Formal portfolio
captures still use their separately pinned acceptance resolution when one is
declared.

## Evidence

### Build and automated tests

The latest tree passed both the Debug renderer project and complete Debug
solution builds with Visual Studio 18.9, one MSBuild worker, the pinned installed
dependency tree, and manifest installation disabled.

Eleven non-interactive test executables returned exit code 0: Contracts, CPU
Reference, Platform GLFW, ReSTIR, Scene Wave 1, Showcase, Software GPU, Hardware
RT, Megakernel, Wavefront, and Reconstruction. This includes the real Dear ImGui
draw-data tests, press-only action routing, tuple atomicity, capture transaction,
provider provenance, and EXR RGBA32F round-trip (`max_abs_error=0`).

The L4 Vulkan smoke separately passed with validation and synchronization
validation enabled, `warnings=0`, `errors=0`, CPU/GPU readback parity, and true
GPU trace timestamps for Flattened SAH and GPU LBVH.

The Wave 2/3 traversal gate passed the canonical triangle, Cornell, and alpha
mask scenes with `validation errors=0`, `warnings=0`. Software and Ray Query
primary diagnostics matched exactly on all three scenes (`relative RMSE=0`,
`max abs=0`), while the gate also exercised Ray Query, RT Pipeline traversal,
BSDF sampling probes, and the canonical material domain.

### 2026-09-02 display and input repair

The renderer, Showcase tests, and Scene Wave 1 tests rebuilt and returned exit
code 0 after the display repair. The repair added keypad digit routing, removed
the hidden cross-domain behavior where `B` or `I` could replace multiple
algorithm fields, made scene/provider rejection visible in the status strip,
and kept the default Algorithm and Debug panels out of the rendered image.

A real Visual Studio F5 launch was then driven through the current IDE session.
The first launch proved that Visual Studio had retained an older in-memory
Cornell/Uniform toolbar command despite the updated `.vcxproj.user` file. That
live command was replaced and committed. The next F5 launch reported Baseline,
Vulkan Ray Query, GPU Megakernel, MIS, Power-weighted Lights, Raw, and
1980x1080 in both the title and status strip. That earlier display profile is
superseded by the current 1280x720 project and per-user Debug launch settings.

Live runs exercised digits `0`, `4`, `5`, `7`, `8`, `9`, and `6`. Baseline,
GGX/MIS, Environment Dome, Backend Parity, Temporal Corridor, and the 100-light
arena switched without changing the algorithm tuple. Sponza was rejected
visibly while the Many Lights scene and complete tuple remained live. Visual
inspection at 1980x1080 also confirmed the repaired Baseline and GGX staging;
finite 32 spp GPU runs covered Optics, Backend Parity, and Many Lights. Raw
low-SPP noise is retained and labeled rather than hidden by an unavailable
reconstruction path.

### Finite live-runtime matrix

| Scene | Backend | Depth | Result |
|---|---|---:|---|
| Cornell | Ray Query | 8 | exit 0, staged full Raw |
| Cornell | Flattened SAH | 8 | exit 0, staged full Raw |
| Whitted Optics | Ray Query | 8 | exit 0, Smooth Glass |
| Whitted Optics | Flattened SAH | 8 | exit 0, Smooth Glass |
| GGX/MIS | Ray Query | 8 | exit 0, rough dielectric |

A 70-frame Cornell Ray Query resize run crossed both programmed swapchain
recreation points and exited 0. Accumulation restarted at each discontinuity as
declared, leaving 9 spp after the second resize.

Before removal of the local isolation hook, all five selected AOVs passed at
64x64, depth 4, on both Ray Query and Flattened SAH. The two indirect AOVs also
completed 64 spp live readback captures with provider-specific metadata and the
Seed/Trace/Shade/Resolve shader hashes.

### Fresh showcase capture

The final-tree primary visual artifact is:

`RenderingEngine/.artifacts/wave5-cornell-staged-raw-rq/manual`

It contains a 640x360, 64 spp linear EXR, PNG preview, and metadata JSON. The
metadata reports `live-runtime`, `fresh`, `complete`, frame 64/sample 63,
`capture:renderer-readback:raw`, and build identity
`RenderingEngine-Wave5-debug-staged-raw`. It hashes the actual Raw Seed, Ray
Query Trace, general Showcase Shade, Resolve, traversal, and presentation
shaders. Visual inspection confirmed a coherent Cornell enclosure, direct area
light, object occlusion, and red/green multi-bounce color bleeding. Noise at
64 spp remains visible and is not presented as a converged reference.

## Fail-closed release boundary

The following remain declared but unavailable in the current Debug showcase:

- GPU LBVH construction adapter and RT Pipeline/SBT production dispatch;
- 1,000/10,000-light production tiers, reservoir debug views, and final
  abi-v3 comparison-capture acceptance;
- Sponza scene publication until its pinned licensed asset and texture-capable
  glTF provider are present;
- final benchmark/reference datasets, asset-license approval, human interactive
  key-by-key UI acceptance, and encoded portfolio video approval.

Wavefront, temporal/A-Trous/SVGF, and the bounded 100-light ReSTIR path are now
attached as Debug showcase selections. This is not Release, visual-quality, or
performance acceptance. RT Pipeline, GPU LBVH, higher light tiers, scene `6`,
and other unavailable destinations still show an explicit disabled reason and
do not fabricate results. Final reference datasets and encoded portfolio
approval remain Release 5 gates.

This evidence supports the label **Wave 5 Debug showcase candidate** only.
