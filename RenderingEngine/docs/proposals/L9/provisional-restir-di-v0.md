# L9 proposal: provisional private ReSTIR DI records and semantics

- Status: L9-private implementation proposal
- Shared ABI impact: none
- Shared descriptor impact: none
- Integration status: not connected to the production renderer

This document defines the semantics implemented by the self-contained L9 CPU
reference and Compute shaders. The records live only in L9-owned directories.
They are deliberately not `abi-v3`, and the set-5 bindings in the shaders are a
private validation seam rather than a production descriptor contract.

## Candidate identity and mapping

A candidate carries these independent concepts:

- stable light ID, stable primitive ID, stable sample/cell ID, and light
  generation;
- original proposal source: uniform light, power-weighted light, emissive
  triangle, or environment;
- reuse provenance: none, temporal, or spatial;
- source-surface index used by reference correction;
- unshadowed RGB contribution and scalar target;
- proposal PDF/PMF, support, and an explicit correction factor.

Temporal/spatial remapping never replaces the original proposal identity. A
light is invalidated when its stable ID no longer exists or its generation no
longer matches. Deletion and mutation therefore cannot silently reuse an array
slot occupied by another light.

## Proposal measures

The initial target excludes visibility. It is a non-negative scalar proxy for
the unshadowed direct-light contribution.

- Uniform and power-weighted finite-light selection use a discrete light PMF.
- Emissive triangles sample uniformly in area after discrete light selection:
  `p_A = p_select / area`. Their target includes receiver cosine, emitter
  cosine, and inverse squared distance in the corresponding area measure.
- Environment cells use solid angle:
  `p_omega = p_cell / solidAngle`. Their target includes receiver cosine.

Zero target and zero support are valid streamed samples and increment `M`; they
have zero selection weight. A zero/non-finite proposal PDF is invalid, is not
accepted into a reservoir, and is counted separately. The implementation also
counts zero-target and zero-support samples so black/backfacing regions remain
observable rather than looking like dropped work.

## Initial RIS and reservoir finalization

For a valid streamed candidate `i`:

```text
w_i = target_i * support_i * correction_i / proposalPdf_i
```

The streaming reservoir increments `M` once per valid candidate, accumulates
`weightSum`, and replaces the representative with probability
`w_i / newWeightSum`. Initial candidates use correction `1`.
An update whose multiplicity would overflow the 32-bit `M` field is rejected
without mutating the reservoir and is counted as invalid; CPU and HLSL follow
the same bounded rule.

The explicitly biased/naive final weight is:

```text
W = weightSum / (M * selectedTarget)
```

If `M` exceeds its configured cap, `weightSum` and the effective `M` are scaled
together once after all sources are merged. This preserves selection
probabilities and keeps `W*M` and the reference denominator on the same cap.

## Temporal and spatial reservoir merge

A finalized source reservoir contributes one representative at the current
surface with multiplicity `M_source` and stream weight:

```text
w_merge = target_at_current * support_at_current * W_source * M_source
```

Temporal validation rejects out-of-bounds reprojection, invalid motion,
camera cuts, resize, relative-depth or normal mismatch, stable
position/instance/material mismatch, scene-generation mismatch, expired age, deleted or
changed lights, and thin-geometry primitive/position mismatch.

Spatial validation additionally uses a position threshold. Thin geometry must
retain the same stable primitive and remain inside the tighter thin-geometry
distance. GPU spatial input and output reservoirs are distinct ping-pong
allocations; in-place neighbor feedback is forbidden. The private shader rejects
more than 30 neighbors because its accepted-source mask is 32-bit. The CPU
reference uses the same limit and returns the center reservoir unchanged with
an explicit invalid-configuration rejection. The center/current representative
keeps reuse provenance `none`; only imported history/neighbors are tagged as
temporal/spatial reuse. Zero or non-finite normals map to the same safe zero
normal in CPU and HLSL before the normal-threshold test.

## Biased and reference modes

`Biased` applies the naive finalization above after temporal/spatial merge.

