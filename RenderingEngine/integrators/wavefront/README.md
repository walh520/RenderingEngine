# L7 GPU Wavefront path tracer

This directory owns the Wave 3 Wavefront stages, queue/compaction contracts,
indirect-dispatch schedule, profiler decoding, shaders, and deterministic CPU
oracles. The production application does not publish this integrator until a
Vulkan provider executes the complete schedule and passes the Wave 3 gate.

## Shared and private contracts

Set 3 bindings 0-4 preserve the published abi-v1 queue records. The Ray A,
Hit, and Shadow records remain binary-identical to the Megakernel/traversal
boundary. Abi-v2 adds append-only bindings for Ray B, extended path state,
flags, scan scratch, indirect commands, profiler output, split radiance AOVs,
and the Shadow AOV sidecar. Material work and capacity policy stay L7-private.

The RNG dimension map is shared with L6. Megakernel remains the immutable
comparison baseline and is never removed by selecting Wavefront.

## Pass graph

`BuildPreparedFramePlan` produces the exact frame reset, RayGen, Intersect,
Shade, hierarchical scan/add/scatter, TraceShadow, NextBounce, Resolve, and
optional counter-visualization sequence. It also assigns queue identities,
scan offsets, direct/indirect dispatch slots, barriers, and timestamp queries.

Atomic append is the minimum-correct path. Prefix-scan compaction is the
explicit alternative. Overflow sets fatal state and cannot silently continue.
Per-bounce active paths, occupancy, dispatch counts, and provider GPU time are
part of the acceptance payload.

## Proof boundary

The local project compiles and validates all production shaders plus Software
and Ray Query adapter probes, and runs CPU schedule/compaction/profiler oracles.
It does not submit the full Wavefront graph to a Vulkan device. Consequently,
queue synchronization under validation, GPU convergence against Megakernel,
and performance remain not run until a real provider supplies those records.
