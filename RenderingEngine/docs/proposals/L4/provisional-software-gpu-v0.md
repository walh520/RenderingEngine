# L4 proposal: private Software GPU records and dispatch plan v0

Status: **L4-private implementation proposal; not a shared ABI**

This document records the adapter used while the shared `abi-v1` gate is
unavailable. Nothing here changes `include/contracts`, shader contracts,
descriptor ownership, the root solution, or another feature line. L0 may
replace the adapter rather than preserving these names or bindings.

## Private records

`include/rt/software_gpu/SoftwareGpu.hpp` and `software_common.hlsli` agree on:

| Record | Size | Private meaning |
|---|---:|---|
| `SoftwareNodeRecord` | 48 bytes | two `float4` bounds lanes and one `uint4` link lane |
| `SoftwarePrimitiveRecord` | 64 bytes | three position lanes and stable primitive identity |
| `SoftwareRayRecord` | 48 bytes | origin/tMin, unit direction/tMax, ray query |
| `SoftwareHitRecord` | 32 bytes | distance/barycentrics/front-face and primitive/ray/status |

The ray query lane is exactly `(rayId, queryMode, visibilityMask, reserved)`,
where query mode zero is closest-hit and one is any-hit. CPU calls reject a
separate `QueryMode` argument that disagrees with the record; Compute consumes
the record value directly. `tMax` remains exclusive. Once a closest candidate
exists, equal-distance candidates remain eligible so the lowest stable
primitive ID deterministically wins.

For a leaf, node links are `(firstPrimitive, primitiveCount, invalid, parent)`.
For an interior node they are `(leftChild, 0, rightChild, parent)`. The CPU SAH
adapter validates one root, unique parentage, no cycles, complete primitive
coverage, bounds containment, and unique stable IDs before flattening.

## LBVH definition

- Triangle AABB centroids are normalized by the supplied scene bounds.
- Each axis is clamped and quantized to ten bits, then interleaved into a
  30-bit Morton code.
- A stable four-bit LSD radix sequence sorts stable primitive ID first (eight
  passes), then Morton code (eight passes). The resulting total order is
  `(morton, stablePrimitiveId)` even for repeated centroids/Morton codes.
- The Karras prefix relation is equivalent to
  `clz64((uint64(morton) << 32 | stableId)_a XOR key_b)`; shader code evaluates
  the high and low words separately. An out-of-range pair has signed delta
  `-1`. Duplicate stable IDs are invalid input rather than a hidden index tie.
- `N=0` creates no nodes. `N=1` creates one root leaf. Otherwise the topology
  contains `N-1` interiors and `N` leaves.
- CPU bounds refit first validates root zero, root-parent invalid, reciprocal
  child/parent links, unique parentage, acyclicity, and full connectivity, then
  uses a two-child arrival rule. The Compute path validates every parent chain
  terminates at root zero and uses explicit depth passes, avoiding an invalid assumption that
  `InterlockedAdd` or a group barrier is a device-wide memory fence.

The implementation follows the equations and invariants in Tero Karras,
"Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees"
and the associated NVIDIA tree-construction article, without copying their
source code:

- <https://research.nvidia.com/publication/2012-06_maximizing-parallelism-construction-bvhs-octrees-and-k-d-trees>
- <https://developer.nvidia.com/blog/thinking-parallel-part-iii-tree-construction-gpu/>

## Provisional Compute dispatch order

All resources currently use private set 2 bindings. Integration must remap
them after L0 publishes the traversal contract.

1. If `N=0`, publish an empty root and skip every LBVH dispatch.
2. Dispatch `software_lbvh_morton.hlsl::ResetMortonValidationCS`, barrier, then
   `CSMain` for triangle AABB centroids.
   Zero extent on an axis maps every centroid on that axis to quantized 512.
   Reset and check the invalid-Morton counter; non-finite centers/bounds fail
   the build rather than silently publishing code zero.
3. For stable-ID shifts `0..28`, dispatch
   `HistogramCS`, barrier, `PrefixCS`, barrier, `ScatterCS`, barrier, and
   ping-pong the pair buffers. Dispatch `ResetStableIdValidationCS`, barrier,
   then `ValidateStableIdsCS`; read the counter and abort if nonzero. This
   globally rejects equal stable IDs while they are adjacent, even when their
   Morton codes differ. Repeat the radix sequence for Morton shifts `0..28`.
   Every pass is stable across ordered workgroups.
4. Dispatch hierarchy `ResetCS`, barrier, then `HierarchyCS`, barrier.
   `ResetCS` resets the invalid-hierarchy counter and all parent slots. `N=1`
   skips `HierarchyCS`. Read the counter and abort if nonzero; atomic
   compare/exchange parent publication also rejects multiple parents.
5. Dispatch `ResetBoundsValidationCS`, barrier, `EmitLeavesCS`, barrier, then
   `ComputeDepthsCS`, barrier. The reset clears both maximum depth and the
   bounds-validation counter. Read the invalid counter and abort before refit.
6. Dispatch `InternalBoundsCS` from deepest interior depth down to depth zero.
   A Vulkan buffer barrier with compute-shader write source access and
   compute-shader read destination access is mandatory between every depth.
   `GroupMemoryBarrier*` is not a substitute for this device-visible barrier.
7. Dispatch `software_trace.hlsl::ResetCountersCS`, barrier, then `CSMain`, and
   barrier before readback.

The nine counter slots are node tests, triangle tests, stack overflow, invalid
ray, invalid hit, maximum stack depth, leaf visits, accumulated leaf
primitives, and maximum leaf occupancy. These indices also exist as the
L4-private C++ `GpuTraversalCounterSlot` layout. Additive shader counters
saturate at `UINT32_MAX` rather than wrapping; maximum slots use atomic max.
A bounded 64-entry shader stack fails
the ray explicitly on overflow; it never returns the partial closest hit.
Traversal also caps popped nodes at `nodeCount`, so a malformed cyclic child
link fails instead of hanging a CPU thread or causing a GPU TDR.

## Benchmark schema and proof boundary

`tests/gpu/L4/RenderingEngine.SoftwareGpu.VulkanSmoke.vcxproj` is the private
runtime path. It creates a Vulkan 1.3 compute device, enables
`VK_LAYER_KHRONOS_validation` when available, uploads a flattened CPU SAH,
executes its trace shader, then executes the complete Morton/radix/Karras/
depth-bounds/trace LBVH sequence. Fixed hit records are read back and compared
with the CPU brute-force oracle by status, stable primitive ID, distance, and
barycentrics. It also runs duplicate-stable-ID and disconnected-parent negative
preflights and fails on any validation error. A runtime without a suitable
Vulkan device is reported as skipped rather than fabricated evidence.

The executable emits `l4-software-gpu-benchmark-v0-private`. Its measured
fields are explicitly `measurementDomain = cpu-mirror` and
`gpuMeasured = false`. The reserved GPU report input is:

```text
measured
buildGpuMilliseconds
traceGpuMilliseconds
uploadBytes
readbackBytes
tracedRays
```

Only Vulkan timestamp queries/readback may set `measured=true`. CPU wall-clock
times or successful SPIR-V validation must never populate GPU timing fields.
This private module supplies algorithm, build, CPU runtime/numerical,
shader-legality, Vulkan execution, validation-layer, and GPU readback parity
evidence. GPU timestamp performance measurements, image output, visual
acceptance, and production/shared-ABI integration remain separate evidence.
