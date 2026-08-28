# L5 handoff: Hardware Ray Query and RT Pipeline all waves

- Status: Lane-private implementation complete; integration and scene-data validation deferred
- Date: 2026-08-24
- Shared contract consumed read-only: human `abi-v0`, numeric wire value `1`
- User override: implement every L5 wave now with a private v0 adapter; do not wait for `abi-v1`
- Owned implementation: `RenderingEngine/rt/hardware/**`

## Delivered scope

### Hardware capability and dispatch

- Enumerates the acceleration-structure, Ray Query, deferred-host-operations,
  buffer-device-address, synchronization2 and optional RT Pipeline
  extension/feature set.
- Queries AS scratch alignment, Compute workgroup-count X, and RT Pipeline
  handle/stride/recursion/invocation limits.
- Publishes an owning `vkCreateDevice` pNext feature chain and exact extension
  list without mutating L0/L1 device creation.
- Loads every AS, synchronization, shader-group and trace-rays device function
  through `vkGetDeviceProcAddr`, failing explicitly if a requested path is
  incomplete.

### BLAS/TLAS and lifetime

- RAII buffer/memory allocation supports persistent host mapping and Vulkan
  device addresses.
- BLAS and TLAS size queries, storage allocation, scratch allocation/alignment,
  build/update recording, AS-to-AS and AS-to-trace barriers, compacted-size
  queries and compact-copy recording are implemented.
- The state machine separates allocation, command recording and submission-fence
  completion. `MarkReady()` is an explicit owner action after the fence, so no
  resource is declared usable merely because commands were recorded.
- BLAS BUILD retains an exact geometry signature; UPDATE requires a `Ready`
  resource, `ALLOW_UPDATE`, exact geometry/count/flags/addresses and adequate
  update scratch. TLAS UPDATE likewise requires the retained instance count.
- BLAS creation and recording reject zero vertex/index addresses, zero stride,
  zero primitive count and invalid format/index descriptions. Compaction
  requires a `Ready` completed-BUILD source and an `Allocated` destination;
  the compacted copy retains the original update/compaction flags.
- The scene fingerprint policy separates no-op, transform-only TLAS refit,
  instance-count TLAS rebuild and topology-driven BLAS+TLAS rebuild.
- `maxVertex` is documented as the highest valid index. Alpha-masked and
  single-sided triangles are deliberately non-opaque; only opaque two-sided
  geometry receives `VK_GEOMETRY_OPAQUE_BIT_KHR`.

### Ray Query main path

- Compute Ray Query records closest and any-hit batches in 64-thread groups and
  splits large batches at `maxComputeWorkGroupCount[0]`.
- Private set 1 adapts the read-only v0 vertex/index/geometry/instance/material
  records. Private traversal set 2 owns TLAS, Ray, Hit and alpha-atlas bindings;
  no other descriptor owner is occupied.
- Candidate confirmation covers alpha cutoff/atlas sampling and single-sided
  rejection. `ForceOpaque` skips only alpha evaluation, while retaining
  sidedness. The one private Vulkan sampler is mapped to an explicit canonical
  sampler ID; mismatched or out-of-range alpha mappings yield `Invalid`.
- Malformed/non-finite rays yield `Invalid`; simultaneous front/back culling
  yields an exact canonical miss without passing an illegal flag combination
  to Vulkan. Accepted hits preserve stable IDs, barycentrics, front face,
  normals and the exact v0 miss encoding.

### RT Pipeline comparison path

- Implements ray-generation, miss, closest-hit and any-hit stages, three shader
  groups and one-level ray dispatch over the same Ray/Hit buffers. Large
  batches are split at `maxRayDispatchInvocationCount` with an explicit ray
  offset shared by both hardware paths.
- Payload is fixed at 32 bytes; triangle attribute is 8 bytes; recursion depth
  is fixed at one.
- SBT construction queries group handles, supports lane-private inline record
  data, aligns record stride to handle alignment, aligns each region/base to
  shader-group base alignment, and emits valid empty callable regions.

### Parity seam

- Publishes a deterministic 16-ray corpus and a readback-executor interface.
- Comparison checks hit/miss, invalid results, stable identity, relative `t`,
  barycentrics, front face and normals. It also rejects non-finite hit fields,
  unknown hit flags and non-canonical miss encodings.
- Equivalent-hit sets cover shared-edge/coplanar/equidistant ambiguity without
  demanding an implementation-specific primitive ID.

The HLSL RT-stage shape and `TraceRay`/`RayDesc` form were checked against the
[official DXC SPIR-V ray-tracing mapping](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst).

## Evidence

