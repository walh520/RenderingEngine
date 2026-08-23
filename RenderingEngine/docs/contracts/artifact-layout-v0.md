# Artifact layout v0

Status: **frozen naming and metadata contract; writer deferred**
Configuration contract: `runtime-config-v0`

This contract gives capture, benchmark, reference comparison, CI, and portfolio
tools one deterministic output shape. Wave 0 can resolve an artifact layout in
memory, but it does not implement image capture, benchmark collection,
reference comparison, directory creation, or file writing.

## Root and run identity

`--artifact-root <path>` selects the general root without creating it. The
default is `.artifacts`. For a capture request, `--capture <directory>` carries
the artifact root for that capture run; it does not replace the `captures`
child. When both options occur, their lexically normalized paths must be equal
or the CLI is invalid (exit 2).

`--run-id <id>` selects the child directory and defaults to `manual`. Planning
sanitizes the supplied ID deterministically: characters
outside ASCII letters, digits, `.`, `_`, and `-` become `_`, leading dots are
removed, and an empty result becomes `manual`. No user text can introduce a
second path component.

The logical layout is:

```text
<artifact-root>/
└── <run-id>/
    ├── captures/
    │   ├── image.exr
    │   └── preview.png
    ├── benchmarks/
    │   ├── timings.csv
    │   └── counters.csv
    ├── references/
    │   └── comparison.json
    ├── logs/
    │   └── validation.log
    └── metadata.json
```

Producers may omit files that do not apply, but may not rename a normative file
or create a plausible empty substitute. `image.exr` is the linear scene-referred
result. `preview.png` is the documented display transform. `comparison.json`
exists only when a reference comparison actually ran.

## Required `metadata.json` metadata

```text
schema version and contract versions
Git commit and dirty-worktree state
executable configuration (Debug/Release) and build identity
GPU, driver, Vulkan API and Vulkan SDK versions
scene ID, scene generation/hash, asset hashes and camera preset
full normalized RuntimeConfig tuple
resolution, render scale, seed, SPP, bounce and frame limits
shader hashes
start/end timestamps and completion state
requested artifact set and files actually produced
```

`timings.csv` records named CPU/GPU intervals and test conditions.
`counters.csv` records rays, paths, samples, invalid values, overflows, memory,
and algorithm-specific work budgets. `validation.log` contains the validation
configuration and messages; an empty file may only mean validation ran and
produced no messages, not that it was skipped.

`--benchmark <preset>` selects the future benchmark sequence and the preset is
recorded in metadata. `--reference <image.exr>` names an input reference image;
it is never copied over the normative output. The comparison result belongs in
`references/comparison.json`.

## Reproducibility and writes

- Paths stored in metadata use `/` separators and UTF-8 text.
- JSON uses stable field names and an explicit schema version.
- Floating-point configuration values are serialized with round-trip precision.
- A producer writes data files first and marks the run complete in
  `metadata.json` only after every requested artifact has succeeded.
- A run ID collision is an error unless an explicit future overwrite policy is
  introduced. No Wave 0 command overwrites an existing artifact.
- Capture and benchmark code records the exact requested and effective config;
  it never hides a capability fallback.

## Wave 0 boundary

Supplying only `--artifact-root` and/or `--run-id` is valid and performs layout
planning without I/O. `--capture`, `--benchmark`, or `--reference` requests an
unimplemented producer and therefore exits with code 4 before artifact-layout
resolution, platform creation, or any filesystem I/O.
