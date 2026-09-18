# L4 handoff: Wave 2 flattened Software GPU

> Historical lane-gate snapshot. Production-runtime wiring is superseded by
> `docs/handoffs/L0/wave2-production-runtime.md`.

- Date: 2026-08-30
- Scope: Wave 2 only (`Flattened Software GPU`)
- Contract: `abi-v1`, numeric version `2`
- Status: implementation, Debug build, fixed-corpus GPU runtime, and Vulkan validation passed; visual and performance acceptance remain open

## 1. Implemented scope

- Converts the canonical L3 SAH BVH to explicit GPU node/triangle records while
  preserving stable instance, primitive, geometry, and material identities.
- Uploads that flattened SAH representation and records closest-hit and any-hit
  Compute traversal through the ABI-v1 traversal backend.
- Uses a bounded stack in the standalone traversal kernel, reports overflow and
  malformed input, and exposes node/triangle/leaf/stack counters.
- Applies exclusive ray bounds, visibility masks, one/two-sided behavior, and
  alpha-mask confirmation consistently with the CPU and Ray Query gates.
- Supplies the L6 Megakernel with a stackless parent-pointer traversal adapter;
  this changes traversal mechanics, not integrator semantics.
- Runs the same generated ray corpus as CPU SAH and Hardware Ray Query on the
  canonical triangle, Cornell, and synthetic alpha-mask scenes.

The flattening/traversal review follows PBRT's BVH construction and traversal
invariants: <https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies>.

## 2. Explicitly unimplemented scope

- GPU LBVH is Wave 3 and is not part of this handoff. Older lane-local LBVH
  source/build output may exist in this worktree, but it is not Wave 2 evidence.
- No production renderer selects or dispatches the flattened backend.
- No fixed-license/hash Sponza asset has been accepted or rendered.
- No million-ray GPU corpus, resize/scene-switch production smoke, visual
  capture, or accepted performance run was executed.

## 3. Modified files

```text
include/rt/software_gpu/SoftwareGpu.hpp
include/rt/software_gpu/SoftwareGpuBackend.hpp
src/rt/software_gpu/SoftwareGpu.cpp
src/rt/software_gpu/SoftwareGpuBackend.cpp
resources/shaders/traversal/software_trace_v1.hlsl
rt/software_gpu/RenderingEngine.SoftwareGpu.vcxproj
rt/software_gpu/RenderingEngine.SoftwareGpu.vcxproj.filters
tests/gpu/L4/TestMain.cpp
tests/gpu/Wave2/RenderingEngine.Wave2TraversalGate.vcxproj
tests/gpu/Wave2/RenderingEngine.Wave2TraversalGate.vcxproj.filters
tests/gpu/Wave2/Wave2TraversalGate.cpp
docs/handoffs/L4/wave2.md
```

## 4. Contract version

The backend consumes published ABI v1 (numeric `2`) and the unchanged ABI-v0
scene records. Ray bounds are open, miss/invalid encodings and stable IDs are
shared, and traversal set 2 remains backend-owned. No ABI-v2 publication is
claimed.

## 5. Build evidence

Debug `x64` central solution build passed with MSBuild 18.9.1 / MSVC v145,
C++20, warning-as-error policy. The dedicated Wave 2 project also built and ran
successfully. No Release configuration was built for this milestone.

## 6. Unit/statistical test evidence

- `RenderingEngine.SoftwareGpu.Tests.exe`: 9/9 groups, 197,176 assertions.
- L3 CPU reference: 1,000,000 rays, zero hit/miss mismatch, zero ID mismatch,
  zero invalid result, maximum relative `t` error `0`.
- The standalone L4 fixed fixture reported overflow `0`, invalid-hit `0`, and
  exactly one deliberately injected invalid ray.
- These are traversal tests; no sampling-statistics claim belongs to L4.

## 7. Vulkan runtime/validation evidence

`RenderingEngine.Wave2TraversalGate.exe` passed on NVIDIA GeForce RTX 4070
Laptop GPU. The three scene corpora contained 264/262/262 rays. CPU SAH,
flattened Software GPU, and Ray Query agreed on hit/miss and non-ambiguous
stable identity with the required relative `t <= 1e-4`. Software traversal
reported stack overflow `0`, invalid hit `0`, and Vulkan validation reported
`0` errors and `0` warnings.

## 8. Visual/capture evidence

Not run. The 16 x 16 floating-point readbacks used by the L6 parity gate are
numerical test buffers, not saved Raw/Reference captures or human visual
acceptance. Sponza was not available.

## 9. Performance data and test conditions

Not accepted. Timestamp-query intervals printed by the Debug gate are
diagnostics only: there was no 120-frame warm-up, 1,000-frame measurement,
three-repeat median/p95 protocol, fixed power-mode record, or memory report.
No throughput target is claimed.

## 10. Known risks and rollback

- The production application has no canonical-scene upload/dispatch owner for
  this backend; capability publication remains fail-closed.
- The dedicated gate currently builds a one-instance TLAS. The L6 emitter map
  is instance-aware, but multi-instance end-to-end traversal still needs a
  future accepted asset/corpus.
- Roll back the L4 files above plus the Software branch of the shared Wave 2
  gate. ABI-v1 records do not need to be removed to disable this provider.
