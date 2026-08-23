# Vulkan renderer canonical ABI v0

Status: **frozen Wave 0 contract**

Numeric ABI version: `1` (`0` is reserved for uninitialized data)

Platform baseline: Windows x64, Vulkan 1.3, HLSL compiled by Vulkan SDK DXC

## Scope

ABI v0 is the canonical CPU/GPU wire format for the first triangle-scene
vertical slice. It freezes:

- scalar, vector, and matrix representation;
- frame and scene constants;
- vertex, geometry, rigid instance, material, and light records;
- traversal ray and resolved hit records;
- BSDF/LightSample mathematical vocabulary without a GPU record layout;
- descriptor-set ownership and canonical scene bindings.

ABI v0 deliberately does **not** define path state, Wavefront queues, GBuffer,
motion/history images, reservoir records, acceleration-structure descriptors,
shader binding table records, or an RT-pipeline payload. Those contracts belong
to later ABI waves and may not be smuggled into v0 reserved fields.

The normative sources are:

```text
include/contracts/*.hpp
resources/shaders/include/contracts/*.hlsli
tests/contracts/AbiLayoutStaticTests.cpp
tests/contracts/AbiV0Offsets.golden.json
```

This document defines field semantics. The golden file is authoritative for
byte offsets, sizes, alignment, descriptor sets, and bindings.

## Global representation rules

- The world is right-handed, `+Y` is up, and the default camera looks toward
  `-Z`.
- World distance is measured in meters.
- Working color values and radiance are linear Rec.709. Tone mapping and sRGB
  encoding occur only at presentation.
- GPU records use only 32-bit IEEE floats and unsigned 32-bit integers.
- `bool`, `size_t`, pointers, native C++ enums, GLM types, and three-component
  vector types never cross the ABI boundary.
- Every record is aligned to 16 bytes and consists of explicit 16-byte lanes.
- `0xffffffffu` is the only invalid ID.
- Reserved fields must be written as zero and ignored by readers.
- Numeric IDs and flag bits are append-only and are never repurposed.

### Matrix convention

`AbiMat4Rows` stores four explicit mathematical rows. Both CPU and shader code
evaluate a column vector with:

```text
result = M * v
result[i] = dot(M.row[i], v)
```

No HLSL `float4x4` or host library matrix crosses the ABI boundary. Points use
`w = 1`; directions use `w = 0`. This removes row-major/column-major compiler
layout ambiguity.

## Frame record

`GpuFrameConstantsV0` is 256 bytes.

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `clipFromWorld` | Current non-jittered world-to-clip transform. |
| 64 | `worldFromClip` | Inverse transform used for primary-ray reconstruction. |
| 128 | `cameraPositionExposure` | xyz world position; w exposure multiplier. |
| 144 | `renderExtentInvExtent` | width, height, reciprocal width, reciprocal height. |
| 160 | `timeAndEpsilon` | time, delta time, ray `tMin`, shadow-ray `tMin`. |
| 176 | `frameInfo` | frame index, sample index, base-seed low/high words. |
| 192 | `renderInfo` | max bounce, SPP/frame, debug-view ID, `FrameFlags`. |
| 208 | `modeInfo` | backend, integrator, sampling, reconstruction IDs. |
| 224 | `sceneInfo` | scene ID, scene generation, camera-cut generation, ABI version. |
| 240 | `reserved0` | Zero. |

`FrameFlags` v0:

```text
bit 0 camera cut
bit 1 fixed-base-seed comparison
bit 2 headless run
```

The seed remains a base seed. Sample/frame indices must still advance the RNG
sequence; fixed-seed comparison must not repeat the same 1-SPP sample forever.

Previous camera matrices, jitter history, and motion data are not part of v0.

## Scene constants

`GpuSceneConstantsV0` is 96 bytes.

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `counts0` | vertex, index, geometry, instance counts. |
| 16 | `counts1` | material, light, texture, sampler counts. |
| 32 | `sceneBoundsMin` | world-space xyz minimum; w zero. |
| 48 | `sceneBoundsMax` | world-space xyz maximum; w zero. |
| 64 | `versionFlags` | ABI version, scene generation, `SceneFlags`, reserved. |
| 80 | `environment` | texture ID, width, height, reserved. |

