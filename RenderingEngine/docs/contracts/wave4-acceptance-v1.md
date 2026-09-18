# Wave 4 acceptance v1

Status: fail-closed evidence contract for Many Lights and ReSTIR DI.

## Required comparison

Each immutable 100, 1,000, and 10,000-light tier is evaluated at one fixed
scene, camera, asset, seed, extent, frame, and generation identity. The primary
comparison contains four technique families:

1. uniform one-light;
2. power-weighted one-light;
3. ReSTIR DI, selecting exactly one explicitly named biased or
   unbiased-reference mode per comparison;
4. high-SPP CPU reference.

The three real-time families must use identical candidate and visibility
budgets within a tier and one common real-time budget across all tiers. The
high-SPP reference has its own explicit budget identity.
The aggregate gate therefore consumes exactly twelve records: four legs for
each of the three tiers. A second ReSTIR bias record cannot replace the
high-SPP leg.

Each record carries a closed provider/provenance identity and explicit
freshness. Synthetic or stale records remain `not-run`; mismatched provider
identity is rejected.

Scene, asset, configuration, machine, driver, power profile, camera, extent,
seed, frame index, frame generation, and config/scene/resource/light
generations are all part of the comparison identity. Provider-observed
candidate counts must equal the declared count; observed visibility rays may
be lower but never exceed their budget. The
three realtime legs carry finite MAE/RMSE/PSNR
against the retained high-SPP oracle; the oracle's own quality field is null.

Realtime duration provenance is a Vulkan GPU timestamp. The high-SPP CPU
oracle uses CPU wall-clock provenance; requiring GPU timing never relabels the
CPU reference.
Realtime performance evidence uses 120 warm-up frames, 1,000 measurement
frames, and at least three repeats. The CPU oracle carries its own explicit
non-zero cadence. Measured memory must be non-zero.

## Frame requirements

The declarative order is:

```text
primary surface
  -> stable current/previous light mapping
  -> candidate generation
  -> initial reservoir
  -> optional temporal reuse
  -> optional spatial reuse
  -> optional reference-visibility validation
  -> exactly one final-winner visibility query
  -> split direct diffuse/specular publication
  -> reconstruction
  -> history publication
  -> optional debug projection
```

Primary-hit ReSTIR and conventional NEE/MIS may not both own direct lighting.
Reference visibility is a separately budgeted validation mode and is not a
claim of a complete correlated-GRIS proof.

## Evidence levels

Evidence is independent and never promoted implicitly:

| Level | Minimum evidence |
|---|---|
| Static | C++/HLSL layout agreement, descriptor registry, source ownership and dependency audit |
| Build | registered Debug and Release projects compile with warnings as errors; shaders validate as Vulkan 1.3 SPIR-V |
| Runtime | production pipeline/descriptor creation, ordered dispatch, shared TraceAny, readback, validation-layer result |
| Numerical | fixed-identity MAE/RMSE/PSNR against retained high-SPP reference plus budget counters |
| Visual | retained captures and explicit human acceptance |
| Performance | fixed machine/driver/power/resolution/warm-up conditions, GPU timestamps, memory, median and nearest-rank p95 |

Missing or synthetic provider data remains `not-run`; inconsistent identity,
ownership, budgets, generations, or visibility counts is rejected. Static or
build success cannot satisfy runtime, numerical, visual, or performance gates.
