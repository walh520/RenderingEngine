# L9 handoff: all-wave Reservoir, RIS, and ReSTIR DI implementation

- Status: **Line-local implementation complete; shared integration and full data gates deferred**
- Date: 2026-08-24
- Branch: `codex/rt-restir-di`
- Worktree baseline: `47fa2dce03b348d64b51f8b164c033a9deb2c043`
- Contract observed before implementation: human-facing `abi-v0`, numeric version `1`
- Related ADRs read: `0001-abi-v0`, `0002-pinned-infrastructure-dependencies`,
  `0003-visual-studio-and-master-workflow`, and `0004-runtime-control-plane-v0`
- Previous handoff read: `docs/handoffs/L0/wave0.md`

The user explicitly superseded the staged Wave/contract gates on 2026-08-24
and asked for every L9 Wave to be implemented before data validation. This
branch therefore contains a private/provisional CPU model, five Compute stages,
tests, benchmark schema, and an actual Vulkan dispatch/readback harness. It
does not change or claim compatibility with a shared ABI or production
renderer integration.

## Delivered scope

The twelve L9 roadmap steps are represented in line-owned code:

1. Equal-weight and weighted Reservoir statistical tests.
2. Explicit candidate identity, proposal PDF, support, target, correction,
   light generation, origin source, reuse source, and source-surface records.
3. Unshadowed candidate target followed by exactly one final winner visibility
   evaluation.
4. Uniform-light, power-weighted, emissive-triangle area-measure, and
   environment solid-angle candidate generators.
5. Initial RIS reservoir streaming, one-time `M` cap, and basic distribution
   comparison tests.
6. Temporal surface/reprojection validation for motion, camera cut, resize,
   depth, position, normal, instance, material, scene generation, and thin
   geometry.
7. Bounded `M` and history age, plus deleted/dynamic light generation rules.
8. Spatial neighbor reuse with geometric/material/thin guards, a 30-neighbor
   bound, and strict input/output ping-pong.
9. A clearly named biased estimator.
10. A separately named validation/reference correction mode with explicit
    reference-visibility accounting; it is not presented as a complete proof
    of correlated-GRIS unbiasedness.
11. Primary-hit ReSTIR ownership mutually exclusive with conventional NEE/MIS
    and emitter-hit ownership.
12. A 100/1,000/10,000-light benchmark schema containing budgets, error state,
    visibility rays, memory estimate, GPU timing state, and checksum.

The actual Vulkan smoke dispatches:

```text
Initial -> barrier -> Temporal -> barrier
        -> Spatial (strict B/C ping-pong) -> barrier
        -> Visibility -> barrier -> Debug -> image/buffer readback
```

