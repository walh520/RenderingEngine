# L6 handoff: Wave 4 primary direct-light ownership

## 1. Scope

L6 exposes primary-hit candidates and freezes the ownership boundary needed by
ReSTIR DI; work is shadow-only.

## 2. Implemented

At primary non-emissive, non-delta hits, ReSTIR DI is the sole direct-light
owner. Conventional primary NEE/MIS is suppressed for that tuple. Emitter
emission, delta paths, and secondary-bounce MIS remain under their existing
owners. Canonical software and Ray Query variants export persistent ABI-v3
candidate data.

## 3. Not implemented

No production dispatch selects the ABI-v3 candidate-export shader and no
runtime buffer connects its output to the ReSTIR recorder.

## 4. Files

The contract spans `RuntimeConfig.hpp`, `pbr_l6_types.hlsli`,
`pbr_canonical_megakernel.hlsl`, `pbr_megakernel.hlsl`,
`pbr_restir_candidate_export_v3.hlsl`, Megakernel bridge/tests, and shader
registrations.

## 5. Contract version

The appended primary direct owner is ReSTIR DI (`3`) and candidate payloads use
ABI-v3 persistent sample/candidate records. Older ABI meanings are unchanged.

## 6. Build

Debug and Release Megakernel CPU/statistical projects pass. Canonical software,
Ray Query, and candidate-export shader variants compile and validate for
Vulkan 1.3.

## 7. Unit and statistical evidence

CPU ownership tests confirm primary conventional direct suppression and
preservation of emitter/secondary paths. Existing statistical checks remain
green. They do not prove the production GPU path selects the new owner.

## 8. Vulkan runtime evidence

Not run for ABI v3. Shader compilation and SPIR-V validation are static gates,
not dispatch evidence.

## 9. Visual and performance evidence

Not run. There is no accepted double-counting capture, convergence comparison,
or GPU timing for the production composition.

## 10. Risks and rollback

The main integration risk is double ownership if a future application enables
ReSTIR without selecting the suppression branch. Capability remains
fail-closed until the whole tuple is attached. Rollback uses the Wave 4
manifest backup only.
