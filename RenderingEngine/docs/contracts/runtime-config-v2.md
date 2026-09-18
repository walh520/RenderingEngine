# RuntimeConfig v2

Status: active split-axis host-control contract.

RuntimeConfig v2 removes the mixed `Integrator` and
`LightProposalDistribution` enums. There are no compatibility aliases or
implicit old-tuple conversions.

## Independent algorithm axes

| Axis | Values | Meaning |
|---|---|---|
| `backend` | canonical linear, CPU brute force/SAH, flattened SAH, LBVH, Ray Query, RT Pipeline | ray traversal implementation |
| `transportModel` | PBR, Whitted | light-transport model |
| `executionArchitecture` | staged, CPU reference, megakernel, wavefront | scheduling and execution architecture |
| `directLightingEstimator` | BSDF-only, NEE, MIS, ReSTIR DI | direct-light estimator |
| `lightSelection` | uniform, power-weighted | discrete light-identity PMF, including an environment light when present |
| `environmentSampler` | uniform sphere, importance map | conditional environment-direction sampler/PDF |
| `reconstruction` | Raw, Temporal, Temporal+A-Trous, SVGF | reconstruction path |
| `shadowMethod` | PCF, PCSS, physical | visibility model |

The capability table evaluates the complete candidate before resources are
created or live state is exchanged. A known value can still be unsupported.
Whitted currently requires staged execution. CPU reference remains a finite,
headless Cornell oracle. Unsupported combinations fail closed and never fall
back to PBR or another backend.

## GPU parameter ownership

The light-selection strategy is serialized independently from the environment
sampler. `sampling.z` selects uniform or power-weighted discrete lights;
`environment.w` selects uniform-sphere or importance-map environment
directions. `output.z` identifies PBR versus Whitted transport. Execution
architecture determines the dispatch path and is not encoded as transport.

The source-verified BSDF/direct-light/termination/AOV comparison is in
[the current implementation record](../../PBR_IMPLEMENTATION.md). Shared
transport does not by itself prove identical AOV definitions or permit a
scheduling-only performance claim.

## History identity

Transport model and execution architecture are distinct history fields.
Changes to either invalidate accumulation, temporal, reservoir and profiler
state. Backend and scene-payload changes additionally rebuild acceleration
structures. Environment-sampler and finite-light-selection changes invalidate
sampling history without pretending to change traversal.

The binary GPU boundary remains independently versioned as `abi-v3`.
