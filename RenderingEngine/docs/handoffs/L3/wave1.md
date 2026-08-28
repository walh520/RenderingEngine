# L3 handoff: Wave 1 CPU reference minimum slice

- Status: **Accepted locally; pending L0 composition**
- Date: 2026-08-24
- Branch: `codex/rt-cpu-reference`
- Baseline checkpoint: `10f791a`
- Contract read before implementation: human-facing `abi-v0`, numeric version `1`
- Related ADRs read: `0001-abi-v0`,
  `0002-pinned-infrastructure-dependencies`,
  `0003-visual-studio-and-master-workflow`, and
  `0004-runtime-control-plane-v0`
- Previous handoff read: `docs/handoffs/L0/wave0.md`

This handoff covers only the first L3 Wave 1 batch named by the roadmap:
CPU intersections, deterministic SAH, and a private CPU reference harness that
can emit the first Cornell reference. Shared contracts, root build files, and
other feature lines were treated as read-only.

## Delivered scope

### CPU geometry and traversal

- Float and double CPU vector/ray/AABB/triangle primitives under
  `include/rt/cpu/`.
- Explicit open ray interval semantics: accepted hits satisfy
  `t > tMin && t < tMax`, matching `abi-v0`.
- ABI-v0 unit-direction validation with a floating-point tolerance; non-unit
  directions are rejected so `t` retains world-distance meaning.
- Parallel-axis-safe AABB testing and a watertight axis-permutation/shear
  triangle test based on the Woop/Benthin/Wald formulation.
- Brute-force closest-hit/any-hit reference traversal.
- Deterministic median and fixed 16-bin binned-SAH BVH construction.
- Stable centroid and equal-distance tie-breaking by primitive ID.
- A bounded CPU traversal stack. Builds deeper than the supported bound are
  rejected rather than silently overflowing.

The BVH is deliberately a private CPU structure. It is not a GPU wire layout,
an ABI-v1 record, a flattened-GPU converter, or a software-GPU backend.

### Wave 1 CPU reference harness

- A private L3 Cornell fixture with diffuse red/green/white surfaces, one
  ceiling area emitter, two blocks, and a fixed camera.
- Double-precision Lambert path integration with emissive hits, next-event
  estimation, power-heuristic MIS, multiple bounces, and Russian roulette.
- Per-pixel/per-sample PCG streams, so output is independent of worker-row
  scheduling.
- Linear RGB EXR output through the pinned TinyEXR dependency, plus a
  deterministic exposure/sRGB BMP preview and JSON metadata.
- A module-local `RenderingEngine.CpuReference.vcxproj` and private harness CLI.
  The harness does not claim the production CLI or runtime-control-plane role
  owned by L10/L0.

The Cornell fixture is marked `l3-private-cornell-fixture-v0`. It does not claim
to be L2's future canonical scene and therefore does not freeze scene IDs,
assets, or a cross-line scene contract.

### Tests

- Ray/AABB/triangle boundary, parallel-axis, shared-edge, and tie-break cases.
- Median and binned-SAH BVH construction/traversal cases.
- One-million-ray brute-force versus CPU-SAH parity corpus.
- Lambert PDF numerical integration and diffuse white-furnace checks.
- Exact fixed-seed single-thread versus four-thread image equality.
- TinyEXR write/read round-trip with channel/value validation.

Harness option/error behavior, finite-pixel accounting, and reference metadata
were checked by the runtime probes below rather than by the Catch2 suite.

## Evidence

Evidence levels are intentionally separate. Passing one row must not be
interpreted as passing a stronger row.

| Evidence layer | Result | Boundary |
|---|---|---|
| Static | Passed | All new tracked candidates are under the L3-owned CPU reference/include/source/test/reference paths plus this handoff. Shared contracts, root solution/project/props/targets, root manifest, and other feature-line files have no diff. L3 project/filter XML parses successfully and the text files have no trailing whitespace. |
| Build | Passed | Module-local CPU reference harness and test projects rebuilt in Debug and Release with MSVC v145, C++20, `/W4 /WX`, 0 warnings and 0 errors. Root `RenderingEngine.sln` was not modified or used as an L3 acceptance claim; L0 still owns composition. |
| CPU runtime | Passed | `--help` returned 0; invalid `--width 0` returned exact exit 2; Debug 16x16/1 SPP and Debug/Release 32x32/4 SPP renders completed with zero non-finite samples. The 256x256/64 SPP Release reference completed and wrote EXR, BMP, and JSON. These runs prove only the private L3 executable, not the production application, Vulkan, or GPU paths. |
| Numerical | Passed for the Wave 1 minimum | Debug and Release Catch2 suites each passed 15 test cases / 175 assertions. For 1,000,000 fixed/random rays: hit/miss mismatches 0, primitive-ID mismatches 0, invalid results 0, maximum relative `t` error 0, and maximum barycentric error 0; this is within the roadmap's `1e-5` CPU threshold. Lambert PDF integration and white-furnace tests passed. No 1-to-4096-SPP RMSE/PSNR convergence sweep was run. |
| Repeatability | Passed | The 256x256/64 SPP/seed 1 render produced the same linear-RGB hash and byte-identical EXR/BMP for automatic threading and one requested thread. This does not prove repeatability across different compilers, standard libraries, CPUs, or future canonical-scene revisions. |
| EXR validity | Passed | TinyEXR successfully read back a generated EXR and the test checked dimensions, channels, and finite sample values. This is file-level CPU evidence, not another renderer's independent comparison. |
| Visual | Passed for bootstrap reference inspection | The 256x256/64 SPP BMP was inspected: Cornell orientation, red/green walls, ceiling light, two blocks, color bleeding, and finite output were visible; no black/empty/flipped image or obvious catastrophic light leak was observed. A single manual inspection is not final-quality, convergence, perceptual-regression, or GPU-parity evidence. |
| GPU/Vulkan | Not run / not applicable | L3 Wave 1 adds no Vulkan, shader, GPU traversal, Ray Query, RT Pipeline, or validation-layer work. |
| Performance | Not accepted | Wall-clock times are recorded only for reproducibility/debugging. No CPU performance target was frozen, and no GPU timestamp evidence exists. |

