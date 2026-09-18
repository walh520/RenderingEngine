# Wave 3 integration and acceptance contract

Wave 3 consists of L4 GPU LBVH, L5 RT Pipeline/SBT, L7 Wavefront, and L8
Temporal/SVGF. Source composition, build success, GPU execution, validation,
numerical acceptance, visual acceptance, and performance acceptance are
independent evidence levels. Passing one never implies a later level.

## Published boundary

- Human ABI: `abi-v2`.
- Numeric ABI: `3`.
- Parent traversal queues: `abi-v1`, numeric `2`.
- Set 3: Wavefront queues/output/profiler, append-only after bindings 0-4.
- Set 4: primary surface, motion, split signal, GBuffer, history, variance,
  A-Trous ping-pong, compose output, and pass constants.
- Set 5 remains reserved. No abi-v3/ReSTIR record is published by Wave 3.

## Frame transaction

`BuildFramePlan` validates a complete 1-SPP RuntimeConfig tuple, provider
availability, history identity, frames-in-flight allocation, and pass order.
It preserves the Megakernel baseline when a convergence comparison is
requested. History uses two logical generations across every flight slot and
never aliases the exact previous-frame read resource.

`ExecuteFrameTransaction` rebuilds the canonical plan from the immutable
request and rejects any drift. It executes providers in order, aborts on the
first failure or non-zero required fault counter, and accepts timing only from
the exact provider-reported Vulkan timestamp query pair. CPU wall time and
synthetic values are explicit rejected provenance. Presentation is intentionally
unmeasured because `vkQueuePresentKHR` is not a command-buffer timestamp scope.

The `PublishHistory` pass prepares the write generation. Its identity is not
committed until every scheduled pass, including present, succeeds. A provider
exception or failed final commit produces a failed transaction and never a
successful evidence record.

## Immutable comparison identity

Every convergence, reconstruction, and backend profile record includes:

- scene, camera, asset, and reference fingerprints;
- backend, estimator, and light proposal;
- base seed and config/scene/resource generations;
- resolution, SPP, maximum path length, and ray budget where applicable.

Megakernel and Wavefront must use the same identity, SPP checkpoints, and ray
budget. Thresholds have no guessed default: final RMSE and relative-delta
limits require a named frozen baseline or accepted ADR.

## Required integration gates

1. Megakernel/Wavefront convergence: at least the policy-required checkpoint
   count, matching identities/budgets, monotonic SPP, zero validation/queue/
   stack/drop/NaN/PDF/hit faults, passed indirect-dispatch validation,
   per-bounce active-path telemetry, and retained Megakernel baseline.
2. Reconstruction: one immutable 1-SPP input must retain Raw, Temporal-only,
   fixed A-Trous-only, and SVGF outputs, each with a high-SPP reference,
   four unique output artifact paths, RMSE, and PSNR.
3. Backend profiling: both GPU LBVH and RT Pipeline must record build and trace
   scopes separately, workload/fault counters, and canonical 120-frame warmup,
   1,000 measured frames, three repeats, median, and p95 from provider GPU
   timestamp queries. Accepted warmup and measurement totals must prove the
   complete sequence, not merely provide 3,000 measurement-shaped samples.

`ComposeWave3Gate` passes only if all four results pass. Test fixtures exercise
the gate logic but are never accepted as runtime evidence.

## Current shadow evidence boundary

- L4 GPU LBVH and L5 RT Pipeline have real-device fixed-corpus smoke/parity
  evidence with Vulkan validation; their printed single-run timings are
  diagnostics, not canonical performance acceptance.
- L7 and L8 have C++ tests plus DXC/SPIR-V validation. Their full Vulkan runtime,
  dynamic-scene visual, convergence, and performance gates are not run.
- Therefore the combined Wave 3 gate remains not accepted, and CapabilityTable
  must stay fail-closed for Wave 3 production tuples.
