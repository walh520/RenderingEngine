# Sponza asset gate v1

Status: **fail-closed / not accepted** (2026-08-30)

Wave 2 requires a fixed Sponza version with texture, normal-map, alpha-mask,
multi-instance, build/trace/memory, dual-backend render, and license/hash
evidence. No Sponza payload exists in this repository, so no code path may
report the Sponza scene as available.

## Accepted-source policy

The preferred acquisition candidate is Intel's official Graphics Research
`Sponza` sample: glTF, advertised as CC BY 3.0, approximately 3.71 GB. It must
be downloaded from the official Intel sample page, not copied from an
unverified mirror: <https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html>.
Before acceptance, record:

```text
source_url
download_utc
original_filename
byte_size
sha256
license_name
license_url_or_embedded_file
required_attribution
selected_scene_path
canonical_loader_version
```

The Khronos `glTF-Sample-Assets` Sponza copy is rejected for this milestone:
its repository license text traces to a restricted CryEngine-era grant and an
open upstream issue questions redistribution terms. It must not be silently
vendored: <https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Sponza/LICENSE.md>
and <https://github.com/KhronosGroup/glTF-Sample-Assets/issues/172>. The smaller
McGuire/Casual Effects archive (<https://casual-effects.com/g3d/data10/index.html>)
may be useful as an
algorithm reference, but its OBJ-to-glTF conversion would be a distinct asset
requiring pinned conversion tooling, output hash, and attribution.

## Loader/runtime prerequisites

The current static glTF loader is not yet sufficient for Sponza acceptance. It
does not ingest image bytes or publish texture/sampler atlas payloads, and it
rejects non-rigid node transforms. The production renderer also does not
consume canonical glTF scenes through L4/L5/L6. Downloading an asset alone
therefore cannot close the gate.

Required implementation before acceptance:

1. Load and validate base-color, normal, metallic-roughness, emissive, and
   alpha-mask image/sampler identities without fallback substitution.
2. Define color-space, mip, wrap/filter, atlas/layer, and alpha cutoff behavior.
3. Either accept and correctly transform legal glTF scale or pin a reproducible
   bake/conversion step; never discard the transform.
4. Build a multi-instance canonical scene and exercise rigid TLAS update policy.
5. Run the same CPU SAH / Software GPU / Ray Query corpus and both L6 adapters.
6. Save fixed-seed Raw/Reference captures and report RMSE/PSNR.
7. Record build/refit/trace/memory separately under the 120/1000/3 protocol.

Until every record and gate above passes, `sponza` remains unavailable and no
placeholder, synthetic hall, or license-incomplete copy may be presented as
the fixed Sponza asset.
