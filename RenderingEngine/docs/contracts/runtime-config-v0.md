# RuntimeConfig v0

Status: **frozen control-plane contract, Wave 1 providers attached without enum changes**
Contract token: `runtime-config-v0`

`RuntimeConfig` is the single canonical description of a requested run. The
CLI, the future GLFW action map, ImGui, automated tests, and renderer factories
must all produce or consume this contract. None of them may keep an independent
mode tuple or silently repair an unsupported request.

This is an application contract, not a CPU/GPU binary layout. The GPU-facing
subset is copied into the versioned frame ABI only when a renderer consumes it.
Its application version value is `0`; this is independent of ABI v0's numeric
wire value `1`.

## Orthogonal selection

The following dimensions remain independent fields:

```text
scene
backend
integrator
directLightingEstimator
lightProposalDistribution
reconstruction
debugView
shadowMethod                 # legacy Baseline Gallery comparison only
```

In particular, a direct-lighting estimator never implies a proposal
distribution. For example, `mis + power` and `mis + uniform` are different
requests and must be reported separately in metadata.

`RenderSettings` contains:

```text
width, height
renderScale
samplesPerFrame
targetSamplesPerPixel
maximumBounce
baseSeed
exposure
verticalFovDegrees
vsync
```

`RunSettings` contains:

```text
frameLimit
resizeTest
headless
validation
captureDirectory (optional request; semantically the capture artifact root)
benchmarkPreset (optional request)
referenceImage (optional input image)
artifactRoot
runIdentifier
```

An unlimited interactive run uses `frameLimit = 0`. A target SPP of zero means
that no target-SPP stop condition was requested; it does not mean zero samples
per frame.

## Wave 0 defaults

| Field | Default |
|---|---|
| Scene | `baseline` (`baseline-gallery` alias) |
| Backend | `legacy-analytic-gpu` |
| Integrator | `pbr` |
| Direct-lighting estimator | `legacy-analytic-direct` |
| Light proposal | `legacy-analytic` |
| Reconstruction | `raw` |
| Debug view | `final` |
| Legacy shadow method | `physical` |
| Resolution | `1280x720` |
| Render scale | `1.0` |
| Samples per frame / target SPP | `1` / `0` |
| Maximum bounce | `8` |
| Base seed | `0` |
| Exposure | `1.0` |
| Vertical FOV | `52` degrees |
| VSync / validation | `renderer-default` |
| Frame limit | `0` |

The project-wide 1920x1080 portfolio target is an acceptance profile, not the
Wave 0 compatibility renderer's startup resolution.

## Capability decision

Parsing a token proves only that the request is syntactically known. Before a
window, Vulkan instance, or output directory is created, the application must
ask the capability table whether the complete normalized configuration is
implemented.

The table exposes per-dimension `IsBuilt(...)` queries for UI/help and one
combination-level `Evaluate(RuntimeConfig)` decision for execution. An unknown
enum value is invalid; a declared value with no implementation is unsupported.

The decision has three possible meanings:

- supported: the current executable can run the request;
- unsupported: the token is part of the frozen roadmap but the implementation
  is absent; return exit code 4 with a reason;
- invalid: the value or combination violates the contract; return exit code 2.

There is no fallback to another scene, backend, estimator, proposal,
reconstruction, validation state, or run mode.

## Implemented capability

Wave 0 supports only this compatibility slice:

| Dimension | Implemented values |
|---|---|
| Scene | `baseline` (`baseline-gallery` alias) |
| Backend | `legacy-analytic-gpu` |
| Integrator | `whitted`, `pbr` |
| Direct estimator | `legacy-analytic-direct` |
| Proposal | `legacy-analytic` |
| Reconstruction | `raw` |
| Debug | `final`, `base-color`, `normal`, `roughness`, `metallic`, `emissive` |
| Legacy shadow | `physical`, `pcf`, `pcss` |

`legacy-analytic-direct` names the current analytic-light direct estimator. Its
default physical sphere-light method and deterministic PCF/PCSS comparison
methods remain a separate `shadowMethod`; none of them is mislabeled as an
estimator or proposal.

The GLFW production renderer accepts frame limit, target SPP, resize test,
resolution, maximum bounce, base seed, exposure, startup FOV,
VSync/validation selection, shadow, integrator, and debug controls. Render scale
is fixed at `1.0`, and samples per frame is fixed at one; any other syntactically
valid value is recognized but unsupported. Live capture is supported for the
baseline GLFW renderer and writes the frozen EXR/PNG/metadata bundle. Headless
execution is supported only by the exact L3 Cornell + CPU SAH + CPU reference
+ MIS + uniform + raw tuple. Benchmark and reference comparison remain
recognized but unsupported.

Target SPP is at most 4096. A non-zero target combined with any debug view other
than `final` is invalid rather than silently ignored. Startup FOV is 25 through
80 degrees. VSync and validation use the explicit three-state policy
`renderer-default|on|off`.

`MakeHeadlessMockRuntimeConfig()` remains a deterministic construction helper.
It does not advertise a general GPU headless path; only the fail-closed L3
tuple above is a production headless capability.

## Mutation and reset rules

Every change is applied at one fixed point at the start of a frame. The
capability table validates the proposed complete tuple before the live config
changes. A rejected action leaves the previous tuple intact and exposes the
reason to the CLI, title/help overlay, or ImGui.

History invalidation follows the reset table in the main development plan.
Display-only changes such as exposure and debug overlay do not alter the base
algorithm selection. A seed is always a base seed; frame and sample indices
must still advance the random sequence.

## Versioning

CLI tokens and serialized field names are append-only within v0. Renaming or
repurposing a token, merging independent dimensions, changing default meaning,
or turning an unsupported request into a silent fallback requires a new ADR.
Adding a genuinely implemented capability is compatible when the capability
table, tests, help text, and handoff evidence are updated together.

ADR 0005 changes the startup integrator from the already-implemented Whitted
mode to the already-implemented PBR mode. This changes only the default
selection: both tokens, their numeric values, capability status, explicit CLI
meaning, and no-fallback behavior remain unchanged.
