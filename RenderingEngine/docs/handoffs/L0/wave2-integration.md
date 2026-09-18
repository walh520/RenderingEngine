# L0 handoff: Wave 2 dual-backend integration

> Historical gate snapshot. Production-runtime wiring is superseded by
> `docs/handoffs/L0/wave2-production-runtime.md`.

- Date: 2026-08-30
- Scope: Wave 2 integration only
- Contract: published `abi-v1`, numeric version `2`
- Status: Debug composition and canonical triangle/Cornell/alpha fixed-corpus dual-backend gates passed; the full Cornell/Sponza integration gate is not closed

## 1. Implemented scope

- Publishes append-only ABI-v1 GPU sampling/queue records, descriptor ownership,
  traversal interface, mock/legacy adapters, and fixed hit fixture.
- Supplies canonical triangle and Cornell scenes, deterministic identity/
  fingerprint validation, L3 CPU reference, canonical-to-traversal expansion,
  and a static glTF loader.
- Composes L4, L5, L6, and L10 projects in the central Debug solution while
  keeping production capabilities fail-closed where adapters are absent.
- Adds one real-device gate that builds canonical data once, runs CPU SAH,
  flattened Software GPU, and Ray Query on the same ray corpus, then dispatches
  the same L6 estimator through both GPU backends.
- Adds a synthetic two-layer alpha atlas gate, explicit visibility-mask zero
  ray, exclusive endpoint confirmation, and stable emitter pair mapping.
- Corrects integration-status wording so `central-build` is explicitly
  configuration-specific and never implies Debug plus Release.
- Reasserts each directly owned executable's pinned `miniz.dll` after `Build`,
  preventing a shared-output vcpkg clean record from leaving a linked but
  unrunnable Debug binary; absence of the pinned runtime fails closed.

## 2. Explicitly unimplemented scope

- No accepted Sponza asset, hash, license record, texture/normal payload, or
  Sponza render exists. See `docs/contracts/sponza-asset-gate-v1.md`.
- No independent CPU Reference image versus high-SPP GPU convergence report,
  saved Raw/Reference image, RMSE/PSNR trend, or visual acceptance exists.
- L4/L5/L6 remain central-build providers, not production runtime providers.
- L10 has no live Wave 2 telemetry producer attachment.
- No Release build or release packaging was requested or performed.
- No accepted performance sequence was executed.

## 3. Modified files

Primary integration-owned files are:

```text
../RenderingEngine.sln
RenderingEngine.Vulkan.vcxproj
docs/adr/0006-abi-v1-gpu-traversal-contract.md
docs/contracts/abi-v1.md
docs/contracts/integration-status-v1.md
include/contracts/AbiVersionV1.hpp
include/contracts/AbiV1.hpp
include/contracts/GpuRecordsAbiV1.hpp
include/contracts/DescriptorRegistryV1.hpp
resources/shaders/include/contracts/AbiVersionV1.hlsli
resources/shaders/include/contracts/AbiV1.hlsli
resources/shaders/include/contracts/GpuRecordsAbiV1.hlsli
resources/shaders/include/contracts/DescriptorRegistryV1.hlsli
include/rt/gpu/*
src/rt/gpu/*
include/scene/CanonicalScene.hpp
include/scene/GltfCanonicalScene.hpp
include/scene/ExperimentScenes.hpp
src/scene/CanonicalScene.cpp
src/scene/GltfCanonicalScene.cpp
src/scene/ExperimentScenes.cpp
src/scene/RenderingEngine.SceneWave1.vcxproj
src/scene/tests/*
msbuild/lanes/Integration.Items.props
tests/gpu/Wave2/*
src/app/IntegratedModuleRegistry.cpp
tests/contracts/RuntimeControlTests.cpp
docs/contracts/sponza-asset-gate-v1.md
docs/handoffs/L0/wave2-integration.md
```

Lane-specific file lists are in the L4/L5/L6/L10 Wave 2 handoffs.

