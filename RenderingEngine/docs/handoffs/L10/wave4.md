# L10 handoff: Wave 4 comparison and evidence gate

## 1. Scope

L10 defines a reproducible, fail-closed Wave 4 comparison/reporting surface in
the shadow. It does not manufacture missing renderer evidence.

## 2. Implemented

The stable four-leg catalog is Uniform, Power-weighted, one explicitly selected
biased or unbiased-reference ReSTIR DI mode, and an independent high-SPP CPU
reference. Requests bind scene/asset/config/machine/driver fingerprints, power
profile, camera, extent, seed, frame and generation identity. Provider-observed
candidate counts must match their declaration; visibility rays may remain below
but never exceed the declared upper bound. Realtime timings are Vulkan GPU timestamps;
High-SPP timing is CPU wall-clock and its self-quality field remains null.
Realtime performance acceptance fixes 120 warm-up frames, 1,000 measured
frames, and at least three repeats; the CPU oracle uses an explicit non-zero
cadence.

## 3. Not implemented

No live ABI-v3 provider supplies fresh four-leg GPU captures. The production
GLFW showcase remains the Wave 2 application.

## 4. Files

Primary files are `include/demos/ManyLightsWave4.hpp`,
`src/demos/ManyLightsWave4.cpp`, `include/ui/Wave4TelemetryAdapter.hpp`,
`src/ui/Wave4TelemetryAdapter.cpp`, their direct tests, and the separately
registered `Wave4Acceptance` contract/tests. No live bridge into the existing
Showcase report/program or renderer provider is present.

## 5. Contract version

The shadow evidence schema is `l10-many-lights-wave4-v1`. Each leg carries the
same immutable tuple and exact declared/observed budget. The aggregate
acceptance gate requires exactly four legs for each 100/1k/10k tier, one chosen
ReSTIR bias per comparison, and a distinct reference-budget identity.

## 6. Build

The final L10 Showcase library/test target compiles with `/W4 /WX` and its test
executable passes in both Debug and Release. The contracts target also rebuilds
and runs to completion in both configurations, including the revised aggregate
`Wave4Acceptance` tests. The merged root Debug target compiled the Wave 4
translation units before stopping on the pre-existing Wave 2 `NOMINMAX`, C4324
`/WX`, and three `std::span` deduction errors; root Release was not run after
that known Debug blocker.

## 7. Unit and statistical evidence

The passing L10 test executable covers all three tiers under both selected
ReSTIR bias modes, catalog completeness, missing fixed identity, declared versus
observed budget mismatch, High-SPP identity/timing/quality rules, stale and
synthetic rejection, provider-ID closure, serializer revalidation, classic-
locale JSON numbers, CSV row/column shape, and telemetry mapping. Its EXR
maximum-absolute-error-zero result is an encoder fixture only. Aggregate
`Wave4Acceptance` tests exercise both bias selections across all three tiers;
they remain synthetic contract fixtures rather than renderer evidence.

## 8. Vulkan runtime evidence

Not run for the four-leg V3 provider. The separate L9 legacy smoke cannot be
promoted into L10 comparison evidence.

## 9. Visual and performance evidence

Not run. Fixture artifacts do not establish visual equivalence. There is no
independent high-SPP production capture or canonical GPU timing cadence.

## 10. Risks and rollback

The remaining risk is composition: all fresh/provider/timing fields are supplied
by a future producer, and no production producer exists yet. These validators
cannot promote their own synthetic unit fixtures into renderer truth. Promotion
and rollback remain controlled by the Wave 4 three-way manifest script.
