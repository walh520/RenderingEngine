# ADR 0006: ABI v1 GPU records and traversal seam

- Status: Accepted
- Contract: `abi-v1`
- Numeric ABI version: `2`

## Decision

ABI v1 is append-only over the frozen ABI v0 frame and scene records. It adds
shared C++/HLSL layouts for BSDF samples, light samples, path state, and
Ray/Hit/Shadow queue records. Traversal set 2 fixes common ray/hit bindings
while leaving binding 0 backend-specific; set 3 fixes the common Wavefront
queue prefix.

The host boundary is `IGpuTraversalBackend`. It records scene build/update,
closest-hit batches, and any-hit batches without performing per-ray CPU/GPU
readback. A callback adapter preserves the legacy analytic renderer seam, and
a CPU-only recording mock plus fixed hit corpus allow L4/L5/L6 integration to
compile against one contract without importing each other's private modules.

## Consequences

- ABI v0 is unchanged and remains valid for Release 0 frame/scene payloads.
- L4/L5/L6 private layouts require explicit adapters; ABI v1 does not silently
  reinterpret private buffers.
- GBuffer/history and Reservoir layouts remain reserved for ABI v2/v3.
- Publishing the contract does not claim a production L4/L5/L6 renderer path.
