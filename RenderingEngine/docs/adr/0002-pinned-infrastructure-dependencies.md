# ADR 0002: Pin infrastructure dependencies through vcpkg

- Status: Accepted
- Date: 2026-08-23
- Contract: `abi-v0`

## Decision

Use the Microsoft vcpkg builtin registry at commit
`127402f1c75bb3d5ff6bce04b285faa4930a5aca`. Pin every approved infrastructure
port again with a manifest override and consume it only through the owning
Visual Studio module project.

The compatibility Win32/Vulkan baseline does not require vcpkg. Dependency
resolution is opt-in until Wave 1 modules consume the packages, so a clean Wave
0 checkout remains buildable with only Visual Studio and the Vulkan SDK.

## Consequences

- Feature branches cannot silently select another GLFW, VMA, loader, UI, or
  test framework version.
- Committed `.sln/.vcxproj/.props/.targets` files remain machine independent;
  local Visual Studio or environment configuration may locate vcpkg, but no
  absolute vcpkg path is committed.
- Package upgrades are deliberate L0 merge work and require a new ADR.
- Core ray tracing, sampling, reconstruction, and acceleration algorithms do
  not move into third-party libraries.
