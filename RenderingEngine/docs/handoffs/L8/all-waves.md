# L8 all-waves handoff — Temporal Infrastructure / SVGF

Date: 2026-08-24
Lane: L8 (`reconstruction/`)
Milestone: all L8 steps implemented in the lane-private module
Shared contracts/root build: read-only; no changes

## Delivery status

The complete L8 algorithm slice defined by the plan is implemented under
`RenderingEngine/reconstruction/`: private primary-hit GBuffer records,
camera/rigid motion with same-time-domain previous depth, frame-safe history,
reprojection validation, reset policy,
temporal moments, short-history variance, A-Trous, diffuse/specular
demodulation, comparison outputs, and debug outputs. The CPU reference and six
Vulkan compute shaders follow the same motion sign and rejection rules.

Because the user explicitly deferred scene data validation and authorized code
landing before shared contracts, this is a lane-private implementation. It does
not claim root renderer/descriptor integration or the final dynamic-scene
acceptance gate.

## Implemented behavior

- Motion is `previousUv - currentUv`; the test covers camera transform, rigid
  transform, jitter, negative/invalid clip W, and post-divide overflow. Motion
  validity remains separate from current-surface validity. `MotionVectorsCS`
  writes motion, expected previous linear depth, and validity directly into the
  private GBuffer consumed by temporal/compose. Miss pixels and out-of-range
  current/previous transform indices are cleared before transform-buffer access.
- Reprojection uses pixel-center-aware 2x2 bilinear taps. Each tap passes the
  complete validation independently; surviving weights are normalized. When
  all four fail, a closest-valid 3x3 fallback is attempted.
- Depth validation compares history depth against the current surface point's
  expected depth in the previous camera, rather than mixing current/previous
  time domains.
- Reject bits distinguish no history, reset, screen bounds, invalid current or
  history, invalid motion, non-finite values, depth, normal, material ID, and
  object ID. CPU and HLSL use the same exact-mask behavior.
- History has `2 * framesInFlight` physical resources, consumes every flight
  slot before changing logical generation, and records the exact published
  frame; a stale slot is not treated as the previous frame. Rotation is covered
  for `N=1/2/3/4`, including even counts.
- Camera cut, FOV, resolution, scene, backend, integrator, reconstruction
  parameter, and shader reload reset temporal resources. Continuous camera and
  rigid motion do not.
- Temporal accumulation tracks capped history length and first/second
  luminance moments with independently clamped color/moment alpha.
- Histories shorter than four frames blend temporal variance with a 7x7
  depth/normal/stable-ID-aware spatial estimate.
- A-Trous uses a 5x5 B3-spline kernel at increasing dilation, skips out-of-range
  taps, applies depth/normal/luminance edge stopping, blocks stable-ID crossing,
  and propagates variance with squared normalized weights while preserving the
  configured minimum variance in both CPU and HLSL.
- All temporal/spatial signals, geometry, moments, variance, and bounded history
  lengths are finite-checked. Bad taps are skipped, center failures use finite
  fallbacks, normals are normalized/clamped, and HLSL padding is zeroed.
  Non-finite current signals retain an explicit reject bit while emitting finite
  zero temporal/history payloads with `history.valid=0`; the following frame
  demonstrably rejects that history instead of reusing contamination.
- Diffuse is albedo-demodulated before accumulation/filtering and remodulated
  for display. Specular follows the same explicit default strategy and can be
  configured to remain modulated.
- Outputs: Raw, Temporal, A-Trous-only, SVGF, motion, history length, moments,
  variance, accepted/rejected history, and reject-reason color.

## Evidence

### Static evidence

Passed by inspection:

- five C++ public/private headers, three C++ sources, one CPU test source;
- one shared private HLSL include and six compute shaders;
- module-local `.vcxproj` and `.filters`; root solution/project untouched;
- HLSL storage records padded to Vulkan-valid relaxed-layout strides;
- all six rebuilt SPIR-V modules contain no `OpUndef` after deterministic
  record/padding initialization;
- source writes are confined to `reconstruction/**` and this L8 handoff.

### Build evidence

Passed in both configurations:

```text
MSBuild 18.9.1, Debug|x64 and Release|x64
/std:c++20 /W4 /WX (from RenderingEngine.Common.props)
RenderingEngine.Reconstruction.Tests.vcxproj ->
  reconstruction/build/x64/{Debug,Release}/RenderingEngine.Reconstruction.Tests.exe
exit 0
```

The inherited desktop environment exposed duplicate case variants of `Path`;
the build was run in a child process with a de-duplicated environment block.
No system environment was changed.

All six shaders passed both stages:

```text
DXC: -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -Ges -WX -T cs_6_6
spirv-val: --target-env vulkan1.3
MotionVectorsCS, PrepareSignalCS, TemporalAccumulationCS,
VarianceBootstrapCS, ATrousCS, ComposeCS
exit 0
```

### Runtime evidence

Passed for the CPU reference executable:

```text
L8 reconstruction tests passed (11 suites).
exit 0
```

The suites cover motion sign/jitter/positive-W/finite projection, `N=1/2/3/4`
history resource indexing, every rejection class and exact masks, same-time-
domain depth validation, invalid-motion recovery, albedo round-trip, temporal
accumulation/moments, all reset classes, resize/config reset, 2x2 failure plus
3x3 fallback, disocclusion rejection, edge stopping, NaN/Inf containment,
variance bootstrap, metrics, and every output surface.

Vulkan renderer dispatch and validation-layer runtime were not run because L8
cannot modify the shared renderer/root wiring in this delivery.

### Numerical evidence

Passed only for deterministic CPU fixtures:

```text
16x16 checkerboard, reference=(1,1,1)
Raw RMSE       0.500000
A-Trous RMSE   0.00183614
Raw PSNR       6.0206 dB
A-Trous PSNR  54.7219 dB
```

Additional exact assertions cover temporal history length `1 -> 2`, a
reprojected temporal value `1/3 -> 2`, and moments `(first, second) = (2, 5)`.

Cornell/Sponza or Temporal Stability Corridor RMSE/PSNR against a high-SPP
reference, moving-scene ghosting, and GPU/CPU numerical parity are deferred as
requested; they are not reported as passed.

### Visual evidence

Not executed. No screenshot or final renderer image is claimed. The debug
surfaces and compose shader exist, but visual acceptance requires the shared
renderer adapter and canonical moving/disocclusion scene.

## Integration handoff

When shared integration resumes, the owning integration lane must:

1. pack primary-hit outputs into `L8GBufferRecord`, preserving linear-depth,
   stable-ID, previous-transform, previous-view depth, and jitter semantics;
2. allocate two history generations per frame-in-flight and synchronize the
   exact prior-frame read before dispatch;
3. dispatch Prepare -> Temporal -> Variance -> A-Trous ping-pong -> Compose,
   including variance ping-pong for every A-Trous iteration;
4. map runtime reset events to `ResetTrigger` and expose all debug outputs;
5. run Vulkan validation, dynamic camera/rigid/disocclusion captures, and
   fixed-input GPU/CPU plus high-SPP RMSE/PSNR acceptance.

No RT traversal, integrator, shared contract, descriptor registry, root project,
solution, scene, or UI file was modified by L8.
