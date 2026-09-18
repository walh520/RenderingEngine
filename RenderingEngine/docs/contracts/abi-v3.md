# abi-v3 ReSTIR GPU contract

Status: Wave 4 contract freeze. This document defines the shared set-5
boundary only; it does not claim production renderer, numerical, visual, or
performance acceptance.

## Version and append-only rule

`abi-v3` has numeric version `4`. The existing revisions remain unchanged:

| Contract | Numeric version | Ownership |
|---|---:|---|
| abi-v0 | 1 | coordinates, colors, Frame/Scene/Material/Light/Ray/Hit |
| abi-v1 | 2 | GPU records, traversal and Wavefront queues |
| abi-v2 | 3 | PrimarySurface, Motion, GBuffer, reconstruction history metadata |
| abi-v3 | 4 | persistent light samples, ReSTIR candidates/reservoirs/history |

The C++ entry point is `include/contracts/AbiV3.hpp`; the shader entry point
is `resources/shaders/include/contracts/AbiV3.hlsli`. No v0/v1/v2 file is
modified by this contract.

## Record layout

All fields are explicit `float4`/`uint4` values. Every record is 16-byte
aligned, uses float32 for GPU estimator values, and has the following fixed
size and offsets:

| Record | Size | Fields and offsets |
|---|---:|---|
| `GpuPersistentLightSampleV3` | 128 | position/distance 0, direction/combined-PDF 16, radiance/discrete-PDF 32, conditional-PDF 48, identity 64, generation 80, metadata 96, source identity 112 |
| `GpuRestirCandidateV3` | 160 | persistent sample 0, target/proposal/support/correction 128, provenance 144 |
| `GpuRestirHistoryIdentityV3` | 64 | surface identity 0, scene identity 16, frame identity 32, reprojection 48 |
| `GpuRestirReservoirV3` | 256 | selected sample 0, weight state 128, selected terms 144, state 160, provenance 176, history identity 192 |
| `GpuRestirDebugV3` | 64 | identity 0, generation 16, scalar values 32, state 48 |
| `GpuRestirStatisticsV3` | 64 | candidate counts 0, reuse counts 16, visibility counts 32, error metrics 48 |
| `GpuRestirParametersV3` | 128 | extent/candidates 0, reuse limits 16, scene/light/history/frame-low 32, config/resource/debug/shadow 48, estimator/source/history/frame-high 64, light counts 80, validation 96, camera 112 |

The C++ `static_assert` checks are in
`tests/contracts/AbiV3LayoutStaticTests.cpp`. The HLSL mirror publishes the
same size/offset probe constants in `RestirAbiV3.hlsli`.

## Semantic freeze

`GpuPersistentLightSampleV3` is the only persistent light-sample wire type.
Its identity is `(stableLightId, primitiveId, sampleIdentityLow,
sampleIdentityHigh)`. Its generation is `(lightGeneration, sceneGeneration,
sampleGeneration, reserved)`. `directionCombinedPdf.w` is the combined
solid-angle density; `radianceDiscretePdf.w` is the discrete light-selection
density; `conditionalPdf.x` is conditional area density and `.y` is
conditional solid-angle density. `metadata.x` uses the abi-v1 sample-measure
enumeration and `metadata.w` stores the source light-table index. History uses
that index only through the append-only previous-to-current mapping; stable
light/primitive identity and per-light generation still decide validity.
Invalid or unused PDF components are zero.

`GpuRestirCandidateV3::targetProposalSupportCorrection` is exactly
`(target, proposalPdf, support, correction)`. `target` is unoccluded direct
contribution and must not include final visibility. `support` is 0 or 1;
`proposalPdf` is the candidate density in its declared measure; `correction`
is finite and is applied by the RIS weight calculation. Candidate source and
reuse source are stable enum values in `provenance.x/y`.

For a valid candidate, the RIS contribution to `weightSum` is:

```text
candidateWeight = target * support * correction / proposalPdf
```

`GpuRestirReservoirV3::state.x` is accepted candidate count `M`, `.y` is
history age, `.z` is `RestirReservoirFlags`, and `.w` is the rejection reason.
`weightState` is `(weightSum, normalizationWeight, selectedTarget,
selectedProposalPdf)`. `selectedTerms` is `(selectedSupport,
selectedCorrection, selectedCandidateWeight, finalContributionWeight)`. A
final estimator must guard zero/non-finite values and may evaluate selected
sample visibility exactly once; visibility is never part of `target`.

`GpuRestirHistoryIdentityV3` prevents reuse across surface identity, scene or
light generation, camera/resolution generation, frame history generation, or
reprojection rejection. Dynamic/deleted lights must increment generation or
set the corresponding history flag before temporal reuse.

`GpuRestirParametersV3` is the only set-5 constant-buffer shape. Frame low and
high words are both published; config/camera and resource/resolution
generations are copied into every history record. Debug mode and shadow mode
have separate fields and never alias frame identity. The host history decision
also includes the shadow method, so changing Physical/PCF/PCSS resets rather
than reusing samples produced with different visibility semantics. Material,
instance, finite-normal, depth,
position/thin-geometry, motion, age, scene, light, frame, config and resource
mismatches fail reuse with an explicit rejection code.

## Descriptor registry

Set 5 is owned by abi-v3 ReSTIR. Existing set 0-4 and set 6 numbers are
preserved. Every set-5 binding is unique and has one cross-stage meaning; no
stage may reuse a binding for a different private resource.

