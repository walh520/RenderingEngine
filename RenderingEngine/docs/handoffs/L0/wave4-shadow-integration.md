# L0 handoff: Wave 4 persistent shadow integration

## 1. Scope

Wave 4 is implemented only under `build/codex-shadow/wave4-20260830/work`.
The live project is a read-only promotion target. This handoff freezes the
integration truth needed for a later guarded replacement.

## 2. Implemented

RuntimeConfig/CLI v1, ABI v3, Many Lights scene/history contracts, L6 direct
ownership, ReSTIR frame planning/constants/shaders/recorder, Wave 3
reconstruction handoff, four-leg showcase evidence, and central project
registration are composed in the shadow. Capability selection remains
fail-closed.

## 3. Not implemented

There is no production application attachment, production ABI-v3 allocation
or pipeline factory, live L6-to-ReSTIR-to-L8 Vulkan dispatch, accepted V3 GPU
capture, numerical convergence result, visual acceptance, or performance run.

## 4. Files

Shared integration is registered through `msbuild/lanes/Integration.Items.props`,
`msbuild/lanes/Renderer.Shaders.Items.props`,
`src/app/IntegratedModuleRegistry.cpp`, and the lane project/test manifests.
The exact promotion surface is defined by the shadow metadata manifest.

## 5. Contract version

Wave 4 publishes ABI v3 numeric `4` and RuntimeConfig/CLI v1. Wave 3 remains
ABI v2 numeric `3`; the attached Wave 2 runtime remains ABI v1 numeric `2`.

## 6. Build

Clean Debug and Release lane builds/tests pass under MSBuild 18.9.1, MSVC v145,
C++20 and `/W4 /WX`. The merged root Debug build compiles the new Wave 4
translation units and shaders, then stops on the same pre-existing Wave 2
`NOMINMAX`, C4324 `/WX`, and `std::span` deduction blockers recorded by Wave 3.
Root Release was not run after that Debug blocker.

## 7. Unit and statistical evidence

ABI v0-v3, runtime/CLI, frame order/history/ownership, frame parameters,
Wave 3 predecessor, Wave 4 acceptance, Many Lights 100/1k/10k, topology
mapping, L6 CPU/statistical, and four-leg serialization tests pass. These are
CPU/static contract evidence, not production GPU evidence.

## 8. Vulkan runtime evidence

All current Wave 4 shader entries compile with DXC `-Ges -WX` for Vulkan 1.3
and pass `spirv-val`. The L9 legacy/private smoke passes on an RTX 4070 Laptop
GPU with validation enabled; it does not execute ABI-v3 production shaders or
the Wave 4 frame graph.

## 9. Visual and performance evidence

No Wave 4 portfolio image, independent high-SPP comparison, live GPU timing,
120/1000/3 cadence, memory result, or visual/performance acceptance was run.
Any fixture EXR equality remains test-fixture evidence only.

## 10. Risks and rollback

The open risk is production composition, not source registration. Promotion
must use the generated three-way script: it verifies base hashes and add-path
absence, creates a scoped backup, applies only manifest paths, and rolls those
paths back on failure. It never stages or commits.
