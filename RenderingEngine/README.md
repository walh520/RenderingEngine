# Vulkan HLSL Whitted Ray Tracer

The active Visual Studio target is a modern Vulkan 1.3 renderer whose startup
default is classic Whitted ray tracing. A separate PBR path integrator is kept
as an optional runtime comparison, and the repository's older OpenGL/CPU
experiments are not compiled into the active executable.

## Active rendering path

- HLSL compute shader compiled to SPIR-V by DXC (`-fvk-use-dx-layout`).
- Analytic sphere and plane intersections with robust near/far sphere roots.
- Default depth-first Whitted reflection/refraction with finite recursion,
  Schlick Fresnel, total internal reflection, ray-origin offsets, throughput
  cut-off, and Beer-Lambert attenuation.
- Direct-light GGX metallic/roughness shading and correct finite-distance
  shadow rays.
- Optional PBR path through `--integrator pbr`, retained for A/B
  comparison without changing the Whitted default requested by this project.
- The physical sphere-light method is the default shadow method. PCF and PCSS
  remain deterministic legacy comparison modes.
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

The Wave 0 compatibility host currently uses Win32 for window creation, raw
mouse input, and `VK_KHR_win32_surface`, behind `IPlatformHost`; renderer code
does not own native window messages or handles. L1 replaces this host with the
frozen GLFW dependency. The active target has no GLFW, GLAD, GLM, or OpenGL
runtime dependency yet.

## Build and run

Open `RenderingEngine.sln` and build `Debug|x64` or `Release|x64`. The solution,
`.vcxproj`, `.props`, `.targets`, and MSBuild are the only official build graph;
this repository does not use CMake. From PowerShell:

~~~powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\RenderingEngine.sln /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\RenderingEngine.exe
~~~

Useful non-interactive selections:

~~~powershell
.\bin\x64\Debug\RenderingEngine.exe --frames 120
.\bin\x64\Debug\RenderingEngine.exe --integrator whitted --shadow physical --max-depth 8 --exposure 1.0
.\bin\x64\Debug\RenderingEngine.exe --integrator pbr --frames 120
.\bin\x64\Debug\RenderingEngine.exe --shadow pcf
.\bin\x64\Debug\RenderingEngine.exe --shadow pcss
.\bin\x64\Debug\RenderingEngine.exe --debug-view normal --frames 8
~~~

Run `RenderingEngine.exe --help` for the executable summary; the normative
tokens, aliases, ranges, capability boundary, and exit codes are in
`docs/contracts/cli-v0.md`.
Run the repeatable build, ABI/shader, CLI/exit-code, runtime-mode, debug-view,
seed/target-SPP, VSync/validation, no-I/O rejection, and resize suite:

~~~powershell
.\RenderingEngine\tools\Validate-Pbr.ps1
~~~

Wave 0 deliberately supports only Baseline Gallery on the legacy analytic GPU
backend, with Whitted/PBR, Raw reconstruction, the existing shadow methods,
and existing material debug views. Future scenes, backends, estimators,
reconstruction modes, production headless rendering, capture, benchmark, and
reference comparison are named by the control-plane contract but return exit
code 4 before window creation or file output. A recognized name is not an
implementation claim.

The 2026-08-24 Wave 0 handoff records passing Debug and Release solution builds,
contract tests, shader/ABI validation, CLI 0/2/4, existing GPU runtime modes,
controlled runtime exit 10, and Vulkan validation. It does not claim manual
visual acceptance, numerical or performance comparison, image-level
repeatability, or automated exit-10 failure injection.

## Controls

These are the current Win32 compatibility-host controls. The future GLFW
algorithm-comparison ActionMap is specified in section 10 of
`VULKAN_RT_PARALLEL_DEVELOPMENT_PLAN.md`; it is not a Wave 0 capability claim.

- W/A/S/D: move.
- Space / Left Ctrl: move vertically.
- Left Shift: sprint.
- Mouse: look.
- Mouse wheel: change field of view.
- 1 / 2 / 3: PCF / PCSS / physical sphere-light shadows.
- Tab: capture or release the mouse cursor.
- Esc: exit.

Camera, FOV, window-size, and shadow-mode changes reset progressive
accumulation.

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
Camera + metallic/roughness GPU scene records
        | per-frame UBO + packed device-local structured buffers
        v
WhittedTrace.hlsl (default) or PbrPathTrace.hlsl (optional comparison)
        | RGBA32F progressive HDR image
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

The selected integrator is wired end-to-end from scene records through Vulkan
presentation, with Whitted as the startup default. Geometry remains the
analytic sphere/plane scene. Mesh loading, texture and normal-map sampling, BVH
acceleration, rough dielectric transport, spectral rendering, denoising, and
HDRI importance sampling remain outside this focused implementation.

## Wave 0 contracts

- `docs/contracts/runtime-config-v0.md`: canonical orthogonal configuration and
  current capability boundary.
- `docs/contracts/cli-v0.md`: CLI grammar, aliases, and exit codes.
- `docs/contracts/artifact-layout-v0.md`: deterministic future output layout;
  Wave 0 performs path planning only.
- `docs/contracts/abi-v0.md`: canonical C++/HLSL records and static layout proof
  boundary.
- `docs/contracts/build-workflow-v0.md`: Visual Studio/MSBuild and master-only
  Git workflow.
- `docs/contracts/wave0-acceptance-v0.md`: required evidence and deferred work.
