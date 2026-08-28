# RenderingEngine.Restir

L9-private CPU reference, tests, benchmark schema, and provisional Compute
shaders for ReSTIR DI. This module does not modify or claim a shared ABI and is
not registered in the root solution.

Build and run Debug:

```powershell
& <MSBuild.exe> RenderingEngine\tests\unit\L9\RenderingEngine.Restir.Tests.vcxproj `
    /t:Rebuild /m:1 /p:BuildInParallel=false /p:UseMultiToolTask=false `
    /p:Configuration=Debug /p:Platform=x64
```

The project compiles five HLSL Compute stages with DXC, validates their SPIR-V,
runs CPU statistical/semantic tests, then attempts an actual Vulkan 1.3 smoke.
The smoke dynamically loads the Vulkan loader, enables the Khronos validation
layer and synchronization validation when available, uploads private inputs,
dispatches Initial -> Temporal -> Spatial (strict ping-pong) -> Visibility ->
Debug with explicit barriers, and reads back reservoirs, direct lighting,
debug/stats buffers, and the RGBA32F debug image. Loader/device absence is the
only skip; Vulkan API or validation errors fail the test executable.

Run the provisional benchmark schema:

```powershell
.\bin\x64\Release\RenderingEngine.Restir.Tests.exe --benchmark
```

Reported `cpu_build_ms` and `cpu_trace_ms` are CPU harness timing only.
`gpu_timing_status`, `gpu_ms`, reference MAE, and RMSE remain explicitly
unmeasured until renderer integration supplies GPU dispatches and images.