| Binding | Stable meaning |
|---:|---|
| 0 | Parameters |
| 1 | Candidates |
| 2 | ReservoirRead |
| 3 | HistoryReservoirRead |
| 4 | SurfaceCurrent |
| 5 | SurfaceHistory |
| 6 | HistoryIdentity |
| 7 | ReservoirWrite |
| 8 | DebugRecord |
| 9 | Statistics |
| 10 | VisibilityResults |
| 11 | CandidateAtCenter |
| 12 | LightTable |
| 13 | PairwiseTargetSupport |
| 14 | DirectLighting |
| 15 | ValidationReasons |
| 16 | DebugImage |
| 17 | HistoryAtCurrent |
| 18 | ReferenceTarget |
| 19 | ReferenceVisibility |
| 20 | CurrentToPreviousLightIndex |
| 21 | PreviousToCurrentLightIndex |
| 22 | NeighborIndices |
| 23 | ShadowRayQueue |
| 24 | DirectDiffuse |
| 25 | DirectSpecular |
| 26 | InitialReservoir |
| 27 | TemporalReservoir |
| 28 | SpatialReservoir |
| 29 | PublishedReservoir |
| 30 | PreviousPublishedReservoir |

Bindings 20-30 are append-only production-frame-graph resources. The light
index buffers are separate current-to-previous and previous-to-current maps;
`NeighborIndices` is the spatial-reuse index stream; `ShadowRayQueue` is the
visibility work queue; `DirectDiffuse` and `DirectSpecular` are separate
lighting outputs. Reservoir stage resources are intentionally distinct:
`InitialReservoir`, `TemporalReservoir`, `SpatialReservoir`, and
`PublishedReservoir` identify the write/read boundary of each current-frame
stage. `PreviousPublishedReservoir` is the immutable prior-frame input used
by temporal reuse; it must not alias current `PublishedReservoir`. A stage
may not overload a generic reservoir or light-table binding with another
stage's resource.

Bindings 26-28 are the three current-frame staging allocations. Bindings 29
and 30 are not two more staging allocations: they are one-reservoir-slot
descriptor views into an owner-allocated `2 * framesInFlight` history ring.
For frame `F` and `N` frames in flight, publication writes
`(F & 1) * N + (F % N)` and an exact previous frame reads
`((F - 1) & 1) * N + ((F - 1) % N)`. The underlying ring therefore owns `2N`
reservoir slots, while each binding exposes exactly one non-overlapping slot.

Bindings 10 and 19 are structured `GpuHitQueueRecordV1` views, not packed
`uint` visibility flags. They must exactly match set-2 binding 2 for the
winner and reference TraceAny descriptor sets. Binding 23 is a
`GpuRayQueueRecordV1` view and must exactly match set-2 binding 1 for both
TraceAny sets. The shared ray range and both hit-output ranges must be
pairwise non-overlapping. A prepare/resolve pair always records the exact
planned ray count; a short batch is invalid because the resolve shader indexes
the complete planned range.

### Production descriptor range contract

Let `P = width * height`, `C = initialCandidateCount`, `S =
spatialNeighborCount`, `W = maximumWinnerVisibilityRays`, `R =
maximumReferenceVisibilityRays`, `L = currentLightCount`, and `Lprev =
previousLightCount`. The immutable frame plan publishes the following minimum
buffer ranges in bytes:

| Binding | Minimum range |
|---:|---:|
| 0 | 128 |
| 1 | `P * C * 160` |
| 5 | `P * 96` on history reuse, otherwise 0 |
| 8 | `P * 64` when debug is enabled, otherwise 0 |
| 9 | 64 |
| 10 | `W * 96` |
| 15 | `P * 4` |
| 17 | `P * 128` on history reuse, otherwise 0 |
| 19 | `R * 96` in unbiased-reference mode, otherwise 0 |
| 20 | `L * 8` (`StructuredBuffer<uint2>`) |
| 21 | `Lprev * 8` on history reuse, otherwise 0 |
| 22 | `P * S * 4` |
| 23 | `max(W, R) * 64` |
| 24, 25 | `P * 16` each |
| 26-30 | `P * 256` each when used; binding 30 is 0 on reset |

The production V3 shaders do not access buffer bindings 2-4, 6-7, 11-14, or
18; their plan minima are zero, although the fixed 0-30 descriptor layout
still requires explicit valid dummy descriptors. Binding 16 is a storage
image rather than a buffer: the production contract is a non-empty
`VK_FORMAT_R32G32B32A32_SFLOAT` view in `GENERAL`, with extent at least the
planned frame extent when debug is enabled.

Every buffer descriptor uses a finite explicit range. `VK_WHOLE_SIZE`, zero
ranges, and offset-plus-range overflow fail closed. Required distinct
resources are compared as full half-open byte intervals, so partial overlap is
also rejected. The exact minima are generated and revalidated in
`src/renderers/ReSTIRDIRuntime.cpp`; the Vulkan gates are in
`src/renderers/VulkanReSTIRRecorder.cpp`. Regression coverage is in
`tests/contracts/ReSTIRDIRuntimeTests.cpp` and
`tests/contracts/VulkanReSTIRRecorderTests.cpp`.

Private L9 records and the old private stage-local set-5 bindings are not
ABI-compatible aliases. A production adapter must explicitly pack/unpack
these records and provide barriers/ping-pong ownership for every resource.

## Acceptance boundary

This contract is necessary for Wave 4 GPU wiring, not sufficient for it. The
remaining acceptance must demonstrate L6 primary-hit/GBuffer production data,
L8 history/reprojection, L4/L5 visibility batches, exactly-once final
visibility, 100/1k/10k dynamic-light comparisons, independent CPU reference,
Vulkan validation, GPU timestamps, visual debug output, and reproducible
seeded captures.
