# RenderingEngine

> Vulkan 1.3 / C++ / HLSL GPU path-tracing and ReSTIR research platform.

RenderingEngine is a Windows x64 rendering laboratory for comparing physically based light transport, ray-traversal backends, GPU execution architectures, temporal reconstruction, and many-light direct-light sampling in one reproducible runtime.

This repository contains an interactive Debug showcase, headless CPU reference tools, Vulkan validation/smoke tests, cross-language GPU ABI contracts, shader validation, and deterministic capture metadata. It is intended as a research and engineering demonstrator rather than a game engine or a finished production renderer.

## Highlights

- Vulkan 1.3 compute/presentation path using HLSL compiled to SPIR-V by DXC.
- PBR path tracing with Cook-Torrance GGX, correlated Smith visibility, Schlick Fresnel, Heitz VNDF sampling, NEE, MIS, multiple-bounce transport, Russian roulette, transmission, and Beer-Lambert attenuation.
- Whitted-style reflective/refractive transport for optical comparison scenes.
- Three interactive traversal providers: canonical linear traversal, CPU-built flattened SAH BVH, and Vulkan Ray Query.
- Three interactive execution architectures: staged dispatch, GPU Megakernel, and GPU Wavefront queues.
- Direct-lighting comparison through BSDF-only, NEE, MIS, and ReSTIR DI.
- Uniform/power-weighted light selection and environment importance sampling.
- Progressive film, current-frame output, temporal accumulation, fixed A-Trous, and SVGF reconstruction.
- ReSTIR DI initial sampling, temporal/spatial reuse, winner visibility, reservoir history, debug views, and GPU statistics readback.
- GLFW camera/input host and Dear ImGui algorithm, scene, debug, profiler, and capture panels.
- Linear RGBA32F EXR, PNG preview, JSON metadata, shader hashes, and reproducible finite-frame captures.
- Versioned C++/HLSL ABI contracts with explicit 16-byte records, static layout checks, append-only descriptor bindings, and fail-closed capability validation.

## Runtime architecture

```text
RuntimeConfig v2
        ↓
CapabilityTable: validate the complete tuple without fallback
        ↓
Canonical experiment scene and material providers
        ↓
Traversal: linear / flattened SAH / Ray Query
        ↓
Transport and execution: PBR / Whitted / staged / Megakernel / Wavefront
        ↓
Direct lighting: NEE / MIS / ReSTIR DI
        ↓
Reconstruction: Raw / Progressive Mean / Temporal / A-Trous / SVGF
        ↓
Debug AOVs, profiler telemetry, EXR/PNG capture, Vulkan presentation
```

The control plane keeps scene, traversal backend, transport model, execution architecture, direct-light estimator, light proposal, environment sampler, reconstruction mode, and shadow method independent. Unsupported combinations are rejected explicitly instead of silently switching to another provider.

## Experiment scenes

The runtime contains nine selectable experiment scenes plus one intentionally gated asset scene:

| Scene | Purpose |
|---|---|
| Baseline Gallery | Visual and material regression baseline |
| Intersection & BVH Lab | Triangle intersection, AABB, BVH, and hit semantics |
| Whitted Optics Room | Reflection, refraction, Fresnel, TIR, and absorption |
| Cornell Box | Multi-bounce diffuse GI, NEE, MIS, and convergence |
| GGX & MIS Material Lab | Metallic-roughness, GGX, rough transmission, and white-furnace studies |
| Environment Sampling Dome | Uniform versus importance-sampled environment lighting |
| Sponza Traversal Hall | Large geometry and alpha-mask traversal; currently asset-gated |
| Backend Parity Benchmark | Fixed-scene traversal correctness and backend comparison |
| Temporal Stability Corridor | Motion vectors, reprojection, disocclusion, and history rejection |
| Many Lights / ReSTIR Arena | 100/1000/10000-light sampling and reservoir reuse |

## Build requirements

- Windows x64
- Visual Studio with the v145 C++ toolset and Windows SDK
- Vulkan SDK with `VULKAN_SDK` configured
- Vulkan 1.3-capable GPU/driver with dynamic rendering, synchronization2, and RGBA32F storage-image support
- vcpkg manifest dependencies from `vcpkg.json`

The official build graph is the Visual Studio solution and MSBuild project files; the repository does not use CMake.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\RenderingEngine.sln `
  /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:CL_MPCount=1 `
  /p:Configuration=Debug /p:Platform=x64