| Evidence layer | Result | Proof boundary |
|---|---|---|
| Static | Passed | Scope audit shows all L5 writes under `rt/hardware/**` plus this handoff. Shared contracts, ADRs, root solution/build files and other lanes were read-only. Source/tests cover synchronization2 enable/query, exact UPDATE signatures and completed states, basic BLAS address/stride checks, compaction states, non-opaque alpha/single-sided policy, safe invalid/double-cull encoding, explicit sampler-ID mapping and both hardware dispatch limits. |
| Shader static | Passed | Vulkan SDK 1.4.328.1 DXC compiled `HardwareRayQuery.hlsl` as `cs_6_6` and `HardwareRtPipeline.hlsl` as `lib_6_6`, both with `-Ges -WX`; `spirv-val --target-env vulkan1.3` passed. SPIR-V disassembly contains `RayQueryKHR` plus GLCompute, and `RayTracingKHR` plus RayGeneration/Miss/ClosestHit/AnyHit entry points. |
| Build | Passed | Lane-local MSBuild Debug and Release Rebuild passed with MSVC v145, C++20, `/W4 /WX`: 0 warnings, 0 errors. The root solution was not changed or used as composition evidence. |
| Runtime | Partial | Debug/Release CPU module tests and capability probes ran. The RTX 4070 Laptop GPU reported `rayQuery=1`, `rtPipeline=1`, scratch alignment `128`, Compute group-count-X `2147483647`, and SBT handle/alignment/base `32/32/64`. No Vulkan logical device, BLAS/TLAS submission, Ray Query dispatch, RT Pipeline dispatch, validation-layer run or fence/lifetime stress was executed. |
| Numerical | Harness logic passed; backend data deferred | Synthetic parity tests exercised the fixed threshold `relative t <= 1e-4`, deliberate failure above threshold, stable IDs, exact miss encoding, equivalent-hit acceptance, non-finite rejection and unknown-hit-flag rejection. CPU/Software/Ray Query/RT Pipeline results on a canonical L2/L3 scene were not available and are not claimed. |
| Visual | Not run | No Cornell/Sponza render, image capture, RMSE/PSNR/SSIM, alpha-mask image, animated-rigid scene or RT Pipeline/Ray Query visual A/B was produced. |

## Acceptance commands executed

```powershell
cmd.exe /d /c 'set Path=& "<MSBuild.exe>" RenderingEngine.HardwareRT.Tests.vcxproj /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:Configuration=Release /p:Platform=x64'
cmd.exe /d /c 'set Path=& "<MSBuild.exe>" RenderingEngine.HardwareRT.Tests.vcxproj /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:Configuration=Debug /p:Platform=x64'
.\artifacts\bin\x64\Debug\RenderingEngine.HardwareRT.Tests.exe --probe
.\artifacts\bin\x64\Release\RenderingEngine.HardwareRT.Tests.exe --probe

& "$env:VULKAN_SDK\Bin\dxc.exe" -spirv '-fspv-target-env=vulkan1.3' '-fspv-extension=SPV_KHR_ray_query' -fvk-use-dx-layout -Ges -WX -T cs_6_6 -E RayQueryMain -I '..\..\resources\shaders\include' 'shaders\HardwareRayQuery.hlsl' -Fo 'artifacts\shaders\HardwareRayQuery.spv'
& "$env:VULKAN_SDK\Bin\spirv-val.exe" --target-env vulkan1.3 'artifacts\shaders\HardwareRayQuery.spv'
& "$env:VULKAN_SDK\Bin\dxc.exe" -spirv '-fspv-target-env=vulkan1.3' '-fspv-extension=SPV_KHR_ray_tracing' -fvk-use-dx-layout -Ges -WX -T lib_6_6 -I '..\..\resources\shaders\include' 'shaders\HardwareRtPipeline.hlsl' -Fo 'artifacts\shaders\HardwareRtPipeline.spv'
& "$env:VULKAN_SDK\Bin\spirv-val.exe" --target-env vulkan1.3 'artifacts\shaders\HardwareRtPipeline.spv'
```

The local environment exposed duplicate `Path`/`PATH` entries to .NET
Framework MSBuild. The commands clear only the duplicate child-process entry;
they do not alter machine/user environment state.

## Deferred integration and data validation

- L0 must compose the module into the root solution/device/queue lifetime and
  decide how the private v0 descriptor layouts migrate to the eventual ABI.
- L2 must provide canonical geometry ordering, instance custom indices and the
  alpha-atlas upload; L3/L4 must provide reference hit files.
- Actual BLAS/TLAS build/update/compaction submissions, synchronization under
  validation, GPU readback parity, dynamic-rigid update counts, memory/time
  comparison, visual comparison and performance capture remain deferred at the
  user's explicit request to validate data later.