## Final acceptance commands

The module-local projects were rebuilt serially to keep the evidence isolated
from other concurrent feature worktrees:

```powershell
& <MSBuild.exe> `
    RenderingEngine/rt/cpu/RenderingEngine.CpuReference.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false `
    /p:UseMultiToolTask=false /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> `
    RenderingEngine/rt/cpu/RenderingEngine.CpuReference.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false `
    /p:UseMultiToolTask=false /p:Configuration=Release /p:Platform=x64

& <MSBuild.exe> `
    RenderingEngine/tests/unit/L3/RenderingEngine.CpuReference.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false `
    /p:UseMultiToolTask=false /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> `
    RenderingEngine/tests/unit/L3/RenderingEngine.CpuReference.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false `
    /p:UseMultiToolTask=false /p:Configuration=Release /p:Platform=x64

.\bin\x64\Debug\RenderingEngine.CpuReference.Tests.exe
.\bin\x64\Release\RenderingEngine.CpuReference.Tests.exe

.\bin\x64\Release\RenderingEngine.CpuReference.exe `
    --output-prefix `
    RenderingEngine/rt/cpu/references/wave1/cornell-256x256-64spp-seed1 `
    --width 256 --height 256 --spp 64 --max-bounces 8 `
    --seed 1 --threads 0 --exposure 1
```

The first dependency restore used the already pinned root manifest through the
Visual Studio bundled vcpkg integration. L3 did not modify or vendor the root
manifest/dependencies.

## Reference artifact

```text
RenderingEngine/rt/cpu/references/wave1/
  cornell-256x256-64spp-seed1.exr
  cornell-256x256-64spp-seed1.bmp
  cornell-256x256-64spp-seed1.json
```

- Scope tag: `l3-private-cornell-fixture-v0`
- Contract tag: `abi-v0`, numeric `1`
- Dimensions/SPP/seed: 256x256, 64 SPP, seed 1
- Maximum bounces: 8
- Scene hash (FNV-1a 64): `0x80597851ae8052c2`
- Linear-RGB hash (FNV-1a 64): `0x6b666a36f93df86d`
- Paths traced: 14,714,873 primary/continuation rays and 8,572,432 shadow rays
- Non-finite count: 0
- EXR SHA-256: `F0352041A02DCE03F7B4A3AD654BA982E645EC2366297CA7130DBAC33D05AB29`
- BMP SHA-256: `E8971EFC1F425408851695DA74503CDDB301008157BA85C6AF056213685FF0EB`
- Observed Release wall time: automatic threading 2289.329 ms; one requested
  thread 13835.465 ms. These observations are not performance acceptance.

## Explicitly deferred

The following are named by the long-term L3 roadmap or later waves but were not
implemented for this Wave 1 minimum:

- L2 canonical-scene consumption and stable cross-line scene/asset IDs;
- GGX conductor/dielectric, smooth/rough glass, Fresnel/Jacobian/eta suites,
  and environment sampling;
- Glossy, Mirror/Glass, HDRI, and Sponza reference sets;
- the 1-to-4096-SPP RMSE/PSNR convergence report and final high-SPP Ground Truth;
- CSV aggregation and an independent external-renderer image comparison;
- GPU-friendly flattened nodes, upload/conversion, software-GPU traversal,
  GPU LBVH, Ray Query, RT Pipeline, shaders, or GPU readback parity;
- production CLI/capture/UI integration and root-solution composition.

These items require a later Wave and/or a published contract from L0. They must
not be inferred from the Wave 1 result.

## Known limits and integration notes

- The fixed CPU traversal stack supports 128 entries. Pathological builds that
  would exceed the supported depth fail explicitly; there is no silent stack
  overwrite.
- The checked reference is a noisy but usable 64 SPP bootstrap image. It is not
  the final 4096 SPP oracle or a frozen perceptual baseline.
- The BMP preview's top-down orientation and exposure/sRGB transform currently
  have manual visual-smoke evidence, not an independent pixel-level unit test.
- The Wave 1 JSON records the requested thread count and wall time, but not the
  resolved worker count, preview exposure, commit, CPU model, or toolchain.
  Reproducibility claims therefore rely on the handoff's baseline/toolchain
  context and artifact hashes rather than on the JSON alone.
- The module projects import the Visual Studio bundled vcpkg integration when a
  user-wide integration is absent. L0 may choose how to compose these projects,
  but should not copy their sources into a shared/root-owned project manifest.
- The root manifest may restore dependencies unused by this small executable;
  this follows ADR 0002 and does not authorize an L3 manifest edit.
- There is no shared-contract proposal in this handoff.

## Rollback

Rollback is confined to the L3-owned CPU reference include/source/test/project
and `rt/cpu/references/wave1/` paths plus this handoff. No shared contract, root
build file, root manifest, or other line needs to be reverted.
