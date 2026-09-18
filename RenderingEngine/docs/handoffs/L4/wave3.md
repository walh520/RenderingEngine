# L4 handoff: Wave 3 GPU LBVH

- Date: 2026-08-31
- Contract: abi-v1 traversal queues under the abi-v2 Wave 3 composition
- Status: implementation, CPU tests, shader validation, real GPU build/trace
  smoke, fixed-corpus parity, and Vulkan validation passed in Debug; canonical
  performance acceptance remains open

## Implemented

- Deterministic Morton encoding with zero-size-bound handling and stable-ID
  tie-breaks for duplicate keys.
- Stable GPU radix histogram/prefix/scatter passes.
- Karras binary radix hierarchy plus bottom-up bounds, with empty/single/
  repeated-centroid and malformed-hierarchy coverage.
- GPU LBVH closest/any traversal using the same stable identity and open ray
  interval semantics as CPU SAH, flattened SAH, and Hardware RT.
- Explicit invalid/overflow counters, memory accounting, and separate provider
  GPU timestamp measurements for build and trace.

The Release standalone Vulkan smoke on NVIDIA GeForce RTX 4070 Laptop GPU
reported validation `0` errors / `0` warnings. Its final diagnostic sample was
GPU LBVH build `0.27456 ms` and trace `0.033504 ms`; both came from Vulkan
timestamp queries. Flattened SAH trace was `0.031488 ms`; its CPU build was
correctly marked as not GPU-timestamped.

These are single-run smoke diagnostics. They do not satisfy the required
120 warmup / 1,000 measured / three-repeat median-p95 profile, and no throughput
or superiority claim follows from them.

## Open evidence

- canonical Software/HW backend profile cadence and memory report;
- production scene/upload provider and dynamic-scene rebuild policy in the app;
- saved visual comparisons and portfolio-resolution performance.

Capability publication remains fail-closed.
