# CLI v2

Status: active RuntimeConfig v2 command-line contract.

## Algorithm selection

```text
--backend canonical-linear-gpu|cpu-brute-force|cpu-sah|gpu-flattened-sah|gpu-lbvh|ray-query|rt-pipeline
--transport pbr|whitted
--execution staged|cpu-reference|megakernel|wavefront
--direct-lighting bsdf-only|nee|mis|restir-di
--light-selection uniform|power
--environment-sampler uniform-sphere|importance-map
--reconstruction current-frame|progressive-mean|temporal|atrous-spatial|svgf
--shadow pcf|pcss|physical
```

The removed `--integrator`, `--light-proposal`, `--light-sampler`,
`legacy-analytic-gpu`, `legacy-analytic-direct` and `legacy-analytic` spellings
are command-line errors. The parser does not translate them into v2 fields.

## Many Lights and ReSTIR

```text
--scene many-lights
--direct-lighting restir-di
--many-lights-tier 100|1000|10000
--restir-stage initial|temporal|temporal-spatial
--restir-bias explicitly-biased|unbiased-reference
--restir-candidates 1..64
--restir-neighbors 0..30
--restir-max-m 1..4096
--restir-history-age 1..4096
--comparison-candidate-budget 1..64
--comparison-visibility-budget 1..64
--animate-many-lights on|off
--animate-rigid-occluders on|off
```

Parsing only establishes intent. `CapabilityTable` validates the complete
tuple and provider availability before platform/Vulkan creation. No rejected
tuple is simplified or substituted.
