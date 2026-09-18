# Vulkan HLSL PBR Path Tracer

The active Visual Studio target is a Vulkan 1.3 algorithm demonstrator. With no
CLI arguments it starts in Baseline with staged PBR path transport; the saved
Debug F5 profile starts in Many Lights with PBR/Wavefront/ReSTIR/SVGF.
Whitted is an independent transport
model; Megakernel and Wavefront are independent execution architectures. Older
OpenGL samples are not compiled into the active executable, and the CPU
reference path remains a finite headless verifier.

## Active rendering path

- HLSL compute shader compiled to SPIR-V by DXC (`-fvk-use-dx-layout`).
- A canonical linear baseline plus provider-owned canonical triangle scenes;
  interactive traversal can use canonical linear search, flattened SAH, or
  Vulkan Ray Query without changing the scene.
- Default PBR path integration with glTF metallic-roughness materials,
  Cook-Torrance GGX, height-correlated Smith visibility, Schlick Fresnel,
  Heitz visible-normal sampling, cosine diffuse sampling, multi-bounce indirect
  light, finite sphere-light next-event estimation, and Russian roulette.
- Smooth and rough dielectric reflection/refraction with exact Fresnel, total internal
  reflection, ray-origin offsets, and Beer-Lambert attenuation.
- Explicit Whitted comparison through `--transport whitted --execution staged`:
  local direct light plus sampled ideal-specular chains, not an exhaustive
  deterministic reflection/refraction tree.
- Physical visibility is the default shadow method. PCF and PCSS remain
  deterministic teaching/comparison modes and are carried through every
  interactive integrator, including Wavefront and ReSTIR DI.
- One sub-pixel Monte Carlo sample per pixel and frame, progressively averaged
  in an RGBA32F storage image and capped at 4096 samples per pixel.
- Fullscreen HLSL presentation with exposure, Khronos PBR Neutral tone mapping,
  and correct sRGB handling.
- Vulkan 1.3 dynamic rendering, `synchronization2`, two frames in flight,
  persistently mapped frame constants, packed device-local scene buffers, and
  resize-safe swapchain recreation.

## Requirements

- Visual Studio with the v145 C++ toolset and a Windows SDK.
- Vulkan SDK; `VULKAN_SDK` must point to its root.
- A Vulkan 1.3 GPU/driver supporting dynamic rendering, `synchronization2`, and
  RGBA32F storage images.

The active application uses the frozen GLFW dependency for window creation,
keyboard/mouse input, and Vulkan surface creation through `IPlatformHost`.
The current shared GPU runtime initializes all three interactive traversal
providers, so it requires buffer device address, acceleration structures,
deferred host operations, and Vulkan Ray Query even when it starts in Linear
or software BVH mode. This is a runtime requirement, not a software-BVH
algorithm requirement. The
active target does not depend on GLAD, GLM, or an OpenGL runtime.

## Build and run

Open `RenderingEngine.sln` and build `Debug|x64` or `Release|x64`. The solution,
`.vcxproj`, `.props`, `.targets`, and MSBuild are the only official build graph;
this repository does not use CMake. From PowerShell:

~~~powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\RenderingEngine.sln /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:CL_MPCount=1 /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\RenderingEngine.exe
~~~

For the user-owned Release handoff, change `Debug` to `Release` in both the
configuration and executable path. `/p:CL_MPCount=1` keeps compiler PDB writes
deterministic on MSVC installations where `/FS` alone still races during a
solution rebuild.

Useful non-interactive selections:

~~~powershell
.\bin\x64\Debug\RenderingEngine.exe --frames 120
.\bin\x64\Debug\RenderingEngine.exe --transport pbr --execution staged --shadow physical --max-depth 8 --exposure 1.0
.\bin\x64\Debug\RenderingEngine.exe --transport whitted --execution staged --frames 120
.\bin\x64\Debug\RenderingEngine.exe --shadow pcf
.\bin\x64\Debug\RenderingEngine.exe --shadow pcss
.\bin\x64\Debug\RenderingEngine.exe --debug-view normal --frames 8
~~~