An absent environment uses invalid texture ID and zero dimensions.

## Vertex and geometry

`GpuVertexV0` is 64 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `position` | object-space xyz in meters; w one. |
| 16 | `normal` | object-space unit normal; w zero. |
| 32 | `tangent` | object-space unit tangent; w handedness. |
| 48 | `texcoord0` | xy UV0; zw zero. |

Indices are a separate packed `uint32` buffer. Canonical upload rebases indices
so `vertexOffset` is non-negative.

`GpuGeometryV0` is 64 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `indexRange` | first index, index count, vertex offset, first global primitive ID. |
| 16 | `identity` | stable geometry ID, mesh ID, material ID, `GeometryFlags`. |
| 32 | `localBoundsMin` | object-space xyz; w zero. |
| 48 | `localBoundsMax` | object-space xyz; w zero. |

For a triangle geometry, local primitive `i` maps to stable global primitive ID
`firstPrimitiveId + i`. `indexCount` must be divisible by three.

## Rigid instance

`GpuInstanceV0` is 224 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `objectToWorld` | Current rigid/object transform. |
| 64 | `worldToObject` | Inverse current transform. |
| 128 | `previousObjectToWorld` | Previous transform retained for later motion reconstruction. |
| 192 | `metadata` | first geometry, geometry count, stable instance ID, `InstanceFlags`. |
| 208 | `reserved0` | Zero. |

ABI v0 does not contain a previous inverse transform, skinning, or morph data.

## Material

`GpuMaterialV0` is 128 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `baseColorFactor` | Linear RGBA factor. |
| 16 | `emissiveFactorStrength` | Linear RGB factor and scalar strength. |
| 32 | `surfaceParams` | metallic, perceptual roughness, normal scale, alpha cutoff. |
| 48 | `transmissionParams` | transmission, IOR, reserved, reserved. |
| 64 | `attenuationColorDistance` | Linear RGB attenuation color and reference distance. |
| 80 | `textureImageIndices` | base-color, metal-rough, normal, emissive image IDs. |
| 96 | `textureSamplerIndices` | Sampler ID paired with each image ID above. |
| 112 | `metadata` | `MaterialModel`, `MaterialFlags`, stable material ID, reserved. |

Missing textures use invalid image and sampler IDs. ABI v0 material models are:

```text
0 metallic-roughness
1 smooth dielectric
```

Rough dielectric transmission is deliberately not encoded in v0.

## Light

`GpuLightV0` is 96 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `positionRange` | Position xyz and optional finite range. |
| 16 | `directionCosOuter` | Direction xyz and spot outer-cone cosine. |
| 32 | `radianceScale` | Linear radiance RGB and scalar multiplier. |
| 48 | `shapeParams` | Type-specific radius/extent/cone parameters. |
| 64 | `identity` | `LightType`, stable light ID, instance ID, primitive ID. |
| 80 | `extra` | texture ID, `LightFlags`, distribution offset, distribution count. |

Light types are point, directional, spot, sphere area, emissive triangle, and
environment. Wave 0 writes invalid/zero distribution offset/count. Alias tables
and ReSTIR persistent light samples are later contracts.

## BSDF and light-sample semantic baseline

ABI v0 freezes names and mathematical responsibilities so CPU reference and GPU
sampling work do not invent incompatible meanings. It deliberately does not
freeze a `BSDFSample` or `LightSample` C++/HLSL record; those layouts belong to
ABI v1 and therefore are not part of the v0 offset golden.

A future BSDF sample carries, as independent values:

```text
sampled direction
BSDF value
total PDF
measure
lobe flags
eta
isDelta
isValid
```

The contract must state radiance transport mode, reflection/transmission
hemisphere, the rough-transmission half-vector and Jacobian, `eta^2` scaling,
geometric/shading-normal correction, and the fact that lobe-selection
probability is included in the reported total PDF. Sample, Evaluate, and PDF
implementations must agree; a delta event is never represented as an ordinary
finite solid-angle PDF.

A future light sample carries:

