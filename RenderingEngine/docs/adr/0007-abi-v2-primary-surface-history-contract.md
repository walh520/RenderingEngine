# ADR 0007: ABI v2 primary-surface and reconstruction boundary

Status: Accepted in the Wave 3 shadow delivery

## Decision

Publish human contract `abi-v2` with numeric version `3`. It is append-only
over `abi-v1` and freezes `GpuPrimarySurfaceV2`, `GpuMotionVectorV2`,
`GpuReconstructionSignalV2`, `GpuHistoryMetadataV2`, the complete Wavefront
set-3 registry, and reconstruction set 4. Reservoir and persistent ReSTIR
history remain reserved for `abi-v3`.

Motion is always `previous jittered UV - current jittered UV`. Depth is
positive view-space depth, and the expected previous depth must be evaluated
for the current surface point in the previous camera/time domain. Stable
material, instance, and primitive identities cross the history boundary.

History publication identifies the exact completed frame and logical
generation. A frame slot is not reusable history merely because it has the
same frames-in-flight index. Camera cuts, resize, scene/backend/integrator
changes, reconstruction parameter changes, and shader reloads invalidate
history.

## Consequences

C++ and HLSL records have fixed 16-byte alignment and explicit size/offset
checks. Set 3 bindings 0 through 4 preserve `abi-v1` in number, layout, and
meaning; Wave 3 bindings 5 through 23 are append-only. Split Shadow AOV data
uses a same-index sidecar and never changes the shared Shadow record. Set 4 no longer means "reserved". L8 may keep private working
records internally, but the renderer boundary must pack/unpack these records
without changing motion, depth, or identity semantics.

Ray Query and RT Pipeline retain the inherited abi-v1 queue records. L5 may
change dispatch mechanics and SBT layout privately, but not Ray/Hit semantics.
History publication uses a deferred commit after all frame providers succeed;
an incomplete pass graph cannot advance the public history identity.
