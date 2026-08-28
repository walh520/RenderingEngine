# L7 handoff: complete private GPU wavefront implementation

- Status: Code complete; GPU/data acceptance deferred by request
- Date: 2026-08-27
- Baseline checkpoint: `47fa2dc`
- Branch: `codex/rt-pbr-wavefront`
- Contract decision: the user explicitly asked to land the private code before
  contract integration. No shared contract, root solution, root build file, or
  other lane was modified.

This handoff records the complete L7-private wavefront implementation. Real
Vulkan scheduling, traversal, convergence, and visual evidence remain clearly
outside the proof supplied by the lane-local build and CPU oracle.

## Delivered scope

### Queue model, schedule, and compaction

- Private C++/HLSL records cover path, material, shadow, counter, indirect
  command, scan, and pass constants with C++ size/alignment assertions.
- RayGen, Intersect, Shade, TraceShadow, NextBounce, Resolve, Reset,
  PrepareDispatch, Scan, AddOffsets, Scatter, and counter-visualization stages
  are implemented as separate compute entry points.
- Atomic append supplies the minimum dense-queue path. Blelloch-style scan,
  recursive block offsets, inactive-tail masking, and stable scatter supply the
  deterministic prefix-compaction path.
- Indirect dispatch uses overflow-safe two-dimensional splitting. The prepared
  plan derives extent, capacity, bounce count, queue mode, and dispatch limits
  from the same bound frame constants to prevent plan/frame drift.
- The schedule exposes transfer-to-compute, acceleration-structure-to-compute,
  inter-pass, indirect-command, and resolve-to-fragment barrier requirements.
  Invalid queue modes, zero depth, invalid capacity/limits, timestamp overflow,
  and indirect-command overflow are rejected.

### Path tracing and safety behavior

- L7 consumes the same private L6 material/BSDF/light sampling and Philox
  dimension mapping used by the megakernel path.
- Camera emission, NEE/MIS, emitter/environment hit MIS, multiple bounce,
  Russian roulette, Beer attenuation, eta-squared transport, and split AOVs are
  present in the stage pipeline.
- Queue overflow sets fatal bits, prevents the out-of-bounds write, and zeros
  future indirect work instead of continuing with corrupt queues.
- Next-bounce and resolve stages validate all candidate state and convert
  invalid terminal accumulation to an explicit failure color/counter rather
  than silently propagating NaN/Inf.
- Per-bounce active paths, queue occupancy, dispatch count, fatal counters, and
  a counter-visualization stage are exposed for later runtime capture.

### Traversal adapters and profiler

- Fixture traversal is usable by the private stages. Software GPU and Ray Query
  adapter seams plus compile probes are present without importing L4/L5 files.
  Those probes prove interface legality only, not real backend traversal.
- The profiler owns timestamp query counts, command recording through
  `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`, availability-aware readback,
  valid-bit wrap handling, and timestamp-period conversion to GPU milliseconds.

## Evidence

| Evidence layer | Result | Proof boundary |
|---|---|---|
| Static | Passed | 36 L7-private source/project files; project XML parses; no trailing whitespace, TODO/FIXME placeholder, out-of-scope path, or generated object outside the lane build directory was found |
| Build | Passed | `Wavefront.Tests.vcxproj` Debug and Release full rebuilds completed with MSVC C++20 `/W4 /WX`, Vulkan SDK linkage, 0 warnings and 0 errors |
| Shader build | Passed | 12 production stages plus Software/Ray Query adapter probes compiled with DXC SM 6.6, `-Ges -WX`, Vulkan 1.3 target and DX layout; all 14 Debug and 14 Release SPIR-V outputs passed `spirv-val` |
| Runtime | CPU self-test only | Debug and Release `Wavefront.Tests.exe` returned 0. The Vulkan timestamp helpers compiled and linked, but no Vulkan device, command buffer submission, queue execution, validation layer, query-pool readback, or real traversal ran |
| Numerical | CPU oracle invariants passed | Stable compaction boundaries, inactive-tail behavior, dispatch splitting, queue overflow simulation, frame/schedule rejection, Philox dimension parity, query availability, timestamp conversion, and valid-bit wrap passed. No GPU convergence or megakernel image parity was measured |
| Visual | Not run | Counter visualization compiled but was not dispatched or captured; no rendered Wavefront image was compared with Megakernel |

## Acceptance commands used

```powershell
& <MSBuild.exe> RenderingEngine\integrators\wavefront\Wavefront.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> RenderingEngine\integrators\wavefront\Wavefront.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=x64

.\RenderingEngine\integrators\wavefront\build\x64\Debug\Wavefront.Tests.exe
.\RenderingEngine\integrators\wavefront\build\x64\Release\Wavefront.Tests.exe
```

## Explicitly deferred

- Binding private queue records and descriptors to the current shared ABI and
  composing the project into the root build graph;
- actual Vulkan dispatch, synchronization validation, queue reset, indirect
  dispatch, and timestamp-query execution;
- real L4 Software GPU and L5 Ray Query traversal adapter binding;
- proving queue-overflow counters remain zero on production scenes;
- identical scene/seed/SPP/ray-budget convergence against Megakernel;
- GPU Sample/Evaluate/PDF readback, RMSE/PSNR, performance, and GPU-ms capture;
- counter-view and final-image visual acceptance;
- SoA/AoS changes, material sorting, or persistent-thread optimization. These
  remain evaluation items and were intentionally not implemented early.

These items are deferred evidence/integration work, not silently accepted
gates. The lane code should not be merged as a runtime-accepted Wavefront
renderer until they are run.

## Algorithm and API references

- Laine, Karras, and Aila, *Megakernels Considered Harmful: Wavefront Path
  Tracing on GPUs*:
  https://research.nvidia.com/sites/default/files/pubs/2013-07_Megakernels-Considered-Harmful/laine2013hpg_paper.pdf
- Blelloch, *Prefix Sums and Their Applications*:
  https://www.cs.cmu.edu/afs/cs.cmu.edu/project/scandal/public/papers/CMU-CS-90-190.html
- Khronos Vulkan synchronization examples:
  https://docs.vulkan.org/guide/latest/synchronization_examples.html
- Khronos Vulkan compute-shader guide:
  https://docs.vulkan.org/guide/latest/compute_shaders.html
