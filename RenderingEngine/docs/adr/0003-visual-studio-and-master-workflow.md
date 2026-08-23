# ADR 0003: Visual Studio build and master-only merge workflow

- Status: Accepted
- Date: 2026-08-24
- Scope: Entire repository

## Decision

This repository uses `RenderingEngine.sln`, `.vcxproj`, `.props`, and `.targets`
as its only committed build system. CMake files and presets are not part of this
project.

`master` is the only integration line. L0 owns merge commits, the
solution, shared MSBuild files, public contracts, and release tags. Feature
work may use a `codex/` branch in a separate Git worktree, but no intermediate
integration branch is created.

Feature branches and worktrees are development-isolation tools only. They are
not release sources, shared merge lines, or substitutes for `master`; release
and contract tags are created from reviewed `master` commits.

## Required gates

The canonical build commands are:

```powershell
& <MSBuild.exe> RenderingEngine.sln /t:Rebuild /m:1 `
    /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64

& <MSBuild.exe> RenderingEngine.sln /t:Rebuild /m:1 `
    /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=x64
```

GPU smoke and validation use `RenderingEngine/tools/Validate-Pbr.ps1`. Build,
runtime, Vulkan validation, numerical, and visual evidence remain separate
proof levels.

## Consequences

- Module ownership is expressed through module-local Visual Studio item/project
  manifests and tests, not a central CMake source list.
- L0 alone edits the solution and shared MSBuild imports when composition
  changes.
- Every worktree keeps its own ignored `build/` and `bin/` directories.
- A future build-system change requires an explicit user decision and a new ADR.
