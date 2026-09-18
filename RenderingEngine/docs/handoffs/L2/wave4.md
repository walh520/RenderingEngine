# L2 handoff: Wave 4 Many Lights scene provider

## 1. Scope

L2 supplies deterministic scene-side data for the Wave 4 Many Lights arena in
the persistent shadow only.

## 2. Implemented

The provider builds 100, 1,000 and 10,000-light tiers with stable light IDs,
deterministic animation, bounded topology changes, current-to-previous and
previous-to-current mappings, and explicit history generations.

## 3. Not implemented

The provider is not called by the production application and does not allocate
or upload ABI-v3 Vulkan buffers.

## 4. Files

Primary files are `include/scene/ManyLightsArena.hpp`,
`src/scene/ManyLightsArena.cpp`, `include/scene/ManyLightsSceneProvider.hpp`,
`src/scene/ManyLightsSceneProvider.cpp`, and their scene tests/project entries.

## 5. Contract version

Provider output targets RuntimeConfig v1 and ABI-v3 set-5 light-table/map
semantics. Stable IDs and per-light generations, not table positions, own
history validity.

## 6. Build

The scene library and tests pass in Debug and Release with `/W4 /WX`; root
Debug reaches the Wave 4 sources before unrelated pre-existing Wave 2 errors.

## 7. Unit and statistical evidence

All three tiers pass deterministic count/identity checks. Reorder/move/delete
coverage reports retained `99`, added `1`, and deleted `1` in the focused
topology test, with mapping and history reset behavior verified.

## 8. Vulkan runtime evidence

Not run. No L2 production upload, descriptor binding, or GPU consumption was
attached.

## 9. Visual and performance evidence

Not run. Construction tests do not establish 10k-light frame time, memory,
image quality, or motion stability.

## 10. Risks and rollback

Production must preserve mapping direction and generation identity when it
uploads buffers 20/21. A mismatch must reset history. Rollback is the scoped
Wave 4 promotion backup; no live file has been changed by this handoff.
