# Vulkan renderer canonical ABI v1

Status: **published Wave 1 contract**

Numeric ABI version: `2`

ABI v1 extends ABI v0 without changing any v0 frame, scene, material, light,
ray, or hit record. Normative sources are:

```text
include/contracts/AbiVersionV1.hpp
include/contracts/GpuRecordsAbiV1.hpp
include/contracts/DescriptorRegistryV1.hpp
include/rt/gpu/IGpuTraversalBackend.hpp
include/rt/gpu/TraversalFixtures.hpp
resources/shaders/include/contracts/*V1.hlsli
```

## Shared records

| Record | Bytes | Role |
|---|---:|---|
| `GpuBsdfSampleV1` | 64 | sampled direction, BSDF value, total PDF, measure, lobe, eta and validity |
| `GpuLightSampleV1` | 96 | stable light/sample identity, radiance, distance and separated discrete/conditional/combined PDFs |
| `GpuRayQueueRecordV1` | 64 | traversal ray plus ray/path/bounce identity and deterministic RNG coordinates |
| `GpuHitQueueRecordV1` | 96 | resolved hit/miss/invalid result with stable scene and path identity |
| `GpuShadowQueueRecordV1` | 80 | any-hit ray plus deferred unoccluded contribution |
| `GpuPathStateV1` | 96 | radiance, throughput, previous-event PDF, RNG and path identity |

Distances use meters, radiance and BSDF values use linear Rec.709, ray bounds
are exclusive, and barycentrics/miss/invalid encodings retain ABI v0 meaning.
Sample PDFs always name their measure. Discrete delta events are not encoded as
ordinary finite solid-angle densities.

## Descriptor ownership

Sets 0/1 retain ABI v0 frame and scene bindings. In traversal set 2, binding 0
is the backend-owned scene representation, bindings 1/2 are shared Ray/Hit
records, and bindings 8/9 retain the alpha atlas/sampler seam. Wavefront set 3
bindings 0 through 4 are constants, path state, ray, hit, and shadow queues.
Bindings outside this published prefix remain module-private until a later ABI.

## Host interface and fixture

`IGpuTraversalBackend` accepts Vulkan command-recording batches; it never
defines a synchronous one-ray readback interface. `GpuTraversalBackendMock`
records validated build/closest/any calls for higher-level modules without a
Vulkan device. `LegacyAnalyticTraversalAdapter` forwards the same operations
through renderer-owned callbacks. `BuildFixedHitFixtureV1` supplies one stable
triangle hit, one miss, and one invalid query so all traversal/integrator lanes
share the same minimum corpus.

This publication is a compatibility boundary, not evidence that Software GPU,
Ray Query, RT Pipeline, Megakernel, or Wavefront production dispatch is wired.
