# L5 handoff: Wave 2 Vulkan Hardware Ray Query

> Historical lane-gate snapshot. Production-runtime wiring is superseded by
> `docs/handoffs/L0/wave2-production-runtime.md`.

- Date: 2026-08-30
- Scope: Wave 2 only (`Hardware Ray Query`)
- Contract: `abi-v1`, numeric version `2`
- Status: implementation, Debug build, real BLAS/TLAS/Ray Query runtime, parity, and validation passed; production/visual/performance acceptance remain open

## 1. Implemented scope

- Queries and validates acceleration-structure, Ray Query, deferred host
  operations, buffer-device-address, synchronization, and device limits.
- Owns BLAS/TLAS size queries, storage/scratch resources, build barriers,
  completion state, update signatures, and compaction APIs.
- Builds a real BLAS and TLAS and dispatches Compute Ray Query closest-hit and
  any-hit batches through the ABI-v1 adapter.
- Forces candidate confirmation where required so exclusive ray endpoints,
  alpha masks, one/two-sided triangles, and sampled-emitter self-ignore cannot
  be bypassed by an opaque auto-commit.
- Resolves canonical local geometry/primitive indices to stable scene IDs and
  fails closed on malformed mapping or alpha-atlas/sampler identity.
- Compares the same corpus against CPU SAH and flattened Software GPU.

Feature ownership, AS lifecycle, and inline traversal semantics were reviewed
against the official Vulkan ray-tracing guide and specification:
<https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html>,
<https://docs.vulkan.org/spec/latest/chapters/accelstructures.html>, and
<https://docs.vulkan.org/spec/latest/chapters/raytraversal.html>.

## 2. Explicitly unimplemented scope

- RT Pipeline/SBT is Wave 3 and is not part of this handoff. Existing
  lane-private comparison code and capability probe output are not Wave 2
  acceptance evidence.
- Compaction, transform-only refit, rebuild policy, and destruction state are
  implemented/tested at module level but were not exercised by the canonical
  Wave 2 Vulkan dispatch.
- No production application device/queue/scene owner dispatches Ray Query.
- Sponza, multi-instance motion, visual capture, and accepted performance runs
  were not executed.

## 3. Modified files

```text
rt/hardware/include/AccelerationStructures.hpp
rt/hardware/include/RayQueryBackend.hpp
rt/hardware/src/AccelerationStructures.cpp
rt/hardware/src/RayQueryBackend.cpp
rt/hardware/shaders/HardwareRayQuery.hlsl
rt/hardware/tests/HardwareRtTests.cpp
resources/shaders/integrators/pbr_ray_query_traversal.hlsli
tests/gpu/Wave2/RenderingEngine.Wave2TraversalGate.vcxproj
tests/gpu/Wave2/RenderingEngine.Wave2TraversalGate.vcxproj.filters
tests/gpu/Wave2/Wave2TraversalGate.cpp
docs/handoffs/L5/wave2.md
```

## 4. Contract version

The adapter implements published ABI v1 (numeric `2`) on top of unchanged
ABI-v0 scene/material records. Instance custom indices address canonical
instance records; stable IDs remain payload data. The shared contract requires
exclusive `tMin/tMax` and relative GPU hit tolerance `1e-4`.

## 5. Build evidence

The Debug `x64` central solution and dedicated Wave 2 gate built with MSBuild
18.9.1, MSVC v145, Vulkan SDK 1.4.328.1, DXC SM 6.6, `-Ges -WX`, and
`spirv-val --target-env vulkan1.3`. No Release build was requested or run.

## 6. Unit/statistical test evidence

`RenderingEngine.HardwareRT.Tests.exe --probe` returned success. Module tests
cover feature-chain ownership, function loading, resource state transitions,
update signatures, compaction preconditions, dispatch splitting, malformed
records, alpha/sidedness policy, and parity comparator tolerance/equivalent-hit
sets. Sampling statistics are owned by L6, not L5.

## 7. Vulkan runtime/validation evidence

- Physical device: NVIDIA GeForce RTX 4070 Laptop GPU.
- Probe: `rayQuery=1`, `scratchAlign=128`.
- The Wave 2 gate built and submitted BLAS/TLAS commands, dispatched closest
  and any-hit Ray Query on three canonical corpora, and matched CPU SAH and
  Software GPU under the `1e-4` relative-distance contract.
- Alpha-mask candidate confirmation matched the Software backend.
- Vulkan validation: `0` errors, `0` warnings.

## 8. Visual/capture evidence

Not run. No Cornell/Sponza Raw/Reference files, alpha-mask screenshot, or human
visual A/B was produced. Numerical image-buffer comparison belongs to L6 and
does not constitute visual acceptance.

## 9. Performance data and test conditions

Not accepted. BLAS/TLAS/Ray Query timestamp-query numbers printed by the Debug
gate are single-run diagnostics. The required 120 warm-up / 1,000 measured / 3
repeat protocol, median/p95, memory, power mode, and production resolution were
not recorded.

## 10. Known risks and rollback

- The gate currently exercises a one-instance TLAS; accepted Sponza/motion
  coverage is still required for update/refit behavior.
- Forcing non-opaque candidate confirmation prioritizes canonical semantics;
  its performance cost has not been measured.
- Roll back the listed L5 files and Ray Query branch of the Wave 2 gate. Keep
  capability publication disabled unless a production adapter is supplied.
