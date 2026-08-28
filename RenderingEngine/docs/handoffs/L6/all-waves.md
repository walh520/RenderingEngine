# L6 handoff: complete private PBR megakernel implementation

- Status: Code complete; GPU/data acceptance deferred by request
- Date: 2026-08-27
- Baseline checkpoint: `47fa2dc`
- Branch: `codex/rt-pbr-wavefront`
- Contract decision: the user explicitly asked to land the private code before
  contract integration. No shared contract, root solution, root build file, or
  other lane was modified.

This handoff records the complete L6-private implementation and keeps static,
build, runtime, numerical, and visual evidence separate. It does not promote
compile success into a GPU convergence claim.

## Delivered scope

### Unified BSDF contract

- `PbrBsdf.hlsli` owns matching `EvaluateBsdfL6`, `PdfBsdfL6`, and
  `SampleBsdfL6` paths with explicit discrete versus solid-angle measure.
- Lambert, cosine/uniform hemisphere sampling, GGX VNDF conductor, GGX
  dielectric reflection, smooth glass, rough dielectric transmission, and
  metallic-roughness routing are implemented.
- Delta events, eta-squared radiance transport, true geometric-normal tests,
  shading-normal correction, invalid-state rejection, and the deliberate
  low-roughness transition to smooth glass are explicit. The implementation
  does not hide unsupported states behind a roughness or PDF clamp.
- A lane-private C++ reference mirrors the core BSDF vocabulary for CPU
  invariants without changing the shared ABI.

### Sampling and light distribution

- Philox4x32-10 provides deterministic dimensions and strict `[0, 1)` float
  conversion, including the maximum-word boundary.
- Vose alias tables cover generic weighted selection and exact lat-long texel
  solid angles for environment importance sampling.
- Uniform and power-weighted one-light selection are available without a
  per-shading-point linear loop over every light.
- Point, directional, hard-edge spot, sphere/area, emissive-triangle, and
  environment proposals expose the discrete/area/solid-angle PDF factors used
  by the estimator.

### Megakernel path tracer

- The private compute path implements multiple bounces, NEE, power-heuristic
  MIS, emitter/environment hit MIS, Russian roulette, Beer attenuation,
  one-sided emission, and terminal pure-emitter behavior.
- Direct/indirect and diffuse/specular AOVs are accumulated separately.
- Frame validation rejects invalid dimensions, path depth, proposal/direct
  mode, RR interval, epsilon, and uint32 pixel-count overflow before tracing.
- Non-finite state, negative/invalid PDF, traversal fixture failures, and frame
  errors feed explicit counters; legitimate delta or out-of-support BSDF
  events terminate without being mislabeled as numerical failures.
- A fixed-hit traversal fixture and sampling probe provide private integration
  seams. Real Software GPU and Ray Query traversal remain external L4/L5
  integration work and are not claimed here.

## Evidence

| Evidence layer | Result | Proof boundary |
|---|---|---|
| Static | Passed | 12 L6-private source/project files; both project XML documents parse; no trailing whitespace, TODO/FIXME placeholder, out-of-scope path, or generated object outside the lane build directory was found |
| Build | Passed | `Megakernel.Tests.vcxproj` Debug and Release full rebuilds completed with MSVC C++20 `/W4 /WX`, 0 warnings and 0 errors |
| Shader build | Passed | `pbr_megakernel.hlsl` and `pbr_sampling_probe.hlsl` compiled with DXC SM 6.6, `-Ges -WX`, Vulkan 1.3 target and DX layout; all 2 Debug and 2 Release SPIR-V outputs passed `spirv-val` |
| Runtime | CPU self-test only | Debug and Release `Megakernel.Tests.exe` returned 0. No Vulkan device, descriptor set, compute dispatch, validation layer, or GPU readback was executed |
| Numerical | Basic CPU invariants passed | Philox known vector/bounds/dimensions, alias/environment distribution construction, frame validation, uniform sampler/PDF, and basic Lambert/GGX/glass/transmission relations passed. This is not a statistical GPU Sample/Evaluate/PDF or convergence result |
| Visual | Not run | No Cornell, Glossy, Glass, HDRI, white-furnace, capture, RMSE/PSNR, or image comparison was produced |

## Acceptance commands used

```powershell
& <MSBuild.exe> RenderingEngine\integrators\megakernel\Megakernel.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> RenderingEngine\integrators\megakernel\Megakernel.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=x64

.\RenderingEngine\integrators\megakernel\build\x64\Debug\Megakernel.Tests.exe
.\RenderingEngine\integrators\megakernel\build\x64\Release\Megakernel.Tests.exe
```

## Explicitly deferred

- Binding the private records to the current shared ABI and root build graph;
- executing the probe or megakernel through a real Vulkan pipeline;
- real L4 Software GPU and L5 Ray Query traversal consumption;
- statistical GPU Sample/Evaluate/PDF tests and white-furnace energy tests;
- NEE/MIS versus BSDF-only ray-budget comparison;
- CPU/GPU Cornell, Glossy, Glass, and HDRI convergence, RMSE, and PSNR;
- visual acceptance and performance profiling;
- scene-data validation that enforces one enabled environment and a unique
  light identity for every emissive primitive.

These items are deferred evidence/integration work, not silently accepted
gates. The lane code should not be merged as a numerically accepted renderer
until they are run.

## Algorithm references

- Eric Heitz, *Sampling the GGX Distribution of Visible Normals*, JCGT 2018:
  https://jcgt.org/published/0007/04/01/
- Walter et al., *Microfacet Models for Refraction through Rough Surfaces*:
  https://diglib.eg.org/items/590e957c-92d6-4d8f-9c4c-c23ec106ecda
- Veach, *Robust Monte Carlo Methods for Light Transport Simulation*:
  https://graphics.stanford.edu/papers/veach_thesis/
- PBRT v4, rough dielectric and path-integrator references:
  https://www.pbr-book.org/4ed/Reflection_Models/Rough_Dielectric_BSDF
  and
  https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer
- D. E. Shaw Research, Random123:
  https://github.com/DEShawResearch/random123
