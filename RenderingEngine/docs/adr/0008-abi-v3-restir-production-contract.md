# ADR 0008: abi-v3 ReSTIR production contract

- Status: Accepted for Wave 4 shadow promotion
- Date: 2026-08-31

## Context

Wave 3 publishes primary-surface, motion, split-signal, and reconstruction
contracts through `abi-v2`. Wave 4 must connect a primary-hit ReSTIR DI path
without reinterpreting private L9 records, overloading descriptors per stage,
or bypassing the shared traversal backend.

## Decision

Publish `abi-v3` with numeric version `4` and append-only set-5 bindings. The
shared wire records are persistent light sample, candidate, history identity,
reservoir, debug projection, and statistics. Set 5 has one canonical meaning
for bindings 0..30; bindings 20..30 add current/previous `uint2` light maps,
neighbor indices, the shared shadow queue, split direct AOVs, three distinct
current-frame staging reservoirs, and current/previous history-ring views.

Production recording uses the shared `IGpuTraversalBackend::RecordTraceAnyBatch`
interface. The ReSTIR recorder owns descriptor and pass recording, while the
traversal implementation retains acceleration-structure ownership and the
reconstruction owner retains temporal/SVGF resources. Pipelines and frame
resources are externally supplied and validated before recording.

The production owner allocates a `2 * framesInFlight` history ring. Bindings
29/30 are non-overlapping one-slot views for current publication and exact
previous-frame reuse; they are not additional staging allocations. Set-5
binding 23 must exactly match set-2 binding 1 for both TraceAny batches, while
set-5 bindings 19/10 must exactly match their set-2 binding-2 hit outputs.
TraceAny ray and hit ranges are non-overlapping and each batch has the exact
planned count.

History is reused only for an exact frame/generation identity. Primary-hit
ReSTIR direct lighting is mutually exclusive with conventional NEE/MIS; emitter
emission and secondary-bounce MIS are unchanged.

## Consequences

- Private pre-v3 L9 buffers are not production-compatible by implication.
- Every v3 resource and pipeline must be present; missing composition fails
  closed rather than falling back.
- Three staging reservoirs are separate allocations, and current/previous
  publication views select distinct slots of a `2N` history ring.
- Descriptor minima, explicit finite ranges, debug-image format/extent, and
  cross-set traversal views fail closed before command recording.
- An unbiased-reference label remains a validation mode, not an unqualified
  mathematical proof.
- Capability publication requires production attachment and runtime evidence;
  contract, source, or shader compilation alone is insufficient.
