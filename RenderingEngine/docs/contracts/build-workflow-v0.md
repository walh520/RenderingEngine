# Build workflow v0

## Authoritative inputs

```text
RenderingEngine.sln
RenderingEngine/RenderingEngine.Vulkan.vcxproj
RenderingEngine/RenderingEngine.Vulkan.vcxproj.filters
RenderingEngine/tests/contracts/RenderingEngine.Contracts.Tests.vcxproj
RenderingEngine/msbuild/RenderingEngine.Common.props
RenderingEngine/msbuild/lanes/*.Items.props
RenderingEngine/msbuild/*.targets
RenderingEngine/msbuild/Verify-AbiV0Offsets.ps1
```

No CMake file is permitted in the repository. Visual Studio/MSBuild is the
definition of the build graph; IDE state and generated `build/`, `bin/`, `out/`,
`.vs/`, `.artifacts/`, and `vcpkg_installed/` directories are not source.

## Merge topology

```text
codex/rt-<feature> in its own worktree
        |
        | handoff + L0 review + required evidence
        v
master (only integration line; no dedicated integration branch)
        |
        v
annotated contract/release tags
```

Feature branches do not merge one another and do not edit the central solution.
When a new module project must enter the solution, its branch delivers the
module-local project/item manifest and L0 performs the solution composition on
`master`.

A feature worktree is temporary development isolation only. Contract and
release tags come from reviewed `master` commits.

## Wave 0 project ownership

- `RenderingEngine.Common.props` freezes C++20, `/W4 /WX`, conformance, UTF-8,
  and shared include roots.
- `Baseline.Items.props` owns the compatibility executable's pre-Wave-0 items.
- `Wave0.Platform.Items.props`, `Wave0.App.Items.props`, and
  `Wave0.Contracts.Items.props` own their new seams without placing parallel
  source lists back in the central project.
- `Renderer.Shaders.Items.props` owns the existing shader inputs.
- `VulkanShaders.targets` builds and validates production SPIR-V.
- `RenderingEngine.Contracts.Tests.vcxproj` plus `AbiV0Validation.targets`
  compiles/runs the C++ layout assertions and RuntimeConfig/CLI/capability/
  artifact-layout tests, compiles the ABI HLSL probe, validates SPIR-V, compares
  the published golden layout, and verifies the HLSL ABI-version and
  descriptor-registry constants.

Shader and ABI validation targets use success stamps as their incremental
outputs. A failed invocation removes/withholds the stamp, so a previous SPIR-V
file cannot make the next build appear green.

Only projects with executable source or an executable test are created. A
future line adds its module-local project/item manifest when implementation
begins; empty projects are not completion evidence.

## Canonical commands

```powershell
& <MSBuild.exe> RenderingEngine.sln /t:Rebuild /m:1 `
    /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> RenderingEngine.sln /t:Rebuild /m:1 `
    /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=x64
```

For isolated ABI diagnosis, replace the solution with
`RenderingEngine/tests/contracts/RenderingEngine.Contracts.Tests.vcxproj`.
That command is useful evidence for the ABI layer, but it does not replace the
complete solution gate.

## Evidence levels

1. Static inspection: project imports, public contracts, shader inputs.
2. Debug and Release build: warnings are errors.
3. CPU contract/unit tests.
4. GPU runtime smoke.
5. Vulkan validation clean.
6. Numerical/reference comparison where applicable.
7. Visual acceptance where applicable.

Only executed levels may be reported as passed.
