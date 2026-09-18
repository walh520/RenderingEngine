# L5 handoff: Wave 3 RT Pipeline and SBT

- Date: 2026-08-31
- Contract: shared abi-v1 Ray/Hit queues under abi-v2 composition
- Status: implementation, module tests, shader validation, real RT Pipeline
  dispatch, fixed-corpus parity, and Vulkan validation passed in Debug;
  production and canonical performance acceptance remain open

## Implemented

- Raygen, miss, closest-hit, and any-hit shader stages with a 32-byte payload.
- Shader groups and SBT handle extraction with device-reported handle/stride/
  base alignment and optional inline records.
- One-level trace dispatch split by device limits and independent Ray/Hit queue
  offsets.
- The same scene constants, geometry/material data, visibility masks, open ray
  intervals, alpha-mask candidate policy, sidedness, stable identity, miss, and
  invalid encoding used by Ray Query.
- RT Pipeline feature/property discovery and required function loading without
  weakening the stable Ray Query main path.

The combined traversal gate built a real BLAS/TLAS/SBT and dispatched closest
and any-hit RT Pipeline rays on NVIDIA GeForce RTX 4070 Laptop GPU. Canonical
triangle, Cornell, and alpha-mask corpora matched CPU SAH and Software GPU.
Vulkan validation reported `0` errors / `0` warnings. The last recorded
Release single-run RT Pipeline trace diagnostics were `0.0625 ms`, `0.1321 ms`,
and `0.0668 ms` for those three corpora.

Those values are smoke timings, not accepted performance results. The required
build/trace 120/1000/3 cadence, median/p95, memory, power state, and production
workload identity were not recorded.

## Open evidence

- production Vulkan device/scene/AS/SBT owner;
- multi-instance rigid update/refit/rebuild runtime coverage;
- saved visual comparison and canonical Software/HW backend profile.

Capability publication remains fail-closed.
