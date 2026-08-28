# Integration status contract v1

This contract separates repository composition from production runtime
availability. It does not change GPU ABI v0 or RuntimeConfig v0.

## Stages

- `missing`: the owning lane did not deliver a usable provider.
- `source-only`: partial source exists but is not part of the central build.
- `central-build`: the lane projects are present in `RenderingEngine.sln` and
  participate in the Debug/Release integration build. This is not runtime,
  GPU, numerical, visual, or performance acceptance.
- `production-runtime`: the provider is attached to the production
  application and may be published by `CapabilityTable` when its complete
  configuration tuple is supported.

`CapabilityTable` remains fail-closed. A lane reaching `central-build` does not
make its RuntimeConfig token supported. Unsupported decisions identify the
owning lane's missing production adapter instead of falling back to another
backend, integrator, estimator, proposal, or reconstruction mode.

`--integration-status` is an information action. It prints the status of L0
through L10 and exits successfully before platform/window creation.
