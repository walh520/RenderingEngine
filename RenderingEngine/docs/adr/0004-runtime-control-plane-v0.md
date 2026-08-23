# ADR 0004: One capability-gated runtime control plane

- Status: Accepted
- Date: 2026-08-24
- Contracts: `runtime-config-v0`, `cli-v0`, `artifact-layout-v0`

## Context

The compatibility renderer historically parsed a small group of options next
to executable startup, while the roadmap needs CLI, GLFW keys, ImGui, tests,
capture, and benchmark automation to select several independent algorithm
dimensions. If each front end owns its own defaults or fallback rules, two
runs with the same visible labels can execute different estimators or proposal
distributions.

Wave 0 also needs to name future modes without presenting them as implemented.
In particular, a test helper that constructs a no-window configuration must not
make the production executable appear headless-capable.

## Decision

Use one normalized `RuntimeConfig` for every front end and keep scene, backend,
integrator, direct estimator, proposal distribution, reconstruction, debug
view, and legacy shadow method as independent fields.

Validate the complete configuration through a central capability table before
creating a platform host, Vulkan object, output directory, or artifact. A known
but absent capability returns exit code 4 with a reason. Invalid CLI or config
returns 2. Runtime failure after validation returns 10. No layer silently
substitutes a supported mode.

Keep `Application` responsible for parse, normalize, capability-check, artifact
planning, platform factory invocation, renderer construction, and exit-code
translation. Keep `Main` as the platform composition root. The renderer receives
only the compatibility options that it actually implements.

Freeze artifact names and metadata now, while deferring capture, benchmark,
reference, and headless production work to their owning later lines.

## Consequences

- CLI, future GLFW actions, ImGui, and tests describe the same run.
- Direct-lighting estimator and light proposal remain independently observable.
- Roadmap tokens can be documented and parsed before implementation without
  becoming false capability claims.
- Help/version and unsupported requests can terminate before opening a window.
- `MakeHeadlessMockRuntimeConfig()` is explicitly test-only.
- Adding support requires a capability-table and evidence update, rather than a
  parser shortcut or renderer fallback.
- Wave 0 retains the analytic GPU Whitted/PBR compatibility renderer with Raw
  reconstruction; later algorithms remain declared but unavailable.

## Alternatives considered

### A single combined render-mode enum

Rejected because it couples independent research variables, makes A/B metadata
ambiguous, and grows combinatorially.

### Let each renderer accept and repair requests

Rejected because silent fallback makes CLI automation and benchmark evidence
untrustworthy.

### Implement placeholder headless/capture paths in Wave 0

Rejected because empty or synthetic output would look like completed algorithm
evidence. The contract is frozen now and unsupported execution is explicit.
