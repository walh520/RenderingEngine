# L7 handoff: Wave 3 GPU Wavefront path tracer

- Date: 2026-08-31
- Contract: abi-v1 queue prefix plus append-only abi-v2 set 3
- Status: source, central composition, CPU oracles, and shader validation
  complete; full Vulkan dispatch and convergence acceptance not run

## Implemented

- RayGen, Intersect, Shade, TraceShadow, NextBounce, Resolve, and counter debug
  stages.
- Atomic append and hierarchical prefix-scan/add/scatter compaction paths.
- Indirect command preparation, explicit compute/indirect/AS/fragment barrier
  requirements, ping-pong Ray queues, and fail-stop overflow state.
- Software GPU and Ray Query traversal adapters over the same shared records.
- L6-compatible RNG dimension mapping, split direct/indirect diffuse/specular
  output, and same-index Shadow AOV sidecar.
- Per-bounce active paths, queue occupancy, dispatch count, and Vulkan timestamp
  decoding contracts.
- A prepared frame plan that derives capacity, scan offsets, dispatch slots,
  barriers, constants, and timestamps from one immutable frame record.

All fourteen production/probe shaders compile as SM 6.6 SPIR-V for Vulkan 1.3
and pass `spirv-val`; CPU schedule, compaction, overflow, RNG, and profiler
oracles pass. These facts do not show that the full pass graph was submitted to
a GPU.

## Open integration gate

- full Vulkan command/resource provider and synchronization validation;
- same scene/seed/SPP/ray-budget GPU comparison against retained Megakernel;
- accepted convergence checkpoints, per-bounce live telemetry, saved outputs,
  and canonical performance cadence.

Until those records exist, Wavefront production capability and the combined
Wave 3 gate remain not accepted.
