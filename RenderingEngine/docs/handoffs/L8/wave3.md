# L8 handoff: Wave 3 Temporal and SVGF

- Date: 2026-08-31
- Contract: abi-v2 set 4, numeric ABI version 3
- Status: ABI bridge, CPU reference/tests, shader implementation, central
  composition, and shader validation complete; GPU/dynamic visual acceptance
  not run

## Implemented

- Primary-surface, motion, split signal, GBuffer, and exact-frame history
  packing/unpacking at the abi-v2 boundary.
- Motion `previous jittered UV - current jittered UV`, camera/rigid previous
  transform handling, expected previous positive linear depth, and stable IDs.
- Two-generation by frames-in-flight history mapping with exact completed-frame
  publication and no read/write alias.
- Per-tap bounds/finite/depth/normal/material/object validation, 2x2 weighted
  reprojection, 3x3 closest-valid fallback, and explicit reject masks.
- Temporal first/second moments and history length, short-history 7x7 variance
  bootstrap, A-Trous edge stopping, split diffuse/specular demodulation and
  remodulation, and Raw/Temporal/A-Trous/SVGF/debug composition.
- Reset rules for cuts, FOV, resize, scene/backend/integrator, parameter, and
  shader generations while preserving history during continuous camera/rigid
  motion.

Twelve CPU suites pass, including abi-v2 packing, motion/jitter sign, history
rotation for multiple flight counts, rejection reasons, disocclusion recovery,
and NaN/Inf containment. On the deterministic CPU fixture, Raw RMSE was `0.5`
and A-Trous RMSE `0.00183614`; Raw PSNR was `6.0206 dB` and A-Trous PSNR
`54.7219 dB`. All six compute shaders compile and pass Vulkan 1.3 SPIR-V
validation.

These numbers are CPU fixture evidence, not a 1-SPP GPU scene comparison.

## Open integration gate

- production Vulkan images/descriptors/dispatch and history commit provider;
- static, camera-motion, rigid-motion, thin-geometry, and disocclusion captures;
- same-input Raw/Temporal/A-Trous/SVGF/high-SPP Reference artifacts with
  RMSE/PSNR and human ghosting review.

Capability publication and the combined Wave 3 gate remain fail-closed.