The reservoir/reuse equations follow the primary ReSTIR paper by
[Bitterli et al., 2020](https://cs.dartmouth.edu/~wjarosz/publications/bitterli20spatiotemporal.html).
The staged resource and ping-pong organization was cross-checked against the
official [RTXDI integration guide](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md),
but this implementation is a clean, repository-local implementation and does
not copy RTXDI source.

## Evidence

Evidence levels are intentionally independent. A pass in one section is not a
claim that a stronger section passed.

### Static evidence

- Before this handoff, all 27 implementation files were inside the L9
  whitelist: `include/restir/`, `src/restir/`, `restir/`,
  `resources/shaders/restir/`, `tests/unit/L9/`, and `docs/proposals/L9/`;
  out-of-scope count was zero.
- Six module-local `.vcxproj`/`.filters` XML files parsed successfully and all
  27 text files had zero trailing-whitespace findings.
- Five stages per configuration compiled as `cs_6_6` for Vulkan 1.3 with
  strict diagnostics and warning-as-error. Independent `spirv-val` validation
  passed all 10 Debug/Release artifacts.
- SPIR-V/host record agreement was checked for Candidate stride 80, Reservoir
  stride 112, Surface stride 48, Debug stride 64, and pairwise `float2` stride
  8.
- CPU/HLSL semantics agree for reuse provenance, temporal position checks,
  safe handling of zero/non-finite normals, rejected 32-bit `M` overflow, and
  the 30-neighbor spatial bound.
- Shared contracts, root solution/build files, and every other feature line
  remained read-only. Debug uses module-local `/Z7` to avoid compiler-PDB
  contention without changing shared MSBuild policy.

### Build evidence

The module-local test project references the private CPU library and shader
utility and runs the CPU plus actual Vulkan suite after every build. Visual
Studio 18/toolset v145, C++20, `/W4 /WX` Debug and Release Rebuilds both passed.

```powershell
& <VS18-MSBuild.exe> `
  RenderingEngine/tests/unit/L9/RenderingEngine.Restir.Tests.vcxproj `
  /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
  /p:Configuration=<Debug|Release> /p:Platform=x64
```

- Debug: 10 cases, 50,167 assertions, 0 failures.
- Release: 10 cases, 50,167 assertions, 0 failures.
- Both configurations compiled all five Compute stages and then executed the
  Vulkan smoke on the produced SPIR-V.

The root `RenderingEngine.sln` was not modified or used as L9 evidence because
composition remains L0-owned.

### Runtime evidence

Debug and Release both passed on `NVIDIA GeForce RTX 4070 Laptop GPU` with the
Khronos validation layer enabled and zero captured validation errors.

- Actual Vulkan 1.3 dispatch executed Initial, Temporal, Spatial, Visibility,
  and Debug in order with `vkCmdPipelineBarrier2` synchronization.
- Temporal and spatial outputs used distinct reservoir allocations; spatial
  B/C ping-pong was checked by the host rather than relying on shader order.
- Host uploads and readbacks covered reservoir, direct-lighting, debug, stats,
  validation-reason, and RGBA32F debug-image resources.
- Readback reported 4/4 non-empty reservoirs, four final visibility rays, four
  debug-image pixels, and deterministic checksum `291091940331195728` in both
  configurations.
- Loader/device absence is the only permitted skip path. Vulkan API,
  validation, pipeline, synchronization, dispatch, and readback failures make
  the test fail.

### Numerical evidence

- The 50,167-assertion CPU suite includes equal/weighted selection-frequency
  checks, candidate/PDF/support contracts, initial RIS, temporal/spatial
  validity, one-time `M` capping and overflow rejection, reference-mode
  labeling, and exactly-once final visibility.
- The Vulkan result proves finite, non-empty structured output and deterministic
  buffer/image readback for a four-reservoir private fixture.
- High-SPP reference comparison, 100/1k/10k-light MAE/RMSE, temporal stability,
  visibility-budget equivalence, and production-renderer comparison were not
  run. Benchmark output deliberately records `reference_error_status=NotMeasured`,
  `mean_absolute_error=NaN`, and `rmse=NaN`.

### Visual evidence

**Not run.** The RGBA32F debug image is read back and checksummed, but it was
not saved, displayed, or visually inspected. This is runtime buffer evidence,
not a visual-quality or temporal-invalidation visualization acceptance.

### Performance evidence

**Not accepted.** The 100/1,000/10,000-light benchmark tiers run and preserve
candidate/visibility budgets, memory estimates, and deterministic checksums,
but the current times are CPU harness diagnostics. `gpu_timing_status` is
`NotMeasured` and `gpu_ms=NaN`; no Vulkan timestamp, warm-up/median/p95, device
memory peak, or same-budget GPU comparison is claimed.

## Integration boundary and deferred work

- Descriptor set 5, all records, and the pre-evaluated candidate/surface/light
  inputs are L9-private. No shared ABI, descriptor registry, root project, or
  lane manifest was changed.
- Production GBuffer/motion/depth, light-distribution ownership, traversal
  visibility generation, renderer composition, dynamic-scene history, and L10
  UI/capture wiring remain L0 integration work after a shared-contract decision.
- Full data validation remains deferred at the user's request: reference image
  generation, MAE/RMSE, long temporal sequences, disocclusion visualization,
  GPU timing, memory profiling, and visual capture are not accepted here.

## Rollback

Rollback is confined to the L9 paths listed in the static-scope whitelist plus
this handoff. No shared contract, root build file, or other feature-line file
needs to be reverted.