```text
stable light and optional primitive identity
sample position or direction
radiance
distance
discrete light-selection PDF
conditional area or solid-angle PDF
combined solid-angle PDF
isDelta
stable sample identity
```

The direct-lighting estimator and proposal distribution remain independent
RuntimeConfig dimensions. A token, semantic list, or v0 light record is not
evidence that NEE, MIS, environment importance sampling, or ReSTIR is
implemented.

## Traversal query and result

`GpuRayV0` is 64 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `originTMin` | origin xyz and exclusive minimum distance. |
| 16 | `directionTMax` | normalized direction xyz and exclusive maximum distance. |
| 32 | `query` | ray ID, visibility mask, `RayFlags`, reserved. |
| 48 | `reserved0` | Zero. |

Ray v0 contains traversal query data only. Pixel/path index, bounce, RNG,
throughput, and queue linkage belong to ABI v1.

A valid closest hit satisfies `t > tMin && t < tMax`. Triangle barycentrics
use `position = (1 - u - v) * p0 + u * p1 + v * p2`; `baryU` therefore belongs
to vertex 1 and `baryV` to vertex 2.

`GpuHitV0` is 96 bytes:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `positionT` | world position xyz and hit distance. |
| 16 | `geometricNormalBaryU` | world geometric normal and barycentric u. |
| 32 | `shadingNormalBaryV` | world shading normal and barycentric v. |
| 48 | `ids` | instance, primitive, geometry, material IDs. |
| 64 | `metadata` | source ray ID, `HitKind`, `HitFlags`, reserved. |
| 80 | `reserved0` | Zero. |

Miss encoding is fixed:

```text
hitKind = miss
all IDs = 0xffffffff
t = input ray tMax
position, normals, barycentrics = 0
```

Invalid input/result uses `HitKindInvalid`, never the miss representation.

## Descriptor registry

| Set | Owner | v0 bindings |
|---:|---|---|
| 0 | Frame | 0: `GpuFrameConstantsV0` UBO |
| 1 | Canonical scene | 0 constants, 1 vertices, 2 indices, 3 geometries, 4 instances, 5 materials, 6 lights, 16 texture array, 17 sampler array |
| 2 | Traversal backend | Ownership reserved; no shared v0 binding. |
| 3 | Wavefront | Reserved and empty in Wave 0. |
| 4 | Reconstruction | Reserved and empty in Wave 0. |
| 5 | ReSTIR | Reserved and empty in Wave 0. |
| 6 | Debug/profiler | Binding 0 reserved for debug constants. |

A traversal implementation may propose private set-2 bindings in its own ABI
wave. It may not occupy another owner's set. Sets 3-5 remaining empty is a Wave
0 acceptance condition.

## Validation contract

`AbiLayoutStaticTests.cpp` must compile with the production C++ language mode
and verifies every record's size, alignment, standard-layout/trivial-copy
traits, and every field offset/size.

`AbiV0LayoutProbe.hlsl` must compile with:

```text
-spirv -fvk-use-dx-layout -Ges -WX
```

The probe reads every ABI field so it remains present in SPIR-V reflection.
SPIR-V member offsets, array strides, descriptor sets, and bindings are compared
to `AbiV0Offsets.golden.json`. The verifier also checks the HLSL ABI-version and
descriptor-registry constants, exact 12-record golden inventory, little-endian
scalar order, and the explicit-row `M * v` matrix convention. Its MSBuild target
publishes a success stamp only after DXC, `spirv-val`, and all comparisons
succeed, preventing an older SPIR-V output from hiding a failed validation. GPU
sentinel round-trip validation is explicitly deferred; the probe source and
golden layout are the Wave 0 deliverable.

The probe's traversal-set bindings 0 and 1 are test-only inputs for `GpuRayV0`
and `GpuHitV0`. They exist solely to keep Ray/Hit member decorations alive and
do not assign or reserve production bindings in traversal set 2. Probe set 6
binding 0 is likewise a test output, not the production debug-constants UBO.

Any incompatible field reorder, removal, type change, semantic change, or
binding reassignment requires a new ABI version and ADR. Adding meaning to a v0
reserved field is also an ABI change.