Run `RenderingEngine.exe --help` for the executable summary; the normative
tokens, ranges, capability boundary, and exit codes are in
`docs/contracts/cli-v2.md`.
Run the repeatable build, ABI/shader, CLI/exit-code, runtime-mode, debug-view,
seed/target-SPP, VSync/validation, no-I/O rejection, and resize suite:

~~~powershell
.\RenderingEngine\tools\Validate-Pbr.ps1
~~~

The Debug executable uses the RuntimeConfig v2 split-axis renderer. The nine
provider-backed experiment scenes (`0`-`5`, `7`-`9`) expose traversal backend,
transport model, execution architecture, direct estimator, discrete-light
selection, environment-direction sampling, reconstruction, and shadow method
independently. ReSTIR DI and all 100/1,000/10,000-light tiers use this same
renderer. Removed mixed and legacy CLI spellings are rejected rather than
translated.

Digit `6` remains a fail-closed Sponza asset gate because the pinned licensed
asset and texture-capable glTF provider are not in this repository. The separate
L3 Cornell + CPU SAH + CPU-reference path remains deliberately headless and is
not an interactive GPU algorithm axis.

The Wave 5 Debug showcase attaches the provider-driven ImGui, action queue,
finite Raw/AOV rendering, reconstruction, live capture, and capability display
to the production GLFW/Vulkan loop. Visual Studio's Debug profile opens the
configured renderer at 1280x720. This remains a Debug showcase, not a Release-quality
or performance claim: see
[`docs/handoffs/L10/wave5-debug-showcase.md`](docs/handoffs/L10/wave5-debug-showcase.md)
for the exact evidence and unavailable-provider boundary.

## GLFW controls

The no-argument executable retains the Baseline default. Visual Studio's Debug
profile starts directly in the algorithm showcase. Every algorithm key
changes exactly one axis; scene digits change only the scene and fixed camera.
Invalid scalar values and genuinely missing providers are rejected atomically
while the previous state stays live, but no Wave-domain tuple lock remains.

- `W/A/S/D`, `Q/E`: move; `Left Shift`/`Left Ctrl`: fast/fine; mouse: look;
  wheel: movement speed; `Alt+Wheel`: vertical FOV.
- `Tab`: capture/release the pointer; `Esc`: release only; `Alt+F4`: exit;
  `Home`: fixed camera; `P`: pause; `O`: one paused step.
- `B`: traversal backend; `I`: transport model; `Ctrl+I`: execution
  architecture; `L`: direct-lighting estimator; `Ctrl+L`: discrete-light
  selection; `Alt+L`: environment-direction sampler; `Ctrl+Alt+L`: shadow
  method; `N`: reconstruction; `V`: final/AOV debug view. Hold `Shift` with
  the same chord to cycle backward. Each command changes only that field.
- main-row or keypad `0`-`9`: request a showcase scene and its fixed camera
  only. `0`-`5` and `7`-`9` are attached; `6` reports the exact Sponza asset
  gate while preserving the current scene and all algorithm selections.
- `R`: reset accumulation/temporal/reservoir history; `K`: lock camera, base
  seed, and animation origin; `[`/`]`: bounce; `-`/`=`: exposure;
  `PageDown`/`PageUp`: request render scale (the current production attachment
  remains fixed at 1.0 and rejects other values without changing the live state).
- `F1`: help; `F2`: algorithm; `F3`: profiler; `F4`: linear EXR + PNG +
  metadata; `F5`: transactional shader reload; `F6`: fixed-seed A/B; `F7`:
  legend; `F8`: short benchmark; `F9`: reference comparison; `F10`: print the
  current scene purpose plus current/recommended tuple and match state;
  `F11`: atomically restore the current scene's teaching recommendation.

