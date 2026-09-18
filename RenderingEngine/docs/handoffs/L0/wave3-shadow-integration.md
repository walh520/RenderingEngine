# L0 handoff: Wave 3 persistent shadow integration

- Date: 2026-08-31
- Baseline commit: `f88e898db0dddb2209089b71d2d72fac7e7511dc`
- Shadow root:
  `build/codex-shadow/wave3-20260830/source`
- Project worktree policy: read-only; this delivery did not write or replace a
  project file. A concurrent Wave 2 delivery changed 27 existing files and
  added one handoff after the initial snapshot; those changes were rebased into
  the shadow and are excluded from the Wave 3 promotion delta.
- Production contract: abi-v1, numeric version 2
- Centrally composed Wave 3 contract: abi-v2, numeric version 3

## Landed in the shadow

- Published C++/HLSL abi-v2 primary-surface, motion, split signal, GBuffer,
  history, descriptor set 3/4, version, layout, and offset contracts.
- L4 GPU LBVH real-device build/trace timestamp provenance.
- L5 shared-queue RT Pipeline/SBT and real-device closest/any parity gate.
- L7 extended Wavefront queues, both compaction modes, indirect schedule,
  traversal adapters, profiler, and all pass shaders.
- L8 abi-v2 bridge, temporal history, motion, validation, variance, A-Trous,
  SVGF, compose, and debug shaders.
- Central immutable Wave 3 frame plan, deferred-history execution transaction,
  evidence identity, convergence/reconstruction/backend profile evaluators, and
  fail-closed combined gate.
- Concurrent Wave 2 source and handoffs are preserved verbatim as the rebase
  baseline except for the two shared integration files, where Wave 2 and Wave
  3 registrations/status text are combined. Wave 3 facts remain in separate
  L4/L5/L7/L8 handoffs.

No abi-v3/ReSTIR record was published. RuntimeConfig tokens remain orthogonal,
and CapabilityTable remains fail-closed because no production Wavefront/L8
Vulkan provider is attached.

## Accepted verification facts

All listed builds used MSBuild 18.9.1, MSVC v145, C++20, `/W4 /WX`, serial
compilation, Vulkan SDK 1.4.328.1, DXC warning-as-error, and Vulkan 1.3
`spirv-val` where shaders are present.

| Scope | Debug | Release | Runtime evidence |
|---|---|---|---|
| Wave 3 central composition before concurrent Wave 2 rebase | pass | pass | `--integration-status` exits 0 without window creation |
| ABI/runtime/Wave 3 contracts | pass | pass | fixture-only contract execution; not GPU evidence |
| L4 Software GPU | pass | pass | 9/9 groups, 197176 assertions; Release Vulkan LBVH smoke/parity, validation 0/0 |
| L5 Hardware RT | pass | pass | module probe sees RTX 4070 Ray Query + RT Pipeline, SBT 32/32/64 |
| L6 Megakernel | pass | pass | CPU/statistical self-tests pass |
| L7 Wavefront | pass | pass | CPU oracles pass; fourteen shaders validate; no full GPU graph |
| L8 Reconstruction | pass | pass | 12 CPU suites; six shaders validate; no GPU visual run |
| Combined traversal gate | pass | pass | CPU/Software/Ray Query/RT Pipeline parity on three corpora, validation 0/0 |

The final Release traversal gate used NVIDIA GeForce RTX 4070 Laptop GPU and
reported:

- canonical triangle: 264 rays, RT Pipeline `0.0625 ms`;
- canonical Cornell: 262 rays, RT Pipeline `0.1321 ms`;
- alpha-mask: 262 rays, RT Pipeline `0.0668 ms`;
- all closest/any results matched the stable-ID/tolerance contract;
- dual-backend L6 relative RMSE and max absolute difference were both zero.

These are fixed-corpus smoke/parity results. They are not portfolio visual or
canonical performance acceptance.

## Concurrent-rebase build boundary

After the accepted Wave 3 builds above, the live project advanced by 27
existing files plus one new Wave 2 handoff. The non-overlapping files were
copied into the persistent shadow as a rebase baseline, while
`Integration.Items.props` and `IntegratedModuleRegistry.cpp` were merged so
neither delivery was discarded.

A clean Debug contract rebuild after that rebase passes with zero warnings and
zero errors, as does a clean Release contract rebuild. A Debug build of the
full rebased central project does not pass: the concurrent Wave 2 production
source currently reports duplicate `NOMINMAX`, intentional-alignment C4324
warnings promoted by `/WX`, and three `UploadBuffer`/`std::span` template
deduction errors. Release was not attempted after this Debug failure. Those
sources are not part of the Wave 3 promotion delta and were not repaired here.
Therefore the earlier central pass is valid Wave 3 composition evidence, but
there is no accepted claim that the current combined Wave 2 + Wave 3 tree
builds.

## Integration gate still open

The combined Wave 3 acceptance result must remain `not-run`, not `accepted`,
until all of the following provider records exist:

- Wavefront and Megakernel GPU convergence under identical immutable identity,
  SPP checkpoints, RNG dimensions, and ray budgets;
- full Wavefront indirect synchronization under Vulkan validation with zero
  queue overflow and live per-bounce telemetry;
- same-input 1-SPP Raw, Temporal, fixed A-Trous, SVGF, and high-SPP Reference
  artifacts in static/camera/rigid/disocclusion scenes;
- L4/L5 build and trace profiles using 120 warmup, 1,000 measured frames, three
  repeats, median/p95, memory, workload counters, and provider GPU timestamps.

No shader compilation, CPU fixture, provider-shaped test sample, or single-run
timestamp is promoted across those proof boundaries.

## Promotion package

The frozen Wave 3 delta contains 73 paths: 28 additions, 45 guarded
replacements, and no deletions. Replacement baselines are the live project
hashes after the concurrent Wave 2 rebase, not the older task-start hashes.
The apply script is validation-only unless invoked with `-Apply`; it checks the
commit, every source hash, every replacement baseline, and every add-path
absence before creating a recoverable backup and copying any file. A failure
rolls back only paths from this manifest. It never stages or commits.

## Reproduction commands

The child environment must clear the duplicate `Path` key before launching
MSBuild. Central builds reuse the project's existing installed vcpkg tree
read-only:

```powershell
cmd.exe /d /c 'set Path=& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" RenderingEngine.Vulkan.vcxproj /m:1 /p:Configuration=Release /p:Platform=x64 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:CL_MPCount=1 /p:VcpkgEnableManifest=true /p:VcpkgManifestInstall=false /p:VcpkgInstalledDir=C:\Users\nitong\source\repos\RenderingEngine\vcpkg_installed\'
```

Use the same serial flags and switch `Configuration` for every lane-local
project. The replacement manifest and guarded apply script live beside the
shadow metadata and are the only authorized later promotion path.
