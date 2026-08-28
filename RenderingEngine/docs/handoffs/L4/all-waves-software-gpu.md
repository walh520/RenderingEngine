# L4 handoff: all-wave Software GPU RT implementation

- Status: **Line-local implementation complete; shared integration and full data gates deferred**
- Date: 2026-08-24
- Branch: `codex/rt-software-gpu`
- Worktree baseline: `47fa2dce03b348d64b51f8b164c033a9deb2c043`
- Contract observed before implementation: human-facing `abi-v0`, numeric version `1`
- Related ADRs read: `0001-abi-v0`, `0002-pinned-infrastructure-dependencies`,
  `0003-visual-studio-and-master-workflow`, and `0004-runtime-control-plane-v0`
- Previous handoffs read: `docs/handoffs/L0/wave0.md` and
  `docs/handoffs/L3/wave1.md`

The user explicitly superseded the staged Wave gates on 2026-08-24 and asked
for every L4 Wave to be implemented before shared-contract and data validation.
Accordingly, this branch implements private/provisional records, descriptor
bindings, projects, shaders, tests, and a real Vulkan execution harness. It
does not edit or claim compatibility with the shared ABI, root solution/build
files, or another feature line.

## Delivered scope

The nine L4 roadmap steps are represented in line-owned code:

1. Explicit GPU-friendly node, primitive, ray, hit, dispatch, and counter
   records, including compile-time layout checks and a CPU-SAH flattening
   adapter.
2. A Vulkan upload path and actual closest-hit/any-hit Compute traversal over
   flattened SAH data.
3. Bounded traversal stack, traversal budget, malformed-input handling, and
   explicit overflow/invalid reporting.
4. Fixed GPU-ray dispatch, readback, and CPU stable-ID, distance, and
   barycentric parity checks.
5. Node/triangle tests, stack depth, leaf visits, primitive occupancy, and
   invalid/overflow counters with a private nine-slot GPU readback contract.
6. Scene-normalized Morton encoding, stable 16-pass radix sorting over
   `(Morton, stablePrimitiveId)`, and a global duplicate stable-ID pass.
7. Karras binary-radix topology, strict parent/root/connectivity validation,
   depth construction, and per-depth bottom-up bounds with explicit barriers.
8. Empty/single-primitive inputs, zero extents, duplicate centroids, duplicate
   IDs, non-finite rays, malformed trees, and disconnected-forest negatives.
9. Separate SAH/LBVH benchmark schema fields for build, trace, memory,
   counters, throughput, upload/readback, and GPU timing measurement state.

The actual Vulkan chain for GPU LBVH is:

```text
reset -> Morton -> 16 x (histogram -> prefix -> scatter)
      -> stable-ID validation -> Karras hierarchy -> leaves
      -> depth construction -> per-depth bounds/barriers -> trace -> readback
```

