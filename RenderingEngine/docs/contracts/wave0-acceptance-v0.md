# Wave 0 acceptance contract

Status: **normative gate**

Wave 0 establishes a trustworthy parallel-development baseline. It does not
claim that the later ray-query, scene, sampling, reconstruction, UI, capture,
benchmark, or headless renderer lines are implemented.

## Required deliverables

- a recoverable baseline checkpoint on `master`;
- Visual Studio solution/project build graph and repeatable MSBuild commands;
- pinned infrastructure dependency manifest without enabling unused restore in
  the compatibility renderer;
- application/platform composition seam that keeps renderer code free of native
  Win32 message and surface details;
- canonical ABI v0 C++/HLSL headers, descriptor ownership, golden offsets, and
  static layout/probe tests;
- `RuntimeConfig`, CLI, exit codes, capability table, and deterministic artifact
  layout planning;
- accepted ADRs for ABI, dependencies, Visual Studio/Git workflow, and runtime
  control-plane ownership;
- a Wave 0 handoff that separates proof levels and lists deferred work.

## Build and test gate

The acceptance run must record each result independently:

| Proof | Required Wave 0 evidence |
|---|---|
| Static | public ownership, include boundaries, and no committed CMake inputs |
| Debug build | complete `RenderingEngine.sln` build with warnings as errors |
| Release build | complete `RenderingEngine.sln` build with warnings as errors |
| C++ ABI | test executable compiles/runs through its VS project in the production language mode |
| Runtime control | the same VS test executable covers parsing, capability status, artifact planning, defaults, and legacy projection without claiming platform/GPU execution |
| Shader ABI | its MSBuild target runs DXC `-spirv -fvk-use-dx-layout -Ges -WX`, SPIR-V validation, golden offsets, and ABI/descriptor constant comparison with failure-safe stamps |
| Runtime | existing Whitted/PBR, shadow and debug selections plus resize smoke |
| Vulkan validation | the executed runtime suite reports no validation warning/error |
| CLI/control plane | execute help/version, invalid=2, unsupported=4 before window/I/O; verify exception-to-10 by static or controlled-failure evidence and label which |

Static ABI evidence proves layout and legal SPIR-V only. It does not prove GPU
sentinel round-trip, traversal correctness, numerical rendering parity, image
quality, performance, or visual acceptance.

## Wave 0 capability boundary

Only `baseline + legacy-analytic-gpu + (whitted|pbr) +
legacy-analytic-direct + legacy-analytic proposal + raw` is an implemented
algorithm tuple. The existing physical/PCF/PCSS comparisons and final/material
debug views remain available.

The following are deliberately not Wave 0 completion criteria:

- GLFW replacement of the compatibility Win32 host;
- canonical triangle scene loading or glTF;
- CPU/software/hardware traversal backends;
- CPU reference, megakernel, or Wavefront path tracing;
- NEE/MIS/ReSTIR implementations;
- temporal/A-Trous/SVGF reconstruction;
- production headless rendering, image capture, benchmark, or reference output;
- nine future algorithm exhibition spaces beyond Baseline Gallery.

Recognized requests for these features must be rejected explicitly. A stub,
empty artifact, hidden fallback, parsed token, or test-only mock is not
acceptance evidence.

## Repository workflow gate

`master` is the only integration line. Feature implementation may occur on
isolated `codex/` branches and Git worktrees, but there is no dedicated
integration branch. The committed build graph is Visual Studio/MSBuild only;
the repository contains no CMake file or preset.
