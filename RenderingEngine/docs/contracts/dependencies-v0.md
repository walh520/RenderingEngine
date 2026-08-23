# Wave 0 dependency manifest

Status: **frozen for `abi-v0`**
Registry: Microsoft vcpkg builtin registry
Registry baseline: `127402f1c75bb3d5ff6bce04b285faa4930a5aca`

The committed `vcpkg.json` and `vcpkg-configuration.json` are the only
authoritative dependency version source. The compatibility
`RenderingEngine.Vulkan.vcxproj` does not enable manifest restore, so normal
Wave 0 baseline builds do not download these packages. Each Wave 1+ module
enables restore only when it actually consumes its declared dependencies.

| Purpose | vcpkg port | Frozen version | Visual Studio/MSBuild consumer contract |
|---|---|---:|---|
| Window, Vulkan surface, input | `glfw3` | 3.5.1 | `glfw` through the module `.vcxproj` |
| Vulkan allocation | `vulkan-memory-allocator` | 3.4.0 | `vk_mem_alloc.h` in the Vulkan Core project |
| glTF parsing | `cgltf` | 1.15 | `cgltf.h` in the Scene project |
| Image loading/writing | `stb` | 2024-07-29#1 | `stb_image.h` / `stb_image_write.h` in the asset/capture projects |
| OpenEXR test/capture I/O | `tinyexr` | 3.2.0 | `tinyexr.h` in the capture/reference projects |
| Debug UI | `imgui[glfw-binding,vulkan-binding]` | 1.92.8#1 | `imgui` plus the GLFW/Vulkan bindings in the UI project |
| CPU unit/statistical tests | `catch2` | 3.15.3 | `Catch2WithMain` in test `.vcxproj` files |

Feature modules consume dependencies only from their own Visual Studio project
and may not add ad-hoc copies or another package manager. A dependency upgrade
requires an ADR, a registry baseline change, an explicit override update, and
Debug/Release plus affected runtime validation.

The renderer algorithms remain project code. These packages provide only the
infrastructure roles listed in the table.
