# L5 Vulkan Hardware RT module

This directory is the L5 implementation over the frozen abi-v0 scene records
and published abi-v1 Ray/Hit queue records. In the Wave 3 shadow it is composed
by the central project through L0-owned item props, while production capability
publication remains disabled until the application supplies a real AS/dispatch
owner.

## Implemented paths

- capability/extension/feature/property discovery and device feature chain,
  including explicit `synchronization2` enablement and compute-dispatch limits;
- device dispatch loading for buffer addresses, acceleration structures,
  synchronization and RT Pipeline entry points;
- RAII Vulkan buffers with device-address allocation;
- BLAS/TLAS size queries, storage/scratch lifetime, build/update recording,
  synchronization, compaction query/copy and explicit completion state;
- exact retained BUILD signatures for conservative BLAS/TLAS UPDATE validation,
  plus completed-source/fresh-destination compaction state checks;
- deterministic rigid-instance update policy (no-op, TLAS refit, TLAS rebuild,
  or BLAS+TLAS rebuild);
- Compute Ray Query closest/any batches split at
  `maxComputeWorkGroupCount[0]`;
- RT Pipeline raygen/miss/closest-hit/any-hit groups and one-level dispatch,
  split at `maxRayDispatchInvocationCount`;
- SBT handle extraction, stride/base alignment, optional inline records and
  device-address regions;
- alpha-mask confirmation from a private `Texture2DArray` alpha atlas,
  single/two-sided handling, front-face flags, invalid-ray rejection and
  canonical miss encoding;
- fixed ray corpus and CPU/Software/HW parity comparison seam, including
  equivalent-hit sets for shared-edge/equidistant ambiguity.

Ray Query and RT Pipeline consume the same `GpuRayQueueRecordV1` and produce
the same `GpuHitQueueRecordV1`. Their six-word push constants carry independent
ray and hit offsets, so split dispatches preserve queue identity. The Wave 3
GPU gate binds the same canonical scene/AS/SBT and compares closest-hit and
any-hit results against CPU SAH and Software GPU without changing scene or
material semantics.

`VkAccelerationStructureInstanceKHR::instanceCustomIndex` must contain the
canonical instance-buffer index. A BLAS geometry order must match the
instance's `metadata.firstGeometry + localGeometryIndex`. `maxVertex` is the
highest valid index, not the vertex count. Alpha-masked or single-sided
geometry must not use `VK_GEOMETRY_OPAQUE_BIT_KHR`, because Ray Query candidate
confirmation / RT any-hit semantics would otherwise be bypassed.

`ForceOpaque` is a canonical alpha-test override only. It does not set the
Vulkan force-opaque ray flag, so single-sided rejection still runs. A ray that
sets both front- and back-face culling is encoded directly as the exact
canonical miss instead of recording an illegal Vulkan flag combination.
Non-finite, non-normalized, out-of-range, unknown-flag or malformed-reserved
rays are encoded as canonical `Invalid` results before traversal.

The lane-private alpha atlas exposes one Vulkan sampler per submitted batch.
Pass that sampler's canonical ID as `alphaSamplerId`; every textured
alpha-masked material reached by the batch must use the same ID. A missing
layer, out-of-range layer or sampler-ID mismatch deterministically produces
`Invalid`. Split batches (or normalize the atlas sampler) when canonical
materials require different samplers. All scene/traversal descriptors must be
fully populated and remain valid for the recorded dispatch.

The owner submitting build commands must call `MarkReady()` only after the
submission fence completes. Source AS storage and compaction-query pools must
remain alive through that fence; compacted destination storage must likewise
remain alive before replacing the source. UPDATE is accepted only from
`Ready`, with the originally retained BUILD flags/counts/geometry description;
compaction requires a `Ready` source created with `ALLOW_COMPACTION` and an
`Allocated` destination produced by `CreateCompactedCopy`.

Build the lane-local project directly for module checks. Central-build status
does not imply production runtime or performance acceptance:

```powershell
msbuild RenderingEngine.HardwareRT.Tests.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64
artifacts\bin\x64\Release\RenderingEngine.HardwareRT.Tests.exe --probe
```