`F11` is a convenience action, not an algorithm lock. It preserves the current
scene, resolution, render scale, SPF/target SPP, exposure, seed, FOV, camera,
validation/VSync, and output paths. Afterwards `B/I/L/N/V` and their modified
chords still compare individual axes normally. The ten versioned profiles and
their exact ownership/reset contract are published in
[`docs/contracts/scene-recommendations-v2.md`](docs/contracts/scene-recommendations-v2.md).

Discrete commands are press-only, enter one FIFO action queue, and commit at a
fixed frame boundary. ImGui keyboard/mouse capture and window focus prevent
input passthrough. Camera/FOV, scene, algorithm, sampling, and resize
discontinuities apply their declared reset masks. A missing resource never
silently substitutes a different implementation.

## Material inspection

The validation scene spans dielectric, smooth glass, gold and silver
conductors, rough painted material, ceramic, checker floor, wall, and two
visible emissive spheres. Deterministic first-hit debug views expose the GPU
material contract:

~~~powershell
.\bin\x64\Debug\RenderingEngine.exe --debug-view base-color
.\bin\x64\Debug\RenderingEngine.exe --debug-view normal
.\bin\x64\Debug\RenderingEngine.exe --debug-view roughness
.\bin\x64\Debug\RenderingEngine.exe --debug-view metallic
.\bin\x64\Debug\RenderingEngine.exe --debug-view emissive
~~~

## Data flow

~~~text
RuntimeConfig v2 + camera + canonical experiment scene
        | CapabilityTable: validate the complete tuple, no fallback
        v
Wave2Runtime: Staged / Megakernel / Wavefront execution
        | shared BSDF + independent light selection / environment direction
        | Canonical Linear / Flattened SAH / Ray Query traversal
        v
Radiance signals + selected reconstruction + RGBA32F output
        v
Present.hlsl (exposure + PBR Neutral + sRGB, fullscreen triangle)
        | Vulkan dynamic rendering
        v
Swapchain
~~~

All GPU records use explicit 16-byte lanes on both C++ and HLSL sides. C++
static assertions guard record sizes, and the shader build treats warnings as
errors.

## Scope

The active pipeline owns canonical experiment geometry, BVH/Ray Query,
shared PBR BSDFs, environmental importance sampling, and the connected
reconstruction/ReSTIR paths. Presence in the pipeline is not a correctness or
quality certificate. Sponza, LBVH, RT Pipeline, and unconnected debug providers
remain explicitly gated. CPU reference remains a restricted Cornell oracle.

[PBR_IMPLEMENTATION.md](PBR_IMPLEMENTATION.md) records the actual transport,
execution, termination, and AOV differences, including what is not yet proven
equivalent. The renderer no longer contains the old RunOptions projection,
duplicate Wave 0 scene upload, or old standalone PBR/Whitted compute entry.

## Active contracts

- [RuntimeConfig v2](docs/contracts/runtime-config-v2.md): independent axes and capability boundary.
- [CLI v2](docs/contracts/cli-v2.md): current algorithm arguments; old mixed-axis spellings are errors.
- [Scene recommendations v2](docs/contracts/scene-recommendations-v2.md): F10/F11 behavior and all ten profiles.
- [ABI v3](docs/contracts/abi-v3.md): independently versioned GPU records.

## Historical Wave 0 records (not current startup instructions)

- `docs/contracts/runtime-config-v0.md`: original configuration and historical
  capability boundary, amended at that time by ADR 0005.
- `docs/contracts/cli-v0.md`: historical CLI grammar, aliases, and exit codes.
- `docs/contracts/artifact-layout-v0.md`: deterministic future output layout;
  Wave 0 performs path planning only.
- `docs/contracts/abi-v0.md`: canonical C++/HLSL records and static layout proof
  boundary.
- `docs/contracts/build-workflow-v0.md`: Visual Studio/MSBuild and master-only
  Git workflow.
- `docs/contracts/wave0-acceptance-v0.md`: required evidence and deferred work.
