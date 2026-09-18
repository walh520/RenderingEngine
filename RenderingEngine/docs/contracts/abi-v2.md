# ABI v2 — Primary surface, motion, GBuffer, and history

- Human version: `abi-v2`
- Numeric version: `3`
- Parent: `abi-v1` (`2`)

ABI v2 is the Wave 3 publication gate. It adds no Reservoir state and changes
no abi-v0/v1 record or binding.

## Records

| Record | Size | Meaning |
|---|---:|---|
| `GpuPrimarySurfaceV2` | 96 | world position/depth, geometric and shading normals, roughness/metallic, diffuse/specular albedo, stable IDs |
| `GpuMotionVectorV2` | 48 | `previousUV-currentUV`, expected previous depth, current/previous UV, stable IDs and validity |
| `GpuReconstructionSignalV2` | 64 | direct/indirect diffuse/specular signals |
| `GpuHistoryMetadataV2` | 48 | moments, variance, history length, stable IDs, exact frame/generation/flags |
| `GpuGBufferRecordV2` | 208 | primary + motion + split signal composite |

All records are 16-byte aligned, standard-layout, trivially copyable, and have
matching C++/HLSL field order.

## Temporal semantics

- Motion is `previous jittered UV - current jittered UV`.
- Linear depth is positive view-space depth.
- Previous depth is evaluated for the current surface point using previous
  object and camera transforms.
- History validation includes bounds, finite values, depth, normal, material,
  instance, primitive, frame, generation, and reset state.
- Physical history allocation is at least two logical generations per
  frames-in-flight slot; publication is tied to the exact completed frame.

## Descriptor ownership

Set 3 is the complete Wavefront queue/output set. Bindings 0-4 preserve both
the v1 numbers and binary records: constants, shared path state, Ray A, Hit,
and Shadow. Binding 5 is Ray B; binding 6 is the L7-private extended path
state; bindings 7-15 are compaction/indirect/profiler resources; bindings
16-22 are outputs; binding 23 is the split diffuse/specular Shadow AOV
sidecar. All bindings 5-23 are append-only. Set 4 owns primary surface, motion,
raw and filtered signals, history, variance, A-Trous ping-pong, compose output,
and per-pass constants. Set 5 remains reserved for abi-v3 ReSTIR.

Ray Query and RT Pipeline both consume the inherited abi-v1 Ray queue and
produce its Hit queue. RT Pipeline does not introduce a second hit semantic or
change the abi-v2 reconstruction boundary.

History identity is published only after the full frame transaction succeeds.
Provider fixtures may validate packing or orchestration but cannot publish
runtime evidence.
