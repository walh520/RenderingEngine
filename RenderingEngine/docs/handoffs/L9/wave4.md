# L9 handoff: Wave 4 ABI-v3 ReSTIR DI

## 1. Scope

L9 delivers the ABI, immutable frame plan, shader stages and Vulkan command
recorder source needed for production ReSTIR DI, entirely in the shadow.

## 2. Implemented

ABI-v3 C++/HLSL mirrors, persistent samples/candidates/history/reservoirs,
append-only set-5 bindings, initial/temporal/spatial reuse, reference and
winner visibility prepare/resolve, split direct publication, history publish,
debug output, statistics, exact per-binding minima, barriers, a `2N` history
ring, and fail-closed recorder validation are present.

## 3. Not implemented

No production owner allocates the canonical resources, creates the pipelines,
supplies the complete descriptor tuple, invokes the recorder, or connects Wave
3 reconstruction. There is no ABI-v3 production GPU run.

## 4. Files

Key surfaces are `include/contracts/*V3*`, `include/renderers/ReSTIR*`,
`src/renderers/ReSTIR*`, `include/renderers/VulkanReSTIRRecorder.hpp`,
`src/renderers/VulkanReSTIRRecorder.cpp`, `resources/shaders/restir/*V3*`,
`pbr_restir_candidate_export_v3.hlsl`, and L9/contracts tests.

## 5. Contract version

ABI v3 is numeric `4`. Set 5 is append-only through binding `30`.
Bindings 26-28 are the only current-frame reservoir staging allocations.
`PublishedReservoir` at 29 and `PreviousPublishedReservoir` at 30 are
non-overlapping one-slot views into an owner-allocated `2 * framesInFlight`
history ring. Frame low/high words and all reuse generations are explicit.

## 6. Build

Final shadow evidence includes the same three checks in both Debug and
Release:

1. `RenderingEngine.Contracts.Tests.vcxproj /t:Rebuild` passed all runtime,
   frame-parameter, Wave 3, Wave 4 and ABI v0-v3 contract suites in both
   configurations.
2. `RenderingEngine.Vulkan.vcxproj /t:ClCompile` with `SelectedFiles` set to
   `src/renderers/VulkanReSTIRRecorder.cpp` compiled the production recorder
   source with `/W4 /WX` in both configurations.
3. `RenderingEngine.Restir.Shaders.vcxproj /t:Rebuild` compiled all 17 shader
   items with DXC strict warning-as-error and passed `spirv-val` for Vulkan
   1.3 in both configurations.

The merged root Debug target also compiled the Wave 4 translation units and
validated the V3 shaders before stopping on the pre-existing Wave 2
`NOMINMAX`, C4324 `/WX`, and three `std::span` deduction errors. Root Release
was not run after that known Debug blocker; no complete root application build
is claimed.

## 7. Unit and statistical evidence

Contract tests cover exact pass order/flags/groups, resource minima, map and
TraceAny record strides, generation overflow, frame high word, exact
prior-frame identity, `2N` non-aliasing history slots, visibility budgets,
partial overlaps, `VK_WHOLE_SIZE`, external-layout ownership, debug-image
metadata, AbortFrame recovery, and rejection of short TraceAny batches. These
are host contract predicates; they do not prove a live V3 descriptor update or
GPU dispatch.

## 8. Vulkan runtime evidence

The legacy/private Vulkan smoke passes on an NVIDIA RTX 4070 Laptop GPU with
validation enabled (`nonempty=4`, `visibility=4`, checksum present). It uses
private records/shaders and is not ABI-v3 Wave 4 runtime evidence. Current V3
shaders pass DXC warning-as-error and `spirv-val`; the recorder source also
compiles independently. Neither is ABI-v3 runtime evidence.

## 9. Visual and performance evidence

Not run for V3. No accepted debug images, high-SPP parity, live GPU timestamps,
memory profile, convergence curve, or 100/1k/10k performance cadence exists.

## 10. Risks and rollback

Production must allocate three staging reservoirs and a separate `2N` history
ring, bind exact set-2/set-5 TraceAny ray/hit views, and issue the published
compute/traversal/reconstruction barriers. Missing, undersized, partially
overlapping, whole-size, or mismatched resources fail closed. The promotion
script backs up and rolls back only manifest paths.