`UnbiasedReference` is a costly validation mode. After the winner is selected,
it re-evaluates that winner's target and visibility support on every valid
source surface and applies the implemented basic correction:

```text
W_reference = weightSum * p_selected_at_source
              / (p_selected_at_current * sum_j(M_j * p_selected_at_j))
```

The same effective-`M` cap scales `weightSum` and each denominator term.
Reference visibility probes are counted separately from final shading.

The name means “unbiased/reference validation path” inside this restricted
candidate model. It is not a claim of a complete generalized resampled
importance sampling proof for arbitrary correlated proposal streams. Production
integration must compare this mode with independent uniform, power-weighted,
and high-SPP references before promoting the records or estimator semantics.

## Visibility and direct-light ownership

Selection target deliberately excludes expensive visibility. After initial,
temporal, and spatial reuse have selected one winner, the final visibility stage
consumes exactly one traversal result for that winner. A second final-visibility
request is rejected and counted.

The provisional ownership policy is mutually exclusive:

- a primary non-emissive, non-delta hit uses ReSTIR direct lighting when
  enabled; conventional NEE/MIS is disabled for that event;
- an emitter hit owns its emission term and disables ReSTIR/NEE for that event;
- secondary non-delta hits use conventional NEE/MIS;
- delta events do not run this direct-light estimator.

This policy is not wired to the shared integrator and therefore cannot yet prove
production double-count prevention.

## Benchmark and debug schema

The CPU harness has explicit 100, 1,000, and 10,000-light tiers. Each result
records estimator mode, proposal, candidate and visibility budgets, CPU build
and trace time, candidate count, final visibility rays, estimated provisional
GPU buffer bytes (including candidate input and ping-pong reservoirs),
and a deterministic checksum. Reference MAE/RMSE and GPU milliseconds have
explicit `NotMeasured` states and NaN values until an integrated renderer can
produce those measurements; CPU time is never labeled as GPU time.

Debug records and counter outputs jointly expose selected stable identity, original/reuse source, `M`, age,
`weightSum`, target, proposal, normalization weight, temporal/spatial rejection
bits, reference visibility cost, final visibility cost, invalid candidates,
zero target/PDF/support, and M clamps.

## Compute stages

The private shader module compiles and validates five stages:

1. initial RIS;
2. temporal validation/reuse;
3. ping-pong spatial validation/reuse;
4. exactly-once final visibility application;
5. reservoir/debug visualization extraction.

The shaders consume pre-evaluated candidate/visibility buffers where traversal,
surface reconstruction, and light remapping would eventually connect. The
L9-local test executable now provides a real Vulkan 1.3 execution smoke: it
dynamically loads the loader, enables the Khronos validation layer and
synchronization validation when present, uploads private inputs, dispatches all
five stages with explicit `vkCmdPipelineBarrier2` dependencies and strict
reservoir ping-pong, then reads back reservoirs, direct lighting, debug/stats,
and an RGBA32F storage image. Actual API or validation errors fail; only loader
or compatible-device absence skips.

That smoke proves dispatch/descriptor/layout/synchronization/readback viability
for the private seam. It is not production-renderer integration, a reference
MAE/RMSE comparison, a GPU timing result, or visual-quality evidence; those
data-driven checks remain deferred.

## Research basis

- Bitterli et al., *Spatiotemporal reservoir resampling for real-time ray
  tracing with dynamic direct lighting*:
  <https://cs.dartmouth.edu/~wjarosz/publications/bitterli20spatiotemporal.html>
- NVIDIA RTXDI reservoir record/reference implementation:
  <https://github.com/NVIDIA-RTX/RTXDI-Library/blob/main/Include/Rtxdi/DI/Reservoir.hlsli>
- NVIDIA RTXDI temporal resampling reference:
  <https://github.com/NVIDIA-RTX/RTXDI-Library/blob/main/Include/Rtxdi/DI/TemporalResampling.hlsli>

The implementation is independent and does not copy NVIDIA source.
