# Command-line interface v0

Status: **frozen application contract; Wave 1 capture implementation attached**
Configuration contract: `runtime-config-v0`

The command line is a deterministic front end to `RuntimeConfig`. It is not a
second renderer configuration path. Parsing and capability validation finish
before creating a platform host, Vulkan object, or artifact directory.

## General behavior

- Options and enum tokens are case-sensitive ASCII.
- Repeated options use the last occurrence. This also applies across aliases
  such as `--max-depth`/`--max-bounce`. Capture and general artifact roots are
  still compared after final normalization and must agree.
- Unknown options, missing values, malformed values, and out-of-range values
  are invalid CLI requests.
- A known roadmap token whose implementation is absent is unsupported, not
  invalid and never a reason to select a different mode.
- `--help`, `--version`, and `--integration-status` succeed without creating a window.
- `--max-depth` is the compatibility spelling of `--max-bounce`.
- `--light-sampler` is the compatibility spelling of
  `--direct-lighting`; both choose the estimator, never the proposal.
- Omitting `--integrator` selects `pbr` according to RuntimeConfig v0 as
  amended by ADR 0005. `--integrator whitted` remains an explicit supported
  comparison and never becomes an implicit fallback.

## Options

| Option | Value | RuntimeConfig destination |
|---|---|---|
| `--help` | none | print contract help and exit |
| `--version` | none | print executable/contract version and exit |
| `--integration-status` | none | print L0-L10 central-build/production attachment boundaries and exit |
| `--scene` | token | `scene` |
| `--backend` | token | `backend` |
| `--integrator` | token | `integrator` |
| `--direct-lighting` | token | `directLightingEstimator` |
| `--light-sampler` | token | compatibility alias above |
| `--light-proposal` | token | `lightProposalDistribution` |
| `--reconstruction` | token | `reconstruction` |
| `--shadow` | token | `shadowMethod` |
| `--debug-view` | token | `debugView` |
| `--resolution` | `WIDTHxHEIGHT`, each 64..16384 | width and height |
| `--render-scale` | `0.0625..2` | render scale; Wave 0 supports only `1.0` |
| `--spp` | `0..4096` | target samples per pixel |
| `--spp-per-frame` | positive integer | samples per frame |
| `--max-bounce` | `1..12` | maximum bounce |
| `--max-depth` | `1..12` | compatibility alias above |
| `--seed` | unsigned 64-bit integer | base seed |
| `--exposure` | `0.01..64` | exposure |
| `--fov` | `25..80` degrees | vertical field of view |
| `--frames` | unsigned 32-bit integer | frame limit; zero is interactive |
| `--resize-test` | none | enable the compatibility resize smoke |
| `--headless` | none | request no-window execution |
| `--capture` | artifact-root directory | request the frozen capture bundle |
| `--benchmark` | preset token | request a benchmark sequence |
| `--reference` | input EXR path | request numerical image comparison |
| `--artifact-root` | directory path | artifact planning root |
| `--run-id` | non-empty text | sanitized artifact run directory name |
| `--validation` | `renderer-default`, `on`, `off` | validation policy |
| `--vsync` | `renderer-default`, `on`, `off` | presentation policy |

Normative enum tokens include:

```text
scene:           baseline, intersection-bvh, whitted-optics,
                 cornell, ggx-mis, environment-dome, sponza,
                 backend-parity, temporal-stability, many-lights
backend:         legacy-analytic-gpu, cpu-brute-force, cpu-sah,
                 gpu-flattened-sah, gpu-lbvh, ray-query, rt-pipeline
integrator:      whitted, cpu-reference, megakernel, wavefront, pbr
direct lighting: legacy-analytic-direct, bsdf-only, nee, mis, restir-di
proposal:        legacy-analytic, uniform, power, environment
reconstruction:  current-frame, progressive-mean, temporal, atrous-spatial, svgf
shadow:          physical, pcf, pcss
debug:           final, base-color, normal, roughness, metallic, emissive
```

The parser also accepts descriptive aliases such as `baseline-gallery`, `cpu-sah-bvh`,
`vulkan-ray-query`, `gpu-megakernel-path`, and
`gpu-wavefront-path`. The short tokens used by the main plan, including
`cornell`, `ray-query`, and `megakernel`, are normative for automation and
future metadata.

The token list describes the roadmap namespace. The Wave 0 implemented subset
is the capability table in `runtime-config-v0.md`; for example `--backend
ray-query` is well-formed but exits as unsupported until L5 is integrated.

## Exit codes

| Code | Meaning |
|---:|---|
| `0` | successful render/run, help, or version request |
| `2` | invalid CLI syntax, value, or internally invalid configuration |
| `4` | recognized but unsupported capability or combination |
| `10` | platform, Vulkan, shader, renderer, or other runtime failure |

An unsupported result must name the rejected field or feature. Automated
callers may rely on code 4 to distinguish future capability from a typo.

## Wave 1 implementation examples

Supported compatibility requests:

```powershell
RenderingEngine.exe --frames 120
RenderingEngine.exe --integrator pbr --shadow physical --max-bounce 8
RenderingEngine.exe --integrator whitted --frames 8
RenderingEngine.exe --integrator pbr --debug-view normal --frames 8
RenderingEngine.exe --resolution 1280x720 --resize-test --frames 90
RenderingEngine.exe --artifact-root artifacts --run-id smoke --frames 1
RenderingEngine.exe --capture artifacts --run-id portfolio --frames 8
RenderingEngine.exe --scene cornell --backend cpu-sah --integrator cpu-reference --direct-lighting mis --light-proposal uniform --headless --spp 64 --run-id cornell-reference
```

Recognized but unsupported in the current production composition:

```powershell
RenderingEngine.exe --backend ray-query
RenderingEngine.exe --reconstruction svgf
RenderingEngine.exe --spp-per-frame 2
RenderingEngine.exe --benchmark short
RenderingEngine.exe --reference reference.exr
```

The latter group must return 4 before a window appears and before files are
created. `--frames 0` means an interactive run, and `--spp 0` means that no
target-SPP stop condition was requested.

`--help`, `--version`, and `--integration-status` are immediate process actions. Once encountered they
print and exit successfully without evaluating later renderer options.

Wave 0 target SPP is limited to 4096. A non-zero target SPP with a non-final
debug view is invalid (exit 2), because deterministic first-hit debug output is
not progressive radiance. Startup FOV is limited to 25 through 80 degrees.

For a capture run, the `--capture` directory is the artifact root and
the images remain under `<directory>/<run-id>/captures/`. If
`--artifact-root` is also present, its normalized value must match the capture
directory or the CLI returns 2. The live renderer writes linear EXR, display
PNG, and metadata after the requested frame/SPP terminal condition; an
interactive F4 request writes a sequence-suffixed bundle without exiting.
`--reference` is an input image and remains unsupported; `--benchmark` is a
named preset, not a path, and also remains unsupported.
