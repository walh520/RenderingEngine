# L0 handoff: Wave 0 foundation

- Status: Accepted
- Date: 2026-08-24
- Baseline checkpoint: `f190437`
- Contracts: `abi-v0`, `runtime-config-v0`, `cli-v0`,
  `artifact-layout-v0`, `build-workflow-v0`

This handoff separates the foundation that exists from the algorithms and
automation that are only named by the roadmap.

## Delivered scope

### Repository and build ownership

- `master` is the only integration line; there is no dedicated integration
  branch.
- Visual Studio `.sln/.vcxproj/.props/.targets` and MSBuild are the complete
  committed build graph. No CMake input is part of the repository.
- Shared C++ policy and lane-owned item manifests keep parallel source
  registration out of the central project.
- `RenderingEngine.Contracts.Tests` is a real executable test project in the
  solution; no empty feature libraries were created.
- The vcpkg manifest pins the approved future infrastructure dependencies. The
  compatibility executable keeps manifest restore disabled because it does not
  consume them yet.

### Contracts

- ABI v0 freezes explicit-lane Frame, Scene, Vertex, Geometry, Instance,
  Material, Light, Ray, Hit, and descriptor ownership records in matching C++
  and HLSL headers.
- BSDF/LightSample responsibilities are frozen as mathematical vocabulary only;
  their concrete records and offsets remain ABI v1 work.
- The C++ ABI test executable, HLSL probe, SPIR-V validation, and golden offsets
  are wired through the Visual Studio test project.
- `RuntimeConfig` keeps scene, backend, integrator, direct estimator, light
  proposal, reconstruction, debug view, and legacy shadow method orthogonal.
- CLI parsing, aliases, exit codes, complete-tuple capability decisions, and
  artifact-layout planning occur before platform creation.
- Unsupported named features return 4; they do not create a window, write a
  plausible artifact, or fall back to the compatibility renderer.

### Composition seam

- The platform host owns native window creation, messages, input, framebuffer
  extent, required Vulkan instance extensions, and surface creation.
- The renderer consumes the platform interface and frame events rather than
  Win32 messages/handles.
- Application composition owns CLI normalization, capability evaluation,
  platform-factory invocation, compatibility-option projection, and exit-code
  translation.
- The Wave 0 production host remains Win32. GLFW replacement is L1 work.

## Implemented runtime capability

```text
scene             baseline / Baseline Gallery
backend           legacy-analytic-gpu / legacy analytic GPU
integrator        whitted or pbr
direct estimator  legacy-analytic-direct
proposal          legacy-analytic
reconstruction    raw
shadow method     physical, pcf, or pcss
debug view        final, base-color, normal, roughness, metallic, emissive
```

The control plane also connects the compatibility renderer's frame/target-SPP,
resize, resolution, maximum-bounce, base-seed, exposure, startup FOV, VSync,
validation, shadow, integrator, and debug settings within the documented Wave 0
bounds. Render scale remains `1.0` and samples per frame remains one.

## Evidence recorded so far

| Evidence layer | Result | Boundary |
|---|---|---|
| Repository workflow audit | Passed | Current local branch list contains only `master`; no CMake file/preset is present |
| Baseline checkpoint Debug/Release + runtime suite | Passed at `f190437` | Pre-Wave-0-composition checkpoint only; not evidence for the final tree |
| Final Debug solution Rebuild | Passed, 0 warnings / 0 errors | Complete latest-tree `RenderingEngine.sln`; not visual/numerical/performance evidence |
| Final Release solution Rebuild | Passed, 0 warnings / 0 errors | Complete latest-tree `RenderingEngine.sln`; same proof boundary as Debug |
| ABI test project, Debug + Release | Passed | MSVC v145, C++20, `/W4 /WX`; 12 records, 7 array strides, 11 resources, C++ layout, DXC probe, `spirv-val`, golden/constants comparison and failure-safe stamps |
| Runtime-control CPU tests, Debug + Release | Passed | Defaults, CLI, capability table, artifact planning, and legacy projection only; no platform/GPU execution |
| Existing production shader target, Debug + Release | Passed for four outputs | DXC and `spirv-val`; shader legality only |
| Platform/renderer seam syntax check | Passed | Translation-unit compile/syntax evidence, not full link/runtime |
| CLI control-plane suite, Debug + Release | Passed | Help/version=0, invalid syntax/config=2, declared unsupported=4, and unsupported capture created no probe path |
| Runtime-failure exit 10 | Passed in a controlled Release probe | Temporarily absent `WhittedTrace.comp.spv` produced `Runtime failure` and exact exit 10; the shader was restored unchanged. This proves the shader/runtime path, not injected platform-factory counting |
| GPU runtime + Vulkan validation, Debug + Release | Passed | Both forced `--validation on`; Whitted/PBR, physical/PCF/PCSS, six debug views, seed 123 + target SPP 2, FOV 60, VSync on/off, and 800x600/90-frame resize completed with no `[Vulkan]` diagnostic |
| Visual acceptance/performance | Not run | Not a Wave 0 foundation claim |

The ABI probe proves static record layout, descriptor decorations, legal
SPIR-V, and agreement with the golden offsets. It does not prove GPU sentinel
round-trip, traversal correctness, numerical convergence, rendered image
parity, visual quality, or performance.

## Final acceptance commands

```powershell
& <MSBuild.exe> RenderingEngine.sln /t:Rebuild /m:1 `
    /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> RenderingEngine.sln /t:Rebuild /m:1 `
    /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=x64

.\RenderingEngine\tools\Validate-Pbr.ps1
```

The accepted Debug and Release runs additionally exercised help/version,
invalid CLI exit 2, unsupported capability exit 4 before window/artifact
creation, target-SPP termination, seed propagation, FOV, VSync/validation
selections, and the existing integrator/shadow/debug/resize matrix. This smoke
does not compare two captured images and therefore does not prove image-level
seeded repeatability.

A separate controlled Release probe temporarily made the Whitted SPIR-V input
unavailable, observed the expected runtime diagnostic and exit 10, and restored
the shader unchanged. Exit 10 now has executed runtime evidence, while direct
counting/throwing platform-factory regression tests remain future test-harness
work.

## Explicitly deferred

- GLFW host and split Vulkan Core;
- canonical triangles/glTF and exhibition spaces 1 through 9;
- CPU reference, software GPU, Ray Query, and RT Pipeline traversal;
- NEE, MIS, modern megakernel/Wavefront PT, SVGF, and ReSTIR DI;
- production headless rendering;
- PNG/EXR capture, benchmark reports, and reference comparison output;
- GPU ABI sentinel round-trip, seeded image/hash reproducibility,
  numerical/visual parity, and performance gates.

## Parallel-line start rule

Each feature conversation starts from the L0-published `master` commit in its
own `codex/` branch and Git worktree, reads the relevant contracts/ADRs, and
writes only its owned module/proposal/handoff files. Only L0 composes the
solution and merges into `master`.
