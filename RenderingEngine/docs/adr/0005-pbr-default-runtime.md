# ADR 0005: Promote the implemented PBR path to the runtime default

- Status: Accepted
- Date: 2026-08-27
- Amends: runtime-config-v0, cli-v0
- Preserves: ABI v0 numeric integrator values and Wave 0 capability boundary

## Context

Wave 0 proved that both the analytic Whitted integrator and the progressive PBR
path integrator build and run through the same Vulkan renderer. The original
compatibility default remained Whitted while the PBR implementation was being
researched and integrated.

The renderer's delivery objective is now the coherent PBR pipeline documented
in PBR_IMPLEMENTATION.md: glTF metallic-roughness materials, Cook-Torrance GGX,
visible-normal importance sampling, indirect transport, finite-emitter
next-event estimation, dielectric transport, progressive HDR accumulation, and
PBR Neutral presentation. Keeping Whitted as the implicit startup selection
would leave that integrated pipeline as an opt-in comparison instead of the
renderer-wide result.

## Decision

RuntimeConfig and its legacy RunOptions projection default to Integrator::Pbr.
Omitting --integrator therefore loads PbrPathTrace.comp.spv. The CLI help,
contract tests, runtime validation matrix, README, and implementation record
must all describe and prove the same default.

Integrator::Pbr keeps numeric value 0 and Integrator::Whitted keeps numeric
value 1. The supported capability set remains {pbr, whitted}; no token is
removed or redefined. An explicit --integrator whitted request still loads
WhittedTrace.comp.spv and remains part of regression validation.

## Consequences

- The zero-argument renderer exercises the PBR path end to end.
- Whitted remains available for deterministic A/B comparison and compatibility
  checks, but is never selected as an undocumented fallback.
- The default change is covered at the RuntimeConfig, projected RunOptions,
  help-text, executable-log, shader-build, and Vulkan-runtime layers.
- Historical Wave 0 handoff evidence remains historical and is not rewritten;
  this ADR and subsequent validation evidence describe the amended state.
- Geometry and production capability boundaries are unchanged: the active
  scene is still the analytic Baseline Gallery, and unsupported roadmap
  features continue to return exit code 4 before platform creation.

## Alternatives considered

### Keep Whitted as default and document PBR as optional

Rejected because it contradicts the PBR delivery objective and allows ordinary
launches to bypass the integrated path-tracing implementation.

### Remove Whitted

Rejected because it would discard a useful compatibility and regression oracle
without improving PBR correctness.

### Select an integrator from scene or shadow mode

Rejected because RuntimeConfig keeps those dimensions orthogonal and forbids
silent tuple repair.
