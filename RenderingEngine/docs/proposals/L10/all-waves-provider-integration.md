# L10 proposal: code-first all-wave provider integration

- Owner: L10
- Scope: UI, debug/profiler presentation, evidence orchestration, reports
- Shared contract change in this branch: none
- Status: lane implementation first; production provider composition pending

The user explicitly requested that L10's complete code surface be implemented
before upstream data contracts and numerical validation are ready.  This
proposal keeps that exception truthful: L10 accepts immutable snapshots and
opaque presentation tokens, but never invents renderer data, Vulkan ownership,
or capability support.

## Snapshot identity and provenance

Every profiler sample, counter, debug resource, benchmark frame, capture, and
comparison result must carry:

```text
stable producer/source ID
origin: LiveRuntime | ImportedArtifact | SyntheticTest
frame/sample index
RuntimeConfig generation
scene/resource generation where applicable
availability: Unavailable | Pending | Fresh | Stale | Invalid
explicit reason when not Fresh
```

The runtime status line follows the same rule.  Resolution, base seed, and
maximum bounce are configuration-owned facts and remain visible from the
effective `RuntimeConfig`; frame, accumulated SPP, and GPU milliseconds are
shown only for a Fresh named provider observation.  Stale, pending, invalid,
or absent observations render as unavailable instead of retaining an older
value.

A numeric zero with `Fresh` is measured zero.  Missing data is never encoded as
zero.  Synthetic fixtures are accepted only by tests and are labelled in every
derived table/report; they are not GPU, visual, numerical-reference, or
performance evidence.

## Producer adapters

L0 remains the composition owner.  It may translate producer-owned data into
the L10 snapshot types without making those producers depend on UI code:

| Producer | Data supplied to L10 | Producer retains ownership of |
|---|---|---|
| L1 | input arbitration, frame/present counters, resolved GPU timestamps, opaque ImGui texture registration | query pools, fences, timestamp period/valid bits, Vulkan descriptors and image transitions |
| L2 | stable scene/card ID, fixed camera preset ID, scene/asset generation and hashes | scene registry, assets, camera data and animation |
| L3 | CPU reference image and explicitly named CPU counters/timings | reference integrator and oracle fixtures |
| L4/L5 | traversal counters/timings and backend debug resources | traversal implementation, AS lifetime and GPU buffers/images |
| L6/L7 | path/integrator counters, pass timings and diagnostic outputs | integrator state, queues and shaders |
| L8 | Raw/Temporal/SVGF diagnostic resources and history validity | history images, reprojection and reconstruction synchronization |
| L9 | reservoir/candidate diagnostics and Many Lights budgets | reservoirs, visibility, target/proposal/correction math |

L10's debug-resource catalog stores an opaque UI token plus format, extent,
legend/range, owner, tuple generation and availability.  It contains no
`VkImage`, `VkImageView`, `VkDescriptorSet`, barrier, queue-family, or lifetime
claim.

## Fixed frame point and resets

L0 applies the existing `ActionQueue` once at frame start and increments the
published RuntimeConfig generation for each committed mutation.  It then:

1. forwards A/T/Q/AS reset requests to their resource owners;
2. forwards P to the profiler-history owner;
3. invalidates snapshots from older config/scene/resource generations;
4. leaves profiler sample count at zero until the first new Fresh sample;
5. forwards capture/benchmark/reference commands only when every required
   provider is attached.

Display-only help, profiler, legend, exposure and capture-panel operations do
not reset renderer histories.

## ImGui and headless use

The L10 ImGui functions consume the same view models used by tests.  L1/L0 owns
Dear ImGui context/backend creation and input arbitration.  In headless mode no
ImGui function is called; the controller, telemetry validation, benchmark
state machine, metrics, CSV/report generation and artifact transaction remain
usable as plain C++.

The status strip is drawn for every valid ImGui frame, independently of panel
visibility.  The Capture panel can start/cancel the same controller-owned A/B
workflow used headlessly, display the pending provider request, and expose the
completed manifest.  The application still owns provider dispatch and submits
the immutable capture records back to the controller.

F1/F2 reuse the same program model to show the selected scene, each selected
algorithm's method summary, its current limitation, completion state, and any
provider/capability reason.  These cards describe code intent; they do not
promote an unavailable algorithm to implemented.

## All-wave orchestration surface

| Plan wave | L10-owned code surface | Upstream fact still required |
|---|---|---|
| Wave 1 | semantic ActionMap, fixed-point RuntimeConfig harness, capture transaction | GLFW translation, renderer readback, production composition |
| Wave 2 | provider-driven debug-resource catalog, rolling CPU/GPU profiler, ImGui panels | resolved timestamps, counters, opaque texture registration |
| Wave 3 | fixed-anchor A/B workflow, reference metrics, benchmark sequence | renderer variants and reference image providers |
| Wave 4 | 100/1k/10k uniform/power/ReSTIR/high-SPP-reference plan | L9 candidates/visibility budgets and reference captures |
| Wave 5 | evidence-bound completion matrix, six-boundary report, license gate, video state machine and shot list | reviewed captures, licenses and human visual approval |

The canonical benchmark preset is 120 warm-up frames, 1000 measured frames and
three repeats.  GPU duration requirements must identify timestamp-query
measurement; a CPU wall-clock timing cannot satisfy such a requirement.

## Evidence transaction

One run may contain:

```text
captures/image.exr
captures/preview.png
benchmarks/timings.csv
benchmarks/counters.csv
references/comparison.json
reports/showcase.md
reports/ab-manifest.json
reports/completion-matrix.csv
reports/asset-licenses.csv
reports/video-shot-list.csv
metadata.json
```

Only requested files are written.  Image/evidence files are committed first;
`metadata.json` is renamed into place last.  Collision remains an error and a
failed transaction rolls back only the newly created run directory.

An A/B manifest preserves the shared camera/seed/animation anchor, identical
frame/sample coordinate, config/scene/resource generations, two distinct run
IDs, exact bundle paths, and provider provenance.  It records orchestration
identity only; it does not claim that numerical or visual comparison passed.

Reference-comparison records bind candidate and reference to the same scene,
camera, seed, config/scene/resource generations, and extent while retaining
their independent frame, sample, and accumulated-SPP identities.  This permits
a high-SPP reference without allowing a different scene generation to pass as
the comparison oracle.

Evidence artifact paths are deliberately restricted to normalized ASCII under
the four approved subdirectories.  This is a fail-closed Windows choice: it
prevents Unicode case-fold aliases from naming one NTFS file twice while still
allowing UTF-8 descriptions and provenance inside the artifacts.

## Capability publication

Compiling a panel, adapter, synthetic fixture, or report does not make a
renderer capability available.  UI actions remain disabled with an owner and
reason until production composition supplies a Fresh compatible provider and
the owning line's runtime evidence passes.

## Deferred data validation

Per the user's instruction, this branch does not freeze performance baselines,
accept synthetic results as renderer truth, or claim GPU/Vulkan/visual
acceptance.  Those validations are performed after real producers are wired;
the source/provenance fields are designed so that later evidence cannot be
confused with this code-first milestone.