```

Run the Debug executable:

```powershell
.\bin\x64\Debug\RenderingEngine.exe
```

Useful finite-frame commands:

```powershell
.\bin\x64\Debug\RenderingEngine.exe --frames 120
.\bin\x64\Debug\RenderingEngine.exe --transport pbr --execution staged --shadow physical --max-depth 8
.\bin\x64\Debug\RenderingEngine.exe --transport whitted --execution staged --frames 120
.\bin\x64\Debug\RenderingEngine.exe --debug-view normal --frames 8
```

The repeatable validation entry point is:

```powershell
.\RenderingEngine\tools\Validate-Pbr.ps1
```

## Controls

- `W/A/S/D`, `Q/E`: camera movement; Shift/Ctrl change movement speed.
- Mouse and wheel: look, movement speed, and FOV control.
- `B`: traversal backend.
- `I`: transport model; Ctrl+I: execution architecture.
- `L`: direct-light estimator; Ctrl+L: discrete light selection; Alt+L: environment sampler.
- `N`: reconstruction; `V`: debug view; Ctrl+Alt+L: shadow method.
- `0`-`9`: scene selection; `F10`: current study card; `F11`: scene recommendation restore; `F12`: scene variant cycle.
- `R`: reset accumulation/history; `P`/`O`: pause and single-step.
- `F4`: EXR/PNG/metadata capture; `F5`: transactional shader reload; `F6`-`F9`: comparison and QA actions.

## Validation and evidence

The solution includes contract, scene, CPU reference, software GPU, hardware RT, Megakernel, Wavefront, reconstruction, ReSTIR, GLFW, Showcase/UI, and Vulkan smoke targets.

The repository records Debug build, shader/SPIR-V validation, Vulkan validation, CPU/GPU parity, fixed-seed film consistency, ReSTIR ABI-v3 GPU oracle, GPU statistics readback, and capture metadata evidence. These results are deliberately reported with their scope: they do not automatically imply Release performance, convergence quality, full-scene coverage, or final portfolio visual acceptance.

## Current boundaries

- GPU LBVH and Vulkan RT Pipeline/SBT remain declared or partially implemented but are not current interactive production providers.
- Sponza remains fail-closed until the licensed, hashed, texture-capable glTF asset path is available.
- Full 1000/10000-light performance benchmarking and complete ReSTIR convergence/unbiasedness studies remain separate acceptance work.
- Release performance, memory, frame-time, and long-duration visual-quality claims require dedicated measurements.

## Repository map

| Path | Responsibility |
|---|---|
| `RenderingEngine/src/app` | Application, CLI, runtime configuration, capability table |
| `RenderingEngine/src/renderers` | Vulkan renderer, mixed runtime, reconstruction and ReSTIR attachment |
| `RenderingEngine/src/scene` | Canonical scenes, experiment variants, material/geometry providers |
| `RenderingEngine/rt` | CPU reference, software traversal, hardware RT |
| `RenderingEngine/integrators` | Megakernel and Wavefront execution paths |
| `RenderingEngine/reconstruction` | Temporal, variance, A-Trous, and SVGF passes |
| `RenderingEngine/restir` | ReSTIR host/shader modules and tests |
| `RenderingEngine/resources/shaders` | Shared BSDF, transport, traversal, reconstruction, and ReSTIR shaders |
| `RenderingEngine/docs` | Contracts, ADRs, scene reviews, acceptance records, and handoffs |
| `RenderingEngine/tools` | Build, shader, capture, and validation scripts |

## Further documentation

- [Detailed renderer README](RenderingEngine/README.md)
- [PBR and Whitted implementation record](RenderingEngine/PBR_IMPLEMENTATION.md)
- [Current scenes and algorithms review](RenderingEngine/docs/current-scenes-and-algorithms-review.md)
- [RuntimeConfig v2 contract](RenderingEngine/docs/contracts/runtime-config-v2.md)
- [ABI v1 contract](RenderingEngine/docs/contracts/abi-v1.md)
- [ABI v2 contract](RenderingEngine/docs/contracts/abi-v2.md)
- [ABI v3 ReSTIR contract](RenderingEngine/docs/contracts/abi-v3.md)
- [Final mixed runtime handoff](RenderingEngine/docs/handoffs/L0/final-mixed-runtime.md)
- [Current P0-2 to P0-4 acceptance record](RenderingEngine/docs/handoffs/L0/p0-2-through-p0-4.md)

## Resume-oriented project description

> Developed a Vulkan 1.3 C++/HLSL GPU path-tracing and ReSTIR research platform with PBR/Whitted transport, GGX/VNDF/MIS sampling, software BVH and Vulkan Ray Query traversal, staged/Megakernel/Wavefront execution, Temporal/SVGF reconstruction, versioned C++/HLSL GPU ABIs, ImGui-based algorithm showcase, EXR/PNG/JSON capture, Vulkan validation, and deterministic CPU/GPU parity testing.
