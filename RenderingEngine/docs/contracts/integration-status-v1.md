# Integration status contract v1

This contract separates repository composition from production runtime
availability. RuntimeConfig v1 appends Wave 4 host controls without enabling
them. The production Wave 2 runtime remains on ABI v1 (numeric 2); Wave 3 is
centrally composed on ABI v2 (numeric 3), and the Wave 4 shadow publishes the
ABI v3 (numeric 4) source/build contract. ABI v3 production resources,
pipelines, dispatch, reconstruction attachment, captures, and acceptance
evidence are still absent.

## Stages

- `missing`: the owning lane did not deliver a usable provider.
- `source-only`: partial source exists but is not part of the central build.
- `central-build`: the lane projects are present in `RenderingEngine.sln` and
  participate in at least one explicitly reported central integration-build
  configuration. This stage is configuration-specific; it never implies that
  both Debug and Release were built. It is not runtime, GPU, numerical, visual,
  or performance acceptance.
- `production-runtime`: the provider is attached to the production
  application and may be published by `CapabilityTable` when its complete
  configuration tuple is supported.

`CapabilityTable` remains fail-closed. A lane reaching `central-build` does not
make its RuntimeConfig token supported. Unsupported decisions identify the
owning lane's missing production adapter instead of falling back to another
backend, integrator, estimator, proposal, or reconstruction mode.

`--integration-status` is an information action. It prints the status of L0
through L10 and exits successfully before platform/window creation.

The factual build configurations, commands, and dates belong in each lane's
handoff. The Wave 3 and Wave 4 frame plans, fail-closed execution transactions,
and acceptance code are contracts: they cannot turn provider-shaped fixture
data into GPU runtime evidence. Live build/trace
acceptance additionally requires provider-reported GPU timestamp queries,
canonical 120/1000/3 cadence, exact workload identity, and zero required fault
counters. No Debug, Release, GPU, validation, visual, convergence, or
performance claim follows from the stage label alone.