The implementation follows the binary radix-tree construction described by
[Karras, 2012](https://research.nvidia.com/publication/2012-06_maximizing-parallelism-construction-bvhs-octrees-and-k-d-trees),
with an additional stable-ID key lane and explicit failure paths required by
this repository.

## Evidence

Evidence levels are intentionally independent. A pass in one section is not a
claim that a stronger section passed.

### Static evidence

- Before this handoff, all 23 implementation files were inside the L4
  whitelist: `include/rt/software_gpu/`, `src/rt/software_gpu/`,
  `rt/software_gpu/`, `resources/shaders/traversal/software_*`,
  `tests/gpu/L4/`, and `docs/proposals/L4/`; out-of-scope count was zero.
- Eight module-local `.vcxproj`/`.filters` XML files parsed successfully, four
  project GUIDs were unique, the PowerShell validator parsed, and all 23 text
  files had zero trailing-whitespace findings.
- Debug and Release each produced 15 Vulkan 1.3 Compute SPIR-V modules with
  `cs_6_6`, strict HLSL diagnostics, and warning-as-error. Independent
  `spirv-val --target-env vulkan1.3` validation passed 30/30 artifacts.
- Reflected layouts agree with the host: Node offsets 0/16/32 and stride 48;
  Primitive offsets 0/16/32/48 and stride 64; Ray offsets 0/16/32 and stride
  48; Hit offsets 0/16 and stride 32.
- Shared contracts, root solution/build files, the L3 worktree, and every other
  line remained read-only. L4 Debug projects use `/Z7` locally to avoid shared
  compiler-PDB contention; no shared MSBuild policy was changed.

### Build evidence

The module-local CPU/shader test, Vulkan smoke, and benchmark projects rebuilt
serially with Visual Studio 18, toolset v145, C++20, `/W4 /WX`, in both Debug
and Release. The final standard commands completed with zero compile errors;
the accepted agent runs and the final no-override reruns produced no compiler
warnings.

```powershell
& <VS18-MSBuild.exe> `
  RenderingEngine/tests/gpu/L4/RenderingEngine.SoftwareGpu.Tests.vcxproj `
  /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
  /p:Configuration=<Debug|Release> /p:Platform=x64

& <VS18-MSBuild.exe> `
  RenderingEngine/tests/gpu/L4/RenderingEngine.SoftwareGpu.VulkanSmoke.vcxproj `
  /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
  /p:Configuration=<Debug|Release> /p:Platform=x64

& <VS18-MSBuild.exe> `
  RenderingEngine/rt/software_gpu/RenderingEngine.SoftwareGpu.Benchmark.vcxproj `
  /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
  /p:Configuration=<Debug|Release> /p:Platform=x64
```

- Debug CPU/shader tests: 8/8 groups, 197,007 assertions.
- Release CPU/shader tests: 8/8 groups, 197,007 assertions.
- Each Vulkan build compiled/validated 15 shader entrypoints and then ran its
  executable as an `AfterTargets=Build` gate.

The root `RenderingEngine.sln` was not modified or used as L4 evidence because
composition remains L0-owned.

### Runtime evidence

Both Debug and Release actual Vulkan runs passed on
`NVIDIA GeForce RTX 4070 Laptop GPU` with Khronos validation and synchronization
validation enabled, reporting zero validation warnings and zero errors.

| Path | Rays / hits | Node / triangle tests | Stack / invalid | Depth | Transfer |
|---|---:|---:|---:|---:|---:|
| Flattened CPU-SAH upload | 11 / 9 | 73 / 13 | overflow 0, invalid ray/hit 0 | 4 | upload 1760 B, readback 388 B |
| Full GPU LBVH build + trace | 11 / 9 | 75 / 13 | overflow 0, invalid ray/hit 0 | 4 | upload 1040 B, readback 528 B |

Both paths reported maximum stack depth 3, 13 leaf visits, 13 accumulated leaf
primitives, and maximum leaf occupancy 1. Duplicate stable IDs at different
Morton positions and a malformed disconnected forest were also dispatched as
negative GPU preflights and triggered their expected counters.

### Numerical evidence

- The fixed 11-ray corpus passed CPU versus GPU hit/miss, stable primitive ID,
  hit distance, and barycentric tolerance checks for both flattened SAH and
  GPU LBVH.
- GPU Morton readback matched the complete CPU `(Morton, stable ID)` order.
- The broader L3 million-ray corpus, CPU/HW/Software cross-backend corpus,
  image convergence, and RMSE/PSNR comparisons were not run in this branch.
  Those are explicitly deferred data gates, so this handoff claims only the
  fixed-corpus numerical smoke above.

### Visual evidence

**Not run.** The Vulkan harness is headless and reads structured buffers; it
does not render, capture, or inspect Cornell/Sponza images. No visual-quality,
image-parity, or presentation claim is made.

### Performance evidence

**Not accepted.** The benchmark executable runs and emits the private schema,
but its current measured domain is explicitly `cpu-mirror` and
`gpuMeasured=false`. A 256-primitive/4096-ray/two-iteration smoke produced
matching SAH/LBVH checksums (`18120791840910874053`) with zero overflow or
invalid results. Its wall times and derived Mrays/s are harness diagnostics,
not GPU performance evidence. Vulkan timestamp measurements remain disabled.

## Integration boundary and deferred work

- The L4 records and descriptor usage are private/provisional; no shared
  contract version, descriptor registry, root solution, or lane manifest was
  changed.
- Production renderer selection, canonical L2 scene upload, L3 fixture
  consumption, L5 hardware parity, Cornell/Sponza rendering, and L10 UI/capture
  composition remain integration work for L0 after a shared-contract decision.
- Full data validation remains deferred at the user's request: larger parity
  corpora, tolerance reports, GPU timestamp build/trace timings, peak device
  memory, warm-up/median/p95, and visual captures are not accepted here.

## Rollback

Rollback is confined to the L4 paths listed in the static-scope whitelist plus
this handoff. No shared contract, root build file, or other feature-line file
needs to be reverted.
