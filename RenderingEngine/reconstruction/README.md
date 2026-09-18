# L8 Temporal Reconstruction

This directory is the L8 temporal/SVGF implementation. `include/reconstruction`
and `src` provide the deterministic CPU reference and the explicit abi-v2
packing bridge; `shaders` provides the Vulkan 1.3 compute implementation. The
Wave 3 shadow composes the shared abi-v2 boundary centrally, but the production
renderer still has no Vulkan resource/dispatch provider for these passes.

## Private data contract

`GBufferPixel` / `L8GBufferRecord` carry current linear depth, expected previous
linear depth, world normal, diffuse and specular albedo, stable material/object
IDs, independent surface/motion validity, and motion. Motion uses one sign
convention throughout:

```text
motionUv = previousJitteredUv - currentJitteredUv
historyUv = currentUv + motionUv
```

`ComputeMotionVector` projects the same object-space hit position through both
the current and previous object-to-world and view-projection matrices. Camera
and rigid motion therefore use the same code path. The previous world-to-view
matrix also produces positive `-view.z` depth, so history depth is compared to
the same surface point in the same time domain. Jitter is expressed in UV units
and included explicitly. The GPU motion pass writes these fields directly into
the private GBuffer consumed by temporal and compose. Miss pixels and transform
indices outside the explicitly supplied current/previous transform counts are
cleared before either transform SSBO is indexed.

These records are L8-private working types, not a replacement shared ABI.
`AbiV2Bridge` packs and unpacks the published primary-surface, motion, signal,
GBuffer, and history records without changing their sign, depth, or identity
semantics.
The HLSL storage-buffer records are padded to Vulkan relaxed-layout-safe 16-byte
strides and validate without requiring scalar block layout.

## Pass graph

```text
MotionVectorsCS
  -> PrepareSignalCS (diffuse/specular albedo demodulation)
  -> TemporalAccumulationCS
       motion-valid + expected-previous-depth validation
       2x2 per-tap validation + surviving-weight normalization
       all-taps-failed 3x3 closest-valid fallback
       history length + first/second luminance moments
  -> VarianceBootstrapCS (7x7 for short histories)
  -> ATrousCS x N (signal and squared-weight variance ping-pong)
  -> ComposeCS (Raw / Temporal / A-Trous / SVGF / debug)
```

The CPU reference runs two spatial paths: A-Trous-only starts from the current
demodulated sample and spatial variance, while SVGF starts from the temporal
signal and bootstrapped variance. Both remodulate diffuse and, by default,
specular at output. Specular demodulation can be disabled explicitly.

History resources use two logical generations, each replicated per
frame-in-flight. Every slot in one generation is consumed before advancing to
the other, so even flight counts traverse all `2*N` resources. A write resource
never aliases the exact previous-frame read resource. Publishing a frame records
the exact frame number, so stale slots are not silently reused.

## Validation and reset behavior

Every reprojected tap is checked independently for:

- screen bounds, motion validity, and finite values;
- current/history validity;
- relative plus absolute linear-depth difference;
- world-normal cosine;
- stable material ID and object ID.

Moments, variance, bounded history length, geometry, signals, and every spatial
tap are finite-checked. Invalid samples cannot poison their 7x7 variance or
A-Trous neighborhoods. Spatial normal weights normalize both normals and clamp
their cosine; every A-Trous iteration preserves minimum variance. Shader padding
is deterministically zero-initialized. A non-finite current signal still records
the `NonFinite` reject bit, but temporal/history payloads use finite zero
signal/moments and publish the history as invalid, preventing next-frame reuse.

If at least one 2x2 tap passes, only passing weights are normalized. If all four
fail, a 3x3 footprint is searched for the closest fully valid tap. Otherwise a
bitmask records the exact reject reasons. Debug selection exposes acceptance,
reasons, motion, history length, moments, and variance.

Temporal history resets on camera cut, FOV, resolution, scene, backend,
integrator, reconstruction-parameter, or shader-reload triggers. Continuous
camera/rigid motion is not a reset trigger and relies on reprojection.

## Local build

The lane project can be built directly; L0's central item composition is a
separate build fact and does not make the provider production-ready:

```powershell
msbuild RenderingEngine.Reconstruction.Tests.vcxproj /m /p:Configuration=Debug /p:Platform=x64
.\build\x64\Debug\RenderingEngine.Reconstruction.Tests.exe
```

The executable runs twelve suites, including abi-v2 bridge coverage,
`N=1/2/3/4` history rotation,
camera/rigid depth-domain reprojection, invalid-motion recovery, exact rejection
masks, and NaN/Inf containment. Shaders use entry point `main`, profile
`cs_6_6`, and are compiled with DXC for `vulkan1.3`. The host integration must
ping-pong both signal and variance for each A-Trous iteration and bind a distinct
history resource for every logical generation/frame-in-flight pair.

No lane-local CPU fixture, shader compilation, or provider-shaped execution
fixture is GPU runtime, dynamic-scene visual, ghosting, or convergence evidence.