## 4. Contract version

ABI v1 is human version `v1`, numeric version `2`; ABI v0 remains unchanged.
Descriptor additions are append-only. L6 frame/light/emitter records and L10
telemetry remain private. ABI v2 is not published because primary-signal and
current/previous transform integration is not complete.

## 5. Build evidence

The central `RenderingEngine.sln` Debug `x64` single-node build passed with
MSBuild 18.9.1, MSVC v145, Vulkan SDK 1.4.328.1, and pinned dependencies. The
dedicated Wave 2 gate and Megakernel projects also passed Debug builds. An initial central build
found and then verified fixes for a missing direct `RayHitAbiV0.hpp` include
and a fixture compatibility overload. Standalone Debug rebuilds with manifest
installation disabled verified deterministic app-local `miniz.dll` restoration
and runnable binaries from the pinned local triplet. An earlier online manifest
refresh failure is not accepted as build evidence. No Release configuration was
built.

## 6. Unit/statistical test evidence

- Scene tests: static glTF loader plus canonical triangle/Cornell passed.
- L3: 1,000,000-ray CPU brute-force/SAH parity, zero mismatches/errors.
- L4: 9/9 groups, 197,176 assertions.
- L5 hardware module tests and physical-device probe passed.
- L6 CPU/statistical/energy/variance tests and 16,384-sample GPU probe passed.
- L10 typed telemetry plus showcase/capture serialization tests passed; EXR
  fixture error was zero.

## 7. Vulkan runtime/validation evidence

The dedicated gate passed on NVIDIA GeForce RTX 4070 Laptop GPU:

| Scene | Rays | CPU closest hits | L6 Software/Ray Query relative RMSE | max abs |
|---|---:|---:|---:|---:|
| canonical-triangle | 264 | 37 | 0 | 0 |
| canonical-cornell | 262 | 260 | 0 | 0 |
| canonical-alpha-mask | 262 | 31 | 0 | 0 |

CPU SAH, Software GPU, and Ray Query closest/any results met the stable-ID and
relative `t <= 1e-4` rules. Validation reported `0` errors and `0` warnings;
Software stack overflow/invalid-hit and hard L6 NaN/Inf/PDF/material/frame
counters were zero.

## 8. Visual/capture evidence

Not passed. No saved Cornell/Sponza Raw/Reference pair or human visual review
was produced. The canonical Cornell numerical framebuffer proves renderability
through both adapters at the test resolution, not portfolio image quality.

## 9. Performance data and test conditions

Not passed. Debug timestamp queries were read to catch reversed/invalid GPU
intervals, but the mandatory fixed power/driver/asset conditions, 120 warm-up
frames, 1,000 measured frames, three repetitions, median/p95, and memory/pass
breakdown were not performed. Printed milliseconds must not be reused as a
performance baseline.

## 10. Known risks and rollback

- Strict Wave 2 is not fully closed until Sponza and independent CPU image
  convergence/visual evidence pass.
- The glTF loader currently imports static triangle geometry, factors,
  alpha-mode flags, instances, cameras, punctual lights, and emissive triangle
  lights, but not image/sampler pixel payloads; it rejects non-rigid nodes.
- Production capability remains fail-closed, preventing an unsupported GPU
  tuple from silently falling back to the legacy renderer.
- Evidence builds rely on the already restored, repository-pinned vcpkg tree;
  the executable runtime-copy targets deliberately fail if that tree is absent
  and do not perform an implicit download.
- On this host, `/m:2` and `/m:4` solution builds reproducibly return exit code
  1 with zero reported warnings/errors; single-node central builds and
  independent parallel project builds pass. No root cause is claimed, so a
  parallel central build is not accepted evidence.
- Roll back integration composition and the dedicated Wave 2 gate first, then
  the individual lane files listed in their handoffs. Preserve unrelated dirty
  work and do not reset the worktree.
