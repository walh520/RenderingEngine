# L6 handoff: Wave 2 GPU Megakernel Path Tracer and MIS

> Historical lane-gate snapshot. Production-runtime wiring is superseded by
> `docs/handoffs/L0/wave2-production-runtime.md`.

- Date: 2026-08-30
- Scope: Wave 2 (`GPU Megakernel PT + MIS`)
- Contract: `abi-v1`, numeric version `2`; private L6 frame/light payload
- Status: algorithm, Debug build, CPU/statistical tests, shader validation, and real dual-backend GPU gate passed; independent CPU image convergence and visual/performance acceptance remain open

## 1. Implemented scope

- Implements matching BSDF Evaluate/Sample/PDF for Lambert, GGX VNDF
  conductor/reflection, smooth glass, rough dielectric transmission, and
  metallic-roughness routing with explicit discrete/solid-angle measures.
- Uses Philox4x32-10 dimension mapping, Vose one-light selection, lat-long
  environment importance, point/directional/spot/sphere/emissive-triangle/
  environment sampling, NEE, and power-heuristic MIS.
- Handles multi-bounce transport, Russian roulette, emitter/environment-hit
  weighting, Beer attenuation, and direct/indirect diffuse/specular signals.
- Rejects invalid/non-finite/PDF states through named counters instead of
  silently clamping them; frame validation also rejects the sole uint32 sample
  index that would wrap the online-mean divisor to zero.
- Selects flattened Software GPU or Hardware Ray Query without changing the
  estimator or RNG dimensions.
- Maps emissive hits and sampled-emitter shadow exclusion by the stable pair
  `(instanceId, primitiveId)`. The host sorts and rejects duplicate keys; the
  shader performs a bounded binary lookup, and both triangle sampling and its
  MIS PDF evaluation verify the same pair before using the light record.

The mathematical review used Heitz's GGX VNDF paper and PBRT v4's microfacet,
MIS, rough-dielectric, and path-tracer derivations:
<https://jcgt.org/published/0007/04/01/>,
<https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory>,
<https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency>,
<https://www.pbr-book.org/4ed/Reflection_Models/Rough_Dielectric_BSDF>, and
<https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer>.

## 2. Explicitly unimplemented scope

- No independent CPU Reference image was compared with high-SPP GPU Cornell,
  Glossy, Glass, or HDRI output; therefore the L6 convergence merge gate is
  still open.
- The real GPU gate uses canonical directional/emissive-triangle lights and a
  material-focused sampling probe; not every generic light/environment path
  has real-device scene coverage.
- No production Vulkan dispatch, Sponza render, saved image, human visual
  acceptance, or accepted performance run exists.
- Wavefront integration is Wave 3. A four-argument fixture compatibility
  overload preserves its existing build but does not extend Wave 3 behavior.

## 3. Modified files

```text
integrators/megakernel/MegakernelBridge.hpp
integrators/megakernel/MegakernelBridge.cpp
integrators/megakernel/Megakernel.Tests.vcxproj
integrators/megakernel/tests/MegakernelCpuTests.cpp
resources/shaders/include/bsdf/PbrBsdf.hlsli
resources/shaders/integrators/pbr_l6_types.hlsli
resources/shaders/integrators/pbr_light_sampling.hlsli
resources/shaders/integrators/pbr_megakernel.hlsl
resources/shaders/integrators/pbr_fixture_traversal.hlsli
resources/shaders/integrators/pbr_traversal_resources.hlsli
resources/shaders/integrators/pbr_traversal_select.hlsli
resources/shaders/integrators/pbr_software_traversal.hlsli
resources/shaders/integrators/pbr_ray_query_traversal.hlsli
resources/shaders/integrators/pbr_canonical_megakernel.hlsl
resources/shaders/integrators/pbr_sampling_probe.hlsl
tests/gpu/Wave2/RenderingEngine.Wave2TraversalGate.vcxproj
tests/gpu/Wave2/Wave2TraversalGate.cpp
docs/handoffs/L6/wave2.md
```

## 4. Contract version

Traversal consumes ABI v1 (numeric `2`). L6 material/light/frame records remain
module-private and have compile-time host sizes plus DX-layout shader
validation. The new emitter-map entry is 16 bytes and is sorted by stable
instance/primitive IDs. No public ABI-v2 record or descriptor is published.

## 5. Build evidence

- Debug `x64` central solution passed.
- `Megakernel.Tests.vcxproj` Debug build passed.
- Generic fixture, flattened-SAH, Ray Query, and sampling-probe shaders compiled
  with Vulkan SDK DXC SM 6.6 and passed `spirv-val` for Vulkan 1.3.
- No Release build was performed.

## 6. Unit/statistical test evidence

- CPU self-tests passed Philox known-answer/range/dimension tests; Vose and
  environment distributions; frame/measure/estimator validation; duplicate
  emitter-key rejection; emitter MIS weights; and BSDF consistency/energy.
- Lambert PDF integration and furnace response passed the `0.5%` rule. GGX
  models passed numerical PDF-mass, empirical mass/moment confidence gates,
  Fresnel-frequency confidence, Sample/PDF consistency, and energy bounds.
- Equal-ray-budget analytic test found NEE and MIS variance below BSDF-only
  while all estimator means matched the same integral.
- GPU sampling probe: 16,384 samples, six material models, uniform-hemisphere
  chi-square `23.3027` (threshold `45`), maximum CPU/GPU field error `0.0046`.

## 7. Vulkan runtime/validation evidence

On the RTX 4070 Laptop GPU, canonical triangle, Cornell, and synthetic
alpha-mask each dispatched 16 x 16 x 8 SPP through both traversal adapters.
Every run reported relative RMSE `0`, maximum absolute difference `0`, matching
camera-ray counts, zero hard numerical/invalid counters, and Vulkan validation
`0` errors / `0` warnings. Cornell mean luminance was non-zero.

`InvalidBsdfSample` was 112 for each Cornell backend. This is reported rather
than hidden: GGX visible-normal samples whose reflected direction falls below
the macrosurface are valid zero-contribution rejections; backend counts must
match, while negative/non-finite PDF, BSDF, throughput, radiance, material, and
frame counters remain zero.

## 8. Visual/capture evidence

Not run. GPU readback arrays were inspected numerically only. No fixed-camera
Raw/Reference EXR, RMSE/PSNR against the independent CPU path, or human visual
review was produced.

## 9. Performance data and test conditions

Not accepted. The displayed L6 timestamp-query values are single Debug-gate
diagnostics at 16 x 16 x 8 SPP. They do not meet the 120-frame warm-up,
1,000-frame measurement, three-repeat median/p95, fixed-power, memory, or
portfolio-resolution protocol.

## 10. Known risks and rollback

- Dual-GPU-backend equality proves adapter consistency, not physical
  correctness; the independent CPU high-SPP image oracle remains mandatory.
- Sponza texture/normal/alpha payload ingestion is not complete.
- The dedicated runtime gate has one TLAS instance. CPU tests prove the sorted
  same-primitive/different-instance map, but real multi-instance GPU coverage
  is still open.
- Roll back the listed L6 records/shaders/tests together because their private
  layouts are coupled. The published ABI-v1 traversal contract remains valid.
