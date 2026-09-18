# Final mixed runtime handoff

Date: 2026-09-07

## Result

The interactive Debug renderer is one mixed runtime. Historical Wave numbers
remain only as ABI and implementation provenance; they no longer partition the
runtime configuration. Scene, traversal backend, integrator, direct-lighting
estimator, light proposal, reconstruction, debug view, and shadow method are
independent controls.

The built interactive catalog contains:

- 9 provider-backed experiment scenes;
- 3 GPU traversal backends: canonical linear, flattened SAH, and Vulkan Ray Query;
- 4 interactive integrators: Whitted, PBR, GPU Megakernel, and GPU Wavefront;
- 5 direct-lighting estimator tokens, including the legacy NEE compatibility token;
- 4 light-proposal tokens, including the legacy uniform compatibility token;
- 4 reconstruction modes: Raw, Temporal, fixed A-Trous, and SVGF;
- 6 attached debug views: Final, Base Color, Normal, Roughness, Metallic, and Emissive;
- 3 shadow methods: Physical, PCF, and PCSS.

This is 155,520 built interactive configurations. The capability test evaluates
the full cross product and requires every one to be supported. ReSTIR DI uses the
same selected scene, backend, integrator, proposal, reconstruction, and shadow
axes rather than installing a private preset.

## Runtime repairs included in the final merge

- Removed the legacy/Wave 2/Wave 3/Wave 4 complete-tuple gate and its warning.
- Routed all interactive integrators through the shared ABI-v2 reconstruction
  output and the ABI-v3 ReSTIR attachment.
- Added canonical-linear traversal as a real standalone software GPU backend and
  allowed it in the Wavefront frame validator.
- Made GPU Megakernel a true single-dispatch complete-bounce implementation.
- Kept PBR and Whitted on the staged route and Wavefront on its queue route.
- Implemented distinct Physical, PCF, and PCSS visibility semantics in both
  Wavefront and ReSTIR, including shadow mode in history identity/reset.
- Bound the canonical material descriptor set for reconstruction Compose so the
  Base Color and Emissive views consume actual scene materials without Vulkan
  descriptor validation errors.
- Kept profiler counters available for staged PBR/Whitted. Only true monolithic
  Megakernel uses the no-profile shader variant.
- Pointed both the authoritative and compatibility Visual Studio project entry
  points at the same executable and the same 1280x720 Many Lights mixed Debug
  profile, with isolated compiler tracking directories.

## Verification performed

- Both `RenderingEngine.Vulkan.vcxproj` and the compatibility
  `RenderingEngine.vcxproj` built in Debug x64 without errors. A final build of
  the authoritative project had no MSB8028 intermediate-directory warning.
- Runtime contract tests passed, including ABI v0-v3, capability, CLI, frame
  graphs, history identity, and ownership.
- Showcase tests passed, including the 155,520-configuration cross product.
- Wavefront CPU/shadow/shader/command-recorder tests passed.
- Megakernel CPU/statistical tests passed.
- Reconstruction tests passed (12 CPU suites plus Vulkan recorder).
- Software traversal tests passed: 9/9 groups and 197,176 assertions.
- ReSTIR tests passed: 10 cases, 50,167 assertions, zero failures; Vulkan smoke
  used the NVIDIA GeForce RTX 4070 Laptop GPU with validation enabled.
- The Wave 2/3 GPU parity gate passed canonical triangle, Cornell, and alpha-mask
  scenes on the RTX 4070 with zero validation errors/warnings and zero relative
  RMSE between software and Ray Query L6 output.
- Validation-enabled live runs passed for canonical-linear/PBR/Raw,
  flattened-SAH/Whitted/Temporal/PCF, Ray Query/Megakernel/A-Trous, and
  canonical-linear/Wavefront/SVGF/PCSS.
- ReSTIR Physical, PCF, and PCSS live runs passed. PCSS was also exercised with
  10,000 lights. A 62-frame ReSTIR/SVGF run crossed both programmed swapchain
  resizes and exited without validation errors.
- The exact Visual Studio F5 Many Lights/Ray Query/Wavefront/ReSTIR/SVGF profile
  ran at 1280x720 with validation enabled and exited cleanly after one frame.
- A live Base Color capture produced PNG, linear EXR, and metadata at
  `.artifacts/final-mixed-proof-20260907/base-color-rayquery`.

## Intentional boundaries

- Sponza remains fail-closed until its pinned external asset and texture-capable
  import path are present. The number-6 scene key reports this resource gate and
  keeps the current scene unchanged.
- GPU LBVH and Vulkan RT Pipeline/SBT remain declared but are not interactive
  production providers. CPU reference tracing remains a separate deterministic
  headless verifier.
- Reservoir-specific debug tokens remain declared but unattached; the six
  material/final views above are the attached interactive cycle.
- Animation flags currently choose a deterministic initial snapshot; they do not
  yet drive continuous live motion.
- Internal render scale and samples-per-frame remain fixed at 1.0 and 1.
- Monolithic Megakernel uses a 1x1 workgroup as an NVIDIA driver-stability
  workaround. Its profiler counters are intentionally unavailable.
- This handoff proves the Debug renderer and the validation paths listed above.
  It is not a Release-performance, convergence, or human visual-quality claim.
